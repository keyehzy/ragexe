#pragma once

#include <cstdint>

#include "render/actor.h"
#include "world/actor_state.h"
#include "world/actor_walk.h"

namespace ragexe::world {

// CGameActor::HoldWhileStoneOrFrozen (00507860): held (+0x82) while the body word (+0x218) is 1,
// stone, or 2, frozen.
void HoldWhileStoneOrFrozen(GameActor& actor);

// CGameActor::RepeatSightEffect (00507740): with no effect word (+0x21c) +0x230 is 36000.
// Otherwise, with Sight (bit 0), effect 0x21 for jobs 4, 8 and 15 (GameActorJob) and 0x16 for
// the rest is launched once +0x230 reaches the effect's duration (render::EffectDuration: 200
// and 270), +0x230 cleared first; the effect made is sent 0x2c with (timeGetTime >> 4) % 10 + 1.
// Then +0x230 counts the update.
void RepeatSightEffect(GameActor& actor, const StateContext& context, std::uint32_t now);

// CGameActor::BrightenColor (00500660): unless a status colours the actor (+0x87), each channel
// of +0x7c plus its amount, kept from 0 to 255.
void BrightenColor(render::RenderObject& object, std::int32_t alpha, std::int32_t red,
                   std::int32_t green, std::int32_t blue);

// CRenderObject::PlayFrameSounds (00500940): the frames since those last sounded (+0x2c the base
// action, +0x30 the frame), each one whose event's name has ".wav" in it played where the actor
// stands, heard by the world's player (context.player). A new base action starts from frame 0
// without playing it. Past the last frame it plays on to the frame count (CActRes::GetFrameCount),
// then from frame 0. Then +0x2c and +0x30 are the base action and the frame.
void PlayFrameSounds(GameActor& actor, const StateContext& context);

// CNpc::RepeatEffectPeriodically (00540390): once timeGetTime is past +0x290 plus +0x1ec
// seconds, +0x290 is timeGetTime and the effect +0xc8 is launched.
void RepeatNpcEffect(GameActor& actor, const StateContext& context, std::uint32_t now);

// CPc::MoveGauge (00543400): its gauge (+0x2dc) moved 0x1e left of the point on screen and the
// screen's height × 12 / 480 × +0x48 below it.
void MovePlayerGauge(PlayerActor& player, const StateContext& context);

// CNpc::Update (005404d0, vf01), an actor's update (docs/world/update.md): over once the fade is
// (UpdateFade); the key colour's end 100 ms after it was keyed; then by the job (GameActorJob):
//  - 0x2d and 0x8b nothing more; 0x68 RepeatNpcEffect; 0x6f the hits due (DeliverDueHits), the
//    windows (UpdateWindows) and the state (ProcessState);
//  - any other HoldWhileStoneOrFrozen, RepeatSightEffect, the hits due, the colour brightened 8
//    while it is not white, the windows and the state.
// Then PlayFrameSounds unless +0x1f4 is 20. The state's result, or kDone without one.
StateResult UpdateNpc(GameActor& actor, const StateContext& context, std::uint32_t now,
                      std::uint32_t server_time);

// CPc::Update (00542e40, vf01), a player's update: RepeatSightEffect, the hits due, the windows,
// MovePlayerGauge, the key colour's end, the colour brightened 8 while it is not white, the
// state (CPc::ProcessState) and PlayFrameSounds. The state's result.
StateResult UpdatePlayer(PlayerActor& player, const StateContext& context, std::uint32_t now,
                         std::uint32_t server_time);

}  // namespace ragexe::world
