// Research instrument (AC4_TRACE_CREATE=1): the call chains by which the game creates its per-player
// objects, so "who makes player 2's AC, controller and pad operator" is measured rather than guessed.
//
// Two mechanisms, both opt-in:
//   * guest_backtrace: a back-chain walk of the guest stack, usable from any hook.
//   * an allocation ring inside the game heap hook: every allocation's address, size, caller and
//     call chain is kept in memory (not logged). A hook that later meets one of those objects (the
//     pad operator, an AC's controller or input) reports which allocation created it and from where,
//     in the same run -- which is what a post-mortem grep of the log cannot do.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "runtime/game/create_hooks.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

#include "ppu/ppu_recomp.h"
#include "runtime/log.h"
#include "runtime/memory.h"

namespace rt::game {

using ppu::r32;
using ppu::r64;

namespace {

constexpr int kMaxDepth = 24;     // frames printed
constexpr int kRingDepth = 20;    // frames kept per allocation
constexpr int kRingSize = 16384;  // allocations remembered (about 2 MB; a run makes far more)
constexpr int kTraceDepth = 20;   // frames printed for an allocation's creation chain

struct AllocRec {
  u32 addr;
  u32 size;
  u32 lr;
  u32 gpr[6];  // r3..r8 at the allocation
  u32 n;
  u32 frames[kRingDepth];
};

AllocRec g_ring[kRingSize];
u32 g_ring_at = 0;      // next slot to overwrite (the oldest)
u64 g_ring_count = 0;
std::mutex g_mu;
std::set<u32> g_traced;  // objects already reported

bool env_is_1(const char* name) {
  char buf[8]{};
  return GetEnvironmentVariableA(name, buf, sizeof buf) > 0 && buf[0] == '1';
}

}  // namespace

bool create_trace_on() {
  static const bool on = env_is_1("AC4_TRACE_CREATE");
  return on;
}

int guest_backtrace(ppu::Ctx& ctx, u32* out, int depth) {
  if (depth > kMaxDepth) depth = kMaxDepth;
  u32 sp = u32(ctx.r[1]);
  if (sp < 0xD0000000u || sp >= 0xF0000000u || (sp & 7)) return 0;  // not a guest thread stack
  int n = 0;
  for (int i = 0; i < depth; ++i) {
    u32 next = u32(r64(sp));  // the caller's frame (the stacks are committed host memory)
    if (next <= sp || (next & 7) || next >= 0xF0000000u || next - sp > 0x100000) break;
    u32 lr = u32(r64(next + 16));  // the saved LR is a 64-bit value: its low word is the address
    if (!lr) break;
    out[n++] = lr;
    sp = next;
  }
  return n;
}

std::string backtrace_text(ppu::Ctx& ctx, int depth) {
  u32 f[kMaxDepth];
  int n = guest_backtrace(ctx, f, depth);
  std::string s = ac4::strprintf("sp 0x%08x lr 0x%08x", u32(ctx.r[1]), u32(ctx.lr));
  for (int i = 0; i < n; ++i) s += ac4::strprintf(" <- 0x%08x", f[i]);
  if (!n) s += " <- (no frames)";
  return s;
}

void create_trace_alloc(ppu::Ctx& ctx) {
  if (!create_trace_on()) return;
  u32 addr = u32(ctx.r[3]);
  if (!addr) return;
  AllocRec r{};
  r.addr = addr;
  r.size = u32(ctx.r[4]);
  r.lr = u32(ctx.lr);
  for (int i = 0; i < 6; ++i) r.gpr[i] = u32(ctx.r[3 + i]);
  r.n = u32(guest_backtrace(ctx, r.frames, kRingDepth));
  std::lock_guard lk(g_mu);
  g_ring[g_ring_at] = r;
  g_ring_at = (g_ring_at + 1) % kRingSize;
  ++g_ring_count;
}

namespace {

// The allocation record for an object: the exact address, else the smallest block holding it.
// On a miss, `out` gets the nearest allocation below the address (diagnostics). The caller holds g_mu.
bool find_alloc(u32 addr, AllocRec* out, bool* exact) {
  bool found = false;
  u64 best = ~0ull;
  u32 nearest = 0;
  for (int i = 0; i < kRingSize; ++i) {
    const AllocRec& r = g_ring[(g_ring_at + i) % kRingSize];
    if (!r.addr) continue;
    if (r.addr == addr) {
      *out = r;
      *exact = true;
      return true;
    }
    if (addr > r.addr && u64(addr - r.addr) < r.size && r.size < best) {
      best = r.size;
      *out = r;
      found = true;
    }
    if (r.addr < addr && r.addr > nearest) {
      nearest = r.addr;
      if (!found) *out = r;
    }
  }
  if (found) *exact = false;
  return found;
}

}  // namespace

void create_trace_object(u32 addr, const char* what) {
  if (!create_trace_on() || !addr) return;
  AllocRec r{};
  bool exact = false;
  bool found = false;
  {
    std::lock_guard lk(g_mu);
    if (!g_traced.insert(addr).second) return;
    found = find_alloc(addr, &r, &exact);
  }
  if (!found) {
    LOG_I("create", "%s 0x%08x: NO recorded allocation (ring %llu allocations, nearest below 0x%08x +%u)", what, addr,
          (unsigned long long)g_ring_count, r.addr, r.size);
    return;
  }
  LOG_I("create", "%s 0x%08x: heap block 0x%08x +%u bytes (%s), allocated by lr 0x%08x, r3..r8 %08x %08x %08x %08x %08x %08x",
        what, addr, r.addr, r.size, exact ? "exact" : "inside", r.lr, r.gpr[0], r.gpr[1], r.gpr[2], r.gpr[3], r.gpr[4], r.gpr[5]);
  std::string s = "   chain:";
  for (u32 i = 0; i < r.n && i < u32(kTraceDepth); ++i) s += ac4::strprintf(" <- 0x%08x", r.frames[i]);
  LOG_I("create", "%s", s.c_str());
}

void create_trace_args(ppu::Ctx& ctx, const char* what) {
  if (!create_trace_on()) return;
  LOG_I("create", "%s: r3 0x%08x r4 0x%08x r5 0x%08x r6 0x%08x r7 0x%08x r8 0x%08x", what, u32(ctx.r[3]), u32(ctx.r[4]),
        u32(ctx.r[5]), u32(ctx.r[6]), u32(ctx.r[7]), u32(ctx.r[8]));
  for (int i = 3; i <= 8; ++i) {
    u32 p = u32(ctx.r[i]);
    if (!p || !rt::vm::committed(p, 256)) continue;
    std::string d;
    for (u32 off = 0; off < 256; off += 16)
      d += ac4::strprintf("\n   r%d+%03x: %08x %08x %08x %08x", i, off, r32(p + off), r32(p + off + 4), r32(p + off + 8),
                          r32(p + off + 12));
    LOG_I("create", "   dump of r%d = 0x%08x (256 bytes):%s", i, p, d.c_str());
  }
}

}  // namespace rt::game
