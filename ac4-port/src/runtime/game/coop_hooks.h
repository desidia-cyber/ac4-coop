// Co-op hooks on the engine's damage and enable seams. Implementations in coop_hooks.cpp; the
// addresses are listed in hooks.txt (0x00a53900, 0x00a5a054, 0x00a5a084).
#pragma once

#include "ppu/ppu_recomp.h"

namespace ppu {
void hook_hp_set(Ctx& ctx, void (*orig)(Ctx&));
void hook_hp_update(Ctx& ctx, void (*orig)(Ctx&));
void hook_xform_set(Ctx& ctx, void (*orig)(Ctx&));
void hook_xform_blit(Ctx& ctx, void (*orig)(Ctx&));
void hook_actor_update(Ctx& ctx, void (*orig)(Ctx&));
void hook_move_enable_lo(Ctx& ctx, void (*orig)(Ctx&));
void hook_command_dispatch(Ctx& ctx, void (*orig)(Ctx&));
void hook_enable_move(Ctx& ctx, void (*orig)(Ctx&));
void hook_enable_attack(Ctx& ctx, void (*orig)(Ctx&));
}  // namespace ppu
