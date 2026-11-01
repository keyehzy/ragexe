#pragma once

#include <array>
#include <cstdint>

#include "render/actor.h"
#include "res/gat.h"
#include "world/actor_state.h"

namespace ragexe::world {

// CGameMode::IsCellBlocked (00512230): whether a cell is off the map or attribute 1 or 5, which no
// one walks.
bool IsCellBlocked(const res::AttrMap& attr, std::int32_t x, std::int32_t y);

// CGameMode::GetFreeCellToward (00511a40): the cell (GetAttrCell) of the point `steps` × 5 units
// from (to_x, to_z) toward (from_x, from_z), along the unit vector (1 over SqrtFloat of the
// squares' sum, times each difference), all in single precision. Blocked (IsCellBlocked), the
// first of its eight neighbours that is not, in the order (1, 0), (1, 1), (1, -1), (-1, 0),
// (-1, 1), (-1, -1), (0, 1), (0, -1), or the last.
std::array<std::int32_t, 2> FreeCellToward(const res::AttrMap& attr, float from_x, float from_z,
                                           float to_x, float to_z, std::int32_t steps);

// CGameMode::StepCellToward (00511b60): the cell (x, y) a step nearer (toward_x, toward_y) on
// each axis that differs; blocked, the first free of the eight neighbours of (x, y) as
// FreeCellToward tries them, or the last.
std::array<std::int32_t, 2> StepCellToward(const res::AttrMap& attr, std::int32_t toward_x,
                                           std::int32_t toward_y, std::int32_t x, std::int32_t y);

// CGameMode::IsWithinCellRange (005118d0): |ax − bx| and |ay − by| no more than `range`, the
// differences wrapping and their absolute values too (cdq, xor, sub).
bool IsWithinCellRange(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by,
                       std::int32_t range);

// CRenderObject::GetDistanceTo (00500560): the distance from the object to (x, y, z), SqrtFloat of
// the squares of the horizontal one and of y apart, each square's sum and root rounded to a float.
float DistanceTo(const render::RenderObject& object, float x, float y, float z);

// CPlayer::SendQueuedMove (0054b880): a move queued (+0x32c) while the player attacks, flinches or
// falls (states 2, 5 and 6) is sent as message 0x5b to its cell (+0x358, +0x35c), which is
// forgotten (-1).
void SendQueuedMove(PlayerActor& player, const StateContext& context);

// CPlayer::PursueTarget (0054b8e0), the pursuit of the own player's target (+0x318,
// docs/world/pursuit.md), each update: in range it acts, otherwise it asks to walk (message 0x5b to
// itself) to a free cell toward the target, once for each cell (+0x308, +0x30c). What it finds of
// the world is the context's: the actor (target_actor), the item (item) and the skill's object
// (skill_object).
void PursueTarget(PlayerActor& player, const StateContext& context, std::uint32_t now);

}  // namespace ragexe::world
