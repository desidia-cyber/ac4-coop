// Co-op: where an AC's world transform actually lives.
//
// The AC body object (vtable 0x10142638, entity id at +4) holds floats 1.0 at +0x10, +0x24 and +0x38
// when the builder creates it, which is the shape of a 4x4 matrix whose three rows start at those
// offsets. This file answers, by observation and then by writing, which of those floats are the
// world position and which object is authoritative.
//
// Settings (all off by default; a run without them is unchanged):
//   AC4_COOP_DUMP=1        once a second: both bodies, 0x180 bytes as floats, plus the pointer-looking
//                          words of the first 0x180 bytes (one level of dereference for the first few)
//   AC4_COOP_DUMP=2        the same, with 0x40 floats at every pointer-looking word
//   AC4_COOP_POKE_FILE=F   the write test: read "<hex offset> <float delta>" from F, add the delta to
//                          that float at the SECOND body for one frame, then truncate F. Writing the
//                          file is how an outside driver times the test exactly.
//   AC4_COOP_POS_OFFS=a,b,c  the offsets coop_body_position() reads as x,y,z (default 0x40,0x44,0x48)
//   AC4_COOP_VEL_OFFS=a,b,c  the offsets it reads as velocity (default: none)
#include "runtime/game/coop_body.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/game/coop_build.h"
#include "runtime/game/coop_net.h"
#include "runtime/game/coop_session.h"
#include "runtime/log.h"
#include "runtime/memory.h"

namespace {

using ppu::r32;
using ppu::wf32;

constexpr u32 kDumpBytes = 0x200;  // past body+0x1b0 (the rotation row 2 input_hooks.cpp reads) and +0x1e4
constexpr u32 kBodyId = 4;       // entity id
constexpr u32 kBodyVt = 0;
constexpr u32 kBodyAp = 0x2a4;     // current AP/HP (measured, research\26 s1)
constexpr u32 kBodyApMax = 0x2a8;
constexpr u32 kBodyAcObj = 708;    // body+0x2c4: AC controller object (f_009acafc)
constexpr u32 kBodyPaObj = 712;    // body+0x2c8: PrimalArmorObject* (vtable 0x10142638 slot +0x64 = f_00a4e798)
constexpr u32 kPaObjCur = 724;     // pa_obj+0x2d4: current PA (f_00a53350 / f_00a53998 / f_00a569f0)
constexpr u32 kPaObjMult = 776;    // pa_obj+0x308: PA max multiplier (f_00a539ec)
constexpr u32 kPaObjMax = 820;     // pa_obj+0x334: base max PA (f_00a53178)
constexpr u32 kAcWpnMgr = 204;     // ac+0xcc: WeaponManager* (f_009acafc, f_00a19f08)
constexpr u32 kAcEnObj = 208;      // ac+0xd0: embedded Generator/EN sub-object (f_00686790, f_00a8f1c8)
constexpr u32 kEnFlags = 0;        // bit 31 (0x80000000): EN depleted / redline
constexpr u32 kEnCur = 4;          // float current EN
constexpr u32 kEnMax = 8;          // float max EN
constexpr u32 kWpnBankR = 428;     // Right bank: +0 active_idx, +4 target_idx, +8 slots[2]*20, +48 count
constexpr u32 kWpnBankL = 480;     // Left bank: identical layout
constexpr u32 kWpnHolders = 1072;  // 6 weapon holder ptrs (f_00a1a7a8): holder+4=Weapon*, holder+8=purged

u32 body_ac_obj(u32 body) {
  if (!body || !rt::vm::committed(body + kBodyAcObj, 4)) return 0;
  const u32 ac = r32(body + kBodyAcObj);
  if (ac < 0x10000000u || !rt::vm::committed(ac + kAcEnObj + 16, 4)) return 0;
  return ac;
}

u32 body_wpn_mgr(u32 body) {
  const u32 ac = body_ac_obj(body);
  if (!ac) return 0;
  const u32 wm = r32(ac + kAcWpnMgr);
  if (wm < 0x10000000u || !rt::vm::committed(wm + kWpnHolders + 24, 4)) return 0;
  return wm;
}

bool read_body_en(u32 body, float* out_cur, float* out_max, bool* out_redline) {
  const u32 ac = body_ac_obj(body);
  if (!ac) return false;
  const u32 en = ac + kAcEnObj;
  const float cur = float(ppu::rf32(en + kEnCur));
  const float maxv = float(ppu::rf32(en + kEnMax));
  if (!std::isfinite(cur) || !std::isfinite(maxv) || maxv <= 10.0f || maxv > 1e7f) return false;
  if (out_cur) *out_cur = cur;
  if (out_max) *out_max = maxv;
  if (out_redline) *out_redline = (r32(en + kEnFlags) & 0x80000000u) != 0;
  return true;
}

void write_body_en(u32 body, float cur, bool redline) {
  const u32 ac = body_ac_obj(body);
  if (!ac || !std::isfinite(cur)) return;
  const u32 en = ac + kAcEnObj;
  const float maxv = float(ppu::rf32(en + kEnMax));
  if (!std::isfinite(maxv) || maxv <= 10.0f || maxv > 1e7f) return;
  float clamped = cur < 0.0f ? 0.0f : (cur > maxv ? maxv : cur);
  wf32(en + kEnCur, clamped);
  u32 flags = r32(en + kEnFlags);
  if (redline)
    flags |= 0x80000000u;
  else
    flags &= ~0x80000000u;
  ppu::w32(en + kEnFlags, flags);
}

constexpr u32 kAmmoFnMag = 0x00abfba4u;     // Magazine AmmoController::GetInfo (ac+4 -> mag+28/+32/+36)
constexpr u32 kAmmoFnDirect = 0x00ac0490u;  // Direct AmmoController::GetInfo (ac+4 cur, ac+8 max)

u32 weapon_ammo_fn(u32 wpn, u32* out_ac) {
  if (wpn < 0x10000000u || !rt::vm::committed(wpn + 12, 4)) return 0;
  const u32 ac = r32(wpn + 8);
  if (ac < 0x10000000u || !rt::vm::committed(ac + 12, 4)) return 0;
  const u32 avt = r32(ac);
  if (avt < 0x10000000u || !rt::vm::committed(avt + 12, 4)) return 0;
  const u32 opd = r32(avt + 8);
  if (opd < 0x00010000u || !rt::vm::committed(opd, 4)) return 0;
  if (out_ac) *out_ac = ac;
  return r32(opd);
}

int16_t read_holder_live_ammo(u32 holder) {
  if (holder < 0x10000000u || !rt::vm::committed(holder + 12, 4)) return -1;
  if (r32(holder + 8) != 0) return -1;  // purged
  u32 ac = 0;
  const u32 fn = weapon_ammo_fn(r32(holder + 4), &ac);
  if (fn == kAmmoFnMag) {
    const u32 mag = r32(ac + 4);
    if (mag < 0x10000000u || !rt::vm::committed(mag + 40, 4)) return -1;
    const s32 cur_mag = s32(r32(mag + 28));
    const s32 res = s32(r32(mag + 36));
    const s32 tot = cur_mag + res;
    if (cur_mag >= 0 && res >= 0 && tot <= 32767) return int16_t(tot);
  } else if (fn == kAmmoFnDirect) {
    const s32 cur = s32(r32(ac + 4));
    const s32 maxa = s32(r32(ac + 8));
    if (cur != 99999 && maxa > 0 && cur >= 0 && cur <= 32767) return int16_t(cur);
  }
  return -1;
}

void write_holder_live_ammo(u32 holder, int16_t want) {
  if (want < 0 || holder < 0x10000000u || !rt::vm::committed(holder + 12, 4)) return;
  if (r32(holder + 8) != 0) return;  // purged
  u32 ac = 0;
  const u32 fn = weapon_ammo_fn(r32(holder + 4), &ac);
  if (fn == kAmmoFnMag) {
    const u32 mag = r32(ac + 4);
    if (mag < 0x10000000u || !rt::vm::committed(mag + 40, 4)) return;
    const s32 cur_mag = s32(r32(mag + 28));
    const s32 res = s32(r32(mag + 36));
    if (cur_mag < 0 || res < 0 || cur_mag + res == s32(want)) return;
    if (s32(want) >= cur_mag) {
      ppu::w32(mag + 36, u32(s32(want) - cur_mag));
    } else {
      ppu::w32(mag + 36, 0u);
      ppu::w32(mag + 28, u32(want));
    }
  } else if (fn == kAmmoFnDirect) {
    const s32 cur = s32(r32(ac + 4));
    const s32 maxa = s32(r32(ac + 8));
    if (cur == 99999 || maxa <= 0) return;
    const s32 clamped = s32(want) > maxa ? maxa : s32(want);
    if (cur != clamped) ppu::w32(ac + 4, u32(clamped));
  }
}

bool read_body_weapons(u32 body, rt::game::coopnet::WeaponSyncState* out) {
  if (!out) return false;
  *out = rt::game::coopnet::WeaponSyncState{};
  const u32 wm = body_wpn_mgr(body);
  if (!wm) return false;
  const s32 r_cnt = s32(r32(wm + kWpnBankR + 48));
  const s32 r_act = s32(r32(wm + kWpnBankR));
  const s32 l_cnt = s32(r32(wm + kWpnBankL + 48));
  const s32 l_act = s32(r32(wm + kWpnBankL));
  const uint8_t rn = (r_cnt > 0 && r_act >= 0 && r_act < 4) ? uint8_t(r_act) : 0x0fu;
  const uint8_t ln = (l_cnt > 0 && l_act >= 0 && l_act < 4) ? uint8_t(l_act) : 0x0fu;
  out->active = uint8_t(rn | (ln << 4));
  uint8_t purged = 0;
  for (int i = 0; i < 6; ++i) {
    const u32 holder = r32(wm + kWpnHolders + 4u * u32(i));
    if (holder >= 0x10000000u && rt::vm::committed(holder + 12, 4)) {
      if (r32(holder + 8) != 0)
        purged |= uint8_t(1u << i);
      else
        out->ammo[i] = read_holder_live_ammo(holder);
    }
  }
  out->purged = purged;
  return true;
}

void write_body_hud_ammo(u32 body, const int16_t ammo[6]) {
  const u32 wm = body_wpn_mgr(body);
  if (!wm) return;
  // Write directly to each weapon's live AmmoController (Magazine +28/+36 or Direct +4) so both
  // actual firing/empty-gun mechanics AND the HUD display stay 100% locked to the host.
  for (int i = 0; i < 6; ++i) {
    if (ammo[i] < 0) continue;
    const u32 holder = r32(wm + kWpnHolders + 4u * u32(i));
    write_holder_live_ammo(holder, ammo[i]);
  }
}

std::mutex g_purge_mu;
std::vector<u32> g_pending_purges;

struct WpnDebounce {
  double sw_mismatch_since[2] = {-1.0, -1.0};
  double purge_mismatch_since[6] = {-1.0, -1.0, -1.0, -1.0, -1.0, -1.0};
  uint8_t purge_queued_mask = 0;
};
WpnDebounce g_wpn_db[3];  // [1] = body1, [2] = body2

void reconcile_body_weapons(int which, u32 body, uint8_t want_active, uint8_t want_purged) {
  if (which < 1 || which > 2 || !body) return;
  const u32 wm = body_wpn_mgr(body);
  if (!wm) return;
  const double now = rt::log_time();
  WpnDebounce& db = g_wpn_db[which];
  constexpr double kDebounce = 0.40;  // survive normal RTT without fighting in-flight inputs
  // 1) Active weapon switch reconciliation (right = bank 0, left = bank 1).
  const u32 banks[2] = {wm + kWpnBankR, wm + kWpnBankL};
  for (int b = 0; b < 2; ++b) {
    const uint8_t want = uint8_t((want_active >> (b * 4)) & 0x0fu);
    const s32 cnt = s32(r32(banks[b] + 48));
    const s32 cur = s32(r32(banks[b]));
    const s32 tgt = s32(r32(banks[b] + 4));
    if (want < 4 && s32(want) < cnt && cur != s32(want) && tgt != s32(want)) {
      if (db.sw_mismatch_since[b] < 0.0) db.sw_mismatch_since[b] = now;
      if (now - db.sw_mismatch_since[b] >= kDebounce) {
        ppu::w32(banks[b] + 4, u32(want));  // target_idx -> native weapon-switch animation
        db.sw_mismatch_since[b] = -1.0;
        LOG_I("coop", "COOP WPN: body%d %s bank switched %d -> %u (reconciled)", which, b == 0 ? "R" : "L", cur,
              unsigned(want));
      }
    } else {
      db.sw_mismatch_since[b] = -1.0;
    }
  }
  // 2) Purged weapon reconciliation: if peer purged holder i and we still hold it, queue native f_00a29aac(slot).
  for (int i = 0; i < 6; ++i) {
    if (!(want_purged & (1u << i))) {
      db.purge_mismatch_since[i] = -1.0;
      continue;
    }
    const u32 holder = r32(wm + kWpnHolders + 4u * u32(i));
    if (holder < 0x10000000u || !rt::vm::committed(holder + 12, 4)) continue;
    if (r32(holder + 8) != 0) {
      db.purge_mismatch_since[i] = -1.0;
      continue;  // already purged locally
    }
    if (db.purge_queued_mask & (1u << i)) continue;
    if (db.purge_mismatch_since[i] < 0.0) db.purge_mismatch_since[i] = now;
    if (now - db.purge_mismatch_since[i] < kDebounce) continue;
    // Locate the 20-byte bank slot referencing this holder (+12 == holder) for f_00a29aac(slot).
    u32 slot_addr = 0;
    for (int b = 0; b < 2 && !slot_addr; ++b) {
      const s32 cnt = s32(r32(banks[b] + 48));
      for (s32 k = 0; k < cnt && k < 2; ++k) {
        const u32 s_ptr = banks[b] + 8u + 20u * u32(k);
        if (r32(s_ptr + 12) == holder) {
          slot_addr = s_ptr;
          break;
        }
      }
    }
    if (slot_addr) {
      db.purge_queued_mask |= uint8_t(1u << i);
      {
        std::lock_guard lk(g_purge_mu);
        g_pending_purges.push_back(slot_addr);
      }
      LOG_I("coop", "COOP WPN: body%d queued native purge for holder[%d] 0x%08x (slot 0x%08x)", which, i, holder,
            slot_addr);
    }
  }
}

void log_weapons_once(u32 b1, u32 b2) {
  static uint16_t logged_ep = 0;
  const uint16_t ep = rt::game::coopsession::epoch();
  if (!ep || ep == logged_ep || !b1 || !b2 || !rt::game::coopsession::linked_in_mission()) return;
  float en1 = 0.0f, enm1 = 0.0f, en2 = 0.0f, enm2 = 0.0f;
  bool r1 = false, r2 = false;
  if (!read_body_en(b1, &en1, &enm1, &r1) || !read_body_en(b2, &en2, &enm2, &r2)) return;
  rt::game::coopnet::WeaponSyncState w1{}, w2{};
  if (!read_body_weapons(b1, &w1) || !read_body_weapons(b2, &w2)) return;
  logged_ep = ep;
  LOG_I("coop", "COOP EN: body1 %.1f/%.1f (red=%d) | body2 %.1f/%.1f (red=%d)", double(en1), double(enm1), int(r1),
        double(en2), double(enm2), int(r2));
  LOG_I("coop", "COOP WPN: b1 act=0x%02x prg=0x%02x ammo=[%d,%d,%d,%d,%d,%d] | b2 act=0x%02x prg=0x%02x ammo=[%d,%d,%d,%d,%d,%d]",
        unsigned(w1.active), unsigned(w1.purged), int(w1.ammo[0]), int(w1.ammo[1]), int(w1.ammo[2]), int(w1.ammo[3]),
        int(w1.ammo[4]), int(w1.ammo[5]), unsigned(w2.active), unsigned(w2.purged), int(w2.ammo[0]), int(w2.ammo[1]),
        int(w2.ammo[2]), int(w2.ammo[3]), int(w2.ammo[4]), int(w2.ammo[5]));
  if (const u32 wm1 = body_wpn_mgr(b1)) {
    std::string winfo;
    for (int i = 0; i < 6; ++i) {
      const u32 h = r32(wm1 + kWpnHolders + 4u * u32(i));
      if (h < 0x10000000u || !rt::vm::committed(h + 12, 4)) continue;
      const u32 w = r32(h + 4);
      if (w < 0x10000000u || !rt::vm::committed(w, 4)) continue;
      const u32 vt = r32(w);
      u32 fn52 = 0, ac = 0, af8 = 0;
      if (vt >= 0x10000000u && rt::vm::committed(vt + 56, 4)) {
        const u32 opd = r32(vt + 52);
        if (opd >= 0x00010000u && rt::vm::committed(opd, 4)) fn52 = r32(opd);
      }
      if (rt::vm::committed(w + 12, 4)) {
        ac = r32(w + 8);
        if (ac >= 0x10000000u && rt::vm::committed(ac, 4)) {
          const u32 avt = r32(ac);
          if (avt >= 0x10000000u && rt::vm::committed(avt + 12, 4)) {
            const u32 aopd = r32(avt + 8);
            if (aopd >= 0x00010000u && rt::vm::committed(aopd, 4)) af8 = r32(aopd);
          }
        }
      }
      winfo += ac4::strprintf(" [%d]w=0x%08x(vt=0x%08x,f52=0x%08x,ac=0x%08x,af8=0x%08x)", i, w, vt, fn52, ac, af8);
    }
    if (!winfo.empty()) LOG_I("coop", "COOP WPN OBJ:%s", winfo.c_str());
  }
}

u32 body_pa_obj(u32 body) {
  if (!body || !rt::vm::committed(body + kBodyPaObj, 4)) return 0;
  const u32 pa = r32(body + kBodyPaObj);
  if (pa < 0x10000000u || !rt::vm::committed(pa + kPaObjMax + 4, 4)) return 0;
  return pa;
}

bool read_body_pa(u32 body, float* out_cur, float* out_max) {
  const u32 pa = body_pa_obj(body);
  if (!pa) return false;
  const float cur = float(ppu::rf32(pa + kPaObjCur));
  const float base = float(ppu::rf32(pa + kPaObjMax));
  const float mult = float(ppu::rf32(pa + kPaObjMult));
  const float maxv = (std::isfinite(mult) && mult > 0.01f && mult <= 10.0f) ? (base * mult) : base;
  if (!std::isfinite(cur) || !std::isfinite(maxv) || maxv <= 1.0f) return false;
  if (out_cur) *out_cur = cur;
  if (out_max) *out_max = maxv;
  return true;
}

std::atomic<u32> g_body[3];  // [1] the local player, [2] the second AC
std::atomic<u32> g_obj[3];

enum class DumpMode { Off, Plain, Deep };
DumpMode g_dump = DumpMode::Off;

struct Offs {
  u32 at[3] = {0x40, 0x44, 0x48};
  bool any = false;
};
Offs g_pos, g_vel;

std::string g_poke_path;

double g_last_dump = -1e9;

// A value that looks like a guest pointer into the committed heap areas.
bool looks_pointer(u32 v) { return v >= 0x00010000u && v < 0x40000000u && (v & 3) == 0; }

// "<a,b,c>" (each hex or decimal) or "none".
Offs parse_offs(const char* env) {
  Offs o;
  char buf[128]{};
  DWORD n = GetEnvironmentVariableA(env, buf, sizeof buf);
  if (!n || n >= sizeof buf) return o;
  std::string s(buf, n);
  if (s == "none" || s.empty()) return o;
  size_t i = 0, k = 0;
  while (i < s.size() && k < 3) {
    size_t j = s.find(',', i);
    if (j == std::string::npos) j = s.size();
    o.at[k++] = u32(strtoul(s.substr(i, j - i).c_str(), nullptr, 0));
    i = j + 1;
  }
  o.any = k == 3;
  return o;
}

void init_once() {
  static std::once_flag once;
  std::call_once(once, [] {
    char buf[8]{};
    DWORD n = GetEnvironmentVariableA("AC4_COOP_DUMP", buf, sizeof buf);
    if (n > 0 && n < sizeof buf) g_dump = buf[0] == '2' ? DumpMode::Deep : buf[0] == '1' ? DumpMode::Plain : DumpMode::Off;
    n = GetEnvironmentVariableA("AC4_COOP_POKE_FILE", buf, 0);  // size query only; real read below
    char path[MAX_PATH]{};
    n = GetEnvironmentVariableA("AC4_COOP_POKE_FILE", path, sizeof path);
    if (n > 0 && n < sizeof path) g_poke_path = path;
    g_pos = parse_offs("AC4_COOP_POS_OFFS");
    // Measured (research\24 s3): the body holds FOUR 4x4 copies of one transform:
    //   +0x10/+0x40  first matrix  (row 3 = +0x40)  <- the one the game keeps current
    //   +0x50/+0x80  second matrix (row 3 = +0x80)  <- trails the first by a frame
    //   +0x90/+0xc0  third matrix  (row 3 = +0xc0)  <- a further copy
    //   +0x190/+0x1c0 fourth       (row 3 = +0x1c0) <- equals the first
    // +0x40 is the authority: it is the row that moves first and the one the writes in the M2 test
    // are applied to.
    if (!g_pos.any) {
      g_pos.at[0] = 0x40;
      g_pos.at[1] = 0x44;
      g_pos.at[2] = 0x48;
      g_pos.any = true;
    }
    g_vel = parse_offs("AC4_COOP_VEL_OFFS");
    if (g_dump != DumpMode::Off)
      LOG_I("coop", "COOP DUMP: %s, position offsets 0x%x/0x%x/0x%x%s%s", g_dump == DumpMode::Deep ? "deep" : "plain",
            g_pos.at[0], g_pos.at[1], g_pos.at[2], g_vel.any ? ", velocity offsets 0x" : "",
            g_vel.any ? ac4::strprintf("%x/0x%x/0x%x", g_vel.at[0], g_vel.at[1], g_vel.at[2]).c_str() : "");
    if (!g_poke_path.empty()) LOG_I("coop", "COOP POKE: watching %s for '<hex offset> <float delta>'", g_poke_path.c_str());
  });
}

// The floats of one object, as one log line per 16 bytes, and its pointer-looking words.
void dump_object(const char* what, u32 obj, u32 bytes, bool deep) {
  if (!obj || !rt::vm::committed(obj, bytes)) {
    LOG_W("coop", "COOP DUMP %s 0x%08x: not committed for 0x%x bytes", what, obj, bytes);
    return;
  }
  LOG_I("coop", "COOP DUMP %s 0x%08x vt 0x%08x +4 %u", what, obj, r32(obj + kBodyVt), r32(obj + kBodyId));
  for (u32 off = 0; off < bytes; off += 16) {
    LOG_I("coop", "COOP DUMP %s +0x%03x %12.4f %12.4f %12.4f %12.4f", what, off, ppu::rf32(obj + off),
          ppu::rf32(obj + off + 4), ppu::rf32(obj + off + 8), ppu::rf32(obj + off + 12));
  }
  std::string ptrs;
  int shown = 0;
  for (u32 off = 0; off < bytes; off += 4) {
    u32 v = r32(obj + off);
    if (!looks_pointer(v)) continue;
    if (shown < 24) ptrs += ac4::strprintf(" +0x%x:0x%08x", off, v);
    ++shown;
  }
  LOG_I("coop", "COOP DUMP %s pointers (%d):%s", what, shown, ptrs.c_str());
  if (!deep) return;
  // One level of dereference: the candidate objects named above, their first 0x100 bytes as floats.
  for (u32 off = 0; off < bytes && off < 0x40; off += 4) {
    u32 v = r32(obj + off);
    if (!looks_pointer(v) || !rt::vm::committed(v, 0x100)) continue;
    std::string line;
    for (u32 i = 0; i < 0x40; i += 4) line += ac4::strprintf(" %10.3f", ppu::rf32(v + i));
    LOG_I("coop", "COOP DUMP   [%s+0x%x] -> 0x%08x floats:%s", what, off, v, line.c_str());
  }
}

// "<hex offset> <float delta>" from the poke file: add it to that float at body 2, one frame only.
// The file is emptied after each test so the next one can be queued by writing it again.
void poke_frame() {
  if (g_poke_path.empty()) return;
  FILE* f = std::fopen(g_poke_path.c_str(), "r");
  if (!f) return;
  char off_s[64] = {}, d_s[64] = {};
  int got = std::fscanf(f, "%63s %63s", off_s, d_s);
  std::fclose(f);
  if (got != 2) return;
  const u32 body = rt::game::coop_body(2);
  if (!body) {
    LOG_W("coop", "COOP POKE: no body2 yet; test dropped");
    std::fopen(g_poke_path.c_str(), "w");
    return;
  }
  const u32 off = u32(strtoul(off_s, nullptr, 0));
  const double delta = std::atof(d_s);
  if (!rt::vm::committed(body + off, 4)) {
    LOG_W("coop", "COOP POKE: body2 0x%08x +0x%x is not committed", body, off);
    return;
  }
  const double before = ppu::rf32(body + off);
  wf32(body + off, before + delta);
  const double after = ppu::rf32(body + off);
  std::fopen(g_poke_path.c_str(), "w");  // truncate: this test is spent
  LOG_W("coop", "COOP POKE: body2 0x%08x +0x%x %.4f -> %.4f (delta %.4f), one frame", body, off, before, after, delta);
}

// The LOCAL AC1 (body1) supplies what is sent. The offsets used to read AC2's position and to write
// it are the same (g_pos), so the error logged is a real measurement of this instance's AC2.
constexpr float kSlop = 1.5f, kSnap = 50.0f, kGain = 0.35f;

bool rot_orthonormal(const float rot[9]) {
  for (int r = 0; r < 3; ++r) {
    float n2 = 0.0f;
    for (int c = 0; c < 3; ++c) {
      const float v = rot[r * 3 + c];
      if (!std::isfinite(v) || std::fabs(v) > 1.05f) return false;
      n2 += v * v;
    }
    if (n2 < 0.90f || n2 > 1.10f) return false;
  }
  return true;
}

bool pos_valid(const float p[3]) {
  return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) &&
         std::fabs(p[0]) < 1e6f && std::fabs(p[1]) < 1e6f && std::fabs(p[2]) < 1e6f;
}

uint16_t g_spawn_offset_epoch = 0;

void maybe_apply_spawn_offset(u32 b1, u32 b2) {
  const uint16_t ep = rt::game::coopsession::epoch();
  if (!ep || g_spawn_offset_epoch == ep || !rt::game::coopsession::linked_in_mission()) return;
  if (!b1 || !b2 || !rt::vm::committed(b1 + 0x1c0, 16) || !rt::vm::committed(b2 + 0x1c0, 16)) return;
  float rot[9], p1[3], p2[3];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) rot[r * 3 + c] = float(ppu::rf32(b1 + 0x10 + 16 * r + 4 * c));
  for (int i = 0; i < 3; ++i) {
    p1[i] = float(ppu::rf32(b1 + 0x40 + 4 * i));
    p2[i] = float(ppu::rf32(b2 + 0x40 + 4 * i));
  }
  if (!rot_orthonormal(rot) || !pos_valid(p1) || !pos_valid(p2)) return;
  if (p1[0] * p1[0] + p1[1] * p1[1] + p1[2] * p1[2] < 1.0f) return;  // wait until placed at real spawn point
  g_spawn_offset_epoch = ep;
  const double dx = double(p2[0]) - p1[0], dy = double(p2[1]) - p1[1], dz = double(p2[2]) - p1[2];
  if (dx * dx + dy * dy + dz * dz < 4.0) {
    const u32 shift = rt::game::coopnet::role_join() ? b1 : b2;
    // Compact echelon wingman offset (+5.0 right, -3.0 back = ~5.8 units total clearance):
    // half the lateral footprint of +10.0 so P2 stays safely inside tight corridors/elevators/platforms.
    constexpr double kRightOffset = 5.0;
    constexpr double kBackOffset = -3.0;
    const double nx = ppu::rf32(shift + 0x40) + double(rot[0]) * kRightOffset + double(rot[6]) * kBackOffset;
    const double ny = ppu::rf32(shift + 0x44) + double(rot[1]) * kRightOffset + double(rot[7]) * kBackOffset;
    const double nz = ppu::rf32(shift + 0x48) + double(rot[2]) * kRightOffset + double(rot[8]) * kBackOffset;
    wf32(shift + 0x40, nx);
    wf32(shift + 0x44, ny);
    wf32(shift + 0x48, nz);
    wf32(shift + 0x1c0, nx);
    wf32(shift + 0x1c4, ny);
    wf32(shift + 0x1c8, nz);
    wf32(shift + 0xf0, nx);
    wf32(shift + 0xf4, ny);
    wf32(shift + 0xf8, nz);
    LOG_I("coop", "COOP SPAWN: shifted %s 0x%08x +%.1f right/%.1f back -> (%.1f, %.1f, %.1f)",
          rt::game::coopnet::role_join() ? "join body1" : "host body2", shift, kRightOffset, kBackOffset, nx, ny, nz);
  }
}

void net_frame() {
  // What to publish: this instance's own AC1 (the local player). The first 4x4 at body+0x10 holds the
  // rotation rows; its row 3 is the translation (measured, research\24 section 3).
  const u32 b1 = rt::game::coop_body(1);
  const u32 b2_now = rt::game::coop_body(2);
  maybe_apply_spawn_offset(b1, b2_now);
  {
    static uint16_t dumped_ep = 0;
    static double dump_due = -1.0;
    const uint16_t ep = rt::game::coopsession::epoch();
    if (ep && ep != dumped_ep && b1 && b2_now && rt::game::coopsession::linked_in_mission()) {
      const double now_t = rt::log_time();
      if (dump_due < 0.0) dump_due = now_t + 6.0;
      if (now_t >= dump_due) {
        dumped_ep = ep;
        dump_due = -1.0;
        rt::game::coop_log_ac_parts("LIVE_B1", b1);
        rt::game::coop_log_ac_parts("LIVE_B2", b2_now);
      }
    } else if (!ep) {
      dump_due = -1.0;
    }
  }
  if (b1 && rt::vm::committed(b1 + 0x10, 0x40)) {
    float pos[3], rot[9], vel[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) pos[i] = float(ppu::rf32(b1 + 0x40 + 4 * i));
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c) rot[r * 3 + c] = float(ppu::rf32(b1 + 0x10 + 16 * r + 4 * c));
    if (g_vel.any && rt::vm::committed(b1 + g_vel.at[2], 4)) {
      for (int i = 0; i < 3; ++i) vel[i] = float(ppu::rf32(b1 + g_vel.at[i]));
    } else if (pos_valid(pos)) {
      // Derive smoothed linear velocity from consecutive body1 positions for high-latency dead reckoning.
      static float prev_p[3] = {}, filt_v[3] = {};
      static double prev_t = -1e9;
      static u32 prev_b1 = 0;
      const double now_t = rt::log_time();
      const double dt = now_t - prev_t;
      if (prev_b1 == b1 && dt > 0.004 && dt < 0.25) {
        double v2 = 0.0;
        float raw_v[3];
        for (int i = 0; i < 3; ++i) {
          raw_v[i] = float((pos[i] - prev_p[i]) / dt);
          v2 += double(raw_v[i]) * raw_v[i];
        }
        if (std::isfinite(v2) && v2 < 1500.0 * 1500.0) {
          for (int i = 0; i < 3; ++i) filt_v[i] = 0.5f * filt_v[i] + 0.5f * raw_v[i];
        } else {
          filt_v[0] = filt_v[1] = filt_v[2] = 0.0f;
        }
      } else if (prev_b1 != b1 || dt >= 0.25) {
        filt_v[0] = filt_v[1] = filt_v[2] = 0.0f;
      }
      prev_b1 = b1;
      prev_t = now_t;
      for (int i = 0; i < 3; ++i) {
        prev_p[i] = pos[i];
        vel[i] = filt_v[i];
      }
    }
    // Only publish a genuinely initialized orthonormal transform (never a zero/NaN pre-spawn matrix).
    if (pos_valid(pos) && rot_orthonormal(rot) && (pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2] > 1.0f)) {
      rt::game::coopnet::set_local_transform(pos, rot, vel, 0);
    }
  }

  // Publish this instance's OWN AC1 AP, PA & active/purged weapons (for Partner HUD & visual AC2 sync).
  if (b1 && rt::vm::committed(b1 + kBodyApMax, 4)) {
    const float ap1 = float(ppu::rf32(b1 + kBodyAp));
    const float apm1 = float(ppu::rf32(b1 + kBodyApMax));
    float pa1 = 0.0f, pam1 = 0.0f;
    read_body_pa(b1, &pa1, &pam1);
    rt::game::coopnet::set_local_self_vitals(ap1, apm1, pa1, pam1);
    rt::game::coopnet::WeaponSyncState w1{};
    if (read_body_weapons(b1, &w1)) rt::game::coopnet::set_local_self_weapons(w1.active, w1.purged);
  }

  // The AP, PA, EN & weapons of the AC this instance simulates FOR the peer (AC2). On the host that
  // is the join player, so the host's damage, PA, EN, ammo & weapon state are authoritative for AC1.
  if (b2_now && rt::vm::committed(b2_now + kBodyApMax, 4)) {
    rt::game::coopnet::set_local_peer_ap(float(ppu::rf32(b2_now + kBodyAp)), float(ppu::rf32(b2_now + kBodyApMax)));
    float pa2 = 0.0f, pam2 = 0.0f;
    if (read_body_pa(b2_now, &pa2, &pam2)) rt::game::coopnet::set_local_peer_pa(pa2, pam2);
    float en2 = 0.0f, enm2 = 0.0f;
    bool en2_red = false;
    if (read_body_en(b2_now, &en2, &enm2, &en2_red)) rt::game::coopnet::set_local_peer_en(en2, enm2, en2_red);
    rt::game::coopnet::WeaponSyncState w2{};
    if (read_body_weapons(b2_now, &w2)) rt::game::coopnet::set_local_peer_weapons(w2);
    log_weapons_once(b1, b2_now);
    // Log verified PA sub-object once per epoch for diagnostics.
    static uint16_t pa_logged_ep = 0;
    const uint16_t ep_now = rt::game::coopsession::epoch();
    if (ep_now && ep_now != pa_logged_ep && b1 && pam2 > 1.0f) {
      float pa1 = 0.0f, pam1 = 0.0f;
      if (read_body_pa(b1, &pa1, &pam1)) {
        pa_logged_ep = ep_now;
        LOG_I("coop", "COOP PA: body1 0x%08x pa_obj 0x%08x (%.1f/%.1f) | body2 0x%08x pa_obj 0x%08x (%.1f/%.1f)",
              b1, body_pa_obj(b1), double(pa1), double(pam1), b2_now, body_pa_obj(b2_now), double(pa2), double(pam2));
      }
    }
  }

  rt::game::coopnet::PadState pad;
  rt::game::coopnet::remote_pad(&pad);   // the pad itself reaches the game through sys_io's port 1
  rt::game::coopnet::frame(pad);

  // What to apply: the peer's AC1 drives this instance's AC2; the host's AP & PA for this player's AC
  // (its AC2) drive the join's own AC1.
  const u32 b2 = rt::game::coop_body(2);
  rt::game::coopnet::Transform t;
  if (!rt::game::coopnet::remote_transform(&t)) return;
  if (rt::game::coopnet::role_join() && b1 && t.apmax >= 1000.0f && t.ap >= 0.0f && t.ap <= t.apmax &&
      rt::vm::committed(b1 + kBodyAp, 4)) {
    static float last_ap = -1.0f;
    const float before = float(ppu::rf32(b1 + kBodyAp));
    ppu::wf32(b1 + kBodyAp, t.ap);
    if (last_ap < 0.0f || std::fabs(t.ap - last_ap) > 1.0f) {
      LOG_I("coop", "COOP AP: join AC1 0x%08x %.2f -> %.2f (host-authoritative, max %.2f)", b1, double(before),
            double(t.ap), double(t.apmax));
      last_ap = t.ap;
    }
  }
  if (rt::game::coopnet::role_join() && b1 && t.peer_pamax > 1.0f && std::isfinite(t.peer_pa) &&
      t.peer_pa >= 0.0f && t.peer_pa <= t.peer_pamax * 1.5f) {
    const u32 pa1 = body_pa_obj(b1);
    float local_cur = 0.0f, local_pamax = 0.0f;
    if (pa1 && read_body_pa(b1, &local_cur, &local_pamax)) {
      const float clamped = t.peer_pa > local_pamax ? local_pamax : t.peer_pa;
      ppu::wf32(pa1 + kPaObjCur, clamped);
    }
  }
  if (rt::game::coopnet::role_join() && b1) {
    if (t.peer_enmax > 10.0f && std::isfinite(t.peer_en)) write_body_en(b1, t.peer_en, t.peer_en_redline);
    write_body_hud_ammo(b1, t.peer_wpn.ammo);
    reconcile_body_weapons(1, b1, t.peer_wpn.active, t.peer_wpn.purged);
  }
  if (b2) reconcile_body_weapons(2, b2, t.self_wpn_active, t.self_wpn_purged);
  if (!b2 || !g_pos.any) return;
  if (!rt::vm::committed(b2 + 0x1c0, 16)) return;
  if (!pos_valid(t.position) || (t.position[0] * t.position[0] + t.position[1] * t.position[1] + t.position[2] * t.position[2] < 1.0f))
    return;
  // The partner's heading: the rows of the body's live 4x4 (+0x10) and of its copy at +0x190 (the
  // one heading_of() reads). Mouse turning never reaches the forwarded pad, so the rotation is carried
  // explicitly (research\28). Guarded by rot_orthonormal so a zero/degenerate matrix never poisons physics.
  if (rot_orthonormal(t.rotation)) {
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c) {
        wf32(b2 + 0x10 + 16 * r + 4 * c, t.rotation[r * 3 + c]);
        wf32(b2 + 0x190 + 16 * r + 4 * c, t.rotation[r * 3 + c]);
      }
  }
  if (!rt::vm::committed(b2 + g_pos.at[2], 4)) return;
  double cur[3];
  bool cur_ok = true;
  for (int i = 0; i < 3; ++i) {
    cur[i] = ppu::rf32(b2 + g_pos.at[i]);
    if (!std::isfinite(cur[i])) cur_ok = false;
  }
  // Extrapolate peer position by up to 150 ms of packet age using their velocity for smooth Internet play.
  double target_pos[3];
  const double lead = std::fmin(double(t.age_ms) * 0.001, 0.15);
  const bool vel_ok = std::isfinite(t.velocity[0]) && std::isfinite(t.velocity[1]) && std::isfinite(t.velocity[2]) &&
                      (double(t.velocity[0]) * t.velocity[0] + double(t.velocity[1]) * t.velocity[1] +
                       double(t.velocity[2]) * t.velocity[2]) < 1500.0 * 1500.0;
  for (int i = 0; i < 3; ++i) target_pos[i] = double(t.position[i]) + (vel_ok ? double(t.velocity[i]) * lead : 0.0);
  if (!cur_ok) {
    for (int i = 0; i < 3; ++i) {
      wf32(b2 + g_pos.at[i], float(target_pos[i]));
      wf32(b2 + 0x1c0 + 4 * i, float(target_pos[i]));
      wf32(b2 + 0xf0 + 4 * i, float(target_pos[i]));
    }
    return;
  }
  const double dx = target_pos[0] - cur[0], dy = target_pos[1] - cur[1], dz = target_pos[2] - cur[2];
  const double err = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!std::isfinite(err) || err <= kSlop) return;
  const double f = err > kSnap ? 1.0 : kGain;
  // Write both the live render transform (+0x40) AND the physics source copy (+0x1c0) that
  // xform_blit reads back every frame; on a snap also sync +0xf0 (previous position) to avoid a
  // bogus single-frame velocity spike in f_004b9eb8.
  for (int i = 0; i < 3; ++i) {
    const float np = float(cur[i] + f * (target_pos[i] - cur[i]));
    wf32(b2 + g_pos.at[i], np);
    wf32(b2 + 0x1c0 + 4 * i, np);
    if (f >= 1.0) wf32(b2 + 0xf0 + 4 * i, np);
  }
  static std::atomic<int> logged{0};
  static double last_snap_log = -1e9;
  const double now = rt::log_time();
  if (logged.fetch_add(1, std::memory_order_relaxed) < 30 || (err > kSnap && now - last_snap_log >= 1.0)) {
    if (err > kSnap) last_snap_log = now;
    LOG_I("coop", "COOP SYNC: AC2 %s %.2f units toward the peer's AC1 (was %.2f, now %.2f)",
          f >= 1.0 ? "snapped" : "moved 35% of", err, err, err * (1.0 - f));
  }
}

}  // namespace

namespace rt::game {

void coop_note_body(int which, u32 body) {
  if (which < 1 || which > 2 || !body) return;
  if (g_body[which].exchange(body, std::memory_order_relaxed) == body) return;
  LOG_I("coop", "COOP BODY#%d registered 0x%08x (entity id %u, vtable 0x%08x)", which, body, r32(body + kBodyId), r32(body));
}

void coop_body_reset() {
  for (int i = 1; i <= 2; ++i) {
    g_body[i].store(0, std::memory_order_relaxed);
    g_obj[i].store(0, std::memory_order_relaxed);
    g_wpn_db[i] = WpnDebounce{};
  }
  std::lock_guard lk(g_purge_mu);
  g_pending_purges.clear();
}

void coop_body_reassert_join_vitals(u32 b1) {
  if (!b1 || !rt::game::coopnet::role_join()) return;
  float en = 0.0f, enmax = 0.0f;
  bool red = false;
  if (rt::game::coopnet::peer_en(&en, &enmax, &red)) write_body_en(b1, en, red);
  rt::game::coopnet::WeaponSyncState w{};
  if (rt::game::coopnet::peer_weapons(&w)) write_body_hud_ammo(b1, w.ammo);
}

size_t coop_body_drain_purges(u32* out_slots, size_t max) {
  if (!out_slots || !max) return 0;
  std::lock_guard lk(g_purge_mu);
  const size_t n = g_pending_purges.size() < max ? g_pending_purges.size() : max;
  for (size_t i = 0; i < n; ++i) out_slots[i] = g_pending_purges[i];
  g_pending_purges.erase(g_pending_purges.begin(), g_pending_purges.begin() + std::ptrdiff_t(n));
  return n;
}

u32 coop_body(int which) { return (which >= 1 && which <= 2) ? g_body[which].load(std::memory_order_relaxed) : 0; }

void coop_note_obj(u32 body, u32 obj) {
  if (!body || !obj) return;
  for (int i = 1; i <= 2; ++i)
    if (g_body[i].load(std::memory_order_relaxed) == body) g_obj[i].store(obj, std::memory_order_relaxed);
}

void coop_body_frame() {
  if (!coop_on()) return;
  init_once();
  poke_frame();
  if (rt::game::coopnet::active()) {
    net_frame();
    rt::game::coopnet::report();
  }
  if (g_dump == DumpMode::Off) return;
  const double now = rt::log_time();
  if (now - g_last_dump < 1.0) return;
  g_last_dump = now;
  const bool deep = g_dump == DumpMode::Deep;
  for (int i = 1; i <= 2; ++i) {
    const u32 body = coop_body(i);
    if (!body) {
      LOG_I("coop", "COOP DUMP body%d: not created yet", i);
      continue;
    }
    dump_object(ac4::strprintf("body%d", i).c_str(), body, kDumpBytes, deep);
    const u32 obj = g_obj[i].load(std::memory_order_relaxed);
    if (obj) dump_object(ac4::strprintf("obj%d", i).c_str(), obj, 0x100, deep);
  }
}

bool coop_body_position(int which, double out[3]) {
  init_once();
  const u32 body = coop_body(which);
  if (!body || !g_pos.any || !rt::vm::committed(body + g_pos.at[2], 4)) return false;
  for (int i = 0; i < 3; ++i) out[i] = ppu::rf32(body + g_pos.at[i]);
  return true;
}

}  // namespace rt::game
