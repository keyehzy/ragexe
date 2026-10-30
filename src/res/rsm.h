#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/resource.h"

namespace ragexe::res {

using Vec3 = std::array<float, 3>;

// A face as the client keeps it (docs/formats/rsm.md).
struct ModelFace {
  std::array<std::uint16_t, 3> vertices{};
  std::array<std::uint16_t, 3> tex_coords{};
  std::uint16_t texture = 0;  // index into the node's textures
  // Bytes 0xe..0x13 of the record. The face sort does not move them, so after loading
  // they belong to the position in the array, not to the face.
  std::uint16_t field_0e = 0;
  std::int32_t field_10 = 0;
  std::int32_t smoothing_group = 0;  // 0..31: faces whose vertex normals are averaged
};

struct ModelTexCoord {
  std::uint32_t field_0 = 0;  // 0xffffffff in version 1.1 files
  float u = 0.0f;
  float v = 0.0f;
};

// ModelResMesh, after C3dModelRes::Load (0042be00) and ModelResMesh::ComputeNormals
// (00408ea0).
struct ModelMesh {
  std::vector<Vec3> vertices;  // transformed by the node's matrix
  std::vector<ModelTexCoord> tex_coords;
  std::vector<ModelFace> faces;  // sorted by texture
  std::vector<Vec3> face_normals;
  std::vector<std::array<Vec3, 3>> corner_normals;  // per face, per corner
};

struct TranslationKey {
  std::int32_t frame = 0;
  Vec3 value{};
};

struct RotationKey {
  std::int32_t frame = 0;
  std::array<float, 4> value{};  // Matrix::MakeRotationFromKey (00409ae0) reads it
};

struct ModelNode {
  std::string name;
  std::string parent_name;
  int parent = -1;                     // index into Model::nodes
  std::vector<int> children;           // in file order
  std::vector<std::int32_t> textures;  // indices into Model::textures
  std::array<float, 12> matrix{};      // as read: rows of x, y, z and the translation
  // Used by C3dNode when the node has no translation or rotation keys.
  Vec3 position{};              // +0x120
  Vec3 rotation_axis{};         // +0x12c
  float rotation_angle = 0.0f;  // +0x138
  Vec3 scale{};                 // +0x13c
  std::uint8_t alpha = 0xff;    // the model's
  ModelMesh mesh;
  std::vector<TranslationKey> translation_keys;
  std::vector<RotationKey> rotation_keys;
};

// A record of the list at C3dModelRes+0x198.
struct ModelVolume {
  std::array<float, 9> values{};
  std::int32_t flag = 0;
};

struct Model {
  std::uint8_t major = 0;
  std::uint8_t minor = 0;
  std::int32_t animation_length = 0;  // +0x194, to ModelInstance +0xb8
  std::int32_t field_190 = 0;         // to ModelInstance +0x24
  std::uint8_t alpha = 0xff;          // +0x1a4, given to every node
  std::vector<std::string> textures;
  std::string root;
  std::vector<ModelNode> nodes;  // in file order
  std::vector<ModelVolume> volumes;
};

// The x87's precision control while a model loads: the CRT's default, double, on
// LazyModelLoadThread, or single on the main thread after DirectDraw's FPUSETUP
// (docs/formats/rsm.md#the-loaders-precision).
enum class FpuPrecision : std::uint8_t { kDouble, kSingle };

// C3dModelRes::Load on a whole .rsm file.
bool DecodeModel(std::span<const std::uint8_t> data, Model& out, std::string* error,
                 FpuPrecision precision = FpuPrecision::kDouble);

// The client's std::sort of faces by texture (00431b30, 00431de0, 00431d40), which moves
// all but bytes 0xe..0x13 of each record.
void SortModelFaces(std::vector<ModelFace>& faces);

// ModelResMesh::ComputeNormals on a mesh whose faces are sorted. False when a face refers
// to a missing vertex or a smoothing group outside 0..31.
bool ComputeModelNormals(ModelMesh& mesh, FpuPrecision precision = FpuPrecision::kDouble);

// C3dModelRes.
class RsmRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const Model& model() const { return model_; }
  const std::string& error() const override { return error_; }

 private:
  Model model_;
  std::string error_;
};

// The original's registration (static initializer 0042b600).
void RegisterModelTypes(ResourceManager& manager);

}  // namespace ragexe::res
