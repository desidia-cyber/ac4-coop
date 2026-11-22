// Co-op link v0: one UDP socket carrying the local pad state and the local AC's transform, so two
// game instances can drive each other's second AC (research\24_COOP_LINK_V0.md).
//
// Native C++ over WinSock (ws2_32), not the guest's sys_net. Gated by the environment:
//   AC4_COOP_HOST=<udp port>     listen on that port; the peer is whoever sends the first packet
//   AC4_COOP_JOIN=<ip>:<port>    send to that address; the peer is fixed
// With neither set nothing is opened and every function here is a no-op, so a normal run is unchanged.
//
// Wire format, little-endian, 8-byte header + payload:
//   { char magic[4] = "AC4C"; u8 version = 1; u8 type; u16 seq; } + payload
//   type 1 (pad):       u16 buttons, s16 lx, ly, rx, ry, u8 lt, rt, u8 port
//   type 2 (transform): s16 mission tag, 3 floats position, 9 floats rotation row-major, 3 floats velocity
// Out-of-order and duplicate packets are dropped by seq within a type.
#pragma once

#include <cstdint>

namespace rt::game::coopnet {

// Reads AC4_COOP_HOST / AC4_COOP_JOIN and opens the socket. False when the link is not configured
// (or the socket could not be opened: logged).
bool init();
bool active();

// The state received from the peer for gamepad port 1. False when the link is not configured or no
// packet has arrived for 1 s (the caller then reports the port as unconnected/neutral).
// The pad in the PS3 layout cellPadGetData reports (research\28): buttons = digital1 | digital2 << 8,
// the sticks 0..255 (128 = centre), lt/rt the L2/R2 pressure. A default value is a neutral pad.
struct PadState {
  uint16_t buttons = 0;
  int16_t lx = 128, ly = 128, rx = 128, ry = 128;
  uint8_t lt = 0, rt = 0;
};
// This instance's port 0 as the game read it (keyboard/mouse bindings included); sent to the peer.
void set_local_pad(const PadState& p);
// Reads every pending datagram (thread-safe; the load barrier pumps while the frame is blocked).
void pump();
// Sends a session datagram (coop_session.cpp) of the given type.
void send_raw(uint8_t type, const void* payload, size_t len);
bool remote_pad(PadState* out);
// True while a peer is considered present (a packet within the last second).
bool peer_present();

// The transform of the LOCAL player's AC (the values sent as type 2). Called once per frame by the
// pad/transform path; the send itself happens every 6th frame.
void set_local_transform(const float position[3], const float rotation[9], const float velocity[3], int mission_tag);

// The AP/PA/EN/weapons of the AC this instance simulates for the PEER (its AC2). On the host that is
// the join player, so the host's simulation is authoritative for the join's own AC1 (research\27).
struct WeaponSyncState {
  uint8_t active = 0xff;  // low nibble: right active idx (0..3, 0xf=n/a), high nibble: left active idx
  uint8_t purged = 0;     // bits 0..5: purged flag for wpn_mgr+1072 holders 0..5
  int16_t ammo[6] = {-1, -1, -1, -1, -1, -1};  // wpn_mgr+556 banks[0..1] slots[0..2] cur_ammo (-1 = n/a)
};
void set_local_peer_ap(float ap, float apmax);
void set_local_peer_pa(float pa, float pamax);
void set_local_peer_en(float en, float enmax, bool redline);
void set_local_peer_weapons(const WeaponSyncState& w);
// The AP/PA & active/purged weapons of this instance's OWN AC1 (sent to the partner for Partner HUD
// and visual weapon sync on AC2).
void set_local_self_vitals(float ap, float apmax, float pa, float pamax);
void set_local_self_weapons(uint8_t active, uint8_t purged);
// The local player AC's vertical look pitch in radians (from look control +48, so mouse vertical aim
// replicates to the partner's AC2 even though mouse pitch bypasses the right stick).
void set_local_pitch(float pitch);
bool remote_pitch(float* pitch);
// Called from hook_look_update while the local mission simulation is actively ticking (not paused).
// When this stops ticking in-mission (>120 ms), the player has opened the local Pause menu: we send
// neutral pad + zero velocity so menu inputs never fire weapons on the partner, and force a fresh
// transform immediately on unpause.
void note_local_sim_tick();
bool local_paused();
// The AP/PA/EN/weapons last received for the peer-controlled AC (the host's AC2 on the join). False
// until a transform packet carrying them has arrived.
bool peer_ap(float* ap, float* apmax);
bool peer_pa(float* pa, float* pamax);
bool peer_en(float* en, float* enmax, bool* redline);
bool peer_weapons(WeaponSyncState* out);
bool remote_self_weapons(uint8_t* active, uint8_t* purged);
// Authoritative partner vitals for the co-op HUD (host reads body2 locally; join reads host's self vitals).
struct PartnerVitals {
  float ap = 0.0f, apmax = 0.0f;
  float pa = 0.0f, pamax = 0.0f;
  bool paused = false;
  bool peer_up = false;
  bool valid = false;
};
PartnerVitals partner_vitals();

// The role this instance runs as, from AC4_COOP_HOST / AC4_COOP_JOIN (research\26).
bool role_host();
bool role_join();

// --- type 3: entity snapshots, host -> join (research\26_ENEMY_SNAPSHOT_SYNC.md) ---------------
//
// The host is authoritative for every non-player entity. One snapshot is a list of records, one per
// OCCUPIED entity (the two player bodies are left to the type-2 path), split over datagrams of at
// most 1200 bytes and cycled at ~10 Hz. The join writes each record into the same entity: the host
// sends the entity's ADDRESS, which is the stable key this port measured (both instances allocate the
// same object graph), and `ord` is the entity's rank in the address-ordered list, used as a fallback
// if that address does not hold an entity on the receiving side.
struct SnapshotRecord {
  uint32_t addr;
  uint32_t entity_id;     // stable mission entity id (*(addr + 4)), immune to heap address shifts
  uint16_t ord;           // rank among non-player entities
  uint16_t broken_parts;  // bitmask of destroyed sub-parts (entity+0x2b0 durability array, cur_hp <= 0)
  float position[3];
  float rotation[9];      // rows of the body's 4x4 at +0x10/+0x20/+0x30
  float hp;               // +0x2a4
};

// Host: send `n` records as one snapshot. Returns the number of datagrams sent (0 when not the host).
// `tag` is carried so the join can log the host's pool base beside its own.
size_t send_snapshot(const SnapshotRecord* recs, size_t n, uint32_t tag);

// Join: move up to `max` received records into `out`. Returns how many; `counter` gets the host's
// snapshot counter of the newest datagram seen (0 when none has arrived).
size_t take_snapshot(SnapshotRecord* out, size_t max, uint32_t* counter);

// Totals for the per-second log: records/datagrams sent, records received.
void snapshot_totals(uint64_t* sent_recs, uint64_t* sent_pkts, uint64_t* recv_recs);

// The host's pool base carried in the snapshot tag (0 until a snapshot arrives).
uint32_t snapshot_tag();

// The transform received from the peer (its AC1), and whether one has arrived yet.
struct Transform {
  int mission_tag = 0;
  float position[3] = {};
  float rotation[9] = {};
  float velocity[3] = {};
  float ap = 0.0f;        // AP of the peer-controlled AC (host's AC2), added in research\27
  float apmax = 0.0f;
  float pitch = 0.0f;     // look pitch in radians (mouse/gamepad vertical aim)
  bool has_pitch = false;
  float self_ap = 0.0f;   // sender's own AC1 AP/PA (for partner HUD)
  float self_apmax = 0.0f;
  float peer_pa = 0.0f;   // sender's AC2 PA (host -> join authoritative PA)
  float peer_pamax = 0.0f;
  float self_pa = 0.0f;
  float self_pamax = 0.0f;
  float peer_en = 0.0f;   // sender's AC2 EN (host -> join authoritative EN)
  float peer_enmax = 0.0f;
  bool peer_en_redline = false;
  WeaponSyncState peer_wpn{};
  uint8_t self_wpn_active = 0xff;
  uint8_t self_wpn_purged = 0;
  bool peer_paused = false;
  uint32_t age_ms = 0;    // milliseconds since this transform sample was received
  uint32_t samples = 0;
};
bool remote_transform(Transform* out);

// One frame's work: read every pending datagram, then send the pad state and (every 6th frame) the
// transform. Never blocks; safe when the peer disappears. `local` is this frame's port-0 pad state.
void frame(const PadState& local);

// Once a second: the packet counters and the last position error (units), for the log.
void report();

// The position error the link computed this frame (0 when no transform has arrived).
float last_position_error();

}  // namespace rt::game::coopnet
