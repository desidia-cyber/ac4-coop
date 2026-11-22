// Experiment E7/E8 (research): make a campaign mission build a SECOND player AC whose pad operator
// reads gamepad port 1, registered in the FIRST player's container.
//
// Where this hooks (all guest addresses in BLUS30027; facts in research\18_PLAYER_CREATE_TRACE.md,
// 19_P2_IN_MISSION.md and 20_VS_VIEW_OBJECT.md):
//   Ac4SortieScene::Init f_00740838 (called from MissionSortieScene::Init with lr 0x007ce638):
//     r3 = the scene, r2 = the scene's TOC. It creates ONE builder, gives it the scene's design
//     data and runs its Build; the Build creates the AC body and, through a virtual call that
//     returns to 0x00b0d6ac, f_00b0b8f0 -> f_00b5193c -> f_00b5129c, the pad operator with port 0.
//   The Build itself f_00b0d354 is reached through the TOC thunk 0x00737d48, so its own r2 is the
//   builder module's TOC (scene TOC + 0x1a44c); the scene TOC is only visible in the caller.
//
// E7 ran the whole Build a second time with a second builder and got a second body and a port-1
// operator, but the scene never touched them, because the Build's first step is to CREATE its own
// container object (builder vtable+32 -> opd 0x00ea0318 -> code 0x00b0b77c, result at builder+64)
// and the second build made a second one. Versus registers BOTH players' bodies in ONE container
// (f_0080f09c: f_00b12634(r3=r32(scene+28), r4=body) per player), so E8 keeps the second build but
// makes its first step return builder1's container:
//
//   1) hook the scene Init to remember (scene, scene TOC);
//   2) hook the Build (lr 0x00740a88) and, after the game's own build succeeded, run the whole
//      creation a second time with a second builder;
//   3) while that second build runs, the container creator (0x00b0b77c) is skipped and returns
//      r32(builder1+64), the operator constructor (0x00b5129c, input_hooks.cpp) takes pad port 1,
//      and the post-build steps listed in AC4_COOP_SKIP may be skipped.
// Everything is gated by AC4_COOP=1.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "ppu/ppu_recomp.h"
#include "runtime/game/coop_body.h"
#include "runtime/game/coop_build.h"
#include "runtime/game/coop_entities.h"
#include "runtime/game/coop_net.h"
#include "runtime/game/coop_session.h"
#include "runtime/log.h"
#include "runtime/memory.h"

// The builder's allocator and its constructor, as Ac4SortieScene::Init calls them.
void f_008fe96c(ppu::Ctx&);  // r3 = 240 (size), r4 = r32(scene toc - 32112), r5 = 103
void f_00737b68(ppu::Ctx&);  // r3 = builder, r4 = scene + 8000
// Partner AC resource loading, as Ac4SortieScene::LoadThread / VsSortieScene call them (scene TOC).
void f_008fdc24(ppu::Ctx&);  // r3 = 0 -> main heap
void f_008fe1a0(ppu::Ctx&);  // (heap, 7716, owner, 349) -> AC resource loader
void f_00739f88(ppu::Ctx&);  // loader ctor (f_006e5a84)
void f_0073bb48(ppu::Ctx&);  // load AC parts/models from 7660-byte design (f_006e850c)
void f_00737ab8(ppu::Ctx&);  // attach loader to scene+7980 under name "ac_1" (f_00b40a20)
void f_0073c528(ppu::Ctx&);  // register design ptr in scene+7980 slot table (f_00b3fe50)

namespace {

using ppu::r32;
using ppu::r64;
using ppu::w32;
using ppu::w64;

constexpr u32 kSceneInitReturn = 0x007ce638u;  // MissionSortieScene::Init -> Ac4SortieScene::Init
constexpr u32 kAllocReturn = 0x007408e8u;      // Ac4SortieScene::Init -> the builder allocator
constexpr u32 kBuilderCtorReturn = 0x00740908u;
constexpr u32 kBuildReturn = 0x00740a88u;  // Ac4SortieScene::Init -> the Build thunk
constexpr u32 kSlot8Return = 0x00b0d610u;  // inside the Build: the container creator's call site
constexpr u32 kSceneChild = 8000;          // the builder's constructor takes scene + 8000
constexpr u32 kSceneDesign = 7980;         // r7 of the Build: the AC design data
constexpr u32 kSceneR8 = 7988;             // r8 of the Build
constexpr u32 kSceneR9 = 7992;             // r9 of the Build
constexpr u32 kAllocOwner = 32112;         // r4 of the allocator = r32(toc - kAllocOwner)
constexpr u32 kColourBytes = 20;           // builder + 176 .. + 195 (copied from the scene)
constexpr u32 kColourAt = 176;
// Builder / container offsets that the Build writes (ppu_gen_118.cpp:17516-17619).
constexpr u32 kBuilderContainer = 64;  // the container the slot-8 call returns
constexpr u32 kBuilderPreA = 72;       // destroyed before container in f_00b0d058
constexpr u32 kBuilderBody = 80;       // the AC body f_00b0bb80 returns
constexpr u32 kBuilderAux = 84;        // the 300-byte object the Build allocates afterwards
constexpr u32 kBuilderPreB = 120;      // destroyed before container in f_00b0d058
constexpr u32 kContainerOwner = 196;   // r3 of vs's body creator (f_00a6e120)
constexpr u32 kContainerList = 200;    // the list f_00b12634 appends a body to
constexpr u32 kSecondPlayerId = 1001;  // the second body's entity id (the first keeps 1000)

std::atomic<u32> g_scene{0};
std::atomic<u32> g_scene_toc{0};
std::atomic<u32> g_container1{0};
std::atomic<u32> g_builder2{0};
std::atomic<u32> g_p2_operator{0};
std::atomic<bool> g_p2_building{false};
u32 g_p2_design_guest = 0;  // persistent 0x2000-byte guest buffer: [0..7659] partner AC design, [0x1f00] "ac_1"
bool g_second_done = false;
std::mutex g_mu;

constexpr u32 kSceneAcDesign = 292;      // Ac4SortieScene+292: 7660-byte AC schematic + 20 colour bytes at +7952
constexpr u32 kAcSchematicBytes = 7660;
constexpr u32 kP2NameOffset = 0x1f00;

bool call_vdtor(ppu::Ctx& ctx, u32 obj);

// Loads the partner's 7660-byte AC schematic into scene+7980 under the name "ac_1" (same sequence
// as Ac4SortieScene::LoadThread and VsSortieScene::LoadThread). Returns the guest pointer to "ac_1"
// on success, or 0 to fall back to "ac_0".
u32 load_partner_ac_resources(ppu::Ctx& ctx, u32 scene, u32 toc, u32 sp, const uint8_t* design_7660) {
  const u32 binder = rt::vm::committed(scene + kSceneDesign, 4) ? r32(scene + kSceneDesign) : 0;
  const u32 owner = rt::vm::committed(toc - kAllocOwner, 4) ? r32(toc - kAllocOwner) : 0;
  if (!binder || !owner) return 0;
  if (!g_p2_design_guest) {
    g_p2_design_guest = rt::vm::alloc(rt::vm::Area::Main, 0x2000, 0x10);
    if (!g_p2_design_guest) return 0;
  }
  for (u32 i = 0; i < kAcSchematicBytes; ++i) ppu::w8(g_p2_design_guest + i, design_7660[i]);
  const u32 name_ptr = g_p2_design_guest + kP2NameOffset;
  const char name[] = "ac_1";
  for (u32 i = 0; i < sizeof(name); ++i) ppu::w8(name_ptr + i, u8(name[i]));

  const u32 new_sp = (sp - 160u) & ~15u;
  if (!rt::vm::committed(new_sp, 160)) return 0;
  w64(new_sp, sp);
  w64(new_sp + 40, toc);

  // 1) Allocate 7716-byte AC loader from main heap (exact Ac4SortieScene::LoadThread call).
  ctx.r[1] = new_sp;
  ctx.r[2] = toc;
  ctx.r[3] = 0;
  ctx.lr = 0x00740240u;
  f_008fdc24(ctx);
  const u32 heap = u32(ctx.r[3]);
  if (!heap) return 0;
  ctx.r[1] = new_sp;
  ctx.r[2] = toc;
  ctx.r[3] = heap;
  ctx.r[4] = 7716;
  ctx.r[5] = owner;
  ctx.r[6] = 349;
  ctx.lr = 0x00740258u;
  f_008fe1a0(ctx);
  const u32 loader2 = u32(ctx.r[3]);
  if (!loader2) {
    LOG_W("coop", "COOP DESIGN: loader2 alloc(7716) failed; falling back to local AC");
    return 0;
  }
  // 2) Construct loader2 (f_00739f88 -> f_006e5a84).
  ctx.r[1] = new_sp;
  ctx.r[2] = toc;
  ctx.r[3] = loader2;
  ctx.lr = 0x0074026cu;
  f_00739f88(ctx);
  // 3) Load parts/models/textures from the partner's 7660-byte schematic (f_0073bb48 -> f_006e850c).
  ctx.r[1] = new_sp;
  ctx.r[2] = toc;
  ctx.r[3] = loader2;
  ctx.r[4] = g_p2_design_guest;
  ctx.r[5] = 0;
  ctx.r[6] = 0;
  ctx.lr = 0x00740290u;
  f_0073bb48(ctx);
  const u32 load_ok = u32(ctx.r[3]);
  if (!load_ok) {
    LOG_W("coop", "COOP DESIGN: loader2 load failed (0); destroying loader2 and falling back to local AC");
    call_vdtor(ctx, loader2);
    return 0;
  }
  // Verify every loaded part slot in loader2+4 (160-byte array: entry ptrs at +4 + 4*s, part_loader at e+4).
  {
    const u32 arr = rt::vm::committed(loader2 + 4, 4) ? r32(loader2 + 4) : 0;
    if (arr && rt::vm::committed(arr + 4, 39 * 4)) {
      u32 ok_slots = 0, bad_slots = 0;
      for (u32 s = 0; s < 39; ++s) {
        const u32 e = r32(arr + 4 + 4 * s);
        if (!e || !rt::vm::committed(e, 8)) continue;
        const u32 pl = r32(e + 4);
        if (!pl || !rt::vm::committed(pl, 16)) continue;
        const u32 m4 = r32(pl + 4), a8 = r32(pl + 8), t12 = r32(pl + 12);
        if (!m4) {
          ++bad_slots;
          LOG_W("coop", "COOP DESIGN: loader2 slot %u missing mesh (part_id=%u pl=0x%08x m4=0x%08x a8=0x%08x t12=0x%08x)",
                s, unsigned(ppu::r16(g_p2_design_guest + 2 * s)), pl, m4, a8, t12);
        } else {
          ++ok_slots;
        }
      }
      LOG_I("coop", "COOP DESIGN: loader2 verified %u ok slot(s), %u incomplete slot(s)", ok_slots, bad_slots);
    }
  }
  // 4) Attach loader2 to scene+7980 under "ac_1" (scene+7980 takes ownership and frees it on cleanup).
  ctx.r[1] = new_sp;
  ctx.r[2] = toc;
  ctx.r[3] = binder;
  ctx.r[4] = name_ptr;
  ctx.r[5] = loader2;
  ctx.lr = 0x007408d0u;
  f_00737ab8(ctx);
  const u32 attach_ok = u32(ctx.r[3]);
  if (!attach_ok) {
    LOG_W("coop", "COOP DESIGN: attach 'ac_1' failed (0); destroying loader2 and falling back to local AC");
    call_vdtor(ctx, loader2);
    return 0;
  }
  // 5) Register partner design pointer in scene+7980 slot table at slot 1 (f_0073c528 -> f_00b3fe50).
  ctx.r[1] = new_sp;
  ctx.r[2] = toc;
  ctx.r[3] = binder;
  ctx.r[4] = 1;
  ctx.r[5] = g_p2_design_guest;
  ctx.lr = 0x007403b8u;
  f_0073c528(ctx);
  LOG_I("coop", "COOP DESIGN: loaded partner AC resources as 'ac_1' (loader 0x%08x, binder 0x%08x)", loader2, binder);
  return name_ptr;
}

// Calls an object's deleting virtual destructor (vtable slot 1, +4) on a clean PPC64 stack frame.
bool call_vdtor(ppu::Ctx& ctx, u32 obj) {
  if (!obj || !rt::vm::committed(obj, 4)) return false;
  const u32 vt = r32(obj);
  if (!vt || !rt::vm::committed(vt + 4, 4)) return false;
  const u32 opd = r32(vt + 4);
  if (!opd || !rt::vm::committed(opd, 8)) return false;
  const u32 code = r32(opd);
  const u32 toc = r32(opd + 4);
  if (code < 0x10000u || code >= 0x00e00000u) return false;
  const u32 sp = u32(ctx.r[1]);
  const u32 new_sp = (sp - 128u) & ~15u;
  if (!rt::vm::committed(new_sp, 128)) return false;
  u64 saved_r[13];
  for (int i = 0; i < 13; ++i) saved_r[i] = ctx.r[i];
  const u64 saved_lr = ctx.lr, saved_ctr = ctx.ctr, saved_cia = ctx.cia;
  w64(new_sp, sp);
  w64(new_sp + 40, ctx.r[2]);
  ctx.r[1] = new_sp;
  ctx.r[2] = toc;
  ctx.r[3] = obj;
  ctx.ctr = code;
  ctx.lr = 0;
  ppu::icall(ctx, code);
  for (int i = 0; i < 13; ++i) ctx.r[i] = saved_r[i];
  ctx.lr = saved_lr;
  ctx.ctr = saved_ctr;
  ctx.cia = saved_cia;
  return true;
}

bool env_is_1(const char* name) {
  char buf[8]{};
  return GetEnvironmentVariableA(name, buf, sizeof buf) > 0 && buf[0] == '1';
}

bool dump_on() {
  static const bool on = env_is_1("AC4_COOP_DUMP");
  return on;
}

// AC4_COOP_SKIP=0x00b12770,0x00af6d60 -- which of the post-build calls the second build may skip.
struct SkipEntry {
  u32 code;
  u32 lr;
  const char* name;
};
const SkipEntry kSkips[rt::game::kCoopSkipCount] = {
    {0x00b12770u, 0x00b0d6c4u, "f_00b12770(container) [lr 0x00b0d6c4]"},
    {0x00b0bf04u, 0x00b0d6f4u, "slot10 f_00b0bf04 [lr 0x00b0d6f4]"},
    {0x00b0b36cu, 0x00b0d778u, "slot9 f_00b0b36c [lr 0x00b0d778]"},
    {0x00af6d60u, 0x00b0d794u, "f_00af6d60 [lr 0x00b0d794]"},
    {0x00af6d40u, 0x00b0d7a0u, "f_00af6d40 [lr 0x00b0d7a0]"},
    {0x00b02718u, 0x00b0d7acu, "f_00b02718 [lr 0x00b0d7ac]"},
};

const bool* skip_table() {
  static const bool* table = [] {
    bool* t = new bool[rt::game::kCoopSkipCount]{};
    char buf[512]{};
    DWORD n = GetEnvironmentVariableA("AC4_COOP_SKIP", buf, sizeof buf);
    std::string list = n && n < sizeof buf ? std::string(buf, n) : std::string();
    for (auto& s : list) {
      if (s == ',' || s == ';' || s == ' ') s = '\0';
    }
    for (const char* p = list.c_str(); p < list.c_str() + list.size();) {
      const char* start = p;
      while (p < list.c_str() + list.size() && *p) ++p;
      if (*start) {
        u32 code = u32(strtoul(start, nullptr, 0));
        for (int i = 0; i < rt::game::kCoopSkipCount; ++i)
          if (kSkips[i].code == code) t[i] = true;
      }
      ++p;  // the separator
    }
    for (int i = 0; i < rt::game::kCoopSkipCount; ++i)
      if (t[i]) LOG_W("coop", "COOP: AC4_COOP_SKIP enabled %s", kSkips[i].name);
    return const_cast<const bool*>(t);
  }();
  return table;
}

// What a builder produced: the body (its +64), the other part pointers, and the body's entity id.
void log_builder(const char* what, u32 builder, u32 result) {
  if (!builder) {
    LOG_I("coop", "COOP %s: builder 0x00000000 result %d", what, int(result));
    return;
  }
  u32 container = rt::vm::committed(builder + kBuilderContainer, 4) ? r32(builder + kBuilderContainer) : 0;
  u32 body = rt::vm::committed(builder + kBuilderBody, 4) ? r32(builder + kBuilderBody) : 0;
  u32 aux = rt::vm::committed(builder + kBuilderAux, 4) ? r32(builder + kBuilderAux) : 0;
  u32 p112 = rt::vm::committed(builder + 112, 4) ? r32(builder + 112) : 0;
  u32 id = (body && rt::vm::committed(body + 4, 4)) ? r32(body + 4) : 0;
  u32 vt = (body && rt::vm::committed(body, 4)) ? r32(body) : 0;
  std::string colours;
  for (u32 i = 0; i < kColourBytes; ++i)
    colours += ac4::strprintf("%02x", rt::vm::committed(builder + kColourAt + i, 1) ? ppu::r8(builder + kColourAt + i) : 0);
  LOG_I("coop",
        "COOP %s: builder 0x%08x result %d | +%d(container) 0x%08x | +%d(body) 0x%08x id %u vt 0x%08x | +%d 0x%08x +112 0x%08x | +%d.. colours %s",
        what, builder, int(result), int(kBuilderContainer), container, int(kBuilderBody), body, id, vt, int(kBuilderAux), aux,
        p112, int(kColourAt), colours.c_str());
}

// The object's vtable, and (for an object whose vtable slot is an OPD) slot -> opd -> code.
void log_slot(u32 obj, u32 off) {
  u32 vt = (obj && rt::vm::committed(obj, 4)) ? r32(obj) : 0;
  u32 opd = (vt && rt::vm::committed(vt + off, 4)) ? r32(vt + off) : 0;
  u32 code = (opd && rt::vm::committed(opd, 4)) ? r32(opd) : 0;
  LOG_I("coop", "COOP: object 0x%08x vtable 0x%08x -> +%d opd 0x%08x -> code 0x%08x", obj, vt, int(off), opd, code);
}

// E8: everything the shared container means, logged once per build.
void log_shared_container(u32 builder1, u32 builder2) {
  const u32 c = g_container1.load(std::memory_order_relaxed);
  const u32 body1 = (builder1 && rt::vm::committed(builder1 + kBuilderBody, 4)) ? r32(builder1 + kBuilderBody) : 0;
  const u32 body2 = (builder2 && rt::vm::committed(builder2 + kBuilderBody, 4)) ? r32(builder2 + kBuilderBody) : 0;
  const u32 cown = (c && rt::vm::committed(c + kContainerOwner, 4)) ? r32(c + kContainerOwner) : 0;
  const u32 clist = (c && rt::vm::committed(c + kContainerList, 4)) ? r32(c + kContainerList) : 0;
  auto id = [](u32 body) { return (body && rt::vm::committed(body + 4, 4)) ? int(r32(body + 4)) : -1; };
  auto vt = [](u32 body) { return (body && rt::vm::committed(body, 4)) ? int(r32(body)) : -1; };
  LOG_I("coop",
        "COOP SHARED: container 0x%08x (+%d) 0x%08x (+%d list) 0x%08x | body1 0x%08x id %d vt 0x%08x | body2 0x%08x id %d vt 0x%08x | builders 0x%08x/0x%08x (+64 of #2) 0x%08x",
        c, int(kContainerOwner), cown, int(kContainerList), clist, body1, id(body1), u32(vt(body1)), body2, id(body2),
        u32(vt(body2)), builder1, builder2, (builder2 && rt::vm::committed(builder2 + kBuilderContainer, 4)) ? r32(builder2 + kBuilderContainer) : 0);
  if (c) log_slot(c, 8);  // the container's own vtable, slot 1 (outside the process' builder slots)
}

// The scene's design-data pointers and the 64 bytes around them (E7 brief item 4: variant B's evidence).
void dump_scene(u32 scene) {
  if (!scene || !dump_on() || !rt::vm::committed(scene + kSceneDesign, 64)) return;
  std::string hex;
  for (u32 off = 0; off < 64; off += 16)
    hex += ac4::strprintf("\n   scene+%d..%d: %08x %08x %08x %08x", int(kSceneDesign + off),
                          int(kSceneDesign + off + 15), r32(scene + kSceneDesign + off),
                          r32(scene + kSceneDesign + off + 4), r32(scene + kSceneDesign + off + 8),
                          r32(scene + kSceneDesign + off + 12));
  LOG_I("coop", "COOP scene 0x%08x design data: r7(scene+%d) 0x%08x r8(scene+%d) 0x%08x r9(scene+%d) 0x%08x%s",
        scene, int(kSceneDesign), r32(scene + kSceneDesign), int(kSceneR8), r32(scene + kSceneR8),
        int(kSceneR9), r32(scene + kSceneR9), hex.c_str());
  u32 r9 = rt::vm::committed(scene + kSceneR9, 4) ? r32(scene + kSceneR9) : 0;
  if (r9 && rt::vm::committed(r9, 4))
    LOG_I("coop", "COOP r32(r9) = r32(0x%08x) = 0x%08x (scene+%d holds the pointer)", r9, r32(r9), int(kSceneR9));
}

}  // namespace

namespace rt::game {

bool coop_on() {
  static const bool on = [] {
    char buf[128]{};
    DWORD n = GetEnvironmentVariableA("AC4_COOP", buf, sizeof buf);
    if (n > 0 && n < sizeof buf) return buf[0] != '0';
    // Automatically enable co-op whenever host or join mode is configured.
    if (GetEnvironmentVariableA("AC4_COOP_HOST", buf, sizeof buf) > 0) return true;
    if (GetEnvironmentVariableA("AC4_COOP_JOIN", buf, sizeof buf) > 0) return true;
    return false;
  }();
  return on;
}
bool coop_p2_building() { return g_p2_building.load(std::memory_order_relaxed); }

void coop_note_p2_operator(u32 op) { g_p2_operator.store(op, std::memory_order_relaxed); }

u32 coop_p2_operator() { return g_p2_operator.load(std::memory_order_relaxed); }

void coop_operator_tick(u32 op) {
  const u32 p2 = g_p2_operator.load(std::memory_order_relaxed);
  if (!coop_on() || !op || op != p2) return;
  static std::atomic<u64> count{0};
  const u64 n = count.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n == 1 || n == 2 || n == 60 || n == 600 || n == 3600 || n % 18000 == 0)
    LOG_I("coop", "COOP: second operator 0x%08x UPDATED, call #%llu (this frame included)", op, (unsigned long long)n);
}

bool coop_skip(CoopSkipId id, ppu::Ctx& ctx) {
  if (!coop_on() || !g_p2_building.load(std::memory_order_relaxed)) return false;
  if (id < 0 || id >= kCoopSkipCount) return false;
  const SkipEntry& e = kSkips[id];
  if (u32(ctx.lr) != e.lr) return false;
  if (!skip_table()[id]) return false;
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 24)
    LOG_W("coop", "COOP: SKIP %s during the second build -> R(3) = 1 (no original call)", e.name);
  ctx.r[3] = 1;
  return true;
}

void coop_log_ac_parts(const char* tag, u32 body) {
  static const bool enabled = env_is_1("AC4_COOP_PART_DUMP");
  if (!enabled || !body || !rt::vm::committed(body + 708, 4)) return;
  const double bx = ppu::rf32(body + 0x40), by = ppu::rf32(body + 0x44), bz = ppu::rf32(body + 0x48);
  const u32 ac_model = rt::vm::committed(body + 292, 4) ? r32(body + 292) : 0;
  const u32 ac = r32(body + 708);
  const u32 pm = (ac && rt::vm::committed(ac + 248, 4)) ? r32(ac + 248) : 0;
  LOG_I("coop", "COOP PARTS [%s] body 0x%08x pos=(%.2f,%.2f,%.2f) model292=0x%08x ac708=0x%08x pm248=0x%08x",
        tag, body, bx, by, bz, ac_model, ac, pm);
  if (!pm || !rt::vm::committed(pm + 32, 39 * 4)) return;
  for (u32 s = 0; s < 39; ++s) {
    const u32 part = r32(pm + 32 + 4 * s);
    if (!part || !rt::vm::committed(part, 64)) continue;
    const u32 pvt = r32(part);
    const u8 pid = ppu::r8(part + 4);
    const u32 pmod = r32(part + 8);
    const u32 p1052 = rt::vm::committed(pm + 1052 + 4 * s, 4) ? r32(pm + 1052 + 4 * s) : 0;
    const u32 beg = r32(part + 24), end = r32(part + 28);
    const u32 n = (beg && end >= beg && (end - beg) <= 256) ? (end - beg) / 4 : 0;
    std::string atts;
    for (u32 k = 0; k < n && k < 6; ++k) {
      const u32 e = rt::vm::committed(beg + 4 * k, 4) ? r32(beg + 4 * k) : 0;
      if (!e || !rt::vm::committed(e, 80)) continue;
      const u32 par = r32(e), ch = r32(e + 4), node = r32(e + 8);
      double nx = 0, ny = 0, nz = 0;
      if (node && rt::vm::committed(node + 128 + 48, 12)) {
        nx = ppu::rf32(node + 128 + 48);
        ny = ppu::rf32(node + 128 + 52);
        nz = ppu::rf32(node + 128 + 56);
      }
      double cx = 0, cy = 0, cz = 0;
      if (ch && rt::vm::committed(ch + 128 + 48, 12)) {
        cx = ppu::rf32(ch + 128 + 48);
        cy = ppu::rf32(ch + 128 + 52);
        cz = ppu::rf32(ch + 128 + 56);
      }
      const double ox = ppu::rf32(e + 64), oy = ppu::rf32(e + 68), oz = ppu::rf32(e + 72);
      atts += ac4::strprintf(" [e%u par=0x%08x ch=(%.2f,%.2f,%.2f) nd=(%.2f,%.2f,%.2f) o=(%.2f,%.2f,%.2f)]",
                             k, par, cx, cy, cz, nx, ny, nz, ox, oy, oz);
    }
    LOG_I("coop", "  PART [%s] slot=%2u id=%2u part=0x%08x vt=0x%08x mod=0x%08x h1052=0x%08x att=%u%s",
          tag, s, unsigned(pid), part, pvt, pmod, p1052, n, atts.c_str());
  }
}

}  // namespace rt::game

namespace ppu {

// Ac4SortieScene::Init (r3 the scene, r2 the scene's TOC, lr the caller). The scene TOC is what the
// Build's own r2 cannot give (the TOC thunk rewrites it), so it is captured here.
void hook_ac_scene_init(Ctx& ctx, void (*orig)(Ctx&)) {
  if (rt::game::coop_on()) {
    u32 scene = u32(ctx.r[3]), toc = u32(ctx.r[2]), lr = u32(ctx.lr);
    LOG_I("coop", "COOP SCENE_INIT scene 0x%08x toc 0x%08x lr 0x%08x", scene, toc, lr);
    if (lr == kSceneInitReturn) {
      g_scene.store(scene, std::memory_order_relaxed);
      g_scene_toc.store(toc, std::memory_order_relaxed);
      // A new sortie: every mission builds its own second player and its own entity table
      // (research\28: the one-shot flag used to leave every mission after the first without AC2).
      {
        std::lock_guard lk(g_mu);
        g_second_done = false;
      }
      rt::game::coop_body_reset();
      rt::game::coop_entities_reset();
      rt::game::coopsession::note_scene_init();
      if (rt::vm::committed(scene + 7976, 4)) rt::game::coopsession::note_mission_number(int(r32(scene + 7976)));
      if (rt::vm::committed(scene + kSceneAcDesign, rt::game::coopsession::kAcDesignBytes)) {
        uint8_t local_design[rt::game::coopsession::kAcDesignBytes];
        for (size_t i = 0; i < sizeof(local_design); ++i)
          local_design[i] = ppu::r8(scene + kSceneAcDesign + u32(i));
        rt::game::coopsession::set_local_ac_design(local_design, sizeof(local_design));
      }
      LOG_I("coop", "COOP mission scene: mission number at +7976 = %d (+7972 0x%08x)",
            rt::vm::committed(scene + 7976, 4) ? int(r32(scene + 7976)) : -1,
            rt::vm::committed(scene + 7972, 4) ? r32(scene + 7972) : 0);
      LOG_W("coop", "COOP mission scene 0x%08x toc 0x%08x allocator owner 0x%08x", scene, toc,
            rt::vm::committed(toc - kAllocOwner, 4) ? r32(toc - kAllocOwner) : 0);
    }
  }
  orig(ctx);
}

// Builder vtable +32 -> OPD -> this function (0x00b0b77c): allocates the 4528-byte CONTAINER that
// the Build stores at builder+64. In the second build it must not create another one: the container
// the scene walks has to stay the first builder's (versus registers both bodies in one container).
void hook_ac_container(Ctx& ctx, void (*orig)(Ctx&)) {
  if (!rt::game::coop_on()) {
    orig(ctx);
    return;
  }
  const u32 lr = u32(ctx.lr);
  if (rt::game::coop_p2_building() && lr == kSlot8Return) {
    const u32 c = g_container1.load(std::memory_order_relaxed);
    LOG_W("coop", "COOP: slot8 CREATOR SKIPPED during the second build: builder 0x%08x gets the SHARED container 0x%08x (lr 0x%08x)",
          u32(ctx.r[3]), c, lr);
    ctx.r[3] = c;
    return;
  }
  orig(ctx);
  if (lr == kSlot8Return) {
    const u32 c = u32(ctx.r[3]);
    g_container1.store(c, std::memory_order_relaxed);
    LOG_I("coop", "COOP: container created 0x%08x for builder 0x%08x (lr 0x%08x)", c, u32(ctx.r[3]), lr);
  }
}

// The calls after the body exists. They run unchanged; AC4_COOP_SKIP can drop one for diagnosis.
void hook_ac_container_final(Ctx& ctx, void (*orig)(Ctx&)) {
  if (rt::game::coop_skip(rt::game::kCoopSkipContainerFinal, ctx)) return;
  orig(ctx);
}
void hook_ac_build_slot10(Ctx& ctx, void (*orig)(Ctx&)) {
  if (rt::game::coop_skip(rt::game::kCoopSkipSlot10, ctx)) return;
  orig(ctx);
}
void hook_ac_build_slot9(Ctx& ctx, void (*orig)(Ctx&)) {
  if (rt::game::coop_skip(rt::game::kCoopSkipSlot9, ctx)) return;
  orig(ctx);
}
void hook_ac_attach_a(Ctx& ctx, void (*orig)(Ctx&)) {
  if (rt::game::coop_skip(rt::game::kCoopSkipAttachA, ctx)) return;
  orig(ctx);
}
void hook_ac_attach_b(Ctx& ctx, void (*orig)(Ctx&)) {
  if (rt::game::coop_skip(rt::game::kCoopSkipAttachB, ctx)) return;
  orig(ctx);
}
void hook_ac_attach_c(Ctx& ctx, void (*orig)(Ctx&)) {
  if (rt::game::coop_skip(rt::game::kCoopSkipAttachC, ctx)) return;
  orig(ctx);
}

// The builder's Build (r3 builder, r5..r10 and two stack slots the arguments). Runs the game's own
// build first, then builds a second player exactly the same way into the same container.
void hook_ac_build(Ctx& ctx, void (*orig)(Ctx&)) {
  if (!rt::game::coop_on() || u32(ctx.lr) != kBuildReturn) {
    orig(ctx);
    return;
  }
  // Nothing simulates before both players are ready (research\28): the sortie waits here, in its
  // load, until the partner has reached the same point of the same mission epoch.
  if (!rt::game::coop_p2_building()) rt::game::coopsession::load_barrier();
  const u32 sp = u32(ctx.r[1]);
  u64 save[11];  // r0..r10 (r0 unused, r1 the caller's frame)
  for (int i = 0; i <= 10; ++i) save[i] = ctx.r[i];
  const u64 arg112 = rt::vm::committed(sp + 112, 8) ? r64(sp + 112) : 0;
  const u64 arg120 = rt::vm::committed(sp + 120, 8) ? r64(sp + 120) : 0;

  // Load the partner's AC resources ("ac_1") BEFORE BUILD#1 runs so large compressed part archives
  // (e.g. cr0010.000 = 6.5 MiB uncompressed) decompress into the unfragmented main heap before
  // BUILD#1 allocates mission world/SFX/Lua/body1 (mirrors VsSortieScene loading all ACs first).
  const u32 pre_scene = g_scene.load(std::memory_order_relaxed), pre_toc = g_scene_toc.load(std::memory_order_relaxed);
  uint8_t peer_design[rt::game::coopsession::kAcDesignBytes];
  const bool have_peer_design = rt::game::coopsession::get_peer_ac_design(peer_design, sizeof(peer_design));
  u32 p2_ac_name = 0;
  {
    char nb[8] = {};
    const bool no_p2 = GetEnvironmentVariableA("AC4_COOP_NO_P2", nb, sizeof nb) && nb[0] == '1';
    bool already_done = false;
    {
      std::lock_guard lk(g_mu);
      already_done = g_second_done;
    }
    if (!no_p2 && !already_done && pre_scene && pre_toc && have_peer_design) {
      p2_ac_name = load_partner_ac_resources(ctx, pre_scene, pre_toc, sp, peer_design);
      for (int i = 1; i <= 10; ++i) ctx.r[i] = save[i];
      w64(sp + 112, arg112);
      w64(sp + 120, arg120);
      ctx.lr = kBuildReturn;
    }
  }

  orig(ctx);  // the game's own player
  const u32 result1 = u32(ctx.r[3]);
  log_builder("BUILD#1", u32(save[3]), result1);
  dump_scene(u32(save[3]));
  if (rt::vm::committed(u32(save[3]) + kBuilderBody, 4)) rt::game::coop_note_body(1, r32(u32(save[3]) + kBuilderBody));
  if (!result1) {
    LOG_W("coop", "COOP: the game's own build failed (0); no second player");
    ctx.r[3] = 0;
    return;
  }
  ctx.r[3] = result1;  // the caller keeps the first result whatever happens next

  const u32 scene = g_scene.load(std::memory_order_relaxed), toc = g_scene_toc.load(std::memory_order_relaxed);
  if (!scene || !toc) {
    LOG_E("coop", "COOP: no scene captured (scene 0x%08x toc 0x%08x); second build skipped", scene, toc);
    return;
  }
  {
    std::lock_guard lk(g_mu);
    if (g_second_done) return;
    g_second_done = true;
  }
  // Experiment switch (research\28): AC4_COOP_NO_P2=1 keeps this instance to one body, to tell
  // whether the second body is what holds the mission-success ending on the host (run s28h).
  {
    char nb[8] = {};
    if (GetEnvironmentVariableA("AC4_COOP_NO_P2", nb, sizeof nb) && nb[0] == '1') {
      LOG_W("coop", "COOP: AC4_COOP_NO_P2=1 - no second body in this instance");
      return;
    }
  }

  // E8: the address of the container creator, as the Build itself resolves it (builder vtable +32),
  // and the container the first build left at builder+64.
  const u32 builder1 = u32(save[3]);
  LOG_I("coop", "COOP: builder1 0x%08x container 0x%08x -> slot+32 resolves to:", builder1,
        g_container1.load(std::memory_order_relaxed));
  log_slot(builder1, 32);

  // (a) a second builder, from the same allocator with the same arguments.
  const u32 owner = rt::vm::committed(toc - kAllocOwner, 4) ? r32(toc - kAllocOwner) : 0;
  ctx.r[1] = sp;
  ctx.r[2] = toc;
  ctx.r[3] = 240;
  ctx.r[4] = owner;
  ctx.r[5] = 103;
  ctx.lr = kAllocReturn;
  f_008fe96c(ctx);
  const u32 builder2 = u32(ctx.r[3]);
  LOG_I("coop", "COOP: builder2 allocator(0x008fe96c) size 240 owner 0x%08x tag 103 -> 0x%08x", owner, builder2);
  if (!builder2) {
    LOG_E("coop", "COOP: the second builder allocation failed; nothing more done");
    return;
  }
  g_builder2.store(builder2, std::memory_order_relaxed);

  // (b) its constructor, then the 20 colour bytes (partner's if received, else builder1's).
  ctx.r[1] = sp;
  ctx.r[2] = toc;
  ctx.r[3] = builder2;
  ctx.r[4] = scene + kSceneChild;
  ctx.lr = kBuilderCtorReturn;
  f_00737b68(ctx);
  u32 copied = 0;
  if (rt::vm::committed(builder2 + kColourAt, kColourBytes)) {
    if (have_peer_design && p2_ac_name) {
      for (u32 i = 0; i < kColourBytes; ++i) {
        ppu::w8(builder2 + kColourAt + i, peer_design[kAcSchematicBytes + i]);
        ++copied;
      }
    } else if (rt::vm::committed(builder1 + kColourAt, kColourBytes)) {
      for (u32 i = 0; i < kColourBytes; ++i) {
        ppu::w8(builder2 + kColourAt + i, ppu::r8(builder1 + kColourAt + i));
        ++copied;
      }
    }
  }
  LOG_I("coop", "COOP: builder2 0x%08x constructed with scene+%d, %u colour byte(s) copied from %s", builder2,
        int(kSceneChild), copied, (have_peer_design && p2_ac_name) ? "partner AC" : "builder1");

  // (c) the Build again, with the caller's arguments, the shared container, partner AC name ("ac_1")
  // and pad port 1. f_00b0bb80 hardcodes slot 0 when querying binder+24600 (f_00b3fad8) for the
  // 7660-byte AC schematic/stats, so temporarily swap slot 0's design pointer to g_p2_design_guest
  // during BUILD#2 and restore it immediately afterwards: body2 gets the partner's real AP, weight,
  // boosters, EN, weapons, and tuning instead of inheriting the local player's stats.
  const u32 binder = rt::vm::committed(scene + kSceneDesign, 4) ? r32(scene + kSceneDesign) : 0;
  constexpr u32 kBinderDesignMap = 24600;
  u32 saved_slot0_design = 0;
  bool swapped_slot0 = false;
  if (p2_ac_name && g_p2_design_guest && binder && rt::vm::committed(binder + kBinderDesignMap, 8) &&
      r32(binder + kBinderDesignMap) == 0u) {
    saved_slot0_design = r32(binder + kBinderDesignMap + 4);
    w32(binder + kBinderDesignMap + 4, g_p2_design_guest);
    swapped_slot0 = true;
  }
  ctx.r[1] = sp;
  ctx.r[2] = u32(save[2]);  // the builder module's TOC, as the thunk left it
  ctx.r[3] = builder2;
  for (int i = 4; i <= 10; ++i) ctx.r[i] = save[i];
  w64(sp + 112, p2_ac_name ? u64(p2_ac_name) : arg112);
  w64(sp + 120, arg120);
  ctx.lr = kBuildReturn;
  g_p2_building.store(true, std::memory_order_relaxed);
  orig(ctx);
  g_p2_building.store(false, std::memory_order_relaxed);
  if (swapped_slot0) w32(binder + kBinderDesignMap + 4, saved_slot0_design);
  const u32 result2 = u32(ctx.r[3]);
  log_builder("BUILD#2", builder2, result2);
  if (rt::vm::committed(builder1 + kBuilderBody, 4) && rt::vm::committed(builder2 + kBuilderBody, 4)) {
    const u32 b1 = r32(builder1 + kBuilderBody), b2 = r32(builder2 + kBuilderBody);
    const double ap1 = (b1 && rt::vm::committed(b1 + 0x2a8, 4)) ? ppu::rf32(b1 + 0x2a8) : 0.0;
    const double ap2 = (b2 && rt::vm::committed(b2 + 0x2a8, 4)) ? ppu::rf32(b2 + 0x2a8) : 0.0;
    LOG_I("coop", "COOP DESIGN STATS: body1 0x%08x maxAP=%.0f | body2 0x%08x maxAP=%.0f (slot0_swapped=%d)",
          b1, ap1, b2, ap2, swapped_slot0 ? 1 : 0);
  }
  if (rt::vm::committed(builder2 + kBuilderBody, 4)) rt::game::coop_note_body(2, r32(builder2 + kBuilderBody));
  // Both bodies come out of the Build with entity id 1000 (ENTITY_ID__PLAYER). Lookups by that id
  // (the mission cutscenes' camera and actions, the scripts' player commands) must find player 1, the
  // local one: on the host they found the second body, so the success cutscene ran on a body without
  // a view and the mission never ended (s28t-s28w). The second body gets its own id.
  if (rt::vm::committed(builder2 + kBuilderBody, 4)) {
    const u32 body2 = r32(builder2 + kBuilderBody);
    if (rt::vm::committed(body2 + 4, 4) && r32(body2 + 4) == 1000u) {
      w32(body2 + 4, kSecondPlayerId);
      LOG_W("coop", "COOP: second body 0x%08x entity id 1000 -> %u", body2, kSecondPlayerId);
    }
  }
  log_shared_container(builder1, builder2);
  const u32 op = rt::game::coop_p2_operator();
  if (op) {
    LOG_I("coop", "COOP: second operator 0x%08x vt 0x%08x +4(enabled) %d +8(input) 0x%08x +12(port) %d", op,
          rt::vm::committed(op, 4) ? r32(op) : 0, rt::vm::committed(op + 4, 4) ? int(r32(op + 4)) : -1,
          rt::vm::committed(op + 8, 4) ? r32(op + 8) : 0, rt::vm::committed(op + 12, 4) ? int(r32(op + 12)) : -1);
  } else {
    LOG_W("coop", "COOP: the second build created no operator object (none reported by 0x00b5129c)");
  }
  ctx.r[3] = result1;
}

// Ac4SortieScene::Cleanup (0x007405d0): destroys builder1 (scene+8280) and its shared container.
// Without destroying builder2 here, every mission leaks builder2's full resource graph (second Lua
// VM, event manager, AC2 sub-objects), exhausting the PS3 main heap by the third mission (s28z2).
// Destruction order matches f_00b0d058: pre-container members (+72, +120) before builder1 destroys
// the shared container (+64), detach +64 on builder2 so it is not double-freed, then run builder2's
// deleting destructor (vtable+4) after builder1 and the container are gone.
void hook_ac_scene_cleanup(Ctx& ctx, void (*orig)(Ctx&)) {
  const u32 scene = u32(ctx.r[3]);
  u32 b2 = 0;
  if (rt::game::coop_on() && scene && scene == g_scene.load(std::memory_order_relaxed)) {
    b2 = g_builder2.exchange(0, std::memory_order_relaxed);
  }
  if (b2 && rt::vm::committed(b2, 240)) {
    const u32 pre_a = r32(b2 + kBuilderPreA);
    if (pre_a) {
      call_vdtor(ctx, pre_a);
      w32(b2 + kBuilderPreA, 0);
    }
    const u32 pre_b = r32(b2 + kBuilderPreB);
    if (pre_b) {
      call_vdtor(ctx, pre_b);
      w32(b2 + kBuilderPreB, 0);
    }
    w32(b2 + kBuilderContainer, 0);  // shared with builder1; builder1's cleanup owns it
  }
  orig(ctx);
  if (b2 && rt::vm::committed(b2, 240)) {
    const bool ok = call_vdtor(ctx, b2);
    g_container1.store(0, std::memory_order_relaxed);
    g_p2_operator.store(0, std::memory_order_relaxed);
    rt::game::coop_body_reset();
    rt::game::coop_entities_reset();
    rt::game::coopsession::reset_mission_objects();
    LOG_I("coop", "COOP: builder2 0x%08x destroyed on scene cleanup (scene 0x%08x, ok=%d)", b2, scene, ok ? 1 : 0);
  }
}

}  // namespace ppu
