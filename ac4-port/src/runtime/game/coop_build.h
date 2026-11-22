// Experiment E7/E8: a campaign mission builds a SECOND player AC, driven by gamepad port 1.
// Everything here is gated by AC4_COOP=1, so a normal run is unaffected.
//
// E8 adds the shared container: the mission's Build (0x00b0d354) first creates a CONTAINER object
// through builder vtable+32 (code 0x00b0b77c, stored at builder+64), and only then the AC body.
// In versus both players' bodies are registered in ONE container, so the second build must not
// create a second container - it must return the first one (hook_ac_container).
#pragma once

#include "ppu/ppu_recomp.h"

namespace rt::game {

// True when AC4_COOP=1 was set for this process.
bool coop_on();

// True while the second builder's Build runs: the pad operator's constructor then takes port 1
// instead of the port it was called with (input_hooks.cpp, hook_player_ctor_e), builder vtable+32
// returns the first builder's container (coop_build.cpp, hook_ac_container), and the calls listed
// in AC4_COOP_SKIP may be skipped.
bool coop_p2_building();

// The operator object the second build constructed (0 until it exists). Logged with its +4/+8/+12
// when the second build returns.
void coop_note_p2_operator(u32 op);
u32 coop_p2_operator();

// Per-frame: counts updates of the second operator and logs the first few (research evidence that
// the engine really drives it, not only that it was constructed).
void coop_operator_tick(u32 op);

// AC4_COOP_SKIP=0x00b12770,... : skip exactly these calls (by their guest code address) while the
// second build runs. Returns true when the hook must not call the original.
enum CoopSkipId {
  kCoopSkipContainerFinal = 0,  // 0x00b12770 (lr 0x00b0d6c4)
  kCoopSkipSlot10,              // 0x00b0bf04 (lr 0x00b0d6f4)
  kCoopSkipSlot9,               // 0x00b0b36c (lr 0x00b0d778)
  kCoopSkipAttachA,             // 0x00af6d60 (lr 0x00b0d794)
  kCoopSkipAttachB,             // 0x00af6d40 (lr 0x00b0d7a0)
  kCoopSkipAttachC,             // 0x00b02718 (lr 0x00b0d7ac)
  kCoopSkipCount
};
bool coop_skip(CoopSkipId id, ppu::Ctx& ctx);

// Diagnostic: log AC part manager (body+708 -> +248 -> +32..+188) slots & attachment transforms
// when AC4_COOP_PART_DUMP=1.
void coop_log_ac_parts(const char* tag, u32 body);

}  // namespace rt::game
