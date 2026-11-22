// Co-op: a write-watchpoint discovery tool for the last two unknown writers (research\27 s5).
//
//   AC4_COOP_WATCH=pos   watch the first entity that moves: its +0x40 (the live transform)
//   AC4_COOP_WATCH=hp    watch up to four live entities' +0x2a4 (HP/AP)
//   AC4_COOP_WATCH=all   one mover's +0x40 and three live entities' +0x2a4 in one run
//
// The watch is a hardware debug-register write breakpoint (DR0-DR3) set on every guest PPU thread.
// The single-step exception is caught by a vectored handler, which logs the faulting HOST rip and
// continues with RF set. The exe keeps its symbols, so the rip maps to the guest function with
// llvm-nm (value - 0x140000000 == rip - module base). Nothing is hooked and no generated code is
// recompiled; the tool arms once entities exist and disarms after a bounded number of hits.
#pragma once

#include "common/bytes.h"

namespace rt::game {
// Called once per frame from coop_entities_frame (after the entity walk).
void watch_frame();
// The entity watched for its transform (+0x40, mode pos/all), 0 when not armed.
u32 watch_entity();
}  // namespace rt::game
