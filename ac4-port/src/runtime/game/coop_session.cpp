// Co-op session layer: lobby, mission start, load barrier, host->join mission events, Lua channel.
// See coop_session.h for the contract and research\28_HOST_DRIVEN_SESSION.md for the measurements.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ppu/ppu_recomp.h"
#include "runtime/game/coop_body.h"
#include "runtime/game/coop_build.h"
#include "runtime/game/coop_net.h"
#include "runtime/game/coop_session.h"
#include "runtime/game/frame_policy.h"
#include "runtime/gpu/window.h"
#include "runtime/log.h"
#include "runtime/memory.h"

void f_002a0bd4(ppu::Ctx&);  // lua_pushlstring(L, s, len)
void f_007cca94(ppu::Ctx&);  // the mission screen's sortie start (hooked: sortie_start)

namespace {

using ppu::r32;
using ppu::r8;
namespace cs = rt::game::coopsession;
namespace net = rt::game::coopnet;

constexpr uint8_t kTypeLobby = 4;
constexpr uint8_t kTypeEvent = 5;
constexpr uint8_t kTypeDesign = 6;
constexpr uint8_t kDesignChunks = 8;
constexpr uint16_t kDesignChunkSize = cs::kAcDesignBytes / kDesignChunks;  // 960 bytes x 8 = 7680
static_assert(kDesignChunks * kDesignChunkSize == cs::kAcDesignBytes, "design chunking exact");
// The scene tree around the hub (research\28 s2, measured in run s28a): the root scene (vtable
// 0x10130680) keeps its children pre-built: +104 the hub (MissionSelectScene, 0x1013b3b0), +92 the
// mission launcher (0x1013a820, mission record at +68). Accepting a mission in the hub makes the root
// switch to the launcher (f_0073da70 -> f_0074cf88: +24 next, +28 request, +60/+64 fades 1.0/0.5).
constexpr u32 kRootVt = 0x10130680;
// The mission-flow scene (vtable 0x101385f8) is what the player is on while choosing a mission and
// preparing (its sub-scene 0x10138598 is the mission list with GARAGE). Its sortie child (+68,
// vtable 0x10138b18) is started by f_007cca94(flow, mission index, r5, r6) - the real "start this
// mission" (run s28c: the flow scene itself is built on every hub entry, with the default mission).
constexpr u32 kFlowVt = 0x101385f8, kFlowSortie = 68, kSortieVt = 0x10138b18;
// The mission list the flow scene runs as its sub-scene (vtable 0x10138598). Its update is the generic
// menu-scene update f_004dd4d0 (slot 3), which dispatches the list's confirm back to the flow scene
// (f_007cce6c -> f_007cca94): the join's automatic start is made from the same place (run s28d).
constexpr u32 kMissionListVt = 0x10138598;
std::atomic<u32> g_flow{0};  // the latest mission-flow scene (ctor_b this)
// The TOC (r2) of the mission-flow code, seen at its constructor: f_007cca94 addresses its globals
// through r2, and the menu update that reaches it does so through a function descriptor with its
// own TOC - calling it with the menu's r2 read garbage and crashed (run s28f).
std::atomic<u64> g_flow_toc{0};

// The sortie start's arguments as one lobby word: index (16 bits), r5 (signed 8), r6 (1), valid bit.
uint32_t key_encode(u64 r4, u64 r5, u64 r6) {
  return 0x80000000u | (u32(r4) & 0xffffu) | ((u32(r5) & 0xffu) << 16) | ((u32(r6) & 1u) << 24);
}
void key_decode(uint32_t k, ppu::Ctx& ctx) {
  ctx.r[4] = u64(k & 0xffffu);
  ctx.r[5] = u64(s64(int8_t((k >> 16) & 0xffu)));
  ctx.r[6] = u64((k >> 24) & 1u);
}
thread_local bool t_in_autostart = false;
bool g_join_ready = false;          // join: confirmed its sortie before the host had started one
uint16_t g_autostart_tried = 0;     // join: the host epoch the automatic start was last tried for
// join: the last host epoch this instance started. A mission is entered once: after it ends here (or
// the player quits it) while the host is still in it, it is not started again (run s28g re-entered
// epoch 30465 and replayed its events a second time).
uint16_t g_join_played = 0;

#pragma pack(push, 1)
struct LobbyWire {
  uint8_t phase;
  uint8_t role;  // 1 host, 2 join
  uint16_t mission;
  uint16_t epoch;
  uint16_t design_mask;  // bits 0..7: chunks of peer's AC design received for this epoch
  uint32_t key;          // the sortie start's arguments (key_encode), 0 = none
  uint32_t ev_ack;       // join: the highest event seq received contiguously
  uint32_t ev_count;     // host: events published in this epoch
};
struct EventWire {
  uint16_t epoch;
  uint16_t len;
  uint32_t seq;  // 1-based within the epoch
};
struct DesignWire {
  uint16_t epoch;
  uint8_t chunk;
  uint8_t total;
  uint16_t len;
  uint16_t reserved;
};
#pragma pack(pop)

std::mutex g_mu;

// Local state.
std::atomic<uint8_t> g_phase{cs::kMenu};
std::atomic<uint16_t> g_epoch{0};
std::atomic<uint16_t> g_mission{0};  // the mission number (Ac4SortieScene +7976), for display
std::atomic<uint32_t> g_key{0};      // the sortie being played (key_encode of f_007cca94's arguments)
std::atomic<u32> g_root{0};          // the root scene, from the scene-change log
double g_autostart_at = -1e9, g_idle_since = -1;
// join: a start decided inside the menu frame, made after it (autostart_after_frame)
// the per-player objects (vtable 0x101463c0) of the two copies of the mission script, [1] and [2]
constexpr u32 kPlayerObjVt = 0x101463c0u;
std::atomic<u32> g_player_obj[3] = {0, 0, 0};
std::atomic<uint16_t> g_kill_epoch{0};  // test aid: the epoch whose success stopped the autokill
bool g_autostart_pending = false;
u32 g_autostart_flow = 0, g_autostart_key = 0;
uint16_t g_epoch_counter = 0;

// Peer state (last lobby).
struct Peer {
  uint8_t phase = cs::kMenu;
  uint16_t mission = 0, epoch = 0;
  uint16_t design_mask = 0;
  uint32_t key = 0;
  uint32_t ev_ack = 0, ev_count = 0;
  double at = -1e9;
} g_peer;

// AC design exchange (7680 bytes = 7660 schematic + 20 colour bytes), keyed by epoch.
uint8_t g_local_design[cs::kAcDesignBytes] = {};
bool g_local_design_valid = false;
uint8_t g_peer_design[cs::kAcDesignBytes] = {};
uint16_t g_peer_design_epoch = 0;
uint16_t g_peer_design_mask = 0;
double g_last_design_send = -1e9;

// Host: events of the current epoch, seq = index + 1.
std::vector<std::string> g_events;
double g_last_resend = -1e9;
uint32_t g_sent_upto = 0;  // highest seq sent at least once

// Join: received events by seq, the next one the script takes, and the contiguous high-water mark.
std::map<uint32_t, std::string> g_rx;
uint32_t g_rx_next = 1;
uint32_t g_rx_contig = 0;

double g_last_lobby = -1e9;
double g_body_gone_since = -1;
std::string g_title;
uint64_t g_polls_sec = 0, g_events_sec = 0;
double g_last_stat = -1e9;

double now_s() { return rt::log_time(); }

bool host() { return net::role_host(); }
bool join() { return net::role_join(); }

void send_lobby() {
  LobbyWire w{};
  w.phase = g_phase.load();
  w.role = host() ? 1 : 2;
  w.mission = g_mission.load();
  w.epoch = g_epoch.load();
  w.key = g_key.load();
  {
    std::lock_guard lk(g_mu);
    w.design_mask = (g_peer_design_epoch == w.epoch) ? g_peer_design_mask : 0;
    w.ev_ack = g_rx_contig;
    w.ev_count = uint32_t(g_events.size());
  }
  net::send_raw(kTypeLobby, &w, sizeof w);
  g_last_lobby = now_s();
}

void send_missing_design_chunks() {
  const uint16_t ep = g_epoch.load();
  if (!ep) return;
  uint8_t local_copy[cs::kAcDesignBytes];
  uint16_t peer_mask = 0;
  {
    std::lock_guard lk(g_mu);
    if (!g_local_design_valid) return;
    if (g_peer.epoch == ep) peer_mask = g_peer.design_mask;
    if ((peer_mask & 0xffu) == 0xffu) return;
    std::memcpy(local_copy, g_local_design, sizeof local_copy);
  }
  uint8_t buf[sizeof(DesignWire) + kDesignChunkSize];
  for (uint8_t c = 0; c < kDesignChunks; ++c) {
    if (peer_mask & (1u << c)) continue;
    DesignWire dw{};
    dw.epoch = ep;
    dw.chunk = c;
    dw.total = kDesignChunks;
    dw.len = kDesignChunkSize;
    std::memcpy(buf, &dw, sizeof dw);
    std::memcpy(buf + sizeof dw, local_copy + size_t(c) * kDesignChunkSize, kDesignChunkSize);
    net::send_raw(kTypeDesign, buf, sizeof buf);
  }
  g_last_design_send = now_s();
}

void send_event(uint16_t epoch, uint32_t seq, const std::string& text) {
  uint8_t buf[1100];
  EventWire w{};
  w.epoch = epoch;
  w.len = uint16_t(text.size() > sizeof buf - sizeof w ? sizeof buf - sizeof w : text.size());
  w.seq = seq;
  std::memcpy(buf, &w, sizeof w);
  std::memcpy(buf + sizeof w, text.data(), w.len);
  net::send_raw(kTypeEvent, buf, sizeof w + w.len);
}

// Host: resend everything the join has not acknowledged (bounded per call).
void resend_events() {
  if (!host()) return;
  const uint16_t ep = g_epoch.load();
  if (!ep) return;
  std::vector<std::pair<uint32_t, std::string>> out;
  {
    std::lock_guard lk(g_mu);
    if (g_peer.epoch != ep) return;  // the join is not in this mission yet: it gets them on arrival
    const uint32_t from = g_peer.ev_ack + 1;
    for (uint32_t s = from; s <= g_events.size() && out.size() < 24; ++s) out.emplace_back(s, g_events[s - 1]);
  }
  for (auto& e : out) send_event(ep, e.first, e.second);
}

void set_title(const std::string& t) {
  if (t == g_title) return;
  g_title = t;
  rt::gpu::window_set_title(t);
  LOG_I("coop", "COOP SESSION: %s", t.c_str());
}

const char* phase_name(uint8_t p) {
  switch (p) {
    case cs::kMenu: return "menu";
    case cs::kSortie: return "sortie";
    case cs::kBarrier: return "ready";
    case cs::kPlaying: return "playing";
  }
  return "?";
}

void update_title() {
  const bool up = net::peer_present();
  Peer p;
  {
    std::lock_guard lk(g_mu);
    p = g_peer;
  }
  std::string role = host() ? "HOST" : "JOIN";
  std::string s = "Armored Core 4 co-op [" + role + "] ";
  if (!up) {
    s += host() ? "- waiting for the partner to connect" : "- connecting to the host...";
  } else if (join() && g_phase.load() == cs::kPlaying && p.phase == cs::kMenu) {
    s += "- host returned to the hub (pause and quit to rejoin)";
  } else if (join() && g_phase.load() == cs::kMenu && p.phase >= cs::kSortie && p.epoch && p.epoch == g_join_played) {
    s += "- mission over here; the host is still finishing it";
  } else if (join() && g_phase.load() == cs::kMenu && p.phase == cs::kPlaying && p.epoch) {
    s += "- host is mid-mission; waiting for the next sortie";
  } else if (join() && g_phase.load() == cs::kMenu && p.phase >= cs::kSortie && p.epoch) {
    s += "- the host started a mission: it starts here as soon as you are back on the mission screen";
  } else if (join() && g_phase.load() == cs::kMenu && g_join_ready) {
    s += "- you are ready; the mission starts when the host starts it";
  } else if (host() && g_phase.load() == cs::kBarrier && p.phase < cs::kSortie) {
    s += "- mission loaded; waiting for the partner to finish preparing";
  } else if (g_phase.load() == cs::kBarrier) {
    s += ac4::strprintf("- mission %03u loaded, waiting for the partner (partner: %s)", g_mission.load(), phase_name(p.phase));
  } else {
    s += ac4::strprintf("- you: %s %03u | partner: %s %03u", phase_name(g_phase.load()), g_mission.load(),
                        phase_name(p.phase), p.mission);
  }
  set_title(s);
}

// Leaving a mission is seen as the player's own body going away (a freed body no longer holds the
// actor vtable); the next sortie sets the phase again.
void phase_watchdog() {
  if (g_phase.load() != cs::kPlaying) {
    g_body_gone_since = -1;
    return;
  }
  // The sortie's own threads (frame_policy.cpp: a *SortieScene thread and the mission's FcsManager)
  // are the reliable "a mission is running" signal; a freed body can keep its vtable word for a while.
  const bool ok = rt::game::mission_running();
  const double t = now_s();
  static bool seen_running = false;
  if (ok) {
    seen_running = true;
    g_body_gone_since = -1;
    return;
  }
  if (!seen_running) return;  // between the barrier and the first gameplay frame
  if (g_body_gone_since < 0) g_body_gone_since = t;
  if (t - g_body_gone_since > 3.0) {
    LOG_I("coop", "COOP SESSION: mission %03u epoch %u over (player body gone); back to the menu phase", g_mission.load(),
          g_epoch.load());
    g_phase = cs::kMenu;
    g_body_gone_since = -1;
    seen_running = false;
  }
}

// --- the Lua channel -------------------------------------------------------------------------

std::string lua_arg1(u32 L) {
  if (!rt::vm::committed(L, 16)) return {};
  const u32 top = r32(L + 8), base = r32(L + 12);
  if (top <= base || !rt::vm::committed(base, 8)) return {};
  if (r32(base) != 4) return {};  // not a string
  const u32 ts = r32(base + 4);
  if (!rt::vm::committed(ts, 16)) return {};
  u32 len = r32(ts + 12);
  if (len > 4000) len = 4000;
  if (!rt::vm::committed(ts + 16, len ? len : 1)) return {};
  std::string s(len, '\0');
  for (u32 i = 0; i < len; ++i) s[i] = char(r8(ts + 16 + i));
  return s;
}

u32 g_lua_buf = 0;

// Pushes `s` as the C function's result. Returns the result count for r3.
int lua_return_string(ppu::Ctx& ctx, u32 L, const std::string& s) {
  std::lock_guard lk(g_mu);
  if (!g_lua_buf) g_lua_buf = rt::vm::alloc(rt::vm::Area::Main, 0x1000, 0x10);
  if (!g_lua_buf) return 0;
  const u32 n = u32(s.size() < 0xff0 ? s.size() : 0xff0);
  for (u32 i = 0; i < n; ++i) ppu::w8(g_lua_buf + i, u8(s[i]));
  ppu::w8(g_lua_buf + n, 0);
  const u64 lr = ctx.lr;
  ctx.r[3] = L;
  ctx.r[4] = g_lua_buf;
  ctx.r[5] = n;
  ctx.lr = 0;
  f_002a0bd4(ctx);
  ctx.lr = lr;
  return 1;
}

int lua_command(ppu::Ctx& ctx, u32 L, const std::string& cmd) {
  // cmd is the text after "\1C".
  const size_t sep = cmd.find('\2');
  const std::string verb = cmd.substr(0, sep);
  const std::string rest = sep == std::string::npos ? std::string() : cmd.substr(sep + 1);
  if (verb == "role") {
    if (!net::active()) return 0;
    return lua_return_string(ctx, L, host() ? "host" : "join");
  }
  if (verb == "ev") {  // host: a top-level handler is about to run
    if (!host()) return 0;
    const uint16_t ep = g_epoch.load();
    uint32_t seq = 0;
    {
      std::lock_guard lk(g_mu);
      if (ep) {
        g_events.push_back(rest);
        seq = uint32_t(g_events.size());
        g_sent_upto = seq;
      }
      ++g_events_sec;
    }
    if (seq) send_event(ep, seq, rest);
    {
      // test aid: the repeating autokill stops once a success-type handler has run (in any copy)
      const std::string head = rest.substr(0, rest.find(''));
      if (head.find("Destroyed") != std::string::npos || head.find("Success") != std::string::npos) g_kill_epoch = ep;
    }
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 2000)
      LOG_I("coop", "COOP EV host #%u epoch %u: %s", seq, ep, rest.substr(0, 160).c_str());
    return 0;
  }
  if (verb == "poll") {  // join: the next host event, if any
    std::string out;
    bool have = false;
    {
      std::lock_guard lk(g_mu);
      ++g_polls_sec;
      auto it = g_rx.find(g_rx_next);
      if (it != g_rx.end()) {
        out = it->second;
        g_rx.erase(it);
        ++g_rx_next;
        have = true;
      }
    }
    if (!have) return 0;
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 2000) LOG_I("coop", "COOP EV join replays #%u: %s", g_rx_next - 1, out.substr(0, 160).c_str());
    return lua_return_string(ctx, L, out);
  }
  if (verb == "log") {
    LOG_I("coop", "COOP LUA: %s", rest.substr(0, 400).c_str());
    return 0;
  }
  if (verb == "flag") {  // test switches from the environment, e.g. "autokill" = AC4_COOP_TEST_AUTOKILL
    if (rest == "autokill" && host() && g_kill_epoch.load() != g_epoch.load()) {
      const char* v = std::getenv("AC4_COOP_TEST_AUTOKILL");
      if (v && *v) return lua_return_string(ctx, L, v);
    }
    // "killp2" = AC4_COOP_TEST_KILLP2=<seconds>: the host kills the second player's body (entity
    // 1001) once, to exercise the second player's death path (research\28, s28z5)
    if (rest == "killp2" && host()) {
      const char* v = std::getenv("AC4_COOP_TEST_KILLP2");
      if (v && *v) return lua_return_string(ctx, L, v);
    }
    return 0;
  }
  if (verb == "epoch") return lua_return_string(ctx, L, ac4::strprintf("%u", g_epoch.load()));
  // Which player body this copy of the mission script belongs to (research\28, run s28q). Every AC
  // the sortie builds gets its own mission-event manager and its own run of the script's Initial,
  // inside that body's Build; so while the second build runs, the script being initialised is the
  // second player's. "2" there, "1" otherwise.
  if (verb == "who") {
    const int who = rt::game::coop_p2_building() ? 2 : 1;
    // print("\1Cwho", eventOperation): the userdata's payload is {class record, instance}; the
    // instance's +4 -> +4 is this player's per-player object (vtable 0x101463c0), the one the
    // eventOperation player methods act on.
    const u32 top = r32(L + 8), base = r32(L + 12);
    u32 obj = 0;
    if (base + 16 <= top && r32(base + 8) == 7) {
      const u32 ud = r32(base + 12);
      const u32 inst = rt::vm::committed(ud + 16, 8) ? r32(ud + 20) : 0;
      const u32 mid = rt::vm::committed(inst + 4, 4) ? r32(inst + 4) : 0;
      obj = rt::vm::committed(mid + 4, 4) ? r32(mid + 4) : 0;
      if (!rt::vm::committed(obj, 4) || r32(obj) != kPlayerObjVt) obj = 0;
    }
    g_player_obj[who] = obj;
    LOG_I("coop", "COOP LUA: script copy of player %d, per-player object 0x%08x", who, obj);
    return lua_return_string(ctx, L, who == 2 ? "2" : "1");
  }
  LOG_W("coop", "COOP LUA: unknown command '%s'", verb.c_str());
  return 0;
}

uint16_t new_epoch() {
  if (!g_epoch_counter) g_epoch_counter = uint16_t((GetTickCount64() >> 6) & 0x7f00);
  ++g_epoch_counter;
  g_epoch_counter &= 0x7fff;
  if (!g_epoch_counter) g_epoch_counter = 1;
  return g_epoch_counter;
}

void reset_rx() {
  std::lock_guard lk(g_mu);
  g_rx.clear();
  g_rx_next = 1;
  g_rx_contig = 0;
}

// f_007cca94(flow, mission index, r5, r6): a player starts a sortie from the mission screen.
// Returns false when the call must not go through (a join that is ready before the host started).
//   host: a new epoch; the arguments are announced (lobby key) and the join plays exactly them.
//   join: the host's sortie replaces whatever was chosen here; with no host sortie yet, the confirm
//         only marks the join ready, and the host's start later starts it (autostart_tick).
bool on_sortie_start(ppu::Ctx& ctx) {
  if (!net::active()) return true;
  const uint32_t mine = key_encode(ctx.r[4], ctx.r[5], ctx.r[6]);
  if (host()) {
    const uint16_t ep = new_epoch();
    {
      std::lock_guard lk(g_mu);
      g_events.clear();
      g_sent_upto = 0;
    }
    g_key = mine;
    g_mission = 0;
    g_epoch = ep;
    g_phase = cs::kSortie;
    LOG_W("coop", "COOP SESSION: host starts sortie index %u (r5 %d r6 %u, flow 0x%08x, lr 0x%08x) -> epoch %u",
          u32(ctx.r[4]) & 0xffff, int(s32(ctx.r[5])), u32(ctx.r[6]) & 1, u32(ctx.r[3]), u32(ctx.lr), ep);
    send_lobby();
    return true;
  }
  Peer p;
  {
    std::lock_guard lk(g_mu);
    p = g_peer;
  }
  if (p.phase >= cs::kSortie && p.phase <= cs::kPlaying && p.epoch && p.epoch == g_join_played) {
    // the mission screen retries a held confirm every frame: say it once per epoch
    static uint16_t said = 0;
    if (said != p.epoch)
      LOG_W("coop", "COOP SESSION: the host is still in mission epoch %u, which this join already played; waiting for the next one",
            p.epoch);
    said = p.epoch;
    g_join_ready = true;
    return false;
  }
  if (p.phase >= cs::kSortie && p.phase <= cs::kPlaying && p.epoch && p.key && net::peer_present()) {
    if (p.key != mine)
      LOG_W("coop", "COOP SESSION: join's sortie (index %u) replaced by the host's (index %u)", mine & 0xffff,
            p.key & 0xffff);
    key_decode(p.key, ctx);
    reset_rx();
    g_key = p.key;
    g_mission = 0;
    g_epoch = p.epoch;
    g_join_played = p.epoch;
    g_phase = cs::kSortie;
    g_join_ready = false;
    LOG_W("coop", "COOP SESSION: join starts the host's sortie index %u, epoch %u%s", p.key & 0xffff, p.epoch,
          t_in_autostart ? " (automatic)" : "");
    send_lobby();
    return true;
  }
  if (!g_join_ready)
    LOG_W("coop", "COOP SESSION: join is ready (chose index %u) before the host started a sortie; it waits on the mission screen",
          mine & 0xffff);
  g_join_ready = true;
  return false;
}

// Join, every tick of the mission-flow scene (on its own thread, after the scene's own update):
// once the host has started a sortie, start the same one here - when the player is on the mission
// screen itself (not in the garage, shop, ACSIS ... which run scene threads of their own) and the
// screen has been at rest for a second, or at once when they had already confirmed (ready).
void autostart_tick(ppu::Ctx& ctx, u32 flow) {
  if (!join() || !net::active() || g_phase.load() != cs::kMenu || !net::peer_present()) {
    g_idle_since = -1;
    return;
  }
  Peer p;
  {
    std::lock_guard lk(g_mu);
    p = g_peer;
  }
  // Only auto-start when host is still loading/at the barrier (not mid-mission kPlaying, which would
  // miss OnMissionStart and hang waiting for design exchange after a mid-mission reconnect).
  if ((p.phase != cs::kSortie && p.phase != cs::kBarrier) || !p.epoch || !p.key || p.epoch == g_join_played) {
    g_idle_since = -1;
    return;
  }
  const double t = now_s();
  if (g_autostart_tried == p.epoch && t - g_autostart_at < 8.0) return;  // one try per 8 s
  if (!rt::vm::committed(flow, 128) || r32(flow) != kFlowVt) return;
  const u32 sortie = r32(flow + kFlowSortie);
  if (!sortie || !rt::vm::committed(sortie, 8) || r32(sortie) != kSortieVt) return;
  const std::string scenes = rt::game::live_scenes();
  // Scene threads that stay up behind the mission screen do not count (TitleAfterInitScene lives for
  // the whole session - run s28e); any other one (GarageScene, AssemblyScene, AcsisScene, ...) means
  // the player is still preparing.
  bool on_mission_screen = true;
  for (size_t a = 0; a < scenes.size();) {
    size_t b = scenes.find(',', a);
    if (b == std::string::npos) b = scenes.size();
    const std::string n = scenes.substr(a, b - a);
    if (!n.empty() && n != "MissionSelectScene" && n != "TitleAfterInitScene") on_mission_screen = false;
    a = b + 1;
  }
  if (!on_mission_screen) {
    g_idle_since = -1;
    static std::string last;
    if (scenes != last) {
      last = scenes;
      LOG_I("coop", "COOP AUTOSTART: the host started a sortie; waiting while the player is in '%s'", scenes.c_str());
    }
    return;
  }
  if (g_idle_since < 0) g_idle_since = t;
  if (!g_join_ready && t - g_idle_since < 1.0) return;
  g_autostart_tried = p.epoch;
  g_autostart_at = t;
  LOG_W("coop", "COOP AUTOSTART: starting the host's sortie index %u (epoch %u) from flow 0x%08x", p.key & 0xffff,
        p.epoch, flow);
  // Not called from here: this tick runs inside the menu frame (f_005eda50), which holds its manager's
  // lock (+792); f_007cca94 hands a job to a worker and waits for it, and that job can need the same
  // lock (f_005ec64c) - a deadlock (runs s28p, s28r; cdb stacks in research\28). The call is made
  // by autostart_after_frame, once the menu frame has returned and released the lock.
  g_autostart_flow = flow;
  g_autostart_key = p.key;
  g_autostart_pending = true;
}

void autostart_call(ppu::Ctx& ctx, u32 flow, u32 key) {
  Peer p;
  p.key = key;
  // Called the way the mission screen calls it; every register touched is restored.
  const u64 r3 = ctx.r[3], r4 = ctx.r[4], r5 = ctx.r[5], r6 = ctx.r[6], lr = ctx.lr;
  // f_007cca94's own function descriptor (0x00e86cd0: code, TOC) gives the TOC to call it with.
  constexpr u32 kSortieStartOpd = 0x00e86cd0;
  if (!rt::vm::committed(kSortieStartOpd, 8) || r32(kSortieStartOpd) != 0x007cca94u) return;
  const u64 toc = r32(kSortieStartOpd + 4), r2 = ctx.r[2];
  if (toc != g_flow_toc.load())
    LOG_I("coop", "COOP AUTOSTART: TOC 0x%08x (r2 at the flow constructor was 0x%08x)", u32(toc), u32(g_flow_toc.load()));
  ctx.r[2] = toc;
  ctx.r[3] = flow;
  key_decode(p.key, ctx);
  t_in_autostart = true;
  f_007cca94(ctx);
  t_in_autostart = false;
  ctx.r[2] = r2;
  ctx.r[3] = r3;
  ctx.r[4] = r4;
  ctx.r[5] = r5;
  ctx.r[6] = r6;
  ctx.lr = lr;
}

}  // namespace

namespace rt::game::coopsession {

// The menu framework's frame (0x0091af7c) has returned: its lock is free, so a pending automatic
// start is made now (same thread, same frame as it was decided).
void autostart_after_frame(ppu::Ctx& ctx) {
  if (!g_autostart_pending) return;
  g_autostart_pending = false;
  const u32 flow = g_autostart_flow;
  // the scene may have moved on during the rest of the frame: the same checks as when it was decided
  if (g_phase.load() != cs::kMenu || !rt::vm::committed(flow, 128) || r32(flow) != kFlowVt) return;
  const u32 sortie = r32(flow + kFlowSortie);
  if (!sortie || !rt::vm::committed(sortie, 8) || r32(sortie) != kSortieVt) return;
  autostart_call(ctx, flow, g_autostart_key);
}

void reset_mission_objects() {
  g_player_obj[1].store(0, std::memory_order_relaxed);
  g_player_obj[2].store(0, std::memory_order_relaxed);
}

void note_scene_init() {
  if (!net::active()) return;
  if (host() && g_phase.load() != cs::kSortie) {
    // Entered Ac4SortieScene::Init without going through MissionSelectScene (e.g. Retry Mission
    // or New Game prologue m000): draw a fresh epoch and clear previous events.
    const uint16_t ep = new_epoch();
    {
      std::lock_guard lk(g_mu);
      g_events.clear();
      g_sent_upto = 0;
    }
    g_epoch = ep;
    g_phase = cs::kSortie;
    LOG_W("coop", "COOP SESSION: host sortie init without menu accept (retry/prologue) -> fresh epoch %u", ep);
    send_lobby();
  } else if (join() && g_phase.load() != cs::kSortie) {
    // Join clicked Retry (or started New Game m000) directly without going through MissionSelectScene:
    // clear epoch so load_barrier adopts host's fresh retry/prologue epoch seamlessly.
    reset_rx();
    Peer p;
    {
      std::lock_guard lk(g_mu);
      p = g_peer;
    }
    if ((p.phase == cs::kSortie || p.phase == cs::kBarrier) && p.epoch && p.epoch != g_epoch.load()) {
      g_epoch = p.epoch;
      g_join_played = p.epoch;
    } else {
      g_epoch = 0;  // on_packet() will adopt host's new epoch as soon as host enters kSortie/kBarrier
    }
    g_phase = cs::kSortie;
    LOG_W("coop", "COOP SESSION: join sortie init without menu accept (retry/prologue) -> epoch %u", g_epoch.load());
    send_lobby();
  }
}

void set_local_ac_design(const uint8_t* data, size_t len) {
  if (!data || len != kAcDesignBytes) return;
  const uint16_t ep = g_epoch.load();
  {
    std::lock_guard lk(g_mu);
    std::memcpy(g_local_design, data, kAcDesignBytes);
    g_local_design_valid = true;
  }
  LOG_I("coop", "COOP DESIGN: captured local AC design (%zu bytes) for epoch %u", len, ep);
  if (ep) send_missing_design_chunks();
}

bool get_peer_ac_design(uint8_t* out, size_t len) {
  if (!out || len != kAcDesignBytes) return false;
  const uint16_t ep = g_epoch.load();
  if (!ep) return false;
  std::lock_guard lk(g_mu);
  if (g_peer_design_epoch != ep || (g_peer_design_mask & 0xffu) != 0xffu) return false;
  std::memcpy(out, g_peer_design, kAcDesignBytes);
  return true;
}

uint16_t epoch() { return g_epoch.load(); }

void note_mission_number(int n) {
  if (n >= 0 && n < 1000) g_mission = uint16_t(n);
}

void note_scene_change(uint32_t cur, uint32_t next) {
  (void)next;
  if (cur && rt::vm::committed(cur, 4) && r32(cur) == kRootVt) g_root = cur;
}
Phase phase() { return Phase(g_phase.load()); }

bool linked_in_mission() {
  if (!net::active() || !net::peer_present()) return false;
  const uint16_t ep = g_epoch.load();
  if (!ep || g_phase.load() != kPlaying) return false;
  std::lock_guard lk(g_mu);
  return g_peer.epoch == ep && g_peer.phase >= kBarrier;
}

void on_packet(uint8_t type, const uint8_t* p, size_t n) {
  if (type == kTypeLobby) {
    if (n < sizeof(LobbyWire)) return;
    LobbyWire w;
    std::memcpy(&w, p, sizeof w);
    std::lock_guard lk(g_mu);
    const bool changed = w.phase != g_peer.phase || w.epoch != g_peer.epoch || w.mission != g_peer.mission;
    g_peer.phase = w.phase;
    g_peer.mission = w.mission;
    g_peer.epoch = w.epoch;
    g_peer.design_mask = w.design_mask;
    g_peer.key = w.key;
    g_peer.ev_ack = w.ev_ack;
    g_peer.ev_count = w.ev_count;
    g_peer.at = now_s();
    if (changed)
      LOG_I("coop", "COOP SESSION: partner is now %s, mission %03u record 0x%08x epoch %u", phase_name(w.phase), w.mission,
            w.key, w.epoch);
    // A join that accepted first (or clicked Retry) adopts the host's epoch once the host is ready.
    if (join() && (g_phase.load() == kSortie || g_phase.load() == kBarrier) && g_epoch.load() == 0 &&
        (w.phase == kSortie || w.phase == kBarrier) && w.epoch && w.epoch != g_join_played &&
        (w.key == g_key.load() || g_key.load() == 0)) {
      g_epoch = w.epoch;
      g_join_played = w.epoch;
      g_rx.clear();
      g_rx_next = 1;
      g_rx_contig = 0;
      LOG_W("coop", "COOP SESSION: join adopts the host's epoch %u for mission record 0x%08x", w.epoch, w.key);
    }
    return;
  }
  if (type == kTypeEvent) {
    if (n < sizeof(EventWire) || !join()) return;
    EventWire w;
    std::memcpy(&w, p, sizeof w);
    if (sizeof w + w.len > n) return;
    std::lock_guard lk(g_mu);
    if (w.epoch != g_epoch.load() || !w.seq) return;  // another mission instance (or not followed yet)
    if (w.seq < g_rx_next) return;                    // already taken by the script
    g_rx.emplace(w.seq, std::string(reinterpret_cast<const char*>(p + sizeof w), w.len));
    while (g_rx_contig + 1 < g_rx_next || g_rx.count(g_rx_contig + 1)) ++g_rx_contig;
    return;
  }
  if (type == kTypeDesign) {
    if (n < sizeof(DesignWire)) return;
    DesignWire dw{};
    std::memcpy(&dw, p, sizeof dw);
    if (!dw.epoch || dw.chunk >= kDesignChunks || dw.total != kDesignChunks || dw.len != kDesignChunkSize) return;
    if (sizeof dw + dw.len > n) return;
    bool completed = false;
    {
      std::lock_guard lk(g_mu);
      if (g_peer_design_epoch != dw.epoch) {
        g_peer_design_epoch = dw.epoch;
        g_peer_design_mask = 0;
        std::memset(g_peer_design, 0, sizeof g_peer_design);
      }
      const bool was_done = (g_peer_design_mask & 0xffu) == 0xffu;
      std::memcpy(g_peer_design + size_t(dw.chunk) * kDesignChunkSize, p + sizeof dw, dw.len);
      g_peer_design_mask |= uint16_t(1u << dw.chunk);
      completed = !was_done && (g_peer_design_mask & 0xffu) == 0xffu;
    }
    if (completed) {
      LOG_I("coop", "COOP DESIGN: received complete partner AC design (%zu bytes) for epoch %u", kAcDesignBytes, dw.epoch);
      send_lobby();
    }
    return;
  }
}

void frame() {
  if (!net::active()) return;
  const double t = now_s();
  if (t - g_last_lobby > 0.2) send_lobby();
  if (host() && t - g_last_resend > 0.25) {
    g_last_resend = t;
    resend_events();
  }
  phase_watchdog();
  update_title();
  if (t - g_last_stat > 5.0) {
    g_last_stat = t;
    uint64_t polls, evs;
    size_t queued;
    {
      std::lock_guard lk(g_mu);
      polls = g_polls_sec;
      evs = g_events_sec;
      queued = g_rx.size();
      g_polls_sec = g_events_sec = 0;
    }
    if (g_phase.load() != kMenu || polls || evs)
      LOG_I("coop", "COOP SESSION %s: phase %s mission %03u epoch %u | 5 s: %llu event(s) published, %llu poll(s), %zu queued, next %u",
            host() ? "host" : "join", phase_name(g_phase.load()), g_mission.load(), g_epoch.load(),
            (unsigned long long)evs, (unsigned long long)polls, queued, g_rx_next);
  }
}

void load_barrier() {
  if (!net::active()) return;
  if (g_phase.load() == kMenu) {
    // A sortie that did not come through the mission-flow constructor (should not happen): unsynced.
    LOG_W("coop", "COOP BARRIER: no mission accept was seen for this sortie; it runs unsynced");
    return;
  }
  g_phase = kBarrier;
  const double t0 = now_s();
  LOG_W("coop", "COOP BARRIER: mission %03u (record 0x%08x) epoch %u loaded; waiting for the partner", g_mission.load(),
        g_key.load(), g_epoch.load());
  double last_seen = t0;
  double both_at = -1.0;
  for (;;) {
    net::pump();
    const uint16_t ep = g_epoch.load();  // a join that came first adopts the host's epoch meanwhile
    bool peer_ready = false, designs_done = false;
    {
      std::lock_guard lk(g_mu);
      peer_ready = ep && g_peer.epoch == ep && g_peer.phase >= kBarrier;
      const bool got_peer = (g_peer_design_epoch == ep && (g_peer_design_mask & 0xffu) == 0xffu);
      const bool peer_got_mine = (g_peer.epoch == ep && (g_peer.design_mask & 0xffu) == 0xffu);
      const bool no_local = !g_local_design_valid;
      designs_done = (got_peer && (peer_got_mine || no_local));
    }
    const double t = now_s();
    if (peer_ready) {
      if (both_at < 0.0) both_at = t;
      if (designs_done || (t - both_at > 5.0)) break;
    }
    if (net::peer_present()) last_seen = t;
    // A loading partner sends nothing while its main thread is busy (the lobby goes out from the frame
    // hook), so silence alone is only fatal after a long time.
    if (t - last_seen > 300.0) {
      LOG_E("coop", "COOP BARRIER: no packet from the partner for 300 s; starting without them");
      break;
    }
    if (t - g_last_lobby > 0.05) send_lobby();
    if (t - g_last_design_send > 0.08) send_missing_design_chunks();
    update_title();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  g_phase = kPlaying;
  send_lobby();
  LOG_W("coop", "COOP BARRIER: both ready after %.1f s; mission %03u epoch %u starts", now_s() - t0, g_mission.load(),
        g_epoch.load());
}

}  // namespace rt::game::coopsession

namespace ppu {

// Lua's `print`: "\1C<command>" is the co-op channel (coop_session.h). Two C functions carry the
// name: the stock luaB_print (0x002a6bdc, base library) and the engine's own (0x00100224, OPD
// 0x00e4c948, from the {name, OPD} table at 0x00e12250, registered over the stock one). The mission
// scripts reach the engine's: hooking only the stock one left the co-op runtime silent (run s28b).
static bool lua_print_common(Ctx& ctx, const char* which) {
  const u32 L = u32(ctx.r[3]);
  const std::string a = lua_arg1(L);
  if (a.size() >= 2 && a[0] == '\1' && a[1] == 'C') {
    static std::atomic<int> seen{0};
    if (seen.fetch_add(1) < 3) LOG_I("coop", "COOP LUA: channel via the %s print (L 0x%08x)", which, L);
    // Research probe: print("\1Cfn\2<label>", f, ...) dumps the raw objects after the string; for a
    // function, its closure words (Lua 5.0 CClosure: isC at +6, nupvalues at +7, f at +12, upvalues
    // from +16) and the upvalue objects - how an engine binding is found at run time.
    if (a.compare(2, 3, "fn\2") == 0) {
      const u32 top = r32(L + 8), base = r32(L + 12);
      for (u32 o = base + 8; o < top && o < base + 8 * 8; o += 8) {
        const u32 tag = r32(o), v = r32(o + 4);
        std::string w;
        if ((tag == 6 || tag == 7) && rt::vm::committed(v, 64))
          for (u32 i = 0; i < 16; ++i) w += ac4::strprintf(" %08x", r32(v + 4 * i));
        LOG_I("coop", "COOP LUA fn %s: arg tag %u value 0x%08x |%s", a.substr(5).c_str(), tag, v, w.c_str());
        // a full userdata (the eventOperation): payload +16 -> the C++ object; its +4 is the inner object
        // the methods forward to (f_00b24bc4). Resolve the mission-ending slots on both.
        if (tag == 7 && rt::vm::committed(v + 16, 8)) {
          const u32 obj = r32(v + 20);  // payload {class record, instance}
          for (int lvl = 0; lvl < 3 && rt::vm::committed(obj, 8); ++lvl) {
            u32 o = obj;
            for (int i = 0; i < lvl && rt::vm::committed(o + 4, 4); ++i) o = r32(o + 4);
            if (!rt::vm::committed(o, 4)) break;
            const u32 vt = r32(o);
            if (!rt::vm::committed(vt, 0x120)) continue;
            auto slot = [&](u32 off) {
              const u32 opd = r32(vt + off);
              return rt::vm::committed(opd, 8) ? r32(opd) : 0u;
            };
            LOG_I("coop", "COOP LUA fn %s:   obj(+4 x%d) 0x%08x vt 0x%08x | 0x38 %08x 0x3c %08x 0x100 %08x 0x114 %08x", a.substr(5).c_str(),
                  lvl, o, vt, slot(0x38), slot(0x3c), slot(0x100), slot(0x114));
          }
        }
        if (tag == 6 && rt::vm::committed(v, 16) && r8(v + 6)) {
          const u32 f = r32(v + 12);
          LOG_I("coop", "COOP LUA fn %s: C closure f 0x%08x -> code 0x%08x toc 0x%08x, %u upvalue(s)", a.substr(5).c_str(), f,
                rt::vm::committed(f, 8) ? r32(f) : 0, rt::vm::committed(f, 8) ? r32(f + 4) : 0, r8(v + 7));
          for (u32 k = 0; k < r8(v + 7) && k < 4; ++k) {
            const u32 ut = r32(v + 16 + 8 * k), uv = r32(v + 20 + 8 * k);
            std::string uw;
            if (rt::vm::committed(uv, 32))
              for (u32 i = 0; i < 8; ++i) uw += ac4::strprintf(" %08x", r32(uv + 4 * i));
            LOG_I("coop", "COOP LUA fn %s:   upvalue %u tag %u value 0x%08x |%s", a.substr(5).c_str(), k, ut, uv, uw.c_str());
            // the userdata's payload (+16) points at the engine's method record: dump it, one level deep,
            // and name anything that looks like a function descriptor {code, toc}
            const u32 rec = rt::vm::committed(uv + 16, 4) ? r32(uv + 16) : 0;
            if (k == 1 && rt::vm::committed(rec, 64)) {
              for (u32 i = 0; i < 16; ++i) {
                const u32 w = r32(rec + 4 * i);
                std::string d;
                if (w >= 0x10000 && rt::vm::committed(w, 32)) {
                  for (u32 j = 0; j < 8; ++j) d += ac4::strprintf(" %08x", r32(w + 4 * j));
                  const u32 c = r32(w), t = r32(w + 4);
                  if (c >= 0x10000 && c < 0x00e00000 && t >= 0x00e00000 && t < 0x00f80000) d += " <- OPD";
                }
                LOG_I("coop", "COOP LUA fn %s:     rec+%u = 0x%08x |%s", a.substr(5).c_str(), 4 * i, w, d.c_str());
              }
              // the whole runtime region around the record: every word that is an object whose first
              // word is a vtable of function descriptors gets its first 6 slots resolved to code
              for (u32 p = rec; p < rec + 0xd0; p += 4) {
                const u32 o = r32(p);
                if (!rt::vm::committed(o, 8)) continue;
                for (int hop = 0; hop < 2; ++hop) {  // the word itself, or an object it points to
                  const u32 obj = hop == 0 ? o : r32(o);
                  if (!rt::vm::committed(obj, 4)) break;
                  const u32 vt = r32(obj);
                  if (!rt::vm::committed(vt, 24)) continue;
                  std::string s;
                  int good = 0;
                  for (u32 j = 0; j < 6; ++j) {
                    const u32 opd = r32(vt + 4 * j);
                    const u32 c = rt::vm::committed(opd, 8) ? r32(opd) : 0;
                    if (c >= 0x10000 && c < 0x00e00000) ++good;
                    s += ac4::strprintf(" %08x", c);
                  }
                  if (good >= 2)
                    LOG_I("coop", "COOP LUA fn %s:       [rec+0x%x] hop %d obj 0x%08x vt 0x%08x code:%s", a.substr(5).c_str(), p - rec,
                          hop, obj, vt, s.c_str());
                }
              }
              // the invoker (first element of the overload vector at rec+16): {vt, member fn, this adjust}
              {
                const u32 vb = r32(rec + 16);
                const u32 inv = rt::vm::committed(vb, 4) ? r32(vb) : 0;
                if (rt::vm::committed(inv, 16)) {
                  const u32 pm = r32(inv + 4), adj = r32(inv + 8);
                  const u32 code = (!(pm & 1) && rt::vm::committed(pm, 8)) ? r32(pm) : 0;
                  LOG_I("coop", "COOP LUA fn %s:     invoker 0x%08x words %08x %08x %08x %08x -> member %s 0x%08x adj %d code 0x%08x",
                        a.substr(5).c_str(), inv, r32(inv), pm, adj, r32(inv + 12), (pm & 1) ? "VIRTUAL slot+" : "opd", pm, int(adj),
                        code);
                }
              }
              // and the raw words, for the layout
              std::string raw;
              for (u32 p = rec; p < rec + 0xd0; p += 4) raw += ac4::strprintf(" %08x", r32(p));
              LOG_I("coop", "COOP LUA fn %s:     raw%s", a.substr(5).c_str(), raw.c_str());
            }
          }
        }
      }
      ctx.r[3] = 0;
      return true;
    }
    ctx.r[3] = u64(lua_command(ctx, L, a.substr(2)));
    return true;
  }
  return false;
}
void hook_lua_print(Ctx& ctx, void (*orig)(Ctx&)) {
  if (!lua_print_common(ctx, "stock")) orig(ctx);
}
void hook_lua_print_engine(Ctx& ctx, void (*orig)(Ctx&)) {
  if (!lua_print_common(ctx, "engine")) orig(ctx);
}

// 0x007cc0ec / 0x007cc19c: the mission-flow scene's two constructors, (this, mission record). Built on
// every hub entry with the default mission (run s28c), so only logged: the sortie start decides.
void hook_flow_ctor_a(Ctx& ctx, void (*orig)(Ctx&)) {
  LOG_I("coop", "COOP FLOW ctor_a this 0x%08x mission %u lr 0x%08x", u32(ctx.r[3]), u32(ctx.r[4]), u32(ctx.lr));
  const u32 flow = u32(ctx.r[3]);
  g_flow_toc = ctx.r[2];
  orig(ctx);
  g_flow = flow;
}
void hook_flow_ctor_b(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 rec = u32(ctx.r[4]);
  std::string words;
  for (u32 i = 0; i < 12 && rt::vm::committed(rec + 4 * i, 4); ++i) words += ac4::strprintf(" %08x", r32(rec + 4 * i));
  LOG_I("coop", "COOP FLOW ctor_b this 0x%08x record 0x%08x lr 0x%08x |%s", u32(ctx.r[3]), rec, u32(ctx.lr), words.c_str());
  const u32 flow = u32(ctx.r[3]);
  g_flow_toc = ctx.r[2];
  orig(ctx);
  g_flow = flow;
}

// 0x007cca94: the mission screen starts a sortie (this = the mission-flow scene, r4 the mission
// index, r5/r6 two more arguments taken from the screen). See on_sortie_start.
void hook_sortie_start(Ctx& ctx, void (*orig)(Ctx&)) {
  if (on_sortie_start(ctx)) orig(ctx);
}

// 0x007cd424: the mission-flow scene's per-tick method (vtable 0x101385f8 slot 10): the join's
// automatic start runs here, on the scene's own thread, after the scene's own work.
// (Not called while the mission list is up - run s28d - so only counted; menu_tick does the work.)
void hook_flow_tick(Ctx& ctx, void (*orig)(Ctx&)) {
  static std::atomic<uint64_t> n{0};
  if (n.fetch_add(1) < 3) LOG_I("coop", "COOP FLOW tick flow 0x%08x", u32(ctx.r[3]));
  orig(ctx);
}

// 0x004dd4d0: the generic menu-scene update (slot 3 of the hub and of the mission list). While the
// mission list of the current mission-flow scene is the scene being updated, the join's automatic
// start runs here, after the list's own work for this frame.
void hook_menu_tick(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 scene = u32(ctx.r[3]);
  orig(ctx);
  if (!rt::game::coopnet::role_join() || !rt::vm::committed(scene, 4) || r32(scene) != kMissionListVt) return;
  static std::atomic<int> n{0};
  const u32 flow = g_flow.load();
  if (n.fetch_add(1) < 2) LOG_I("coop", "COOP FLOW mission list 0x%08x ticking (flow 0x%08x)", scene, flow);
  if (flow) autostart_tick(ctx, flow);
}

// The per-player object's player actions (vtable 0x101463c0 slots 0x38 SetMissionCAPEvent, 0x3c
// SetMissionEndingFlowCAPEvent, 0x100 SetMissionMode, 0x114 SetEnableImmortal): one aimed at the
// second player's object goes to the first's - the local player, whose view the ending and the mission
// cutscenes play on. The second copy of the script is the one the event-group events reach (s28h-s28s),
// so without this MissionSuccess ended the mission for the remote player's body only.
static void player_redirect(Ctx& ctx, void (*orig)(Ctx&), const char* what) {
  const u32 self = u32(ctx.r[3]);
  const u32 p1 = g_player_obj[1].load(), p2 = g_player_obj[2].load();
  if (p1 && p2 && self == p2 && rt::vm::committed(p1, 4) && r32(p1) == kPlayerObjVt) {
    // Both objects get it: player 1 for the local view, player 2 as well - the mission end waits for
    // both bodies' ending flows (s28t: player 1 only -> the ending played and then stayed on black).
    static std::atomic<int> n{0};
    if (n.fetch_add(1) < 40) LOG_I("coop", "COOP PLAYER: %s for player 2's object 0x%08x -> also player 1's 0x%08x", what, p2, p1);
    u64 saved[8];
    for (int i = 0; i < 8; ++i) saved[i] = ctx.r[3 + i];
    const double f1 = ctx.f[1], f2 = ctx.f[2];
    const u64 lr = ctx.lr;
    ctx.r[3] = p1;
    orig(ctx);
    for (int i = 0; i < 8; ++i) ctx.r[3 + i] = saved[i];
    ctx.f[1] = f1;
    ctx.f[2] = f2;
    ctx.lr = lr;
  }
  orig(ctx);
}
void hook_player_cap(Ctx& ctx, void (*orig)(Ctx&)) { player_redirect(ctx, orig, "SetMissionCAPEvent"); }
void hook_player_ending(Ctx& ctx, void (*orig)(Ctx&)) { player_redirect(ctx, orig, "SetMissionEndingFlowCAPEvent"); }
void hook_player_mode(Ctx& ctx, void (*orig)(Ctx&)) { player_redirect(ctx, orig, "SetMissionMode"); }
void hook_player_immortal(Ctx& ctx, void (*orig)(Ctx&)) { player_redirect(ctx, orig, "SetEnableImmortal"); }
// Slot 0x88 (invoker 0x15cef3f0 holds slot+1 = 0x89, read at run time in s28z). Without it the host's
// SetMissionResult(SUCCESS/FAILURE) only reached player 2's object, so the host's own mission never
// had a result and its ending stayed on black (s28t, s28y) while the join, which replays the event
// through copy 1, reached the debriefing.
void hook_player_result(Ctx& ctx, void (*orig)(Ctx&)) { player_redirect(ctx, orig, "SetMissionResult"); }

// 0x0074cf88: a scene requests the next scene (r3 the current scene, r4 the next, f1/f2 the fades).
// Logged so the hub -> mission chain is on record (and the hub scene object is known).
void hook_scene_change(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 cur = u32(ctx.r[3]), next = u32(ctx.r[4]);
  LOG_I("coop", "COOP SCENE change: cur 0x%08x vt 0x%08x -> next 0x%08x vt 0x%08x | fades %.2f %.2f | lr 0x%08x", cur,
        rt::vm::committed(cur, 4) ? r32(cur) : 0, next, rt::vm::committed(next, 4) ? r32(next) : 0, ctx.f[1], ctx.f[2],
        u32(ctx.lr));
  rt::game::coopsession::note_scene_change(cur, next);
  orig(ctx);
}

}  // namespace ppu
