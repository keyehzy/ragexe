#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "render/scene.h"
#include "res/client_tables.h"

namespace ragexe::render {

// The fog's globals (00625ea0 to 00625eb4; docs/render/fog.md). The start and end are
// fractions of the way from the near plane, 10, to the far one, 1500.
struct FogState {
  std::uint32_t mode = 3;   // 00625ea0, a D3DFOGMODE: LINEAR, as SetFogEnabled leaves it
  std::uint32_t color = 0;  // 00625ea4, FOGCOLOR
  std::uint32_t range = 1;  // 00625ea8, RANGEFOGENABLE, as SetFogEnabled leaves it
  float density = 0.0f;     // 00625eac, FOGDENSITY
  float start = 0.0f;       // 00625eb0
  float end = 0.0f;         // 00625eb4
};

// ParseHexDigits (004e4cc0): the hexadecimal digits from the third character on, the
// last one lowest; any other character is skipped but still takes its four bits, and the
// shifts wrap at 32. 0 for a text shorter than 3.
std::uint32_t ParseHexDigits(std::string_view text);

// CGameMode 00516570 with the fog flag on (ClientState+0x182c, the registry's FOG): the
// map's record in the sorted fogParameterTable.txt (ClientState 005991f0), the first not
// less than the .rsw name and equal to it up to a NUL. Its four values give the start,
// the end, the colour and the density (atof, then ParseHexDigits for the colour). Returns
// whether it was found, which is whether the fog goes on; fog is left as it was if not.
bool FindMapFog(const res::LineRecords& table, std::string_view rsw_name, FogState& fog);

// ComputeVertexFog (0040c2e0): the specular colour AddPrimitive gives a vertex at this rhw
// when the device has vertex fog. The depth 1/rhw is placed between near and far, then
// between the fog's start and end, clamped to 0 to 1 (0 when unordered); 255 times what
// is left goes, truncated, to the alpha, the fog factor. The colour channels are cleared.
std::uint32_t VertexFog(float rhw, const FogState& fog);

// The fog states SceneRenderer::SetupProjection (00411090) sets when the fog is on (+0x44),
// on a hardware device (C3dDevice+0x1ec clear), with or without the device's vertex fog
// (D3DPRASTERCAPS_FOGVERTEX). Without it, linear fog turns FOGENABLE off and any other
// mode shows the error message, which sets missing_vertex_fog. The transforms it also sets
// are left out: the client's vertices come already transformed.
struct FogSetup {
  std::vector<SceneCall> calls;
  bool missing_vertex_fog = false;
};

FogSetup SetupFog(const FogState& fog, bool enabled, bool vertex_fog);

}  // namespace ragexe::render
