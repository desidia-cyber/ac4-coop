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
constexpr u32 kBodyAp = 0x2a4;   // current AP/HP (measured, research\26 s1)
constexpr u32 kBodyApMax = 0x2a8;
constexpr u32 kBodyPa = 0x2d4;     // current Primal Armor (f_00a53998 / f_00a569f0: body+724)
constexpr u32 kBodyPaMax = 0x334;  // max Primal Armor (f_00a53998 / f_00a569f0: body+820)

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

  // Publish this instance's OWN AC1 AP & PA (for Partner HUD on the peer).
  if (b1 && rt::vm::committed(b1 + kBodyApMax, 4)) {
    const float ap1 = float(ppu::rf32(b1 + kBodyAp));
    const float apm1 = float(ppu::rf32(b1 + kBodyApMax));
    float pa1 = 0.0f, pam1 = 0.0f;
    if (rt::vm::committed(b1 + kBodyPaMax, 4)) {
      pa1 = float(ppu::rf32(b1 + kBodyPa));
      pam1 = float(ppu::rf32(b1 + kBodyPaMax));
    }
    rt::game::coopnet::set_local_self_vitals(ap1, apm1, pa1, pam1);
  }

  // The AP & PA of the AC this instance simulates FOR the peer (AC2). On the host that is the join
  // player, so the host's damage & Primal Armor model is authoritative for the join's own AC1.
  if (b2_now && rt::vm::committed(b2_now + kBodyApMax, 4)) {
    rt::game::coopnet::set_local_peer_ap(float(ppu::rf32(b2_now + kBodyAp)), float(ppu::rf32(b2_now + kBodyApMax)));
    if (rt::vm::committed(b2_now + kBodyPaMax, 4))
      rt::game::coopnet::set_local_peer_pa(float(ppu::rf32(b2_now + kBodyPa)), float(ppu::rf32(b2_now + kBodyPaMax)));
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
  if (rt::game::coopnet::role_join() && b1 && t.peer_pamax > 100.0f && std::isfinite(t.peer_pa) &&
      t.peer_pa >= 0.0f && t.peer_pa <= t.peer_pamax * 1.5f && rt::vm::committed(b1 + kBodyPaMax, 4)) {
    const float local_pamax = float(ppu::rf32(b1 + kBodyPaMax));
    if (local_pamax > 100.0f) {
      const float clamped = t.peer_pa > local_pamax ? local_pamax : t.peer_pa;
      ppu::wf32(b1 + kBodyPa, clamped);
    }
  }
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
  }
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
