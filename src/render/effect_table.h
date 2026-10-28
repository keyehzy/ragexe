#pragma once

#include <cstdint>
#include <string>

#include "core/crt_rand.h"

namespace ragexe::render {

// The effect ids CRagEffect knows: its vf01 (0054d4c0) switches on 0 to 0x151.
inline constexpr std::int32_t kEffectIdCount = 0x152;

// CRagEffect::InitEffect (0054eee0): the .str, in texture\effect, that effect id plays;
// empty for ids without one (0 to 9 draw only primitives). With minimal effects on
// (g_ClientState+0x1840, 00687eb0) some ids play a lighter _min variant. Seven ids pick one
// of two to four numbered variants with rand: those draw from it, the others do not.
std::string EffectStrName(std::int32_t id, bool minimal, core::CrtRand& rand);

// The frame CRagEffect::vf01 ends an effect at (+0xc0, from the table at 00617148), in
// world steps; 0 past the table's 0x152 entries.
std::int32_t EffectDuration(std::int32_t id);

}  // namespace ragexe::render
