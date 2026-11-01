#pragma once

#include <cstdint>

#include "world/actor_state.h"
#include "world/actor_walk.h"

namespace ragexe::world {

// How high over its point on screen an actor's windows stand, before its pixel scale: the
// screen's height × 81 / 480, whole, as CGameActor 00503210 and message 0x52 each work it out
// once, into 0067cf18 and 0067cf10 (docs/world/windows.md).
float WindowRise(std::int32_t screen_height);

// CGameActor::SendMsg's message 0x52 (0050607e), which packets 0x011a and 0x013e send: a cast
// of `duration` ms from timeGetTime `now`. Without a gauge one is made (context.windows):
// created 0x3c by 6, moved 0x1e left of the actor's point on screen and WindowRise × +0x48 above
// it, and added. Then +0x1d8 is the cast's end and +0x1dc its start.
void StartGameActorCast(GameActor& actor, std::int32_t duration, const StateContext& context,
                        std::uint32_t now);

// CGameActor::SendMsg's messages that close a window (UIWindowMgr::QueueRemoval, 004a90c0):
// 0x21 the chat room's title (00505e92), 0x36 the shop's (00505e5c) and 0x53 the cast's gauge
// (00506175). False for the other messages, which it leaves alone.
bool CloseGameActorWindow(GameActor& actor, std::int32_t message, const StateContext& context);

// CGameActor 00503210, which the classes' vf01 call each update: the windows follow the
// actor's point on screen.
//  - The balloon, until 5 s after it spoke (+0x1a4), centred by its width (+0x1e0), its height
//    (+0x1e4) above the rise, kept on screen to 3 pixels past each edge by its window's size;
//    then it is queued for removal.
//  - The chat room's title 0x46 left and 17 above the rise, the shop's 17 above that.
//  - The gauge 0x1e left at the rise, filled to the time gone of the cast's (from +0x1dc to
//    +0x1d8, 1 when that is negative), until timeGetTime `now` passes the end: then the actor
//    sends itself 0x53, and in state 8 stands (vf10). A player's states are CPc::SetState's.
void StepGameActorWindows(GameActor& actor, const StateContext& context, std::uint32_t now);
void StepGameActorWindows(PlayerActor& player, const StateContext& context, std::uint32_t now);

}  // namespace ragexe::world
