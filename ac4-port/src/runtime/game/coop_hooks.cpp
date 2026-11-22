// Co-op: hooks on the engine's damage/enable seams (research\27_DAMAGE_AND_FREEZE.md).
//
// Three guest functions are intercepted:
//   0x00a53900  the HP/AP setter: `*(float*)(this + 0x2a4) = f1`, then it notifies the owner
//   0x00a5a054  SetEnableMove(self, value):   *(u32*)(*(self+0xa0) + 320) = value
//   0x00a5a084  SetEnableAttack(self, value): dispatches command 25 on *(self+0xa0)
//
// The setter is the single place a body's hit points/AP change (only two functions in the whole
// recompiled image write a non-stack +0x2a4), so its call log is the authoritative answer to "who
// damaged what". On the join it also blocks a local death the host has not confirmed: the host's AP
// for this player (received over the link) wins over the join's own simulation.
//
// The two enable calls are logged, and with AC4_COOP_FREEZE=1 on the join they are rewritten to
// "disable", which is the lever the mission scripts themselves use (SetEnableMove/SetEnableAttack).
#include "runtime/game/coop_hooks.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "ppu/ppu_recomp.h"
#include "runtime/game/coop_body.h"
#include "runtime/game/coop_build.h"
#include "runtime/game/coop_entities.h"
#include "runtime/game/coop_net.h"
#include "runtime/game/coop_watch.h"
#include "runtime/log.h"
#include "runtime/memory.h"

namespace {

using ppu::r32;
using ppu::rf32;
using ppu::wf32;

constexpr u32 kHp = 0x2a4;
constexpr u32 kHpMax = 0x2a8;

bool freeze_on() {
  static std::once_flag once;
  static bool on = false;
  std::call_once(once, [] {
    char b[8]{};
    DWORD n = GetEnvironmentVariableA("AC4_COOP_FREEZE", b, sizeof b);
    // Default ON for the join (host snapshots are authoritative); set AC4_COOP_FREEZE=0 to disable.
    on = (n > 0) ? (b[0] != '0') : rt::game::coopnet::role_join();
    if (on) LOG_I("coop", "COOP FREEZE: non-player local simulation will be frozen on the join");
  });
  return on && rt::game::coopnet::role_join();
}

// One line per call site and object, then one summary line a second, so a mission-long stream of
// damage remains readable.
struct CallLog {
  std::mutex mu;
  uint64_t calls = 0;
  double last = -1e9;
};

CallLog g_enable_move, g_enable_attack;

void log_enable(CallLog& log, const char* what, u32 self, u32 value, u64 lr, u32 forced) {
  ++log.calls;
  const double now = rt::log_time();
  if (now - log.last < 1.0) return;
  log.last = now;
  LOG_I("coop", "COOP %s t=%.1f calls=%llu | last self 0x%08x value %u lr 0x%08x%s", what, now,
        (unsigned long long)log.calls, self, value, u32(lr),
        forced != value ? ac4::strprintf(" -> forced %u on the join", forced).c_str() : "");
  log.calls = 0;
}

}  // namespace

namespace ppu {

// 0x00a53900: HP/AP setter. r3 = object, f1 = new value.
void hook_hp_set(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 self = u32(ctx.r[3]);
  const float before = rt::vm::committed(self + kHp, 4) ? float(rf32(self + kHp)) : 0.0f;
  const float after = float(ctx.f[1]);

  static std::atomic<uint64_t> writes{0};
  const bool player = self && (self == rt::game::coop_body(1) || self == rt::game::coop_body(2));
  const bool lethal = after <= 0.0f && before > 0.0f;

  if (player || lethal) {
    const uint64_t n = writes.fetch_add(1, std::memory_order_relaxed);
    if (n < 400 || lethal) {
      LOG_I("coop", "COOP HP %s 0x%08x 0x%08x %.4f -> %.4f lr 0x%08x", player ? "PLAYER" : "death", self,
            r32(self + 4), double(before), double(after), u32(ctx.lr));
    }
  }

  // The join does not get to kill the host's player: only the host's AP for this body is
  // authoritative. A local lethal write is replaced by the last AP the host sent (still lethal when
  // the host has actually killed the player and sent 0).
  if (lethal && rt::game::coopnet::role_join() && self == rt::game::coop_body(1)) {
    float ap = 0.0f, apmax = 0.0f;
    if (rt::game::coopnet::peer_ap(&ap, &apmax) && apmax > 0.0f && ap > 0.0f) {
      LOG_W("coop", "COOP HP: blocked a local death of the join player 0x%08x (host AP %.1f); local %.1f",
            self, double(ap), double(after));
      ctx.f[1] = double(ap);
    }
  } else if (lethal && rt::game::coopnet::role_join() && self == rt::game::coop_body(2)) {
    // AC2 is the host's player on the join; the host owns it. Never let the join's simulation kill it.
    static std::atomic<int> blocked{0};
    if (blocked.fetch_add(1, std::memory_order_relaxed) < 20)
      LOG_W("coop", "COOP HP: blocked a local death of the host player 0x%08x on the join (%.1f)", self,
            double(after));
    ctx.f[1] = double(before > 0.0f ? before : 1.0f);
  }
  orig(ctx);
}

// 0x00a569f0: the other object-field HP writer (the port's earlier scan found exactly two). It is
// the AC's per-frame HP/PA update: it reads the damage fields around +0x2d4/+0x33c and writes
// +0x2a4. Logged for the two player bodies, and on the join the player's HP is re-asserted from the
// host after the update, so a local enemy can never decide the join player's AP.
void hook_hp_update(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 self = u32(ctx.r[3]);
  const float before = rt::vm::committed(self + kHp, 4) ? float(rf32(self + kHp)) : 0.0f;
  orig(ctx);
  const float after = rt::vm::committed(self + kHp, 4) ? float(rf32(self + kHp)) : 0.0f;
  const bool player = self && (self == rt::game::coop_body(1) || self == rt::game::coop_body(2));
  if (player && before != after) {
    static std::atomic<uint64_t> n{0};
    const uint64_t c = n.fetch_add(1, std::memory_order_relaxed);
    if (c < 300) LOG_I("coop", "COOP HPU %s 0x%08x %.4f -> %.4f", self == rt::game::coop_body(1) ? "P1" : "P2",
                       self, double(before), double(after));
  }
  if (rt::game::coopnet::role_join() && self == rt::game::coop_body(1)) {
    float ap = 0.0f, apmax = 0.0f;
    if (rt::game::coopnet::peer_ap(&ap, &apmax) && apmax > 0.0f) wf32(self + kHp, ap);
  }
}

// 0x00a68dbc: the transform setter found by the write watch (research\27 s2.3). It copies a 4x4
// from r4 into r3+0x10 (rotation rows and the +0x40 position row). Logging its caller for the entity
// the watch is following gives the guest address of the per-frame entity update.
void hook_xform_set(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 self = u32(ctx.r[3]);
  const u32 src = u32(ctx.r[4]);
  const u32 target = rt::game::watch_entity();
  if (target && self == target) {
    static std::atomic<uint64_t> n{0};
    const uint64_t c = n.fetch_add(1, std::memory_order_relaxed);
    if (c < 160)
      LOG_I("coop", "COOP XFORM entity 0x%08x <- src 0x%08x lr 0x%08x (%llu)", self, src, u32(ctx.lr),
            (unsigned long long)c);
  }
  orig(ctx);
}

// 0x00a4f968: the entity's vtable slot 12. It calls the 4x4 setter (0xa68dbc) and copies `src` to
// this+0x190. The watch showed this is the transform the renderer follows; its CALLER is therefore
// the per-frame update that M3 could not find.
void hook_xform_blit(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 self = u32(ctx.r[3]);
  const u32 src = u32(ctx.r[4]);
  const u32 target = rt::game::watch_entity();
  if (target && self == target) {
    static std::atomic<uint64_t> n{0};
    const uint64_t c = n.fetch_add(1, std::memory_order_relaxed);
    if (c < 200)
      LOG_I("coop", "COOP XBLIT entity 0x%08x <- src 0x%08x caller 0x%08x (%llu)", self, src, u32(ctx.lr),
            (unsigned long long)c);
  }
  // Instant 1-frame actor discovery for newly spawned reinforcements on both host and join.
  if (rt::game::coopnet::active() && self >= 0x10000000u) rt::game::coop_entities_note_live(self);
  // The freeze: this blit is what moves the entity's live +0x10 transform. On the join, for every
  // non-player entity, do not run it once both player ACs are built (during BUILD#1/BUILD#2 both
  // bodies and initial mission spawns must receive their spawn transforms!).
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  if (freeze_on() && b1 && b2 && self && self != b1 && self != b2 &&
      rt::vm::committed(self, 8) && r32(self) == 0x10142638 &&
      r32(self + 4) != 1000u && r32(self + 4) != 1001u) {
    static std::atomic<uint64_t> n{0};
    const uint64_t c = n.fetch_add(1, std::memory_order_relaxed);
    if (c < 10) LOG_I("coop", "COOP FREEZE: transform blit skipped for entity 0x%08x (call %llu)", self, (unsigned long long)c);
    return;
  }
  orig(ctx);
}

// 0x004b9eb8: the per-object motion update. On the join, we intentionally allow this to run while
// hook_xform_blit stays skipped for non-player entities: host snapshots hold every entity at its
// exact host position (research\27 A1: 0.001 mean drift), while letting actor_update run allows
// enemies to fire visible projectiles/missiles from host positions AND execute real death
// explosions/cleanup when host streams hp=0. (Set AC4_COOP_FREEZE_AI=1 to skip actor_update too.)
void hook_actor_update(Ctx& ctx, void (*orig)(Ctx&)) {
  static const bool freeze_ai = [] {
    char b[8]{};
    return GetEnvironmentVariableA("AC4_COOP_FREEZE_AI", b, sizeof b) > 0 && b[0] == '1';
  }();
  const u32 self = u32(ctx.r[3]);
  if (rt::game::coopnet::active() && self >= 0x10000000u) rt::game::coop_entities_note_live(self);
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  if (freeze_ai && freeze_on() && b1 && b2 && self && self != b1 && self != b2 &&
      rt::vm::committed(self, 8) && r32(self) == 0x10142638 &&
      r32(self + 4) != 1000u && r32(self + 4) != 1001u) {
    static std::atomic<uint64_t> n{0};
    const uint64_t c = n.fetch_add(1, std::memory_order_relaxed);
    if (c < 10) LOG_I("coop", "COOP FREEZE: actor update skipped for 0x%08x (call %llu)", self, (unsigned long long)c);
    return;
  }
  orig(ctx);
}

// 0x00af4cd0, 0x00a65198, 0x00a5a054, 0x00a5a084: mission-layer move/attack enable hooks.
// Kept passive (logging only): hook_xform_blit + hook_actor_update already freeze every non-player
// entity on the join while excluding body1/body2, whereas blindly forcing move/attack off here
// could lock the join player's controls in missions that toggle player move/attack around cutscenes.
void hook_move_enable_lo(Ctx& ctx, void (*orig)(Ctx&)) {
  orig(ctx);
}

void hook_command_dispatch(Ctx& ctx, void (*orig)(Ctx&)) {
  orig(ctx);
}

// 0x00a5a054: SetEnableMove(self, value). r3 = self, r4 = value.
void hook_enable_move(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 self = u32(ctx.r[3]);
  const u32 value = u32(ctx.r[4]);
  log_enable(g_enable_move, "ENABLE MOVE", self, value, ctx.lr, value);
  orig(ctx);
}

// 0x00a5a084: SetEnableAttack(self, value). r3 = self, r4 = value.
void hook_enable_attack(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 self = u32(ctx.r[3]);
  const u32 value = u32(ctx.r[4]);
  log_enable(g_enable_attack, "ENABLE ATTACK", self, value, ctx.lr, value);
  orig(ctx);
}

// 0x0068f860: RadarBlipEntry::Update(entry, ctx_struct, dt).
// AC4 fades stationary non-locked ACs off the radar after ~3 s via entry+56 (fade_timer, advanced
// in f_0068c95c) and dims them between radar sweeps via entry+48 (pulse, 0.01..1.0). Keep the
// co-op partner AC (body2) persistently visible and smoothly tracked as a green ally dot even
// while standing still at 0 km/h.
void hook_radar_blip_update(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 entry = u32(ctx.r[3]);
  orig(ctx);
  if (!rt::game::coop_on() || ctx.r[3] == 0 || !entry || !rt::vm::committed(entry, 76)) return;
  const u32 entity = r32(entry + 16);
  const u32 b2 = rt::game::coop_body(2);
  if (!entity || !b2 || entity != b2) return;
  wf32(entry + 56, 0.0f);
  wf32(entry + 48, 1.0f);
  if (rt::vm::committed(b2 + 0x40, 12)) {
    wf32(entry + 24, rf32(b2 + 0x40));
    wf32(entry + 28, rf32(b2 + 0x44));
    wf32(entry + 32, rf32(b2 + 0x48));
  }
}

}  // namespace ppu
