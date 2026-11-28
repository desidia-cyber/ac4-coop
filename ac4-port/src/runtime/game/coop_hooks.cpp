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

void f_00a4ece4(ppu::Ctx&);  // Actor::OnEvent(entity, type, id, arg, flag), TOC = 0x00ef5c94
void f_00a5091c(ppu::Ctx&);  // Actor::ApplyDamage(entity, DamageMsg*, flag), TOC = 0x00ef5c94
void f_00a50cb8(ppu::Ctx&);  // Actor::ApplyDirectDamage(entity, float damage), TOC = 0x00ef5c94
void f_00a53900_guest(ppu::Ctx&);  // Actor::SetHp(entity, float hp) + durability/gauge notify, TOC = 0x00ef5c94
void f_00a29aac(ppu::Ctx&);  // WeaponSlot::Purge(slot), TOC = 0x00ef5c94

namespace {

using ppu::r32;
using ppu::rf32;
using ppu::wf32;

constexpr u32 kHp = 0x2a4;
constexpr u32 kGameToc = 0x00ef5c94u;

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

void drain_ppu_coop_jobs(Ctx& ctx) {
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  if (!b1 || !b2) return;
  static thread_local bool in_ppu_jobs = false;
  if (in_ppu_jobs) return;
  in_ppu_jobs = true;
  if (rt::game::coopnet::role_join()) {
    u32 kills[16];
    const size_t nk = rt::game::coop_entities_drain_kills(kills, 16);
    for (size_t i = 0; i < nk; ++i) {
      const u32 tgt = kills[i];
      if (tgt < 0x10000000u || tgt == b1 || tgt == b2 || !rt::vm::committed(tgt + 0x2a8, 4)) continue;
      if (r32(tgt) != 0x10142638u) continue;
      const u32 eid = r32(tgt + 4);
      if (eid == 1000u || eid == 1001u) continue;
      const float cur_hp = float(rf32(tgt + kHp));
      if (!std::isfinite(cur_hp) || cur_hp <= 0.0f) continue;
      const Ctx saved = ctx;
      ctx.r[1] = saved.r[1] - 128u;
      ctx.r[2] = kGameToc;
      ctx.r[3] = tgt;
      ctx.f[1] = double(cur_hp) + 10000.0;
      f_00a50cb8(ctx);
      ctx = saved;
      float after_hp = float(rf32(tgt + kHp));
      bool via_event = false;
      if (std::isfinite(after_hp) && after_hp > 0.0f) {
        // Script-immortal actor (entity+752 != 0) skipped f_00a5091c: dispatch native death event
        // 0x80020064 (same path as KillEventGroup / f_00b0edfc) so explosions & radar clear run.
        const u32 arg_sp = u32(saved.r[1]) - 64u;
        if (rt::vm::committed(arg_sp, 16)) {
          ppu::w32(arg_sp + 0, 0u);
          ppu::w32(arg_sp + 4, 0u);
          ppu::w32(arg_sp + 8, 1u);
          ppu::w32(arg_sp + 12, 0u);
          ctx.r[1] = u64(arg_sp) - 128u;
          ctx.r[2] = kGameToc;
          ctx.r[3] = tgt;
          ctx.r[4] = 2u;
          ctx.r[5] = 0x80020064u;
          ctx.r[6] = arg_sp;
          ctx.r[7] = 0u;
          f_00a4ece4(ctx);
          ctx = saved;
          wf32(tgt + kHp, 0.0f);
          after_hp = 0.0f;
          via_event = true;
        }
      }
      static std::atomic<uint64_t> kill_logs{0};
      const uint64_t kl = kill_logs.fetch_add(1, std::memory_order_relaxed);
      if (kl < 40)
        LOG_I("coop", "COOP DEATH: native kill 0x%08x (id=%u, hp %.1f -> %.1f%s)", tgt, eid, double(cur_hp),
              double(after_hp), via_event ? ", via 0x80020064" : "");
    }
    rt::game::PartBreakJob parts[8];
    const size_t nparts = rt::game::coop_entities_drain_part_breaks(parts, 8);
    for (size_t i = 0; i < nparts; ++i) {
      const u32 tgt = parts[i].addr;
      const s32 pid = parts[i].part_id;
      if (tgt < 0x10000000u || tgt == b1 || tgt == b2 || pid < 0 || pid >= 16) continue;
      if (!rt::vm::committed(tgt + 0x2b8, 4) || r32(tgt) != 0x10142638u) continue;
      const u32 beg = r32(tgt + 0x2b0), end = r32(tgt + 0x2b4);
      if (beg < 0x10000000u || end <= beg || u32(pid) >= ((end - beg) >> 3)) continue;
      if (!rt::vm::committed(beg + u32(pid) * 8u, 8)) continue;
      const float pcur = float(rf32(beg + u32(pid) * 8u));
      if (!std::isfinite(pcur) || pcur <= 0.0f) continue;
      // Preserve main body HP when breaking a sub-part on a still-living boss/ship so f_00a5091c
      // cannot prematurely kill the main body before the host does.
      const float keep_hp = (std::isfinite(parts[i].host_hp) && parts[i].host_hp > 0.0f) ? parts[i].host_hp : 0.0f;
      if (keep_hp > 0.0f) wf32(tgt + kHp, keep_hp + pcur + 10000.0f);
      const Ctx saved = ctx;
      const u32 msg_sp = u32(ctx.r[1]) - 128u;
      if (rt::vm::committed(msg_sp, 96)) {
        for (u32 off = 0; off < 64; off += 4) ppu::w32(msg_sp + off, 0u);
        ppu::w32(msg_sp + 0, 3u);
        ppu::w32(msg_sp + 4, u32(pid));
        wf32(msg_sp + 8, pcur + 1000.0f);
        wf32(msg_sp + 12, 0.0f);
        if (rt::vm::committed(tgt + 0x40, 12)) {
          wf32(msg_sp + 16, rf32(tgt + 0x40));
          wf32(msg_sp + 20, rf32(tgt + 0x44));
          wf32(msg_sp + 24, rf32(tgt + 0x48));
        }
        ctx.r[1] = u64(msg_sp) - 128u;
        ctx.r[2] = kGameToc;
        ctx.r[3] = tgt;
        ctx.r[4] = msg_sp;
        ctx.r[5] = 0;
        f_00a5091c(ctx);
      }
      ctx = saved;
      if (keep_hp > 0.0f) wf32(tgt + kHp, keep_hp);
      LOG_I("coop", "COOP PART: native break 0x%08x (id=%u) part[%d] %.1f -> %.1f", tgt, r32(tgt + 4), pid,
            double(pcur), double(rf32(beg + u32(pid) * 8u)));
    }
    rt::game::HpSyncJob hps[16];
    const size_t nhp = rt::game::coop_entities_drain_hp_sync(hps, 16);
    for (size_t i = 0; i < nhp; ++i) {
      const u32 tgt = hps[i].addr;
      if (tgt < 0x10000000u || tgt == b1 || tgt == b2 || !rt::vm::committed(tgt + 0x2a8, 4)) continue;
      if (r32(tgt) != 0x10142638u) continue;
      const u32 eid = r32(tgt + 4);
      if (eid == 1000u || eid == 1001u) continue;
      if (!std::isfinite(hps[i].hp) || hps[i].hp <= 0.0f) continue;
      const Ctx saved = ctx;
      ctx.r[1] = saved.r[1] - 128u;
      ctx.r[2] = kGameToc;
      ctx.r[3] = tgt;
      ctx.f[1] = double(hps[i].hp);
      f_00a53900_guest(ctx);
      ctx = saved;
    }
  }
  u32 purges[4];
  const size_t np = rt::game::coop_body_drain_purges(purges, 4);
  for (size_t i = 0; i < np; ++i) {
    const u32 slot = purges[i];
    if (slot < 0x10000000u || !rt::vm::committed(slot, 20)) continue;
    const Ctx saved = ctx;
    ctx.r[1] = saved.r[1] - 128u;
    ctx.r[2] = kGameToc;
    ctx.r[3] = slot;
    f_00a29aac(ctx);
    ctx = saved;
    LOG_I("coop", "COOP WPN: executed native purge on slot 0x%08x", slot);
  }
  if (rt::game::coopnet::role_join()) {
    u32 epurges[4];
    const size_t nep = rt::game::coop_entities_drain_enemy_purges(epurges, 4);
    for (size_t i = 0; i < nep; ++i) {
      const u32 slot = epurges[i];
      if (slot < 0x10000000u || !rt::vm::committed(slot, 20)) continue;
      const Ctx saved = ctx;
      ctx.r[1] = saved.r[1] - 128u;
      ctx.r[2] = kGameToc;
      ctx.r[3] = slot;
      f_00a29aac(ctx);
      ctx = saved;
      LOG_I("coop", "COOP WPN: executed native enemy NEXT purge on slot 0x%08x", slot);
    }
  }
  in_ppu_jobs = false;
}

// 0x00a569f0: PrimalArmorObject::Update(pa_obj, dt), where pa_obj = *(u32*)(body + 712)
// (vtable 0x10142638 slot +0x64 = f_00a4e798). Reads PA stats at +0x308/+0x334 and updates current
// PA at +0x2d4. On the join, re-assert the host's authoritative PA/EN/ammo for AC1 right after the
// local tick, and drain any PPU-thread co-op jobs (native enemy death explosions, weapon purges).
void hook_hp_update(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 self = u32(ctx.r[3]);
  orig(ctx);
  const u32 b1 = rt::game::coop_body(1);
  if (!b1) return;
  if (rt::vm::committed(b1 + 712, 4) && self == r32(b1 + 712)) {
    if (rt::game::coopnet::role_join() && rt::vm::committed(self + 824, 4)) {
      float pa = 0.0f, pamax = 0.0f;
      if (rt::game::coopnet::peer_pa(&pa, &pamax) && pamax > 1.0f) {
        const float base = float(rf32(self + 820));
        const float mult = float(rf32(self + 776));
        const float local_max = (mult > 0.01f && mult <= 10.0f) ? (base * mult) : base;
        if (local_max > 1.0f) wf32(self + 724, pa > local_max ? local_max : pa);
      }
      rt::game::coop_body_reassert_join_vitals(b1);
    }
    drain_ppu_coop_jobs(ctx);
  } else if (rt::game::coopnet::role_join() && self == b1) {
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
  if (rt::game::coopnet::active() && self >= 0x10000000u) {
    rt::game::coop_entities_note_live(self);
    if (rt::vm::committed(self, 8)) {
      const u32 vt = r32(self);
      if (vt != 0x10142638u) {
        static std::mutex vt_mu;
        static u32 seen_vts[16] = {};
        static int seen_n = 0;
        std::lock_guard lk(vt_mu);
        bool known = false;
        for (int i = 0; i < seen_n; ++i)
          if (seen_vts[i] == vt) { known = true; break; }
        if (!known && seen_n < 16) {
          seen_vts[seen_n++] = vt;
          LOG_I("coop", "COOP XBLIT non-actor vtable 0x%08x on obj 0x%08x id=%u caller 0x%08x", vt, self, r32(self + 4),
                u32(ctx.lr));
        }
      }
    }
  }
  // The freeze: this blit is what moves the entity's live +0x10 transform. On the join, for every
  // LIVE non-player entity, do not run it once both player ACs are built (during BUILD#1/BUILD#2
  // both bodies and initial mission spawns must receive their spawn transforms, and already-dead
  // entities are allowed through so native death wreckage/collapse animations play cleanly).
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  if (b1 && b2) drain_ppu_coop_jobs(ctx);
  const bool entity_alive = !rt::vm::committed(self + kHp, 4) || float(rf32(self + kHp)) > 0.0f;
  if (freeze_on() && b1 && b2 && self && self != b1 && self != b2 && entity_alive &&
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
