// Co-op session layer (research\28_HOST_DRIVEN_SESSION.md): host-driven campaign flow on top of the
// UDP link (coop_net.h).
//
//   * Lobby state, both directions, ~5 Hz (type 4): phase (menu / sortie / barrier / playing), the
//     mission number and the mission EPOCH. The epoch is a 15-bit tag the host draws each time it
//     accepts a mission; every per-mission packet (transform, snapshot, events) carries it, so state
//     from another mission instance is never applied.
//   * Mission start: when the host accepts a mission its number is announced; the join's next mission
//     accept (by hand or automatic, see coop_session.cpp) is rewritten to the host's mission at the
//     mission-flow scene's constructor, before any mission data is chosen.
//   * Load barrier: both instances stop inside the sortie load (just before the player AC is built)
//     until the peer has reached the same point of the same epoch. Nothing simulates before both are
//     ready, whatever either player did in the garage first.
//   * Mission events (type 5, reliable, host -> join): the host's mission script reports every
//     engine-dispatched handler it runs; the join's script runs none of its own and replays the host's,
//     in order (the Lua side is injected into each mission's event bundle by tools\coop_patch.py).
//   * Lua channel: the stock `print` (0x002a6bdc) with a first argument starting "\1C" is a command
//     to this module instead of a log line, and may return one string to the script.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ppu {
struct Context;
using Ctx = Context;
}  // namespace ppu

namespace rt::game::coopsession {

enum Phase : uint8_t { kMenu = 0, kSortie = 1, kBarrier = 2, kPlaying = 3 };

// The epoch of the mission this instance is in (0 = none / solo / not synced).
uint16_t epoch();
Phase phase();

// True while both instances are inside the same mission epoch and past the load barrier: the
// remote pad, transforms and snapshots are meaningful only then.
bool linked_in_mission();

// From coop_net's receive loop: a lobby (4) or event (5) datagram's payload.
void on_packet(uint8_t type, const uint8_t* payload, size_t len);

// Once per rendered frame (hook_present): lobby send, event resend, phase watchdog, window title.
void frame();

// The load barrier (called on the guest thread that builds the player AC).
void load_barrier();
// Join: makes a pending automatic sortie start, after the menu frame (menu_mouse.cpp hook_menu_frame).
void autostart_after_frame(ppu::Ctx& ctx);

// The mission number the sortie was built with (Ac4SortieScene +7976), for display.
void note_mission_number(int n);
// Every scene-change request (hook_scene_change): remembers the root scene.
void note_scene_change(uint32_t cur, uint32_t next);
// Clears per-mission Lua player-object pointers on sortie cleanup.
void reset_mission_objects();
// Called on Ac4SortieScene::Init: ensures host has a fresh epoch on Retry or New Game (m000).
void note_scene_init();

// Partner AC design exchange (7660 bytes from Ac4SortieScene+292 + 20 colour bytes at +7952 = 7680).
constexpr size_t kAcDesignBytes = 7680;
void set_local_ac_design(const uint8_t* data, size_t len);
bool get_peer_ac_design(uint8_t* out, size_t len);

}  // namespace rt::game::coopsession
