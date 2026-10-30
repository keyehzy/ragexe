#include "render/world_scene.h"

#include <cstdint>
#include <set>
#include <utility>

#include "render/world_load.h"

namespace ragexe::render {

namespace {

bool Fail(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
  return false;
}

}  // namespace

bool LoadWorldScene(const res::DataFiles& files, std::string_view map, float player_x,
                    float player_z, WorldScene& out, std::string* error) {
  std::string reason;
  const std::string rsw = "data\\" + std::string(map) + ".rsw";
  auto bytes = files.Load(rsw);
  if (!bytes || !res::DecodeWorld(*bytes, true, out.world, &reason))
    return Fail(error, rsw + ": " + (reason.empty() ? "not found" : reason));
  bytes = files.Load("data\\" + out.world.ground);
  if (!bytes || !res::DecodeGround(*bytes, out.ground, &reason))
    return Fail(error, out.world.ground + ": " + (reason.empty() ? "not found" : reason));
  bytes = files.Load("data\\" + out.world.attr);
  if (!bytes || !res::DecodeAttrMap(*bytes, out.attr, &reason))
    return Fail(error, out.world.attr + ": " + (reason.empty() ? "not found" : reason));
  const GroundLight light{out.world.light_vector, out.world.diffuse, out.world.ambient};
  if (!BuildGround(out.ground, light, true, out.mesh, &reason))
    return Fail(error, out.world.ground + ": " + reason);

  const CellColorLookup cell_color = [&](float x, float, float z) {
    return GroundCellColor(out.mesh, x, z);
  };
  std::set<std::string> single_files;
  for (const res::WorldModel& placed : out.world.models)
    if (IsNearModel(placed, player_x, player_z)) single_files.insert(placed.file);
  const auto order =
      ModelLoadOrder(out.world.models, player_x, player_z, [](std::size_t) { return true; });
  for (const std::size_t index : order) {
    const res::WorldModel& placed = out.world.models[index];
    auto [it, added] = out.model_files.try_emplace(placed.file);
    if (added) {
      auto model = std::make_unique<res::Model>();
      std::string ignored;
      const auto precision = single_files.contains(placed.file) ? res::FpuPrecision::kSingle
                                                                : res::FpuPrecision::kDouble;
      bytes = files.Load("data\\model\\" + placed.file);
      if (bytes && res::DecodeModel(*bytes, *model, &ignored, precision))
        it->second = std::move(model);
    }
    if (!it->second) continue;
    ModelInstance instance;
    if (!CreateModelInstance(*it->second, placed.position, placed.rotation, placed.scale,
                             placed.animation_speed, instance))
      continue;
    LightModel(instance, light, cell_color);
    out.models.push_back(std::move(instance));
    out.records.push_back(index);
    if (IsNearModel(placed, player_x, player_z)) ++out.near_count;
  }
  BuildSceneGraph(out.mesh, out.attr, out.models, out.world.bounds, out.graph);
  return true;
}

}  // namespace ragexe::render
