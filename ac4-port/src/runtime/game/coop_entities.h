// Co-op: the mission entity table (research\25_ENTITY_TABLE.md).
//
// M1 first, then M2/M3: this file starts as a measuring instrument, because the container that maps a
// mission entity id to its object has to be FOUND before it can be walked. Everything is driven by one
// request file so that a discovery run needs no rebuild:
//
//   AC4_COOP_REQ=<path>   commands, processed at most once a second, then the file is emptied:
//                           scan <hex word>          every committed 4-byte word equal to it; each hit is
//                                                    logged with its context and the object that
//                                                    contains it (the vtable found above it)
//                           scan <hex word> [max]    the same with an explicit hit cap
//                           findvt <hex vtable>      every object whose first word is that vtable
//                           mem <hex addr> <hex len>  a hex/float dump of guest memory
//                           watch <hex addr>[ f]     appended to the watch list: logged every second
//                           sweep                    one pass over committed memory for objects that
//                                                    look like a transform entity (vtable + id +
//                                                    orthonormal 4x4), grouped by class
//                           sweepall                 the same, listing every candidate
//                           clear                    forget the watch list
//
//   AC4_COOP_ENTS=1       per-second entity walk (M2): every object of the actor class (vtable
//                         0x10142638) with an id, hp and alive summary count.
//   AC4_COOP_METER=<file> the same walk appended to a file once a second, one line per entity:
//                         <t> <id> <vtable> <x> <y> <z> <hp> <alive>   (M4 divergence meter)
//
// A run with none of these set is unchanged: coop_entities_frame() returns immediately.
#pragma once

#include <cstddef>

#include "common/bytes.h"

namespace rt::game {

// True when any of the AC4_COOP_{REQ,ENTS,METER} variables was set for this process.
bool coop_entities_on();

// Called once per frame from hook_present, after coop_body_frame().
void coop_entities_frame();

// A copy of the address-ordered live entity list (the same list the host publishes). Returns how
// many were written; used by the write-watchpoint discovery tool (coop_watch.cpp).
size_t coop_entity_addrs(u32* out, size_t max);

// A new sortie: forget the previous mission's pool window, slot list and interpolation targets.
void coop_entities_reset();

// Instant 1-frame discovery of newly spawned actors/reinforcements from hook_actor_update / hook_xform_blit.
void coop_entities_note_live(u32 addr);

// Host: records a non-player actor death event (0x80020064 via hook_menu_tick) so snapshot_publish
// reports hp=0 even for scripted kills (KillEventGroup / KillEntity) that bypass entity+0x2a4.
void coop_entities_note_dead(u32 addr);

// Join: drains enemy actor addresses that transitioned alive->dead on the host and need native
// death explosion execution (f_00a50cb8) on the PPU game thread (hook_xform_blit).
size_t coop_entities_drain_kills(u32* out, size_t max);

// Join: drains multi-part boss/ship sub-part breaks (entity, part_id, host_main_hp) for native
// f_00a5091c part destruction VFX & hitbox/turret disable on the PPU game thread.
struct PartBreakJob {
  u32 addr;
  s32 part_id;
  float host_hp;
};
size_t coop_entities_drain_part_breaks(PartBreakJob* out, size_t max);

}  // namespace rt::game
