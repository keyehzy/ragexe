#include "res/rsm.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "core/x87.h"
#include "res/data_file.h"
#include "res/record_reader.h"
#include "res/vc6_sort.h"

namespace ragexe::res {

namespace {

constexpr std::size_t kNameSize = 40;

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

// FPU arithmetic in double, with the x87's NaN rules: a NaN operand comes out quiet (the
// one with the larger significand when both are), and an invalid operation gives the
// negative quiet NaN, which other processors do not produce by themselves. At single
// precision each result is rounded once to 24 bits: every operand then has 24 bits, so a
// sum or product in double is exact or, its smaller operand being under half a 24-bit
// step, rounds the same; a quotient is rounded by its remainder (core::RoundedQuotient).
// The exponent's wider range on the x87 is not kept, which only values past a float's
// would show.
constexpr std::uint64_t kSignificand = (std::uint64_t{1} << 52) - 1;

double Indefinite() { return std::bit_cast<double>(std::uint64_t{0xfff8000000000000}); }

double Quiet(double a) {
  return std::bit_cast<double>(std::bit_cast<std::uint64_t>(a) | (std::uint64_t{1} << 51));
}

double PickNan(double a, double b) {
  if (!std::isnan(b)) return Quiet(a);
  if (!std::isnan(a)) return Quiet(b);
  const std::uint64_t sa = std::bit_cast<std::uint64_t>(Quiet(a)) & kSignificand;
  const std::uint64_t sb = std::bit_cast<std::uint64_t>(Quiet(b)) & kSignificand;
  return Quiet(sb > sa ? b : a);
}

double Checked(double r, double a, double b) {
  if (std::isnan(a) || std::isnan(b)) return PickNan(a, b);
  return std::isnan(r) ? Indefinite() : r;
}

thread_local FpuPrecision g_precision = FpuPrecision::kDouble;

// Sets the precision for the loading in its scope.
class PrecisionScope {
 public:
  explicit PrecisionScope(FpuPrecision precision) : previous_(g_precision) {
    g_precision = precision;
  }
  ~PrecisionScope() { g_precision = previous_; }
  PrecisionScope(const PrecisionScope&) = delete;
  PrecisionScope& operator=(const PrecisionScope&) = delete;
  PrecisionScope(PrecisionScope&&) = delete;
  PrecisionScope& operator=(PrecisionScope&&) = delete;

 private:
  FpuPrecision previous_;
};

double Rounded(double r) {
  return g_precision == FpuPrecision::kSingle && !std::isnan(r)
             ? static_cast<double>(static_cast<float>(r))
             : r;
}

double Add(double a, double b) { return Checked(Rounded(a + b), a, b); }
double Sub(double a, double b) { return Checked(Rounded(a - b), a, b); }
double Mul(double a, double b) { return Checked(Rounded(a * b), a, b); }
double Div(double a, double b) {
  if (g_precision == FpuPrecision::kSingle && b != 0.0 && std::isfinite(a) && std::isfinite(b))
    return core::RoundedQuotient(static_cast<float>(a), b);
  return Checked(a / b, a, b);
}
float F(double a) { return static_cast<float>(a); }
double D(float a) { return static_cast<double>(a); }

// SqrtFloat (00409570): the argument and the result are floats.
float SqrtFloat(float a) {
  if (std::isnan(a)) return F(Quiet(D(a)));
  if (a < 0.0f) return F(Indefinite());
  return F(std::sqrt(D(a)));
}

// The length the client normalises by: SqrtFloat of (x·x + y·y) + z·z.
float Length(const Vec3& v) {
  return SqrtFloat(F(Add(Add(Mul(v[0], v[0]), Mul(v[1], v[1])), Mul(v[2], v[2]))));
}

void Scale(Vec3& v, float length) {
  const double inv = Div(1.0, D(length));
  for (float& c : v) c = F(Mul(inv, D(c)));
}

// The part of a face record the client's copies move.
struct Moving {
  std::array<std::uint16_t, 3> vertices;
  std::array<std::uint16_t, 3> tex_coords;
  std::uint16_t texture;
  std::int32_t smoothing_group;
};

}  // namespace

void SortModelFaces(std::vector<ModelFace>& faces) {
  std::vector<Moving> moving(faces.size());
  for (std::size_t k = 0; k < faces.size(); ++k) {
    const ModelFace& f = faces[k];
    moving[k] = {f.vertices, f.tex_coords, f.texture, f.smoothing_group};
  }
  Vc6Sort(moving.data(), moving.data() + moving.size(),
          [](const Moving& a, const Moving& b) { return a.texture < b.texture; });
  for (std::size_t k = 0; k < faces.size(); ++k) {
    ModelFace& f = faces[k];
    f.vertices = moving[k].vertices;
    f.tex_coords = moving[k].tex_coords;
    f.texture = moving[k].texture;
    f.smoothing_group = moving[k].smoothing_group;
  }
}

bool ComputeModelNormals(ModelMesh& mesh, FpuPrecision precision) {
  const PrecisionScope scope(precision);
  const auto& v = mesh.vertices;
  for (const ModelFace& face : mesh.faces) {
    for (std::uint16_t index : face.vertices) {
      if (index >= v.size()) return false;
    }
    if (face.smoothing_group < 0 || face.smoothing_group >= 32) return false;
  }

  // The face normal, (p2 - p1) x (p2 - p0) in the client's order of operations: the x and
  // y of p2 - p1 are stored as floats, everything else stays on the FPU.
  mesh.face_normals.resize(mesh.faces.size());
  for (std::size_t k = 0; k < mesh.faces.size(); ++k) {
    const Vec3& p0 = v[mesh.faces[k].vertices[0]];
    const Vec3& p1 = v[mesh.faces[k].vertices[1]];
    const Vec3& p2 = v[mesh.faces[k].vertices[2]];
    const double a = Sub(p2[0], p0[0]);
    const double b = Sub(p2[1], p0[1]);
    const double c = Sub(p2[2], p0[2]);
    const double d = F(Sub(p2[0], p1[0]));
    const double e = F(Sub(p2[1], p1[1]));
    const double f = Sub(p2[2], p1[2]);
    Vec3& n = mesh.face_normals[k];
    n = {F(Sub(Mul(e, c), Mul(f, b))), F(Sub(Mul(f, a), Mul(c, d))), F(Sub(Mul(b, d), Mul(e, a)))};
    Scale(n, Length(n));
  }

  // Vertex normals per smoothing group: the sum of the normals of the group's faces that
  // use the vertex, in face order, normalised unless its length is 0 or NaN.
  mesh.corner_normals.assign(mesh.faces.size(), {});
  std::vector<Vec3> sums(v.size());
  for (std::int32_t group = 0; group < 32; ++group) {
    bool any = false;
    std::fill(sums.begin(), sums.end(), Vec3{});
    for (std::size_t k = 0; k < mesh.faces.size(); ++k) {
      const ModelFace& face = mesh.faces[k];
      if (face.smoothing_group != group) continue;
      any = true;
      for (int corner = 0; corner < 3; ++corner) {
        const std::uint16_t index = face.vertices[corner];
        // A vertex used twice by one face is counted once.
        if ((corner > 0 && face.vertices[0] == index) ||
            (corner > 1 && face.vertices[1] == index)) {
          continue;
        }
        for (int i = 0; i < 3; ++i)
          sums[index][i] = F(Add(mesh.face_normals[k][i], sums[index][i]));
      }
    }
    if (!any) continue;
    for (Vec3& sum : sums) {
      const float length = Length(sum);
      if (length != 0.0f && !std::isnan(length)) Scale(sum, length);
    }
    for (std::size_t k = 0; k < mesh.faces.size(); ++k) {
      const ModelFace& face = mesh.faces[k];
      if (face.smoothing_group != group) continue;
      for (int corner = 0; corner < 3; ++corner)
        mesh.corner_normals[k][corner] = sums[face.vertices[corner]];
    }
  }
  return true;
}

bool DecodeModel(std::span<const std::uint8_t> data, Model& out, std::string* error,
                 FpuPrecision precision) {
  const PrecisionScope scope(precision);
  out = Model{};
  RecordReader in(data);
  char magic[4] = {};
  if (!in.Bytes(magic, 4) || std::memcmp(magic, "GRSM", 4) != 0) {
    // 00606194, in CP949: "a model file of the wrong format."
    return Fail(error, "a model file of the wrong format");
  }
  if (!in.Value(out.major) || !in.Value(out.minor)) return Fail(error, "the file is truncated");
  if (out.major != 1 || out.minor > 5) {
    // 0060615c, in CP949: "an unsupported version; the contents may be broken".
    return Fail(error, "an unsupported version; the contents may be broken");
  }
  const std::uint8_t minor = out.minor;
  const char* truncated = "the file is truncated";
  if (!in.Value(out.animation_length) || !in.Value(out.field_190)) return Fail(error, truncated);
  if (minor >= 4 && !in.Value(out.alpha)) return Fail(error, truncated);
  std::uint8_t reserved[16];
  std::int32_t count = 0;
  if (!in.Bytes(reserved, sizeof reserved) || !in.Count(count, kNameSize))
    return Fail(error, truncated);
  out.textures.resize(static_cast<std::size_t>(count));
  for (std::string& name : out.textures) {
    if (!in.Name(name)) return Fail(error, "a texture name without a NUL, or a truncated file");
  }
  if (!in.Name(out.root)) return Fail(error, "a root name without a NUL, or a truncated file");

  std::int32_t node_count = 0;
  if (!in.Count(node_count, 2 * kNameSize)) return Fail(error, truncated);
  out.nodes.resize(static_cast<std::size_t>(node_count));
  for (ModelNode& node : out.nodes) {
    node.alpha = out.alpha;
    if (!in.Name(node.name) || !in.Name(node.parent_name))
      return Fail(error, "a node name without a NUL, or a truncated file");
    if (!in.Count(count, 4)) return Fail(error, truncated);
    node.textures.resize(static_cast<std::size_t>(count));
    for (std::int32_t& id : node.textures) {
      if (!in.Value(id)) return Fail(error, truncated);
      if (id < 0 || static_cast<std::size_t>(id) >= out.textures.size())
        return Fail(error, "a texture index outside the model's textures");
    }
    if (!in.Bytes(node.matrix.data(), 48) || !in.Bytes(node.position.data(), 12) ||
        !in.Value(node.rotation_angle) || !in.Bytes(node.rotation_axis.data(), 12) ||
        !in.Bytes(node.scale.data(), 12)) {
      return Fail(error, truncated);
    }

    ModelMesh& mesh = node.mesh;
    if (!in.Count(count, 12)) return Fail(error, truncated);
    mesh.vertices.resize(static_cast<std::size_t>(count));
    const auto& m = node.matrix;
    for (Vec3& p : mesh.vertices) {
      if (!in.Bytes(p.data(), 12)) return Fail(error, truncated);
      const double x = p[0];
      const double y = p[1];
      const double z = p[2];
      p = {F(Add(Add(Add(Mul(m[6], z), Mul(m[3], y)), Mul(m[0], x)), m[9])),
           F(Add(Add(Add(Mul(m[1], x), Mul(m[7], z)), Mul(m[4], y)), m[10])),
           F(Add(Add(Add(Mul(m[2], x), Mul(m[8], z)), Mul(m[5], y)), m[11]))};
    }

    if (!in.Count(count, minor == 1 ? 8 : 12)) return Fail(error, truncated);
    mesh.tex_coords.resize(static_cast<std::size_t>(count));
    for (ModelTexCoord& tc : mesh.tex_coords) {
      if (minor == 1) {
        // Copied through the FPU, which quiets a signalling NaN.
        if (!in.Value(tc.u) || !in.Value(tc.v)) return Fail(error, truncated);
        tc.field_0 = 0xffffffff;
        tc.u = F(D(tc.u));
        tc.v = F(D(tc.v));
      } else if (!in.Value(tc.field_0) || !in.Value(tc.u) || !in.Value(tc.v)) {
        return Fail(error, truncated);
      }
    }

    if (!in.Count(count, minor == 1 ? 20 : 24)) return Fail(error, truncated);
    mesh.faces.resize(static_cast<std::size_t>(count));
    for (ModelFace& face : mesh.faces) {
      std::uint8_t record[24] = {};
      if (!in.Bytes(record, minor == 1 ? 20 : 24)) return Fail(error, truncated);
      std::memcpy(face.vertices.data(), record, 6);
      std::memcpy(face.tex_coords.data(), record + 6, 6);
      std::memcpy(&face.texture, record + 12, 2);
      if (minor != 1) {
        // A 1.1 face keeps 14 of its 20 bytes; the rest of the record is left unset.
        std::memcpy(&face.field_0e, record + 14, 2);
        std::memcpy(&face.field_10, record + 16, 4);
        std::memcpy(&face.smoothing_group, record + 20, 4);
      }
    }
    SortModelFaces(mesh.faces);
    if (!ComputeModelNormals(mesh, precision))
      return Fail(error, "a face refers to a missing vertex or smoothing group");

    if (minor >= 5) {
      if (!in.Count(count, 16)) return Fail(error, truncated);
      node.translation_keys.resize(static_cast<std::size_t>(count));
      for (TranslationKey& key : node.translation_keys) {
        if (!in.Value(key.frame) || !in.Bytes(key.value.data(), 12)) return Fail(error, truncated);
      }
    }
    if (!in.Count(count, 20)) return Fail(error, truncated);
    node.rotation_keys.resize(static_cast<std::size_t>(count));
    for (RotationKey& key : node.rotation_keys) {
      if (!in.Value(key.frame) || !in.Bytes(key.value.data(), 16)) return Fail(error, truncated);
    }
  }

  // C3dModelRes::method_0042cbd0: the first node of that name.
  const auto find = [&](const std::string& name) {
    for (std::size_t k = 0; k < out.nodes.size(); ++k) {
      if (out.nodes[k].name == name) return static_cast<int>(k);
    }
    return -1;
  };
  if (minor < 5) {
    const int root = find(out.root);
    if (root < 0) return Fail(error, "the root node is missing");
    if (!in.Count(count, 16)) return Fail(error, truncated);
    auto& keys = out.nodes[static_cast<std::size_t>(root)].translation_keys;
    keys.resize(static_cast<std::size_t>(count));
    for (TranslationKey& key : keys) {
      if (!in.Value(key.frame) || !in.Bytes(key.value.data(), 12)) return Fail(error, truncated);
    }
  }
  for (std::size_t k = 0; k < out.nodes.size(); ++k) {
    ModelNode& node = out.nodes[k];
    if (node.parent_name.empty()) continue;
    node.parent = find(node.parent_name);
    if (node.parent < 0) return Fail(error, "a node's parent is missing");
    out.nodes[static_cast<std::size_t>(node.parent)].children.push_back(static_cast<int>(k));
  }

  if (!in.Value(count)) return Fail(error, truncated);
  if (minor >= 2 && count > 0) {
    const std::size_t size = minor >= 3 ? 0x28 : 0x24;
    if (static_cast<std::size_t>(count) > in.remaining() / size) return Fail(error, truncated);
    out.volumes.resize(static_cast<std::size_t>(count));
    for (ModelVolume& volume : out.volumes) {
      in.Bytes(volume.values.data(), 36);
      if (minor >= 3) {
        in.Value(volume.flag);
        if (volume.flag == 0) {
          volume.values[0] = F(Add(volume.values[0], 3.0));  // 005e29d8
          volume.values[2] = F(Add(volume.values[2], 3.0));
        }
      }
    }
  }
  return true;
}

std::unique_ptr<Resource> RsmRes::Clone() const { return std::make_unique<RsmRes>(); }

bool RsmRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "cannot open " + file.path();
    return false;
  }
  error_.clear();
  return DecodeModel(file.data(), model_, &error_);
}

void RsmRes::Reset() {
  // Slot 4 (0042bc70) frees the nodes, the root names, the name map and the volumes.
  model_.nodes.clear();
  model_.root.clear();
  model_.volumes.clear();
}

void RegisterModelTypes(ResourceManager& manager) {
  manager.RegisterType("rsm", "model\\", std::make_unique<RsmRes>());
}

}  // namespace ragexe::res
