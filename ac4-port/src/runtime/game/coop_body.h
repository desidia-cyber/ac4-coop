// Co-op: the AC bodies the second-player work creates, their transform layout, and the write test
// that decides which object owns each AC's world position. Everything is gated by AC4_COOP=1
// (AC4_COOP_DUMP / AC4_COOP_POKE for the diagnostics), so a normal run is unaffected.
#pragma once

#include "ppu/ppu_recomp.h"

namespace rt::game {

// The mission's two AC bodies (coop_build.cpp): 1 = the local player's AC, 2 = the second AC.
void coop_note_body(int which, u32 body);
u32 coop_body(int which);
// The AC object the input survey resolved for that body (input_hooks.cpp): body+4 links back to it.
void coop_note_obj(u32 body, u32 obj);

// Per frame (hook_present in frame_hooks.cpp): the AC4_COOP_DUMP transform dump and the
// AC4_COOP_POKE write test.
void coop_body_frame();

// A body's world position as the layout work established it. False while the body is unknown.
bool coop_body_position(int which, double out[3]);

// A new sortie: the previous mission's bodies are gone.
void coop_body_reset();

}  // namespace rt::game
