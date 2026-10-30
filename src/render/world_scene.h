#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "render/ground.h"
#include "render/model.h"
#include "render/scene_graph.h"
#include "res/data_files.h"
#include "res/gat.h"
#include "res/gnd.h"
#include "res/rsm.h"
#include "res/rsw.h"

namespace ragexe::render {

// A world as CWorld::LoadWorld (004c3ab0) leaves it once its lazy loader has finished and
// CWorld::UpdateGameObjects has put the late models in the scene graph
// (docs/render/model.md#loading).
struct WorldScene {
  res::World world;
  res::Ground ground;
  res::AttrMap attr;
  GroundMesh mesh;
  std::map<std::string, std::unique_ptr<res::Model>> model_files;  // by .rsw file name
  std::vector<ModelInstance> models;  // CWorld+0x48, in the order they were added
  std::vector<std::size_t> records;   // each instance's model in the .rsw
  std::size_t near_count = 0;         // the first models, placed at once (CWorld+0x54)
  SceneGraph graph;
};

// Loads data\<map>.rsw, its .gnd and its .gat, builds the ground with the .rsw's light
// (ISLIGHTMAP on), then places the models as LoadWorld does from the player's point
// (x, z): in ModelLoadOrder's order, each .rsm loaded once at the precision its first
// loader runs at (single when a near model names it), lit by the map's light and the
// ground's cell colours. A model whose .rsm does not load or has no nodes is left out.
// Then the scene graph, with the .rsw's bounds when it has them. False, with error, when
// the .rsw, .gnd or .gat does not load or the ground does not build.
bool LoadWorldScene(const res::DataFiles& files, std::string_view map, float player_x,
                    float player_z, WorldScene& out, std::string* error);

}  // namespace ragexe::render
