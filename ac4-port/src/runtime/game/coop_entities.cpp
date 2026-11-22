// Co-op: the mission entity table. M1 is a SEARCH, so this file starts as the instrument that finds the
// container (see coop_entities.h for the request-file commands) and then walks it.
//
// Measured facts this file relies on (research\24, research\21):
//   the AC body object: vtable 0x10142638 at +0, entity id (u32, player = 1000) at +4,
//                       a 4x4 world transform with axis rows at +0x10/+0x20/+0x30 and the position row
//                       at +0x40/+0x44/+0x48 (the block at +0x50.. is its inverse, not a copy).
//   the game's class vtables live in one data band: 0x1013f400, 0x10142638, 0x10146340, 0x10146710,
//                       0x101475c8, 0x10147998, 0x10147a50, 0x101486b0, 0x10148768 were all observed.
//
// Guest memory is BIG-endian (ppu_recomp.h r32 byte-swaps), so the raw scan byte-swaps too.
#include "runtime/game/coop_entities.h"
#include "runtime/game/coop_session.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ppu/ppu_recomp.h"
#include "runtime/game/coop_body.h"
#include "runtime/game/coop_net.h"
#include "runtime/game/coop_watch.h"
#include "runtime/log.h"
#include "runtime/memory.h"

namespace {

using ppu::r32;

struct Ent {
  u32 obj, id, vt;
  float x, y, z, hp, hpmax;
  bool alive;
};

// The band the game's class vtables were observed in (research\24, research\21, research\15).
constexpr u32 kVtLo = 0x10130000u;
constexpr u32 kVtHi = 0x10160000u;
constexpr u32 kMaxHits = 64;      // per scan request
constexpr u32 kMaxListed = 4000;  // per sweep
constexpr int kExamplesPerClass = 12;

std::atomic<bool> g_on{false};
std::atomic<bool> g_ents{false};
std::atomic<bool> g_nosnap{false};
std::atomic<bool> g_reset_req{false};
std::string g_req_path;
std::string g_meter_path;
std::mutex g_mu;
std::vector<std::pair<u32, bool>> g_watch;  // (address, is_float)
double g_last_req = -1e9;
double g_last_walk = -1e9;

u8* mem() { return ppu::mem(); }
inline bool in_vt(u32 v) { return v >= kVtLo && v < kVtHi; }
inline bool looks_pointer(u32 v) { return v >= 0x00010000u && v < 0x40000000u && (v & 3) == 0; }

void env_init() {
  static std::once_flag once;
  std::call_once(once, [] {
    char buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableA("AC4_COOP_REQ", buf, sizeof buf);
    if (n > 0 && n < sizeof buf) g_req_path = buf;
    n = GetEnvironmentVariableA("AC4_COOP_ENTS", buf, sizeof buf);
    if (n > 0 && buf[0] == '1') g_ents = true;
    char path[MAX_PATH]{};
    n = GetEnvironmentVariableA("AC4_COOP_METER", path, sizeof path);
    if (n > 0 && n < sizeof path) g_meter_path = path;
    char nosnap[8]{};
    n = GetEnvironmentVariableA("AC4_COOP_NOSNAP", nosnap, sizeof nosnap);
    if (n > 0 && n < sizeof nosnap) g_nosnap = nosnap[0] == '1';
    // The co-op link needs the walk too: the host builds the snapshots from the slot list, and the
    // join resolves a slot index through its own pool base (research\26). The write-watchpoint tool
    // (coop_watch.cpp) also needs the walk.
    char watch[16]{};
    n = GetEnvironmentVariableA("AC4_COOP_WATCH", watch, sizeof watch);
    g_on = !g_req_path.empty() || g_ents.load() || !g_meter_path.empty() || (n > 0 && n < sizeof watch) ||
           rt::game::coopnet::active();
    if (g_on)
      LOG_I("coop", "COOP ENT: instrument on (req %s, walk %s, meter %s, snapshots %s, apply %s)",
            g_req_path.empty() ? "off" : g_req_path.c_str(), g_ents.load() ? "on" : "off",
            g_meter_path.empty() ? "off" : g_meter_path.c_str(),
            rt::game::coopnet::active() ? (rt::game::coopnet::role_host() ? "host" : "join") : "off",
            g_nosnap.load() ? "OFF (AC4_COOP_NOSNAP=1)" : "on");
  });
}

// Every committed block of the areas a game object can live in.
template <typename Fn>
void for_each_block(Fn fn) {
  rt::vm::for_each_block(rt::vm::Area::Main, fn);
  rt::vm::for_each_block(rt::vm::Area::User64k, fn);
  rt::vm::for_each_block(rt::vm::Area::User1m, fn);
  rt::vm::for_each_block(rt::vm::Area::Stack, fn);
}

bool orthonormal(u32 a) {
  if (!rt::vm::committed(a, 0x40)) return false;
  double r[3][3];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      const double v = ppu::rf32(a + 16 * i + 4 * j);
      if (!std::isfinite(v) || std::fabs(v) > 1.001) return false;
      r[i][j] = v;
    }
  for (int i = 0; i < 3; ++i) {
    const double n = r[i][0] * r[i][0] + r[i][1] * r[i][1] + r[i][2] * r[i][2];
    if (std::fabs(n - 1.0) > 0.02) return false;
  }
  for (int i = 0; i < 3; ++i)
    for (int j = i + 1; j < 3; ++j) {
      const double d = r[i][0] * r[j][0] + r[i][1] * r[j][1] + r[i][2] * r[j][2];
      if (std::fabs(d) > 0.02) return false;
    }
  const double w = ppu::rf32(a + 0x3c);  // the position row's 4th element is 1.0
  return std::fabs(w - 1.0) < 0.01;
}

// The object that contains `hit`: the LOWEST address in [hit-0x100, hit] whose first word is a vtable.
void describe_owner(const char* what, u32 hit) {
  u32 base = 0;
  for (u32 b = (hit >= 0x100 ? hit - 0x100 : 0); b <= hit; b += 4)
    if (rt::vm::committed(b, 8) && in_vt(r32(b))) {
      base = b;
      break;
    }
  std::string ctx;
  for (u32 a = (hit >= 0x20 ? hit - 0x20 : 0); a <= hit + 0x20; a += 4)
    ctx += ac4::strprintf("%s0x%08x", a == hit ? " *" : " ", rt::vm::committed(a, 4) ? r32(a) : 0xeeeeeeeeu);
  if (base)
    LOG_I("coop", "COOP ENT %s hit 0x%08x owner 0x%08x (+0x%x) vt 0x%08x id %u |%s", what, hit, base, hit - base,
          r32(base), r32(base + 4), ctx.c_str());
  else
    LOG_I("coop", "COOP ENT %s hit 0x%08x owner ? |%s", what, hit, ctx.c_str());
}

void do_scan(u32 target, const char* what, u32 cap) {
  u32 hits = 0, total = 0;
  for_each_block([&](u32 begin, u32 size) {
    const u32 end = begin + size;
    for (u32 a = (begin + 3) & ~3u; a + 4 <= end; a += 4) {
      if (r32(a) != target) continue;
      ++total;
      if (hits >= cap) continue;
      ++hits;
      describe_owner(what, a);
    }
  });
  LOG_I("coop", "COOP ENT %s 0x%08x: %u hit(s) in all committed memory (%u logged)", what, target, total, hits);
}

// Every object whose first word is `vt` (no id test, no matrix test): the class listing.
void do_findvt(u32 vt) {
  u32 found = 0;
  std::string list;
  for_each_block([&](u32 begin, u32 size) {
    const u32 end = begin + size;
    for (u32 a = (begin + 3) & ~3u; a + 8 <= end; a += 4) {
      if (r32(a) != vt) continue;
      ++found;
      if (found > 400) continue;
      u32 moff = 0;
      for (u32 o : {0x10u, 0x50u, 0x90u, 0x100u, 0x190u})
        if (orthonormal(a + o)) {
          moff = o;
          break;
        }
      if (moff)
        list += ac4::strprintf("\n   obj 0x%08x id %u +8 0x%08x moff 0x%-3x pos %10.2f %10.2f %10.2f", a, r32(a + 4),
                               r32(a + 8), moff, ppu::rf32(a + moff + 0x30), ppu::rf32(a + moff + 0x34),
                               ppu::rf32(a + moff + 0x38));
      else
        list += ac4::strprintf("\n   obj 0x%08x id %u +8 0x%08x no matrix", a, r32(a + 4), r32(a + 8));
    }
  });
  LOG_I("coop", "COOP ENT findvt 0x%08x: %u object(s)%s", vt, found, list.empty() ? " (none)" : list.c_str());
}

void do_mem(u32 addr, u32 len) {
  if (len > 0x4000) len = 0x4000;
  if (!rt::vm::committed(addr, len)) {
    LOG_W("coop", "COOP ENT mem 0x%08x+0x%x is not committed (%s)", addr, len,
          rt::vm::describe(addr).c_str());
    return;
  }
  LOG_I("coop", "COOP ENT mem 0x%08x..0x%08x (%s)", addr, addr + len, rt::vm::describe(addr).c_str());
  for (u32 off = 0; off < len; off += 16) {
    std::string hex, fl;
    for (u32 i = 0; i < 16; i += 4) {
      hex += ac4::strprintf(" %08x", r32(addr + off + i));
      fl += ac4::strprintf(" %11.4g", ppu::rf32(addr + off + i));
    }
    LOG_I("coop", "COOP ENT +0x%04x %s |%s", off, hex.c_str() + 1, fl.c_str());
  }
}

// One pass for objects that look like a live entity: a class vtable at +0, a small id at +4, and an
// orthonormal 4x4 somewhere in the first 0x200 bytes (which is where the AC body keeps it).
void do_sweep(bool list_all) {
  struct Class {
    u32 vt;
    u32 n;
    int listed;
  };
  std::vector<Class> classes;
  u32 found = 0;
  std::string list;
  for_each_block([&](u32 begin, u32 size) {
    const u32 end = begin + size;
    for (u32 a = (begin + 3) & ~3u; a + 8 <= end; a += 4) {
      const u32 vt = r32(a);
      if (!in_vt(vt)) continue;
      const u32 id = r32(a + 4);
      if (id == 0 || id > 0x100000) continue;
      u32 moff = 0;
      for (u32 o : {0x10u, 0x50u, 0x90u, 0x100u, 0x190u})
        if (orthonormal(a + o)) {
          moff = o;
          break;
        }
      if (!moff) continue;
      ++found;
      Class* c = nullptr;
      for (auto& e : classes)
        if (e.vt == vt) c = &e;
      if (!c) {
        classes.push_back({vt, 0, 0});
        c = &classes.back();
      }
      ++c->n;
      if ((list_all || c->listed < kExamplesPerClass) && found <= kMaxListed) {
        ++c->listed;
        list += ac4::strprintf("\n   id %-6u obj 0x%08x vt 0x%08x moff 0x%-3x pos %10.2f %10.2f %10.2f", id, a, vt,
                               moff, ppu::rf32(a + moff + 0x30), ppu::rf32(a + moff + 0x34),
                               ppu::rf32(a + moff + 0x38));
      }
    }
  });
  LOG_I("coop", "COOP ENT sweep: %u candidate(s) in %zu class(es)", found, classes.size());
  for (const auto& c : classes)
    LOG_I("coop", "COOP ENT sweep class vt 0x%08x: %u entity object(s)", c.vt, c.n);
  LOG_I("coop", "COOP ENT sweep list:%s", list.empty() ? " (none)" : list.c_str());
}

void do_watch() {
  std::vector<std::pair<u32, bool>> w;
  {
    std::lock_guard lk(g_mu);
    w = g_watch;
  }
  if (w.empty()) return;
  std::string line;
  for (auto [addr, is_float] : w) {
    if (!rt::vm::committed(addr, 4)) {
      line += ac4::strprintf(" 0x%08x=n/a", addr);
      continue;
    }
    u32 v = r32(addr);
    line += is_float ? ac4::strprintf(" 0x%08x=%u(%.6g)", addr, v, ppu::rf32(addr))
                     : ac4::strprintf(" 0x%08x=%u(0x%x)", addr, v, v);
  }
  LOG_I("coop", "COOP ENT watch%s", line.c_str());
}

void handle_line(const std::string& line) {
  char cmd[32] = {}, a[64] = {}, b[64] = {};
  int n = std::sscanf(line.c_str(), "%31s %63s %63s", cmd, a, b);
  if (n < 1) return;
  const std::string c(cmd);
  if (c == "scan" && n >= 2) {
    do_scan(u32(std::strtoul(a, nullptr, 0)), "scan", n >= 3 ? u32(std::strtoul(b, nullptr, 0)) : kMaxHits);
  } else if (c == "scanf" && n >= 2) {  // the value is written as a decimal float, searched as its bits
    const float f = float(std::atof(a));
    do_scan(ppu::fbits(f), "scanf", n >= 3 ? u32(std::strtoul(b, nullptr, 0)) : kMaxHits);
  } else if (c == "findvt" && n >= 2) {
    do_findvt(u32(std::strtoul(a, nullptr, 0)));
  } else if (c == "mem" && n >= 3) {
    do_mem(u32(std::strtoul(a, nullptr, 0)), u32(std::strtoul(b, nullptr, 0)));
  } else if (c == "watch" && n >= 2) {
    const bool as_float = n >= 3 && (b[0] == 'f' || b[0] == 'F');
    std::lock_guard lk(g_mu);
    g_watch.push_back({u32(std::strtoul(a, nullptr, 0)), as_float});
  } else if (c == "sweep") {
    do_sweep(false);
  } else if (c == "sweepall") {
    do_sweep(true);
  } else if (c == "dumpbody") {  // both AC bodies, 0x200 bytes: the reference for the entity layout
    for (int i = 1; i <= 2; ++i) {
      const u32 body = rt::game::coop_body(i);
      if (!body) continue;
      LOG_I("coop", "COOP ENT body%d 0x%08x vt 0x%08x id %u", i, body, r32(body), r32(body + 4));
      do_mem(body, 0x200);
    }
  } else if (c == "clear") {
    std::lock_guard lk(g_mu);
    g_watch.clear();
  } else {
    LOG_W("coop", "COOP ENT: unknown request '%s'", line.c_str());
  }
}

void poll_requests() {
  if (g_req_path.empty()) return;
  const double now = rt::log_time();
  if (now - g_last_req < 1.0) return;
  FILE* f = std::fopen(g_req_path.c_str(), "r");
  if (!f) return;
  std::string text;
  char buf[512];
  while (std::fgets(buf, sizeof buf, f)) text += buf;
  std::fclose(f);
  if (text.empty()) return;
  g_last_req = now;
  std::fopen(g_req_path.c_str(), "w");  // spent: the driver writes the next request when it wants one
  LOG_I("coop", "COOP ENT: %zu byte request", text.size());
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find('\n', i);
    if (j == std::string::npos) j = text.size();
    std::string line = text.substr(i, j - i);
    i = j + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    handle_line(line);
  }
}

// ---------------------------------------------------------------------------------------------
// M2/M3: the entity walk. Measured (research\e10_raw\ent2_*.log, run 20261115-115551):
//   the player's AC body  0x12fd7ff0  vt 0x10142638  id 1000 at +4  matrix +0x10  position +0x40
//   an m010 unit          0x12f9f8e0  vt 0x10142638  id    2 at +4  matrix +0x10  position +0x40
//   AP (player, 35888)    body+0x2a4 = current, body+0x2a8 = maximum, both floats
//   a unit's HP           0x12ddf2c0+0x2a4 = 1.0 (max +0x2a8 = 1.0); a DEAD unit beside it read
//                         0x12dd8240+0x2a4 = 0.0 with its first axis row collapsed to (0,0,0)
// so one class covers both the player AC and the mission's units, hp is a float pair at +0x2a4/+0x2a8,
// and "alive" is hp > 0.
constexpr u32 kActorVt = 0x10142638;
// The ELF image (0x00010000-0x0FFFFFFF) holds one data word equal to the actor vtable — the false
// positive research\25 s2 counted (0x00ef499c). Every live entity and every AC body was measured at an
// address above 0x10000000 (the runtime-owned half of main memory), so the walk ignores below that.
constexpr u32 kEntAddrFloor = 0x10000000;
constexpr u32 kEntId = 4;
constexpr u32 kEntPos = 0x40;
constexpr u32 kEntHp = 0x2a4;
constexpr u32 kEntHpMax = 0x2a8;

u32 g_pool_lo = 0, g_pool_hi = 0;  // the pool's address window, learned by one full scan
double g_last_rescan = -1e9;

// --- M1: the stable key ---------------------------------------------------------------------------
// research\25 called the pool's spacing 0x7080, but a measurement of a live mission shows it is only
// the AVERAGE: the objects are heap allocations, and the gaps are 0x7080, 0x7170, 0x150e0 ... (M1
// measured this in the meter: 319 objects, and no residue modulo 0x7080 holds more than 16 of them).
// So the key is not a stride index. What the two instances DO share is the object graph itself: both
// allocate the same objects at the same addresses (research\24 M3 saw both log body2 0x10d9c100).
// The key is therefore the ENTITY ADDRESS, and its rank in the address-ordered list is carried along
// so a join whose layout ever differed could still map by order.
constexpr u32 kEntRot = 0x10;  // axis rows at +0x10 / +0x20 / +0x30, 16 bytes each

bool g_pool_dumped = false;
std::vector<u32> g_ent_addr;        // all occupied entities, ascending
std::vector<u32> g_nonplayer_addr;  // non-player entities only, ascending (for heap-shift-safe ordinal matching)
std::unordered_map<u32, u32> g_id_unique;  // entity_id -> addr when unique (0 = ambiguous/shared id)
std::unordered_set<u32> g_known_set;       // fast O(1) membership check for instant reinforcement discovery
std::mutex g_live_mu;
std::vector<u32> g_pending_live;           // newly seen actor addresses from hook_actor_update/hook_xform_blit
u32 g_ent_lowest = 0;         // diagnostic: the lowest entity address of the last walk

void take_entity(std::vector<Ent>& out, u32 a) {
  Ent e{};
  e.obj = a;
  e.vt = r32(a);
  e.id = r32(a + kEntId);
  e.x = float(ppu::rf32(a + kEntPos));
  e.y = float(ppu::rf32(a + kEntPos + 4));
  e.z = float(ppu::rf32(a + kEntPos + 8));
  e.hp = float(ppu::rf32(a + kEntHp));
  e.hpmax = float(ppu::rf32(a + kEntHpMax));
  e.alive = std::isfinite(e.hp) && e.hp > 0.0f;
  out.push_back(e);
}

// `full` scans every committed block; otherwise only the window learned by the previous full scan,
// which is the entity pool (its objects sit 0x7080 bytes apart, so the window is ~6 MB and cheap).
void collect_entities(std::vector<Ent>& out, bool full) {
  out.clear();
  if (!full && g_pool_lo && g_pool_hi > g_pool_lo && rt::vm::committed(g_pool_lo, g_pool_hi - g_pool_lo)) {
    for (u32 a = (g_pool_lo + 3) & ~3u; a + kEntHpMax + 4 <= g_pool_hi; a += 4)
      if (a >= kEntAddrFloor && r32(a) == kActorVt) take_entity(out, a);
    if (!out.empty()) return;
    out.clear();
  }
  u32 lo = 0, hi = 0;
  for_each_block([&](u32 begin, u32 size) {
    const u32 end = begin + size;
    for (u32 a = (begin + 3) & ~3u; a + kEntHpMax + 4 <= end; a += 4) {
      if (a < kEntAddrFloor || r32(a) != kActorVt) continue;
      take_entity(out, a);
      if (lo == 0 || a < lo) lo = a;
      if (a + kEntHpMax + 4 > hi) hi = a + kEntHpMax + 4;
    }
  });
  if (lo) {
    g_pool_lo = lo;
    g_pool_hi = hi;
  }
}

// Once, when the mission is populated: the whole occupancy/order table, so two instances can be
// compared line by line (M1: is the entity set the same, in the same order, with the same spawn
// position?).
void pool_dump_once(const std::vector<Ent>& ents, double now) {
  g_pool_dumped = true;
  LOG_I("coop", "COOP POOL t=%.1f n=%zu lowest 0x%08x (address order)", now, ents.size(), ents.front().obj);
  u32 shown = 0;
  for (const auto& e : ents) {
    if (shown >= 400) break;
    LOG_I("coop", "COOP POOL ord %u addr 0x%08x id %u pos %.3f %.3f %.3f hp %.4f", shown, e.obj, e.id, e.x, e.y,
          e.z, e.hp);
    ++shown;
  }
}

void entity_walk() {
  std::vector<Ent> ents;
  const double now = rt::log_time();
  const bool full = (now - g_last_rescan > 20.0) || !g_pool_lo;
  if (full) g_last_rescan = now;
  collect_entities(ents, full);
  std::sort(ents.begin(), ents.end(), [](const Ent& a, const Ent& b) { return a.obj < b.obj; });
  g_ent_addr.clear();
  g_nonplayer_addr.clear();
  g_id_unique.clear();
  {
    std::lock_guard lk(g_live_mu);
    g_known_set.clear();
    for (const auto& e : ents) g_known_set.insert(e.obj);
  }
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  for (const auto& e : ents) {
    g_ent_addr.push_back(e.obj);
    if (e.obj != b1 && e.obj != b2 && e.id != 1000u && e.id != 1001u) {
      g_nonplayer_addr.push_back(e.obj);
      if (e.id != 0) {
        auto [it, inserted] = g_id_unique.emplace(e.id, e.obj);
        if (!inserted) it->second = 0;  // shared id across multiple actors: fall back to ordinal
      }
    }
  }
  g_ent_lowest = ents.empty() ? 0 : ents.front().obj;
  u32 alive = 0;
  for (const auto& e : ents)
    if (e.alive) ++alive;
  if (g_ents.load())
    LOG_I("coop", "COOP ENT t=%.1f objects=%zu alive=%u lowest 0x%08x (pool 0x%08x..0x%08x)", now, ents.size(),
          alive, g_ent_lowest, g_pool_lo, g_pool_hi);
  if (!g_pool_dumped && ents.size() >= 200) pool_dump_once(ents, now);
  if (g_meter_path.empty()) return;
  FILE* f = std::fopen(g_meter_path.c_str(), "a");
  if (!f) return;
  std::fprintf(f, "# t=%.1f n=%zu lowest=0x%08x\n", now, ents.size(), g_ent_lowest);
  u32 ord = 0;
  for (const auto& e : ents) {
    std::fprintf(f, "%.1f 0x%08x %u 0x%x %.3f %.3f %.3f %.4f %d %u\n", now, e.obj, e.id, e.vt, e.x, e.y, e.z, e.hp,
                 e.alive ? 1 : 0, ord);
    ++ord;
  }
  std::fclose(f);
}

// ---------------------------------------------------------------------------------------------
// M2: entity snapshots, host -> join. The host sends every occupied slot (minus the two player
// bodies) at ~10 Hz; the join writes each record into the same slot of its own pool: the rotation
// rows (+0x10/+0x20/+0x30), the position (+0x40/+0x44/+0x48) and hp (+0x2a4). No smoothing in v0.
double g_last_pub = -1e9, g_last_snap_log = -1e9;
uint64_t g_apply_total = 0, g_skip_total = 0, g_apply_sec = 0, g_skip_sec = 0;
uint64_t g_sent_sec = 0, g_pkts_sec = 0;
double g_maxerr_sec = 0, g_err_sum_sec = 0;
uint64_t g_err_n_sec = 0;
u32 g_worst_addr_sec = 0;
uint32_t g_seen_counter = 0;

// Continuous application (research\27). The snapshot is not written once when it arrives and then
// left to the local simulation: the join re-writes every entity's transform EVERY frame, between
// the two host snapshots interpolating from the previous one. That keeps the entity where the host
// put it even though the join's own AI keeps running underneath (the reason research\26's write
// "snapped back": the write was durable, but a 5-7 Hz stream could not hold a per-frame simulation).
struct Target {
  float prev_pos[3] = {};
  float cur_pos[3] = {};
  float cur_rot[9] = {};
  float hp = 0.0f;
  uint16_t broken_parts = 0;
  uint16_t queued_parts = 0;
  double t_prev = 0.0, t_cur = 0.0;
  bool have = false, have_prev = false;
  bool death_queued = false;
  double death_t = -1e9;
};
std::unordered_map<u32, Target> g_targets;
std::mutex g_kill_mu;
std::vector<u32> g_pending_kills;
std::vector<rt::game::PartBreakJob> g_pending_parts;
uint64_t g_kills_queued_total = 0;
std::mutex g_dead_mu;
std::unordered_set<u32> g_host_dead;
uint64_t g_host_dead_logs = 0;

// Reads entity+672+12 sub-part durability vector (f_00b76ecc: begin at +0x2b0, end at +0x2b4,
// 8 bytes per part: { float cur_hp, float max_hp }). Returns bitmask of parts with cur_hp <= 0.
uint16_t read_broken_parts(u32 addr) {
  if (!rt::vm::committed(addr + 0x2b8, 4)) return 0;
  const u32 beg = r32(addr + 0x2b0);
  const u32 end = r32(addr + 0x2b4);
  if (beg < 0x10000000u || end <= beg || ((end - beg) & 7u) != 0) return 0;
  const u32 cnt = (end - beg) >> 3;
  if (cnt == 0 || cnt > 16 || !rt::vm::committed(beg, end - beg)) return 0;
  uint16_t mask = 0;
  for (u32 p = 0; p < cnt; ++p) {
    const float cur = float(ppu::rf32(beg + p * 8u));
    const float maxv = float(ppu::rf32(beg + p * 8u + 4u));
    if (std::isfinite(maxv) && maxv > 0.0f && std::isfinite(cur) && cur <= 0.0f) mask |= uint16_t(1u << p);
  }
  return mask;
}

struct SentState {
  float pos[3] = {};
  float rot[9] = {};
  float hp = -1.0f;
  uint16_t broken_parts = 0;
  uint8_t dead_sends = 0;
  double last_t = -1e9;
};
std::unordered_map<u32, SentState> g_sent;

void drain_pending_live() {
  std::vector<u32> fresh;
  {
    std::lock_guard lk(g_live_mu);
    if (g_pending_live.empty()) return;
    fresh.swap(g_pending_live);
  }
  bool added = false;
  for (u32 a : fresh) {
    if (a < kEntAddrFloor || !rt::vm::committed(a + kEntHpMax + 4, 4) || r32(a) != kActorVt) continue;
    auto it = std::lower_bound(g_ent_addr.begin(), g_ent_addr.end(), a);
    if (it != g_ent_addr.end() && *it == a) continue;
    g_ent_addr.insert(it, a);
    if (g_pool_lo == 0 || a < g_pool_lo) g_pool_lo = a;
    if (a + kEntHpMax + 4 > g_pool_hi) g_pool_hi = a + kEntHpMax + 4;
    added = true;
  }
  if (!added) return;
  g_nonplayer_addr.clear();
  g_id_unique.clear();
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  for (u32 a : g_ent_addr) {
    if (!rt::vm::committed(a + kEntHpMax + 4, 4) || r32(a) != kActorVt) continue;
    const u32 eid = r32(a + kEntId);
    if (a != b1 && a != b2 && eid != 1000u && eid != 1001u) {
      g_nonplayer_addr.push_back(a);
      if (eid != 0) {
        auto [it, ins] = g_id_unique.emplace(eid, a);
        if (!ins) it->second = 0;
      }
    }
  }
  g_ent_lowest = g_ent_addr.empty() ? 0 : g_ent_addr.front();
}

void snapshot_publish() {
  if (!rt::game::coopnet::role_host() || g_ent_addr.empty()) return;
  const double now = rt::log_time();
  if (now - g_last_pub < 0.1) return;  // ~10 Hz
  g_last_pub = now;
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  std::vector<rt::game::coopnet::SnapshotRecord> recs;
  recs.reserve(g_ent_addr.size());
  u16 ord = 0;
  for (u32 a : g_ent_addr) {
    if (!rt::vm::committed(a + kEntHpMax + 4, 4) || r32(a) != kActorVt) continue;
    const u32 eid = r32(a + kEntId);
    if (a == b1 || a == b2 || eid == 1000u || eid == 1001u) continue;  // players belong to type-2 path
    const u16 this_ord = ord++;
    rt::game::coopnet::SnapshotRecord r{};
    r.addr = a;
    r.entity_id = eid;
    r.ord = this_ord;
    for (int i = 0; i < 3; ++i) r.position[i] = float(ppu::rf32(a + kEntPos + 4 * i));
    for (int row = 0; row < 3; ++row)
      for (int c = 0; c < 3; ++c) r.rotation[row * 3 + c] = float(ppu::rf32(a + kEntRot + 16 * row + 4 * c));
    r.hp = float(ppu::rf32(a + kEntHp));
    {
      std::lock_guard lk(g_dead_mu);
      if (g_host_dead.count(a) != 0) r.hp = 0.0f;
    }
    r.broken_parts = read_broken_parts(a);

    // Bandwidth culling: send immediately when moving, rotating, damaged, sub-part broken, or newly dead (5x);
    // stationary / long-dead entities refresh at ~1 Hz heartbeat (~70-90% UDP savings).
    const bool dead = std::isfinite(r.hp) && r.hp <= 0.0f;
    auto it = g_sent.find(a);
    bool send_now = (it == g_sent.end());
    if (!send_now) {
      const SentState& s = it->second;
      if (dead) {
        send_now = (s.dead_sends < 5) || (r.broken_parts != s.broken_parts) || (now - s.last_t >= 1.0);
      } else {
        const float dx = r.position[0] - s.pos[0], dy = r.position[1] - s.pos[1], dz = r.position[2] - s.pos[2];
        float dr = 0.0f;
        for (int k = 0; k < 9; ++k) dr = std::max(dr, std::fabs(r.rotation[k] - s.rot[k]));
        const bool moved = (dx * dx + dy * dy + dz * dz) > (0.05f * 0.05f);
        const bool turned = dr > 0.005f;
        const bool hp_chg = std::fabs(r.hp - s.hp) > 0.01f;
        const bool part_chg = (r.broken_parts != s.broken_parts);
        send_now = moved || turned || hp_chg || part_chg || (now - s.last_t >= 1.0);
      }
    }
    if (!send_now) continue;
    SentState& s = g_sent[a];
    std::memcpy(s.pos, r.position, sizeof s.pos);
    std::memcpy(s.rot, r.rotation, sizeof s.rot);
    s.hp = r.hp;
    s.broken_parts = r.broken_parts;
    s.dead_sends = dead ? (s.dead_sends < 250 ? uint8_t(s.dead_sends + 1) : 250) : 0;
    s.last_t = now;
    recs.push_back(r);
  }
  const size_t pk = recs.empty() ? 0 : rt::game::coopnet::send_snapshot(recs.data(), recs.size(), g_ent_lowest);
  g_sent_sec += recs.size();
  g_pkts_sec += pk;
  if (now - g_last_snap_log < 1.0) return;
  g_last_snap_log = now;
  LOG_I("coop", "COOP SNAP host t=%.1f sent %llu records in %llu datagrams (%zu/%u active entity(s), lowest 0x%08x)",
        now, (unsigned long long)g_sent_sec, (unsigned long long)g_pkts_sec, recs.size(), unsigned(ord), g_ent_lowest);
  g_sent_sec = g_pkts_sec = 0;
}

// Records received this frame go into the interpolation table; nothing is written here.
void snapshot_ingest() {
  if (!rt::game::coopnet::role_join()) return;
  rt::game::coopnet::SnapshotRecord recs[512];
  uint32_t ctr = 0;
  const size_t n = rt::game::coopnet::take_snapshot(recs, 512, &ctr);
  if (n) g_seen_counter = ctr;
  if (!n) return;
  if (g_nosnap.load()) return;
  const double now = rt::log_time();
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  for (size_t i = 0; i < n; ++i) {
    // Heap-shift-safe resolution (when host & join equip different AC parts, pool addresses can shift):
    //   1) Fast path: exact guest address AND matching entity_id (not a player body).
    //   2) Unique entity_id match in join's non-player pool.
    //   3) Non-player ordinal fallback (excludes player bodies so player allocation order never shifts it).
    u32 addr = 0;
    const u32 cand = recs[i].addr;
    if (cand && cand != b1 && cand != b2 && rt::vm::committed(cand + kEntHpMax + 4, 4) &&
        r32(cand) == kActorVt && r32(cand + kEntId) == recs[i].entity_id) {
      addr = cand;
    }
    if (!addr && recs[i].entity_id != 0) {
      auto it = g_id_unique.find(recs[i].entity_id);
      if (it != g_id_unique.end() && it->second != 0) addr = it->second;
    }
    if (!addr && recs[i].ord < g_nonplayer_addr.size()) {
      addr = g_nonplayer_addr[recs[i].ord];
    }
    if (!addr || addr == b1 || addr == b2 || !rt::vm::committed(addr + kEntHpMax + 4, 4) || r32(addr) != kActorVt) {
      ++g_skip_sec;
      ++g_skip_total;
      continue;
    }
    Target& t = g_targets[addr];
    if (t.have) {
      std::memcpy(t.prev_pos, t.cur_pos, sizeof t.prev_pos);
      t.t_prev = t.t_cur;
      t.have_prev = true;
    }
    std::memcpy(t.cur_pos, recs[i].position, sizeof t.cur_pos);
    std::memcpy(t.cur_rot, recs[i].rotation, sizeof t.cur_rot);
    t.hp = recs[i].hp;
    t.broken_parts = recs[i].broken_parts;
    t.t_cur = now;
    t.have = true;
    ++g_apply_sec;
    ++g_apply_total;
  }
}

// Every frame, after the join's own simulation and before the frame is presented: hold each entity
// at the host's interpolated transform. This is the fix for "the write snaps back".
void snapshot_apply() {
  if (!rt::game::coopnet::role_join() || g_targets.empty() || g_nosnap.load()) return;
  if (!rt::game::coopsession::linked_in_mission()) return;
  const double now = rt::log_time();
  for (auto& kv : g_targets) {
    const u32 addr = kv.first;
    Target& t = kv.second;
    if (!t.have || !rt::vm::committed(addr + kEntHpMax + 4, 4) || r32(addr) != kActorVt) continue;
    double alpha = 1.0;
    if (t.have_prev) {
      const double dt = t.t_cur - t.t_prev;
      if (dt > 1e-4) alpha = (now - t.t_cur) / dt;
      if (alpha < 0.0) alpha = 0.0;
      if (alpha > 1.25) alpha = 1.25;  // one host snapshot of extrapolation at most
    }
    float pos[3];
    for (int k = 0; k < 3; ++k)
      pos[k] = t.have_prev ? float(t.prev_pos[k] + alpha * (t.cur_pos[k] - t.prev_pos[k])) : t.cur_pos[k];

    const double dx = double(pos[0]) - ppu::rf32(addr + kEntPos);
    const double dy = double(pos[1]) - ppu::rf32(addr + kEntPos + 4);
    const double dz = double(pos[2]) - ppu::rf32(addr + kEntPos + 8);
    const double err = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (std::isfinite(err)) {
      g_err_sum_sec += err;
      ++g_err_n_sec;
      if (err > g_maxerr_sec) {
        g_maxerr_sec = err;
        g_worst_addr_sec = addr;
      }
    }

    // Write both live (+0x10/+0x40) and controller-source (+0x190/+0x1c0) transforms so active AI
    // on the join aims/fires from the host's exact position. When host reports hp <= 0, queue a
    // native kill (f_00a50cb8 on the PPU thread) instead of silently zeroing HP & collapsing the
    // rotation matrix on frame 0 — that triggers AC4's full explosion VFX, debris, and SFX.
    const bool dead = std::isfinite(t.hp) && t.hp <= 0.0f;
    const bool have_copy4 = rt::vm::committed(addr + 0x190, 0x40);
    if (dead) {
      const float local_hp = float(ppu::rf32(addr + kEntHp));
      if (!t.death_queued) {
        t.death_queued = true;
        t.death_t = now;
        if (std::isfinite(local_hp) && local_hp > 0.0f) {
          std::lock_guard lk(g_kill_mu);
          g_pending_kills.push_back(addr);
          ++g_kills_queued_total;
          continue;  // leave live transform intact 1 frame so explosion spawns at exact world position
        }
      }
      if (!std::isfinite(local_hp) || local_hp <= 0.0f) {
        continue;  // native death/explosion/wreck animation is running cleanly; don't stomp it
      }
      if (now - t.death_t < 1.5) continue;  // waiting for PPU thread to run f_00a50cb8
      // Safety fallback only if an immortal/un-killable actor still has local_hp > 0 after 1.5 s:
      for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 3; ++c) {
          ppu::wf32(addr + kEntRot + 16 * row + 4 * c, 0.0f);
          if (have_copy4) ppu::wf32(addr + 0x190 + 16 * row + 4 * c, 0.0f);
        }
      ppu::wf32(addr + kEntHp, 0.0f);
      continue;
    }
    t.death_queued = false;
    for (int row = 0; row < 3; ++row)
      for (int c = 0; c < 3; ++c) {
        const float v = t.cur_rot[row * 3 + c];
        ppu::wf32(addr + kEntRot + 16 * row + 4 * c, v);
        if (have_copy4) ppu::wf32(addr + 0x190 + 16 * row + 4 * c, v);
      }
    for (int k = 0; k < 3; ++k) {
      ppu::wf32(addr + kEntPos + 4 * k, pos[k]);
      if (have_copy4) ppu::wf32(addr + 0x1c0 + 4 * k, pos[k]);
    }
    ppu::wf32(addr + kEntHp, t.hp);
    // Multi-part boss/ship sub-part destruction sync (entity+0x2b0 durability array):
    // when Host breaks sub-part p and Joiner's part p is still intact, queue native f_00a5091c.
    if (t.broken_parts) {
      const uint16_t local_broken = read_broken_parts(addr);
      const uint16_t missing = uint16_t(t.broken_parts & ~local_broken & ~t.queued_parts);
      if (missing) {
        std::lock_guard lk(g_kill_mu);
        for (int p = 0; p < 16; ++p) {
          if (missing & (1u << p)) {
            t.queued_parts |= uint16_t(1u << p);
            g_pending_parts.push_back({addr, s32(p), t.hp});
          }
        }
      }
    }
  }
  if (now - g_last_snap_log < 1.0) return;
  g_last_snap_log = now;
  const double mean_err = g_err_n_sec ? g_err_sum_sec / double(g_err_n_sec) : 0.0;
  LOG_I("coop", "COOP SNAP join t=%.1f applied %llu rec (total %llu), skipped %llu, counter %u, host lowest 0x%08x vs local 0x%08x, pre-apply error mean %.2f max %.2f at 0x%08x, %zu live target(s), kills_q=%llu%s",
        now, (unsigned long long)g_apply_sec, (unsigned long long)g_apply_total, (unsigned long long)g_skip_sec,
        g_seen_counter, rt::game::coopnet::snapshot_tag(), g_ent_lowest, mean_err, g_maxerr_sec, g_worst_addr_sec,
        g_targets.size(), (unsigned long long)g_kills_queued_total,
        g_nosnap.load() ? " [AC4_COOP_NOSNAP=1: received, NOT applied]" : "");
  g_apply_sec = g_skip_sec = 0;
  g_maxerr_sec = 0;
  g_err_sum_sec = 0;
  g_err_n_sec = 0;
  g_worst_addr_sec = 0;
}

}  // namespace

namespace rt::game {

bool coop_entities_on() {
  env_init();
  return g_on.load();
}

size_t coop_entity_addrs(u32* out, size_t max) {
  if (!out || !max) return 0;
  const size_t n = g_ent_addr.size() < max ? g_ent_addr.size() : max;
  for (size_t i = 0; i < n; ++i) out[i] = g_ent_addr[i];
  return n;
}

void coop_entities_reset() { g_reset_req = true; }  // done on the frame thread (coop_entities_frame)

void do_reset() {
  g_targets.clear();
  g_sent.clear();
  g_ent_addr.clear();
  g_nonplayer_addr.clear();
  g_id_unique.clear();
  {
    std::lock_guard lk(g_live_mu);
    g_known_set.clear();
    g_pending_live.clear();
  }
  {
    std::lock_guard lk(g_kill_mu);
    g_pending_kills.clear();
    g_pending_parts.clear();
  }
  {
    std::lock_guard lk(g_dead_mu);
    g_host_dead.clear();
    g_host_dead_logs = 0;
  }
  g_kills_queued_total = 0;
  g_pool_lo = g_pool_hi = 0;
  g_pool_dumped = false;
  g_last_rescan = -1e9;
  g_seen_counter = 0;
  LOG_I("coop", "COOP ENT: reset for a new sortie");
}

void coop_entities_note_live(u32 addr) {
  if (addr < kEntAddrFloor || !rt::vm::committed(addr + kEntHpMax + 4, 4)) return;
  if (r32(addr) != kActorVt) return;
  std::lock_guard lk(g_live_mu);
  if (g_known_set.insert(addr).second) g_pending_live.push_back(addr);
}

void coop_entities_note_dead(u32 addr) {
  if (addr < kEntAddrFloor || !rt::vm::committed(addr + kEntHpMax + 4, 4)) return;
  if (r32(addr) != kActorVt) return;
  const u32 b1 = rt::game::coop_body(1), b2 = rt::game::coop_body(2);
  const u32 eid = r32(addr + kEntId);
  if (addr == b1 || addr == b2 || eid == 1000u || eid == 1001u) return;
  std::lock_guard lk(g_dead_mu);
  if (g_host_dead.insert(addr).second && g_host_dead_logs < 30) {
    ++g_host_dead_logs;
    LOG_I("coop", "COOP ENT: host death event 0x%08x (id=%u, hp=%.1f)", addr, eid, double(ppu::rf32(addr + kEntHp)));
  }
}

size_t coop_entities_drain_kills(u32* out, size_t max) {
  if (!out || !max) return 0;
  std::lock_guard lk(g_kill_mu);
  const size_t n = g_pending_kills.size() < max ? g_pending_kills.size() : max;
  for (size_t i = 0; i < n; ++i) out[i] = g_pending_kills[i];
  g_pending_kills.erase(g_pending_kills.begin(), g_pending_kills.begin() + std::ptrdiff_t(n));
  return n;
}

size_t coop_entities_drain_part_breaks(PartBreakJob* out, size_t max) {
  if (!out || !max) return 0;
  std::lock_guard lk(g_kill_mu);
  const size_t n = g_pending_parts.size() < max ? g_pending_parts.size() : max;
  for (size_t i = 0; i < n; ++i) out[i] = g_pending_parts[i];
  g_pending_parts.erase(g_pending_parts.begin(), g_pending_parts.begin() + std::ptrdiff_t(n));
  return n;
}

void coop_entities_frame() {
  if (!coop_entities_on()) return;
  if (g_reset_req.exchange(false)) do_reset();
  drain_pending_live();  // 1-frame discovery for newly spawned reinforcements/bosses
  poll_requests();
  snapshot_ingest();   // join: update the interpolation table from anything that arrived
  snapshot_apply();    // join: re-assert every entity's host transform this frame
  snapshot_publish();  // host: ~10 Hz, delta/idle-culled from the live entity list
  const double now = rt::log_time();
  if (now - g_last_walk < 1.0) return;
  g_last_walk = now;
  do_watch();
  entity_walk();
  watch_frame();
}

}  // namespace rt::game
