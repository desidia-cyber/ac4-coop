// Co-op link v0 over WinSock (coop_net.h). See research\24_COOP_LINK_V0.md for the measurements.
//
// One non-blocking UDP socket. The host learns its peer from the first packet; the joiner has the
// peer from its configuration. Nothing here blocks and the peer disappearing only stops the flow
// (port 1 then returns neutral, so the remote AC simply stands still).
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/game/coop_build.h"
#include "runtime/game/coop_body.h"
#include "runtime/game/coop_net.h"
#include "runtime/game/coop_session.h"
#include "runtime/game/coop_steam.h"
#include "runtime/log.h"

namespace {

constexpr char kMagic[4] = {'A', 'C', '4', 'C'};
constexpr uint8_t kVersion = 2;
constexpr uint8_t kTypePad = 1;
constexpr uint8_t kTypeTransform = 2;
constexpr uint8_t kTypeSnapshot = 3;
constexpr size_t kHeader = 8;
constexpr size_t kPadLen = kHeader + 19;  // + 6 bytes ACKed reliable rising-edge delivery (edge_seq, edge_buttons, edge_ack)
constexpr size_t kTransformLen = kHeader + 2 + 4 * (3 + 9 + 3) + 12 + 25 + 24 + 8;  // + AP/PA/EN/self_EN/weapons/ammo + flags
constexpr size_t kMaxDatagram = 1200;  // the largest snapshot datagram (research\26 M2)
constexpr double kPadTimeout = 1.0;    // seconds without a packet: port 1 goes neutral
constexpr double kPeerTimeout = 5.0;   // seconds without any packet: link considered down (survives load stalls)
constexpr int kTransformEvery = 3;     // send the transform at ~20 Hz for crisp partner heading/drift sync

#pragma pack(push, 1)
struct Header {
  char magic[4];
  uint8_t version, type;
  uint16_t seq;
};
struct PadPacket {
  uint16_t buttons;
  int16_t lx, ly, rx, ry;
  uint8_t lt, rt, port;
  uint16_t edge_seq;      // increments on every 0->1 button transition; retransmitted until edge_ack catches up
  uint16_t edge_buttons;  // rising-edge bits for edge_seq (switch weapon, purge, OB, AA, etc.)
  uint16_t edge_ack;      // highest peer edge_seq received, so toggles survive multi-second packet outages
};
struct TransformPacket {
  int16_t mission_tag;
  float position[3];
  float rotation[9];  // rows 0-2 of the body's 4x4, row-major
  float velocity[3];
  float ap, apmax;    // AP of the AC the sender simulates for the peer (research\27)
  float pitch;        // local player look pitch in radians (NaN if unavailable)
  float self_ap, self_apmax;  // sender's own AC1 AP (for partner HUD)
  float peer_pa, peer_pamax;  // sender's AC2 PA (host -> join authoritative PA)
  float self_pa, self_pamax;  // sender's own AC1 PA (for partner HUD)
  uint8_t flags;              // bit 0: sender local_paused, bit 1: peer_en_redline, bit 2: self_en_redline
  float peer_en, peer_enmax;  // sender's AC2 EN (host -> join authoritative EN)
  float self_en, self_enmax;  // sender's own AC1 EN (for accurate partner boost/OB animations on AC2)
  uint8_t peer_wpn_active;    // low nibble: R active idx, high nibble: L active idx (0xf=n/a)
  uint8_t peer_wpn_purged;    // bits 0..5: purged holders on AC2
  uint8_t self_wpn_active;    // sender's own AC1 active weapon nibbles
  uint8_t self_wpn_purged;    // sender's own AC1 purged holders
  int16_t peer_ammo[6];       // AC2 wpn_mgr+556 banks[0..1] slots[0..2] cur_ammo (-1 = n/a)
};
// Type 3: one snapshot, split over datagrams. `tag` carries the host's entity-pool base so the join
// can log both bases beside each other; `counter` counts whole snapshots.
struct SnapshotPayload {
  uint32_t counter;
  uint32_t tag;
  uint16_t count;
  uint16_t epoch;  // the host's mission epoch (coop_session.h); was reserved (0)
};
struct SnapshotRecordWire {
  uint16_t ord;
  uint32_t part_state;  // 2 bits per sub-part (up to 16 parts): 0=>50%, 1=<=50%, 2=<=30%, 3=broken
  uint32_t addr;
  uint32_t entity_id;
  float position[3];
  float rotation[9];
  float hp;
  uint8_t ext_flags;    // bit 0: has_input, bit 1: has_ac
  uint8_t wpn_active;
  uint8_t wpn_purged;
  int8_t in_axes[5];
  uint32_t in_btn[4];
  float pitch;
  float pa;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 8, "8-byte header");
static_assert(sizeof(PadPacket) == 19, "pad payload");
static_assert(sizeof(TransformPacket) == 2 + 4 * 15 + 12 + 25 + 24 + 8, "transform payload");
static_assert(sizeof(SnapshotPayload) == 12, "snapshot payload header");
static_assert(sizeof(SnapshotRecordWire) == 98, "snapshot record");
static_assert(kHeader + sizeof(SnapshotPayload) + 12 * sizeof(SnapshotRecordWire) <= kMaxDatagram,
              "12 records fit in one datagram");

SOCKET g_sock = INVALID_SOCKET;
bool g_transport_steam = false;
bool g_host = false;
sockaddr_in g_peer{};
bool g_have_peer = false;
bool g_started = false;

std::atomic<uint64_t> g_sent{0}, g_recv{0}, g_dropped{0}, g_bad{0};
std::atomic<uint64_t> g_frames{0};
std::atomic<uint32_t> g_last_pad_ms{0}, g_last_xform_ms{0}, g_last_sim_ms{0};
std::atomic<bool> g_seq_reset{false};  // set on peer change/reconnect so a restarted peer's seq=1 is accepted
int g_sim_latency_ms = 0, g_sim_jitter_ms = 0;  // optional AC4_COOP_SIM_LATENCY_MS / AC4_COOP_SIM_JITTER_MS
struct DelayedPkt {
  uint64_t deliver_ms;
  sockaddr_in peer;
  std::vector<uint8_t> bytes;
};
std::mutex g_delay_mu;
std::deque<DelayedPkt> g_delay_q;
uint32_t g_rng_state = 0x9e3779b9u;

std::mutex g_mu;
std::mutex g_rx_mu;  // receive_all: the frame and the load barrier may both pump
rt::game::coopnet::PadState g_local_pad;  // this instance's port 0, PS3 layout (set by sys_io)
rt::game::coopnet::PadState g_pad;      // the peer's pad, as last received
rt::game::coopnet::Transform g_xform;   // the peer's AC1 transform
std::atomic<bool> g_have_xform{false};
std::atomic<float> g_pos_error{0};

std::atomic<uint64_t> g_snap_sent_recs{0}, g_snap_sent_pkts{0}, g_snap_recv_recs{0};
std::atomic<uint32_t> g_snap_counter{0}, g_snap_tag{0};
std::vector<rt::game::coopnet::SnapshotRecord> g_snap_in;  // received, drained by take_snapshot()

uint16_t g_pad_seq = 0, g_xform_seq = 0, g_snap_seq = 0;
struct PendingEdge {
  uint16_t seq;
  uint16_t bits;
  uint32_t ts_ms;
};
uint16_t g_tx_prev_buttons = 0, g_tx_edge_seq = 0, g_tx_edge_acked = 0;
std::deque<PendingEdge> g_tx_edges;
uint16_t g_rx_edge_seq = 0, g_rx_edge_bits = 0;
int g_rx_edge_ttl = 0;
std::atomic<uint64_t> g_edges_sent{0}, g_edges_acked{0};
float g_local_pos[3] = {};
float g_local_rot[9] = {};
float g_local_vel[3] = {};
float g_local_ap = 0.0f, g_local_apmax = 0.0f;
float g_local_peer_pa = 0.0f, g_local_peer_pamax = 0.0f;
float g_local_peer_en = 0.0f, g_local_peer_enmax = 0.0f;
bool g_local_peer_en_redline = false;
rt::game::coopnet::WeaponSyncState g_local_peer_wpn{};
uint8_t g_local_self_wpn_active = 0xff, g_local_self_wpn_purged = 0;
float g_local_self_ap = 0.0f, g_local_self_apmax = 0.0f;
float g_local_self_pa = 0.0f, g_local_self_pamax = 0.0f;
float g_local_self_en = 0.0f, g_local_self_enmax = 0.0f;
bool g_local_self_en_redline = false;
std::atomic<float> g_local_pitch{NAN};
int g_mission_tag = 0;
double g_last_report = -1e9;

uint32_t now_ms() { return uint32_t(GetTickCount64() & 0xffffffffu); }

bool env(const char* name, char* buf, size_t n) {
  DWORD got = GetEnvironmentVariableA(name, buf, DWORD(n));
  return got > 0 && got < n;
}

// Splits "ip:port" (the port may be omitted: 7777).
bool parse_endpoint(const std::string& s, sockaddr_in* out) {
  std::string ip = s;
  unsigned port = 7777;
  size_t colon = s.find_last_of(':');
  if (colon != std::string::npos) {
    ip = s.substr(0, colon);
    port = unsigned(strtoul(s.substr(colon + 1).c_str(), nullptr, 10));
  }
  std::memset(out, 0, sizeof *out);
  out->sin_family = AF_INET;
  out->sin_port = htons(uint16_t(port));
  if (inet_pton(AF_INET, ip.c_str(), &out->sin_addr) != 1) return false;
  return true;
}

void note_peer(const sockaddr_in& from) {
  std::lock_guard lk(g_mu);
  if (g_have_peer && from.sin_addr.s_addr == g_peer.sin_addr.s_addr && from.sin_port == g_peer.sin_port) return;
  g_peer = from;
  g_have_peer = true;
  g_seq_reset.store(true, std::memory_order_relaxed);
  char ip[64] = {};
  inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
  LOG_I("coop", "COOP NET: peer %s:%u", ip, unsigned(ntohs(from.sin_port)));
}

void flush_delayed_sends() {
  if (g_sim_latency_ms <= 0 || (!g_transport_steam && g_sock == INVALID_SOCKET)) return;
  const uint64_t now = GetTickCount64();
  std::vector<DelayedPkt> due;
  {
    std::lock_guard lk(g_delay_mu);
    while (!g_delay_q.empty() && g_delay_q.front().deliver_ms <= now) {
      due.push_back(std::move(g_delay_q.front()));
      g_delay_q.pop_front();
    }
  }
  for (const auto& p : due) {
    if (g_transport_steam) {
      if (rt::game::coopsteam::send(p.bytes.data(), p.bytes.size())) {
        g_sent.fetch_add(1, std::memory_order_relaxed);
      }
    } else if (sendto(g_sock, reinterpret_cast<const char*>(p.bytes.data()), int(p.bytes.size()), 0,
                      reinterpret_cast<const sockaddr*>(&p.peer), sizeof p.peer) != SOCKET_ERROR) {
      g_sent.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void send_packet(uint8_t type, uint16_t seq, const void* payload, size_t len) {
  if (!g_transport_steam && g_sock == INVALID_SOCKET) return;
  sockaddr_in peer{};
  if (!g_transport_steam) {
    std::lock_guard lk(g_mu);
    if (!g_have_peer) return;
    peer = g_peer;
  } else {
    if (!rt::game::coopsteam::peer_steam_id()) return;
  }
  uint8_t buf[kMaxDatagram];
  if (kHeader + len > sizeof buf) return;
  Header h{};
  std::memcpy(h.magic, kMagic, 4);
  h.version = kVersion;
  h.type = type;
  h.seq = seq;
  std::memcpy(buf, &h, kHeader);
  std::memcpy(buf + kHeader, payload, len);
  if (g_sim_latency_ms > 0) {
    int delay = g_sim_latency_ms;
    std::lock_guard lk(g_delay_mu);
    if (g_sim_jitter_ms > 0) {
      g_rng_state = g_rng_state * 1664525u + 1013904223u;
      const int span = 2 * g_sim_jitter_ms + 1;
      delay += int(g_rng_state % uint32_t(span)) - g_sim_jitter_ms;
      if (delay < 0) delay = 0;
    }
    const uint64_t at = GetTickCount64() + uint64_t(delay);
    if (g_delay_q.size() < 4096) {
      DelayedPkt dp{at, peer, std::vector<uint8_t>(buf, buf + kHeader + len)};
      // Keep queue sorted by deliver_ms so flush pops in order even with jitter.
      auto it = g_delay_q.end();
      while (it != g_delay_q.begin() && std::prev(it)->deliver_ms > at) --it;
      g_delay_q.insert(it, std::move(dp));
    }
    return;
  }
  if (g_transport_steam) {
    if (!rt::game::coopsteam::send(buf, kHeader + len)) return;
  } else {
    if (sendto(g_sock, reinterpret_cast<const char*>(buf), int(kHeader + len), 0,
               reinterpret_cast<const sockaddr*>(&peer), sizeof peer) == SOCKET_ERROR)
      return;
  }
  g_sent.fetch_add(1, std::memory_order_relaxed);
}

void process_datagram(const uint8_t* buf, int n) {
  if (n < int(kHeader)) return;
  Header h{};
  std::memcpy(&h, buf, kHeader);
  if (std::memcmp(h.magic, kMagic, 4) != 0 || h.version != kVersion) {
    g_bad.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Drop out-of-order and duplicate packets within a type. Reset on peer change or after >5 s
  // silence so a restarted partner whose sequence numbers start at 1 is accepted immediately.
  static uint16_t last_pad = 0, last_xform = 0;
  const uint32_t prev_ms = g_last_pad_ms.load(std::memory_order_relaxed);
  if (g_seq_reset.exchange(false, std::memory_order_relaxed) ||
      (prev_ms && now_ms() - prev_ms > uint32_t(kPeerTimeout * 1000))) {
    last_pad = 0;
    last_xform = 0;
    std::lock_guard lk(g_mu);
    g_rx_edge_seq = 0;
    g_rx_edge_bits = 0;
    g_rx_edge_ttl = 0;
    g_tx_edge_acked = 0;
    g_tx_edges.clear();
  }
  if (h.type == kTypePad) {
    if (n < int(kPadLen)) return;
    if (int16_t(h.seq - last_pad) <= 0) {
      g_dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    last_pad = h.seq;
    PadPacket p{};
    std::memcpy(&p, buf + kHeader, sizeof p);
    {
      std::lock_guard lk(g_mu);
      g_pad.buttons = p.buttons;
      g_pad.lx = p.lx;
      g_pad.ly = p.ly;
      g_pad.rx = p.rx;
      g_pad.ry = p.ry;
      g_pad.lt = p.lt;
      g_pad.rt = p.rt;
      if (p.edge_ack && (g_tx_edge_acked == 0 || int16_t(p.edge_ack - g_tx_edge_acked) > 0)) {
        g_tx_edge_acked = p.edge_ack;
        while (!g_tx_edges.empty() && int16_t(g_tx_edge_acked - g_tx_edges.front().seq) >= 0) {
          g_tx_edges.pop_front();
          g_edges_acked.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (p.edge_seq && (g_rx_edge_seq == 0 || int16_t(p.edge_seq - g_rx_edge_seq) > 0) && p.edge_buttons) {
        g_rx_edge_seq = p.edge_seq;
        g_rx_edge_bits |= p.edge_buttons;
        g_rx_edge_ttl = 2;  // guarantee at least 2 game reads see brief rising-edge taps
      }
    }
    g_last_pad_ms = now_ms();
    g_recv.fetch_add(1, std::memory_order_relaxed);
  } else if (h.type == kTypeTransform) {
    if (n < int(kTransformLen)) return;
    if (int16_t(h.seq - last_xform) <= 0) {
      g_dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    last_xform = h.seq;
    TransformPacket t{};
    std::memcpy(&t, buf + kHeader, sizeof t);
    {
      std::lock_guard lk(g_mu);
      g_xform.mission_tag = t.mission_tag;
      std::memcpy(g_xform.position, t.position, sizeof t.position);
      std::memcpy(g_xform.rotation, t.rotation, sizeof t.rotation);
      std::memcpy(g_xform.velocity, t.velocity, sizeof t.velocity);
      g_xform.ap = t.ap;
      g_xform.apmax = t.apmax;
      g_xform.has_pitch = std::isfinite(t.pitch) && std::fabs(t.pitch) <= 1.6f;
      if (g_xform.has_pitch) g_xform.pitch = t.pitch;
      g_xform.self_ap = t.self_ap;
      g_xform.self_apmax = t.self_apmax;
      g_xform.peer_pa = t.peer_pa;
      g_xform.peer_pamax = t.peer_pamax;
      g_xform.self_pa = t.self_pa;
      g_xform.self_pamax = t.self_pamax;
      g_xform.peer_en = t.peer_en;
      g_xform.peer_enmax = t.peer_enmax;
      g_xform.peer_en_redline = (t.flags & 2u) != 0;
      g_xform.self_en = t.self_en;
      g_xform.self_enmax = t.self_enmax;
      g_xform.self_en_redline = (t.flags & 4u) != 0;
      g_xform.peer_wpn.active = t.peer_wpn_active;
      g_xform.peer_wpn.purged = t.peer_wpn_purged;
      std::memcpy(g_xform.peer_wpn.ammo, t.peer_ammo, sizeof t.peer_ammo);
      g_xform.self_wpn_active = t.self_wpn_active;
      g_xform.self_wpn_purged = t.self_wpn_purged;
      g_xform.peer_paused = (t.flags & 1u) != 0;
      ++g_xform.samples;
    }
    g_have_xform = true;
    g_last_xform_ms = now_ms();
    g_recv.fetch_add(1, std::memory_order_relaxed);
  } else if (h.type >= 4 && h.type <= 6) {
    g_last_pad_ms = now_ms();  // any session packet proves the peer is alive
    g_recv.fetch_add(1, std::memory_order_relaxed);
    rt::game::coopsession::on_packet(h.type, buf + kHeader, size_t(n) - kHeader);
  } else if (h.type == kTypeSnapshot) {
    // Records are absolute state, so a datagram that arrives late is still usable; only the
    // sequence guard of the other types is skipped here.
    if (n < int(kHeader + sizeof(SnapshotPayload))) return;
    SnapshotPayload sp{};
    std::memcpy(&sp, buf + kHeader, sizeof sp);
    g_last_pad_ms = now_ms();
    if (sp.epoch != rt::game::coopsession::epoch() || !rt::game::coopsession::linked_in_mission()) {
      g_dropped.fetch_add(1, std::memory_order_relaxed);
      return;  // a snapshot of another mission instance, or this side is not in the mission yet
    }
    const size_t avail = (size_t(n) - kHeader - sizeof sp) / sizeof(SnapshotRecordWire);
    size_t count = sp.count;
    if (count > avail) count = avail;
    std::lock_guard lk(g_mu);
    for (size_t i = 0; i < count; ++i) {
      SnapshotRecordWire w{};
      std::memcpy(&w, buf + kHeader + sizeof sp + i * sizeof w, sizeof w);
      rt::game::coopnet::SnapshotRecord r{};
      r.addr = w.addr;
      r.entity_id = w.entity_id;
      r.ord = w.ord;
      r.part_state = w.part_state;
      std::memcpy(r.position, w.position, sizeof r.position);
      std::memcpy(r.rotation, w.rotation, sizeof r.rotation);
      r.hp = w.hp;
      r.ext_flags = w.ext_flags;
      r.wpn_active = w.wpn_active;
      r.wpn_purged = w.wpn_purged;
      std::memcpy(r.in_axes, w.in_axes, sizeof r.in_axes);
      std::memcpy(r.in_btn, w.in_btn, sizeof r.in_btn);
      r.pitch = w.pitch;
      r.pa = w.pa;
      if (g_snap_in.size() < 8192) g_snap_in.push_back(r);
    }
    g_snap_recv_recs.fetch_add(count, std::memory_order_relaxed);
    if (sp.counter > g_snap_counter.load(std::memory_order_relaxed)) g_snap_counter = sp.counter;
    g_snap_tag = sp.tag;
    g_last_pad_ms = now_ms();  // a snapshot also proves the peer is alive
    g_recv.fetch_add(1, std::memory_order_relaxed);
  }
}

void receive_all() {
  if (g_transport_steam) {
    rt::game::coopsteam::pump([](uint64_t, const uint8_t* data, size_t len) {
      if (rt::game::coopsteam::take_peer_changed()) g_seq_reset.store(true, std::memory_order_relaxed);
      process_datagram(data, int(len));
    });
    if (rt::game::coopsteam::take_peer_changed()) g_seq_reset.store(true, std::memory_order_relaxed);
    return;
  }
  uint8_t buf[kMaxDatagram + 8];
  for (int i = 0; i < 128; ++i) {  // bounded: never spin on a busy socket
    sockaddr_in from{};
    int flen = sizeof from;
    int n = recvfrom(g_sock, reinterpret_cast<char*>(buf), sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen);
    if (n == SOCKET_ERROR) {
      if (WSAGetLastError() != WSAEWOULDBLOCK) g_bad.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (n < int(kHeader)) continue;
    Header h{};
    std::memcpy(&h, buf, kHeader);
    if (std::memcmp(h.magic, kMagic, 4) != 0 || h.version != kVersion) {
      g_bad.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    // The host takes the peer from the first packet; both sides then only accept that peer.
    if (g_host) note_peer(from);
    {
      std::lock_guard lk(g_mu);
      if (g_have_peer && (from.sin_addr.s_addr != g_peer.sin_addr.s_addr || from.sin_port != g_peer.sin_port)) continue;
    }
    process_datagram(buf, n);
  }
}

}  // namespace

namespace rt::game::coopnet {

bool init() {
  if (g_started) return active();
  g_started = true;
  char host[64]{}, join[128]{}, steam_host[32]{}, steam_join[64]{};
  bool want_steam_host = env("AC4_COOP_STEAM_HOST", steam_host, sizeof steam_host) && steam_host[0] != '0';
  bool want_steam_join = env("AC4_COOP_STEAM_JOIN", steam_join, sizeof steam_join);
  bool want_host = env("AC4_COOP_HOST", host, sizeof host);
  bool want_join = env("AC4_COOP_JOIN", join, sizeof join);
  if (!want_steam_host && !want_steam_join && !want_host && !want_join) return false;

  if (want_steam_host || want_steam_join) {
    uint64_t join_lid = 0;
    if (want_steam_join && std::strcmp(steam_join, "auto") != 0 && std::strcmp(steam_join, "1") != 0) {
      join_lid = std::strtoull(steam_join, nullptr, 10);
    }
    if (!rt::game::coopsteam::init(want_steam_host, join_lid)) {
      LOG_E("coop", "COOP NET: Steam Spacewar transport failed to initialize; no link");
      return false;
    }
    g_transport_steam = true;
    g_host = want_steam_host;
  } else {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      LOG_E("coop", "COOP NET: WSAStartup failed; no link");
      return false;
    }
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
      LOG_E("coop", "COOP NET: socket() failed (%d); no link", WSAGetLastError());
      return false;
    }
    u_long nonblocking = 1;
    ioctlsocket(g_sock, FIONBIO, &nonblocking);

    if (want_host) {
      const unsigned port = unsigned(strtoul(host, nullptr, 10));
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_port = htons(uint16_t(port));
      if (bind(g_sock, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == SOCKET_ERROR) {
        LOG_E("coop", "COOP NET: bind udp/%u failed (%d); no link", port, WSAGetLastError());
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
      }
      g_host = true;
      LOG_I("coop", "COOP NET: host, listening on udp/%u (the peer is whoever sends first)", port);
    } else {
      sockaddr_in addr{};
      if (!parse_endpoint(join, &addr)) {
        LOG_E("coop", "COOP NET: bad AC4_COOP_JOIN '%s' (want ip:port); no link", join);
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
      }
      {
        std::lock_guard lk(g_mu);
        g_peer = addr;
        g_have_peer = true;
      }
      char ip[64] = {};
      inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof ip);
      LOG_I("coop", "COOP NET: joining %s:%u", ip, unsigned(ntohs(addr.sin_port)));
    }
  }
  char sim_buf[32]{};
  if (env("AC4_COOP_SIM_LATENCY_MS", sim_buf, sizeof sim_buf)) {
    g_sim_latency_ms = std::atoi(sim_buf);
    if (env("AC4_COOP_SIM_JITTER_MS", sim_buf, sizeof sim_buf)) g_sim_jitter_ms = std::atoi(sim_buf);
    if (g_sim_latency_ms > 0)
      LOG_W("coop", "COOP NET: simulated one-way latency %d ms (+/- %d ms jitter)", g_sim_latency_ms, g_sim_jitter_ms);
  }
  return true;
}

void shutdown() {
  if (g_transport_steam) {
    rt::game::coopsteam::shutdown();
    g_transport_steam = false;
  }
  if (g_sock != INVALID_SOCKET) {
    closesocket(g_sock);
    g_sock = INVALID_SOCKET;
  }
}

bool active() { return g_transport_steam ? rt::game::coopsteam::active() : (g_sock != INVALID_SOCKET); }
bool transport_steam() { return g_transport_steam && rt::game::coopsteam::active(); }
std::string transport_status() { return g_transport_steam ? rt::game::coopsteam::status_label() : std::string(); }

bool role_host() { return active() && g_host; }
bool role_join() { return active() && !g_host; }

size_t send_snapshot(const SnapshotRecord* recs, size_t n, uint32_t tag) {
  if (!role_host()) return 0;
  const size_t per = (kMaxDatagram - kHeader - sizeof(SnapshotPayload)) / sizeof(SnapshotRecordWire);
  const uint32_t counter = g_snap_counter.fetch_add(1, std::memory_order_relaxed) + 1;
  size_t pkts = 0;
  size_t off = 0;
  do {
    const size_t count = (n - off) < per ? (n - off) : per;
    uint8_t payload[kMaxDatagram - kHeader];
    SnapshotPayload sp{};
    sp.counter = counter;
    sp.tag = tag;
    sp.count = uint16_t(count);
    sp.epoch = rt::game::coopsession::epoch();
    std::memcpy(payload, &sp, sizeof sp);
    for (size_t i = 0; i < count; ++i) {
      SnapshotRecordWire w{};
      w.addr = recs[off + i].addr;
      w.entity_id = recs[off + i].entity_id;
      w.ord = recs[off + i].ord;
      w.part_state = recs[off + i].part_state;
      std::memcpy(w.position, recs[off + i].position, sizeof w.position);
      std::memcpy(w.rotation, recs[off + i].rotation, sizeof w.rotation);
      w.hp = recs[off + i].hp;
      w.ext_flags = recs[off + i].ext_flags;
      w.wpn_active = recs[off + i].wpn_active;
      w.wpn_purged = recs[off + i].wpn_purged;
      std::memcpy(w.in_axes, recs[off + i].in_axes, sizeof w.in_axes);
      std::memcpy(w.in_btn, recs[off + i].in_btn, sizeof w.in_btn);
      w.pitch = recs[off + i].pitch;
      w.pa = recs[off + i].pa;
      std::memcpy(payload + sizeof sp + i * sizeof w, &w, sizeof w);
    }
    send_packet(kTypeSnapshot, ++g_snap_seq, payload, sizeof sp + count * sizeof(SnapshotRecordWire));
    g_snap_sent_recs.fetch_add(count, std::memory_order_relaxed);
    g_snap_sent_pkts.fetch_add(1, std::memory_order_relaxed);
    ++pkts;
    off += count;
  } while (off < n);
  return pkts;
}

size_t take_snapshot(SnapshotRecord* out, size_t max, uint32_t* counter) {
  if (!out || !max) return 0;
  std::lock_guard lk(g_mu);
  const size_t n = g_snap_in.size() < max ? g_snap_in.size() : max;
  for (size_t i = 0; i < n; ++i) out[i] = g_snap_in[i];
  g_snap_in.erase(g_snap_in.begin(), g_snap_in.begin() + std::ptrdiff_t(n));
  if (counter) *counter = g_snap_counter.load(std::memory_order_relaxed);
  return n;
}

void snapshot_totals(uint64_t* sent_recs, uint64_t* sent_pkts, uint64_t* recv_recs) {
  if (sent_recs) *sent_recs = g_snap_sent_recs.load(std::memory_order_relaxed);
  if (sent_pkts) *sent_pkts = g_snap_sent_pkts.load(std::memory_order_relaxed);
  if (recv_recs) *recv_recs = g_snap_recv_recs.load(std::memory_order_relaxed);
}

uint32_t snapshot_tag() { return g_snap_tag.load(std::memory_order_relaxed); }

bool remote_pad(PadState* out) {
  if (!active() || !out) return false;
  if (!rt::game::coopsession::linked_in_mission()) return false;  // menus never see the partner's pad
  const uint32_t last = g_last_pad_ms.load(std::memory_order_relaxed);
  if (!last || now_ms() - last > uint32_t(kPadTimeout * 1000)) return false;
  std::lock_guard lk(g_mu);
  *out = g_pad;
  if (g_rx_edge_ttl > 0 && g_rx_edge_bits) {
    out->buttons |= g_rx_edge_bits;
    if (--g_rx_edge_ttl == 0) g_rx_edge_bits = 0;
  }
  return true;
}

bool peer_present() {
  if (!active()) return false;
  const uint32_t last = g_last_pad_ms.load(std::memory_order_relaxed);
  return last && now_ms() - last <= uint32_t(kPeerTimeout * 1000);
}

void set_local_transform(const float position[3], const float rotation[9], const float velocity[3], int mission_tag) {
  if (!active()) return;
  std::memcpy(g_local_pos, position, sizeof g_local_pos);
  std::memcpy(g_local_rot, rotation, sizeof g_local_rot);
  std::memcpy(g_local_vel, velocity, sizeof g_local_vel);
  g_mission_tag = mission_tag;
}

void set_local_peer_ap(float ap, float apmax) {
  std::lock_guard lk(g_mu);
  g_local_ap = ap;
  g_local_apmax = apmax;
}

void set_local_peer_pa(float pa, float pamax) {
  std::lock_guard lk(g_mu);
  g_local_peer_pa = pa;
  g_local_peer_pamax = pamax;
}

void set_local_peer_en(float en, float enmax, bool redline) {
  std::lock_guard lk(g_mu);
  g_local_peer_en = en;
  g_local_peer_enmax = enmax;
  g_local_peer_en_redline = redline;
}

void set_local_peer_weapons(const WeaponSyncState& w) {
  std::lock_guard lk(g_mu);
  g_local_peer_wpn = w;
}

void set_local_self_vitals(float ap, float apmax, float pa, float pamax) {
  std::lock_guard lk(g_mu);
  g_local_self_ap = ap;
  g_local_self_apmax = apmax;
  g_local_self_pa = pa;
  g_local_self_pamax = pamax;
}

void set_local_self_en(float en, float enmax, bool redline) {
  std::lock_guard lk(g_mu);
  g_local_self_en = en;
  g_local_self_enmax = enmax;
  g_local_self_en_redline = redline;
}

void set_local_self_weapons(uint8_t active, uint8_t purged) {
  std::lock_guard lk(g_mu);
  g_local_self_wpn_active = active;
  g_local_self_wpn_purged = purged;
}

PartnerVitals partner_vitals() {
  PartnerVitals v{};
  if (!active()) return v;
  v.peer_up = peer_present();
  if (!rt::game::coopsession::linked_in_mission()) return v;
  std::lock_guard lk(g_mu);
  if (g_have_xform.load(std::memory_order_relaxed) &&
      uint16_t(g_xform.mission_tag) == rt::game::coopsession::epoch()) {
    v.paused = g_xform.peer_paused;
  }
  if (g_host) {
    // Host is authoritative for Join's AC (its local body2).
    if (g_local_apmax >= 1000.0f && std::isfinite(g_local_ap)) {
      v.ap = g_local_ap;
      v.apmax = g_local_apmax;
      v.pa = (g_local_peer_pamax > 1.0f && std::isfinite(g_local_peer_pa)) ? g_local_peer_pa : 0.0f;
      v.pamax = (g_local_peer_pamax > 1.0f && std::isfinite(g_local_peer_pamax)) ? g_local_peer_pamax : 0.0f;
      v.valid = true;
    }
  } else if (g_have_xform.load(std::memory_order_relaxed) &&
             uint16_t(g_xform.mission_tag) == rt::game::coopsession::epoch()) {
    // Join reads Host's authoritative self vitals from TransformPacket.
    if (g_xform.self_apmax >= 1000.0f && std::isfinite(g_xform.self_ap)) {
      v.ap = g_xform.self_ap;
      v.apmax = g_xform.self_apmax;
      v.pa = (g_xform.self_pamax > 1.0f && std::isfinite(g_xform.self_pa)) ? g_xform.self_pa : 0.0f;
      v.pamax = (g_xform.self_pamax > 1.0f && std::isfinite(g_xform.self_pamax)) ? g_xform.self_pamax : 0.0f;
      v.valid = true;
    }
  }
  return v;
}

void set_local_pitch(float pitch) {
  if (std::isfinite(pitch) && std::fabs(pitch) <= 1.6f) g_local_pitch.store(pitch, std::memory_order_relaxed);
}

bool remote_pitch(float* pitch) {
  if (!active() || !g_have_xform.load(std::memory_order_relaxed) || !pitch) return false;
  if (!rt::game::coopsession::linked_in_mission()) return false;
  std::lock_guard lk(g_mu);
  if (uint16_t(g_xform.mission_tag) != rt::game::coopsession::epoch() || !g_xform.has_pitch) return false;
  *pitch = g_xform.pitch;
  return true;
}

bool peer_ap(float* ap, float* apmax) {
  if (!active() || !g_have_xform.load(std::memory_order_relaxed)) return false;
  if (!rt::game::coopsession::linked_in_mission()) return false;
  std::lock_guard lk(g_mu);
  if (uint16_t(g_xform.mission_tag) != rt::game::coopsession::epoch()) return false;
  // A missing/freed body reads 0/0; only a real AC's AP pair (thousands of points) is authoritative.
  if (g_xform.apmax < 1000.0f) return false;
  if (ap) *ap = g_xform.ap;
  if (apmax) *apmax = g_xform.apmax;
  return true;
}

bool peer_pa(float* pa, float* pamax) {
  if (!active() || !g_have_xform.load(std::memory_order_relaxed)) return false;
  if (!rt::game::coopsession::linked_in_mission()) return false;
  std::lock_guard lk(g_mu);
  if (uint16_t(g_xform.mission_tag) != rt::game::coopsession::epoch()) return false;
  if (g_xform.peer_pamax <= 1.0f || !std::isfinite(g_xform.peer_pa)) return false;
  if (pa) *pa = g_xform.peer_pa;
  if (pamax) *pamax = g_xform.peer_pamax;
  return true;
}

bool peer_en(float* en, float* enmax, bool* redline) {
  if (!active() || !g_have_xform.load(std::memory_order_relaxed)) return false;
  if (!rt::game::coopsession::linked_in_mission()) return false;
  std::lock_guard lk(g_mu);
  if (uint16_t(g_xform.mission_tag) != rt::game::coopsession::epoch()) return false;
  if (g_xform.peer_enmax <= 10.0f || !std::isfinite(g_xform.peer_en)) return false;
  if (en) *en = g_xform.peer_en;
  if (enmax) *enmax = g_xform.peer_enmax;
  if (redline) *redline = g_xform.peer_en_redline;
  return true;
}

bool peer_weapons(WeaponSyncState* out) {
  if (!active() || !g_have_xform.load(std::memory_order_relaxed) || !out) return false;
  if (!rt::game::coopsession::linked_in_mission()) return false;
  std::lock_guard lk(g_mu);
  if (uint16_t(g_xform.mission_tag) != rt::game::coopsession::epoch()) return false;
  *out = g_xform.peer_wpn;
  return true;
}

bool remote_self_weapons(uint8_t* active_out, uint8_t* purged_out) {
  if (!active() || !g_have_xform.load(std::memory_order_relaxed)) return false;
  if (!rt::game::coopsession::linked_in_mission()) return false;
  std::lock_guard lk(g_mu);
  if (uint16_t(g_xform.mission_tag) != rt::game::coopsession::epoch()) return false;
  if (active_out) *active_out = g_xform.self_wpn_active;
  if (purged_out) *purged_out = g_xform.self_wpn_purged;
  return true;
}

bool remote_transform(Transform* out) {
  if (!active() || !g_have_xform.load(std::memory_order_relaxed) || !out) return false;
  if (!rt::game::coopsession::linked_in_mission()) return false;
  std::lock_guard lk(g_mu);
  if (uint16_t(g_xform.mission_tag) != rt::game::coopsession::epoch()) return false;
  *out = g_xform;
  const uint32_t last = g_last_xform_ms.load(std::memory_order_relaxed);
  out->age_ms = last ? (now_ms() - last) : 0;
  return true;
}

float last_position_error() { return g_pos_error.load(std::memory_order_relaxed); }

void note_local_sim_tick() { g_last_sim_ms.store(now_ms(), std::memory_order_relaxed); }

bool local_paused() {
  if (!active() || !rt::game::coopsession::linked_in_mission()) return false;
  const uint32_t last = g_last_sim_ms.load(std::memory_order_relaxed);
  return last != 0 && (now_ms() - last > 120u);
}

void pump() {
  if (!active()) return;
  flush_delayed_sends();
  std::lock_guard lk(g_rx_mu);
  receive_all();
}

void send_raw(uint8_t type, const void* payload, size_t len) {
  if (!active()) return;
  send_packet(type, 0, payload, len);
}

void set_local_pad(const PadState& p) {
  std::lock_guard lk(g_mu);
  g_local_pad = p;
}

void frame(const PadState&) {
  if (!active()) return;
  pump();
  rt::game::coopsession::frame();
  PadState local;
  {
    std::lock_guard lk(g_mu);
    local = g_local_pad;
  }
  const bool paused = local_paused();
  static bool was_paused = false;
  const bool just_unpaused = was_paused && !paused;
  if (paused != was_paused) {
    LOG_I("coop", "COOP NET: local simulation %s", paused ? "PAUSED (sending neutral pad)" : "RESUMED (forcing transform sync)");
    was_paused = paused;
  }
  if (!rt::game::coopsession::linked_in_mission() || paused) local = PadState{};  // neutral outside a mission or while paused
  const uint64_t f = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;

  // Reliable ACKed rising-edge delivery: queue every 0->1 digital button transition and retransmit
  // the oldest un-ACKed edge in PadPacket until the peer echoes edge_ack (2.5 s stale cap), so
  // toggle taps (Switch Weapon, Purge, OB, Assault Armor) survive even >1 s packet outages.
  const uint16_t rising = uint16_t(local.buttons & ~g_tx_prev_buttons);
  g_tx_prev_buttons = local.buttons;
  uint16_t out_edge_seq = 0, out_edge_bits = 0, out_edge_ack = 0;
  {
    std::lock_guard lk(g_mu);
    const uint32_t tnow = now_ms();
    if (!rt::game::coopsession::linked_in_mission()) {
      g_tx_edges.clear();
    } else if (rising) {
      if (++g_tx_edge_seq == 0) g_tx_edge_seq = 1;
      if (g_tx_edges.size() >= 16) g_tx_edges.pop_front();
      g_tx_edges.push_back({g_tx_edge_seq, rising, tnow});
      g_edges_sent.fetch_add(1, std::memory_order_relaxed);
    }
    while (!g_tx_edges.empty() && (tnow - g_tx_edges.front().ts_ms) > 2500u) g_tx_edges.pop_front();
    if (!g_tx_edges.empty()) {
      out_edge_seq = g_tx_edges.front().seq;
      out_edge_bits = g_tx_edges.front().bits;
    }
    out_edge_ack = g_rx_edge_seq;
  }

  PadPacket p{};
  p.buttons = local.buttons;
  p.lx = local.lx;
  p.ly = local.ly;
  p.rx = local.rx;
  p.ry = local.ry;
  p.lt = local.lt;
  p.rt = local.rt;
  p.port = 0;
  p.edge_seq = out_edge_seq;
  p.edge_buttons = out_edge_bits;
  p.edge_ack = out_edge_ack;
  send_packet(kTypePad, ++g_pad_seq, &p, sizeof p);

  if (f % uint64_t(kTransformEvery) == 0 || just_unpaused) {
    TransformPacket t{};
    t.mission_tag = int16_t(rt::game::coopsession::epoch());
    std::memcpy(t.position, g_local_pos, sizeof t.position);
    std::memcpy(t.rotation, g_local_rot, sizeof t.rotation);
    if (!paused) std::memcpy(t.velocity, g_local_vel, sizeof t.velocity);
    uint8_t flags = paused ? 1u : 0u;
    {
      std::lock_guard lk(g_mu);
      t.ap = g_local_ap;
      t.apmax = g_local_apmax;
      t.self_ap = g_local_self_ap;
      t.self_apmax = g_local_self_apmax;
      t.peer_pa = g_local_peer_pa;
      t.peer_pamax = g_local_peer_pamax;
      t.self_pa = g_local_self_pa;
      t.self_pamax = g_local_self_pamax;
      t.peer_en = g_local_peer_en;
      t.peer_enmax = g_local_peer_enmax;
      if (g_local_peer_en_redline) flags |= 2u;
      t.self_en = g_local_self_en;
      t.self_enmax = g_local_self_enmax;
      if (g_local_self_en_redline) flags |= 4u;
      t.peer_wpn_active = g_local_peer_wpn.active;
      t.peer_wpn_purged = g_local_peer_wpn.purged;
      std::memcpy(t.peer_ammo, g_local_peer_wpn.ammo, sizeof t.peer_ammo);
      t.self_wpn_active = g_local_self_wpn_active;
      t.self_wpn_purged = g_local_self_wpn_purged;
    }
    t.pitch = g_local_pitch.load(std::memory_order_relaxed);
    t.flags = flags;
    send_packet(kTypeTransform, ++g_xform_seq, &t, sizeof t);
  }
  flush_delayed_sends();

  // The error the caller (coop_body.cpp) acts on: the local AC2 against the peer's AC1.
  Transform t;
  double local_pos[3];
  if (remote_transform(&t) && rt::game::coop_body_position(2, local_pos) &&
      (t.position[0] * t.position[0] + t.position[1] * t.position[1] + t.position[2] * t.position[2] > 1.0f)) {
    const double dx = local_pos[0] - t.position[0], dy = local_pos[1] - t.position[1], dz = local_pos[2] - t.position[2];
    const double err = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (std::isfinite(err)) g_pos_error = float(err);
  }
}

void report() {
  if (!active()) return;
  const double now = rt::log_time();
  if (now - g_last_report < 1.0) return;
  g_last_report = now;
  Transform t;
  const bool have = remote_transform(&t);
  const bool peer = peer_present();
  LOG_I("coop", "COOP NET: %s (%s) | sent %llu recv %llu dropped %llu bad %llu | edges %llu/%llu | peer %s | pad age %u ms | xform %s, error %.2f | snap sent %llu rec in %llu pkt, recv %llu rec",
        g_host ? "host" : "join", g_transport_steam ? "steam" : "udp", (unsigned long long)g_sent.load(), (unsigned long long)g_recv.load(),
        (unsigned long long)g_dropped.load(), (unsigned long long)g_bad.load(),
        (unsigned long long)g_edges_acked.load(), (unsigned long long)g_edges_sent.load(),
        peer ? "UP" : "down",
        unsigned(now_ms() - g_last_pad_ms.load(std::memory_order_relaxed)),
        have ? ac4::strprintf("%u samples, mission %d", t.samples, t.mission_tag).c_str() : "none",
        double(g_pos_error.load()), (unsigned long long)g_snap_sent_recs.load(),
        (unsigned long long)g_snap_sent_pkts.load(), (unsigned long long)g_snap_recv_recs.load());
}

}  // namespace rt::game::coopnet
