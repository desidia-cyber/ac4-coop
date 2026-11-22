// Hardware write-watchpoint discovery (research\27 s5). See coop_watch.h.
#include "runtime/game/coop_watch.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ppu/ppu_recomp.h"
#include "runtime/game/coop_entities.h"
#include "runtime/log.h"
#include "runtime/ppu_thread.h"

namespace {

using ppu::rf32;

constexpr u32 kEntPos = 0x40;
constexpr u32 kEntHp = 0x2a4;
constexpr u32 kEntHpMax = 0x2a8;
constexpr uint64_t kMemBase = 0x0000200000000000ull;

std::atomic<bool> g_on{false};
std::atomic<int> g_mode{0};  // 1 = transform, 2 = hp
u32 g_target[4] = {};
int g_ntarget = 0;
std::atomic<bool> g_armed{false};
bool g_pos_slot = false;
std::atomic<uint64_t> g_hits{0};
std::atomic<bool> g_veh{false};
double g_last_scan = -1e9, g_last_install = -1e9;
bool g_done = false;

struct Prev {
  float pos[3] = {};
  bool have = false;
};
std::unordered_map<u32, Prev> g_prev;
std::mutex g_mu;

LONG CALLBACK watch_veh(EXCEPTION_POINTERS* ep) {
  if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* c = ep->ContextRecord;
  int slot = -1;
  for (int i = 0; i < 4; ++i)
    if (c->Dr6 & (1ull << i)) {
      slot = i;
      break;
    }
  if (slot < 0 || slot >= g_ntarget) return EXCEPTION_CONTINUE_SEARCH;
  const uint64_t rip = uint64_t(ep->ExceptionRecord->ExceptionAddress);
  const uint64_t base = uint64_t(GetModuleHandleA(nullptr));
  const uint64_t n = g_hits.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n <= 120)
    LOG_I("watch", "WATCH HIT %s dr%d guest 0x%08x rip 0x%llx rva 0x%llx (hit %llu)",
          (g_pos_slot && slot == 0) ? "pos" : "hp", slot, g_target[slot], (unsigned long long)rip,
          (unsigned long long)(rip - base), (unsigned long long)n);
  c->EFlags |= 0x10000;  // RF: let this instruction complete without re-trapping on it
  return EXCEPTION_CONTINUE_EXECUTION;
}

void set_debug_regs(HANDLE h, bool is_self, const u32* addrs, int n, bool clear) {
  CONTEXT c{};
  c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  if (!is_self) SuspendThread(h);
  if (!GetThreadContext(h, &c)) {
    if (!is_self) ResumeThread(h);
    return;
  }
  c.Dr0 = c.Dr1 = c.Dr2 = c.Dr3 = 0;
  c.Dr7 = 0;
  if (!clear) {
    for (int i = 0; i < n && i < 4; ++i) {
      const uint64_t a = kMemBase + addrs[i];
      if (i == 0) c.Dr0 = a;
      else if (i == 1) c.Dr1 = a;
      else if (i == 2) c.Dr2 = a;
      else c.Dr3 = a;
      // L_i enable, RW_i = 01 (write), LEN_i = 11 (4 bytes)
      c.Dr7 |= (1ull << i) | (1ull << (16 + 4 * i)) | (3ull << (18 + 4 * i));
    }
  }
  SetThreadContext(h, &c);
  if (!is_self) ResumeThread(h);
}

struct ThreadJob {
  const u32* addrs;
  int n;
  bool clear;
  int set;
};

void thread_job(rt::PpuThread& t, void* user) {
  ThreadJob* j = static_cast<ThreadJob*>(user);
  if (!t.host || t.state.load(std::memory_order_relaxed) == 2) return;
  const bool self = t.host_tid == GetCurrentThreadId();
  set_debug_regs(HANDLE(t.host), self, j->addrs, j->n, j->clear);
  ++j->set;
}

void apply_all(const u32* addrs, int n, bool clear) {
  ThreadJob j{addrs, n, clear, 0};
  rt::thread_for_each(thread_job, &j);
}

void disarm(const char* why) {
  if (!g_armed.exchange(false)) return;
  apply_all(nullptr, 0, true);
  LOG_I("watch", "WATCH disarmed: %s after %llu hit(s)", why, (unsigned long long)g_hits.load());
}

// The entity that moved most since the previous scan (a rocket/unit, not scenery), or 0.
u32 pick_mover(const u32* addrs, size_t n, double* moved) {
  u32 best = 0;
  double bestd = 20.0;
  std::unordered_map<u32, Prev> now;
  for (size_t i = 0; i < n; ++i) {
    const u32 a = addrs[i];
    Prev p;
    for (int k = 0; k < 3; ++k) p.pos[k] = float(rf32(a + kEntPos + 4 * k));
    p.have = true;
    auto it = g_prev.find(a);
    if (it != g_prev.end() && it->second.have) {
      const double dx = p.pos[0] - it->second.pos[0], dy = p.pos[1] - it->second.pos[1],
                   dz = p.pos[2] - it->second.pos[2];
      const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d > bestd) {
        bestd = d;
        best = a;
      }
    }
    now[a] = p;
  }
  g_prev.swap(now);
  if (moved) *moved = bestd;
  return best;
}

// Up to `max` live entities, appended from slot `start`.
int pick_hp(const u32* addrs, size_t n, int start, int max) {
  // Prefer the entities that are moving: in m010 the kamikaze swarm dies while flying, so a
  // moving entity's +0x2a4 is the one that will be written soon.
  struct Cand {
    u32 a;
    double moved;
  };
  std::vector<Cand> cs;
  std::unordered_map<u32, Prev> now;
  for (size_t i = 0; i < n; ++i) {
    const u32 a = addrs[i];
    const float hp = float(rf32(a + kEntHp));
    const float mx = float(rf32(a + kEntHpMax));
    Prev p;
    for (int k = 0; k < 3; ++k) p.pos[k] = float(rf32(a + kEntPos + 4 * k));
    p.have = true;
    double moved = 0.0;
    auto it = g_prev.find(a);
    if (it != g_prev.end() && it->second.have) {
      const double dx = p.pos[0] - it->second.pos[0], dy = p.pos[1] - it->second.pos[1],
                   dz = p.pos[2] - it->second.pos[2];
      moved = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    now[a] = p;
    if (hp > 0.0f && mx > 0.0f) cs.push_back({a, moved});
  }
  g_prev.swap(now);
  std::sort(cs.begin(), cs.end(), [](const Cand& x, const Cand& y) { return x.moved > y.moved; });
  int k = 0;
  for (const Cand& c : cs) {
    if (k >= max) break;
    g_target[start + k] = c.a + kEntHp;
    ++k;
  }
  return k;
}

void pick_targets() {
  u32 addrs[512];
  const size_t n = rt::game::coop_entity_addrs(addrs, 512);
  if (n < 100) return;
  const int mode = g_mode.load();
  int slots = 0;
  g_pos_slot = false;
  if (mode & 1) {  // pos (1) or all (3)
    double moved = 0.0;
    const u32 mover = pick_mover(addrs, n, &moved);
    if (!mover) return;  // first scan has no previous sample: wait one second, then arm with both
    g_target[slots++] = mover + kEntPos;
    g_pos_slot = true;
    LOG_I("watch", "WATCH arming transform: entity 0x%08x moved %.1f units, watching +0x40", mover, moved);
  }
  if (mode & 2) {  // hp (2) or all (3)
    const int k = pick_hp(addrs, n, slots, 4 - slots);
    if (k) LOG_I("watch", "WATCH arming hp on %d entities", k);
    slots += k;
  }
  if (!slots) return;
  g_ntarget = slots;
  if (!g_veh.exchange(true)) AddVectoredExceptionHandler(1, watch_veh);
  apply_all(g_target, g_ntarget, false);
  g_armed = true;
}

}  // namespace

namespace rt::game {

u32 watch_entity() { return (g_pos_slot && g_ntarget > 0) ? g_target[0] - kEntPos : 0; }

void watch_frame() {
  static bool init = false;
  if (!init) {
    init = true;
    char b[16]{};
    DWORD n = GetEnvironmentVariableA("AC4_COOP_WATCH", b, sizeof b);
    if (n > 0 && n < sizeof b) {
      if (b[0] == 'h' || b[0] == 'H') g_mode = 2;
      else if (b[0] == 'a' || b[0] == 'A') g_mode = 3;
      else if (b[0] == 'p' || b[0] == 'P') g_mode = 1;
      if (g_mode) {
        g_on = true;
        LOG_I("watch", "WATCH enabled mode %s",
              g_mode.load() == 2 ? "hp (+0x2a4)" : g_mode.load() == 3 ? "transform + hp" : "transform (+0x40)");
      }
    }
  }
  if (!g_on || g_done) return;
  const double now = rt::log_time();
  if (g_armed && g_hits.load() >= 80) {
    g_done = true;
    disarm("hit bound reached");
    return;
  }
  if (!g_armed) {
    if (now - g_last_scan < 1.0) return;
    g_last_scan = now;
    pick_targets();
    return;
  }
  // Re-assert the registers on every thread (new guest threads appear during a mission).
  if (now - g_last_install >= 1.0) {
    g_last_install = now;
    apply_all(g_target, g_ntarget, false);
  }
}

}  // namespace rt::game

