// Research instrument: where the game creates its per-player objects (AC, controller, pad operator).
// Everything here is a no-op unless AC4_TRACE_CREATE=1, so a normal run is unaffected.
#pragma once
#include <string>

#include "ppu/ppu_context.h"

namespace rt::game {

// True when AC4_TRACE_CREATE=1 was set for this process.
bool create_trace_on();

// The guest call chain above `ctx`: a back chain walk (sp = R(1); next = r64(sp); return address =
// r32(next + 16); sp = next), guarded against null, unaligned and non-increasing frames. Fills up to
// `depth` return addresses into `out` and returns how many: out[0] is the innermost (the call that
// is running), out[n-1] the outermost. Reusable by any hook.
int guest_backtrace(ppu::Context& ctx, u32* out, int depth);

// The same on one line, prefixed with ctx.lr: "lr 0x... <- 0x... <- ...".
std::string backtrace_text(ppu::Context& ctx, int depth);

// Called from the heap-allocator hook after the allocation: r3 the returned pointer, r4 the bytes.
// Keeps (address, size, caller, r3..r8, call chain) in a ring, so a later hook that meets one of
// those objects can report which allocation created it and from where (same run).
void create_trace_alloc(ppu::Context& ctx);

// Report the allocation that created `addr` (exact, else the smallest block containing it) with the
// call chain that made it. Once per address. `what` names the object in the log line.
void create_trace_object(u32 addr, const char* what);

// Log r3..r8 of the current call and 256 bytes of every argument that is a live guest pointer.
void create_trace_args(ppu::Context& ctx, const char* what);

}  // namespace rt::game
