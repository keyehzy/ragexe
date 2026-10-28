#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/resource.h"
#include "res/rsm.h"

namespace ragexe::res {

// AnimModelResNode (0x58 bytes): a mesh with a set of vertices per frame (docs/formats/rsx.md).
struct AnimModelNode {
  std::string name;
  std::string parent_name;
  int parent = -1;                     // index into AnimModel::nodes
  std::vector<int> children;           // in file order
  std::vector<std::int32_t> textures;  // indices into AnimModel::textures
  std::int32_t vertex_count = 0;
  std::vector<Vec3> vertices;  // frame by frame, vertex_count each
  std::int32_t tex_coord_count = 0;
  // +0x54: non-zero when one set of texture coordinates serves every frame.
  std::int32_t shared_tex_coords = 0;
  std::vector<ModelTexCoord> tex_coords;  // frame by frame, or one set when shared
  std::vector<ModelFace> faces;           // as read, the .rsm layout
};

struct AnimModel {
  std::uint8_t major = 0;
  std::uint8_t minor = 0;
  std::int32_t field_140 = 2;  // the constructor's defaults (0042dbb0)
  std::int32_t frame_count = 0;
  std::int32_t field_148 = 0;
  float field_14c = 1.0f;         // 1.2 and later
  Vec3 field_154{};               // 1.2 and later
  std::int32_t field_160 = 0xff;  // 1.3
  std::vector<std::string> textures;
  std::string root;
  std::vector<AnimModelNode> nodes;  // in file order
};

// C3dAnimModelRes::Load (0042df00) on a whole .rsx file.
bool DecodeAnimModel(std::span<const std::uint8_t> data, AnimModel& out, std::string* error);

// C3dAnimModelRes.
class RsxRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const AnimModel& model() const { return model_; }
  const std::string& error() const override { return error_; }

 private:
  AnimModel model_;
  std::string error_;
};

// The original's registration (static initializer 0042da00).
void RegisterAnimModelTypes(ResourceManager& manager);

}  // namespace ragexe::res
