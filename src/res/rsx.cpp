#include "res/rsx.h"

#include <cstring>

#include "res/data_file.h"
#include "res/record_reader.h"

namespace ragexe::res {

namespace {

constexpr std::uint32_t kNodeMarker = 0x12345678;
// The least a node can take: two markers, two names and four counts.
constexpr std::size_t kMinNodeSize = 4 + 80 + 4 + 4 + 8 + 4 + 4;

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

// `sets` sets of `count` records of `size` bytes, when the rest of the file holds them.
bool Fits(const RecordReader& in, std::int64_t sets, std::int64_t count, std::size_t size) {
  return count >= 0 && static_cast<std::uint64_t>(sets) * static_cast<std::uint64_t>(count) <=
                           in.remaining() / size;
}

}  // namespace

bool DecodeAnimModel(std::span<const std::uint8_t> data, AnimModel& out, std::string* error) {
  out = AnimModel{};
  RecordReader in(data);
  const char* truncated = "the file is truncated";
  // The first 4 bytes, "GRSX" in data.grf, are read and never checked.
  if (!in.Skip(4) || !in.Value(out.major) || !in.Value(out.minor)) return Fail(error, truncated);
  if (out.major == 1 && out.minor > 1) {
    if (!in.Value(out.field_14c) || !in.Bytes(out.field_154.data(), 12))
      return Fail(error, truncated);
    if (out.minor == 3 && !in.Value(out.field_160)) return Fail(error, truncated);
  }
  if (!in.Value(out.field_140) || !in.Value(out.frame_count) || !in.Value(out.field_148) ||
      !in.Skip(16)) {
    return Fail(error, truncated);
  }
  if (out.frame_count < 0) return Fail(error, "a negative frame count");
  std::int32_t count = 0;
  if (!in.Count(count, 40)) return Fail(error, truncated);
  out.textures.resize(static_cast<std::size_t>(count));
  for (std::string& name : out.textures) {
    if (!in.Name(name)) return Fail(error, "a texture name without a NUL, or a truncated file");
  }
  if (!in.Name(out.root)) return Fail(error, "a root name without a NUL, or a truncated file");

  if (!in.Count(count, kMinNodeSize)) return Fail(error, truncated);
  out.nodes.resize(static_cast<std::size_t>(count));
  for (AnimModelNode& node : out.nodes) {
    std::uint32_t marker = 0;
    if (!in.Value(marker)) return Fail(error, truncated);
    if (marker != kNodeMarker) return Fail(error, "data corrupt");  // 006061f0
    if (!in.Name(node.name) || !in.Name(node.parent_name))
      return Fail(error, "a node name without a NUL, or a truncated file");

    if (!in.Count(count, 4)) return Fail(error, truncated);
    node.textures.resize(static_cast<std::size_t>(count));
    for (std::int32_t& id : node.textures) {
      if (!in.Value(id)) return Fail(error, truncated);
      if (id < 0 || static_cast<std::size_t>(id) >= out.textures.size())
        return Fail(error, "a texture index outside the model's textures");
    }

    if (!in.Value(node.vertex_count) || !Fits(in, out.frame_count, node.vertex_count, 12))
      return Fail(error, "the vertices run past the file");
    node.vertices.resize(static_cast<std::size_t>(out.frame_count) *
                         static_cast<std::size_t>(node.vertex_count));
    for (Vec3& v : node.vertices) in.Bytes(v.data(), 12);

    // With the flag set, the one set goes to frame 0, even when there are no frames.
    if (!in.Value(node.tex_coord_count) || !in.Value(node.shared_tex_coords))
      return Fail(error, truncated);
    const std::int64_t sets = node.shared_tex_coords != 0 ? 1 : out.frame_count;
    if (!Fits(in, sets, node.tex_coord_count, 12))
      return Fail(error, "the texture coordinates run past the file");
    node.tex_coords.resize(static_cast<std::size_t>(sets) *
                           static_cast<std::size_t>(node.tex_coord_count));
    for (ModelTexCoord& tc : node.tex_coords) {
      in.Value(tc.field_0);
      in.Value(tc.u);
      in.Value(tc.v);
    }

    if (!in.Count(count, 24)) return Fail(error, truncated);
    node.faces.resize(static_cast<std::size_t>(count));
    for (ModelFace& face : node.faces) {
      std::uint8_t record[24];
      in.Bytes(record, sizeof record);
      std::memcpy(face.vertices.data(), record, 6);
      std::memcpy(face.tex_coords.data(), record + 6, 6);
      std::memcpy(&face.texture, record + 12, 2);
      std::memcpy(&face.field_0e, record + 14, 2);
      std::memcpy(&face.field_10, record + 16, 4);
      std::memcpy(&face.smoothing_group, record + 20, 4);
    }
    if (!in.Value(marker)) return Fail(error, truncated);
    if (marker != kNodeMarker) return Fail(error, "data corrupt");
  }

  // C3dAnimModelRes::method_0042de70: the first node of that name.
  for (std::size_t k = 0; k < out.nodes.size(); ++k) {
    AnimModelNode& node = out.nodes[k];
    if (node.parent_name.empty()) continue;
    for (std::size_t p = 0; p < out.nodes.size(); ++p) {
      if (out.nodes[p].name == node.parent_name) {
        node.parent = static_cast<int>(p);
        break;
      }
    }
    if (node.parent < 0) return Fail(error, "a node's parent is missing");
    out.nodes[static_cast<std::size_t>(node.parent)].children.push_back(static_cast<int>(k));
  }
  return true;
}

std::unique_ptr<Resource> RsxRes::Clone() const { return std::make_unique<RsxRes>(); }

bool RsxRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "cannot open " + file.path();
    return false;
  }
  error_.clear();
  return DecodeAnimModel(file.data(), model_, &error_);
}

void RsxRes::Reset() {
  // Slot 4 (0042db50, through 0042ddc0) frees the nodes; the root names stay.
  model_.nodes.clear();
}

void RegisterAnimModelTypes(ResourceManager& manager) {
  manager.RegisterType("rsx", "model\\", std::make_unique<RsxRes>());
}

}  // namespace ragexe::res
