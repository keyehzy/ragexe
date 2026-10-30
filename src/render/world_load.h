#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "res/gat.h"
#include "res/rsw.h"

namespace ragexe::render {

// How CWorld::LoadWorld (004c3ab0) brings a world's models in (docs/render/model.md#loading).

// CGameMode::method_00511de0: the centre of an attribute cell in world x and z,
// (cell - side × 0.5) × cell size + cell size × 0.5, each step in single precision.
std::array<float, 2> CellCentre(const res::AttrMap& attr, std::int32_t cell_x, std::int32_t cell_y);

// Whether LoadWorld places a model at once: its distance from the player's point (x, 0, z),
// sqrt((dx² + dy²) + dz²) in single precision, is at most 200 (005e8974), or NaN. Their
// .rsm files load on the main thread, at single precision (res::FpuPrecision::kSingle).
bool IsNearModel(const res::WorldModel& model, float player_x, float player_z);

// The order the world's models are added to CWorld+0x48, and so to the scene graph:
// - first, in the file's order, the near ones (IsNearModel), which LoadWorld places at once;
// - then the others by their distance, nearest first, in the file's order where equal:
//   LazyModelLoadThread (004c33a0) sorts them and CWorld::AddLoadedModel (004c5030) adds each
//   in turn, which CWorld::UpdateGameObjects puts in the scene graph.
// A model loads leaves out the
// ones whose .rsm is missing or lacks the named node, as both loops do. The original breaks
// ties by the records' addresses, which follow the file's order as the world resource
// allocates them.
std::vector<std::size_t> ModelLoadOrder(std::span<const res::WorldModel> models, float player_x,
                                        float player_z,
                                        const std::function<bool(std::size_t)>& loads);

}  // namespace ragexe::render
