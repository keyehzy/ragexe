#include "res/rsm.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "res/data_file.h"

namespace ragexe::res {

namespace {

constexpr std::size_t kNameSize = 40;

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

// A cursor over the file that fails, instead of reading past its end.
class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

  bool Bytes(void* out, std::size_t n) {
    if (data_.size() - pos_ < n) return false;
    std::memcpy(out, data_.data() + pos_, n);
    pos_ += n;
    return true;
  }
  template <typename T>
  bool Value(T& out) {
    return Bytes(&out, sizeof out);
  }
  // A count of records of at least `size` bytes each that the file can hold.
  bool Count(std::int32_t& n, std::size_t size) {
    return Value(n) && n >= 0 && static_cast<std::size_t>(n) <= remaining() / size;
  }
  bool Name(std::string& out) {
    std::uint8_t record[kNameSize];
    if (!Bytes(record, kNameSize)) return false;
    const std::uint8_t* begin = record;
    const std::uint8_t* end = begin + kNameSize;
    const std::uint8_t* nul = std::find(begin, end, std::uint8_t{0});
    if (nul == end) return false;
    out.assign(begin, nul);
    return true;
  }
  std::size_t remaining() const { return data_.size() - pos_; }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

// FPU arithmetic in double, with the x87's NaN rules: a NaN operand comes out quiet (the
// one with the larger significand when both are), and an invalid operation gives the
// negative quiet NaN, which other processors do not produce by themselves.
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

double Add(double a, double b) { return Checked(a + b, a, b); }
double Sub(double a, double b) { return Checked(a - b, a, b); }
double Mul(double a, double b) { return Checked(a * b, a, b); }
double Div(double a, double b) { return Checked(a / b, a, b); }
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

void InsertionSort(Moving* first, Moving* last) {
  if (first == last) return;
  for (Moving* i = first + 1; i != last; ++i) {
    const Moving v = *i;
    if (v.texture < first->texture) {
      std::copy_backward(first, i, i + 1);
      *first = v;
    } else {
      Moving* p = i;
      for (; v.texture < (p - 1)->texture; --p) *p = *(p - 1);
      *p = v;
    }
  }
}

std::uint16_t MedianKey(const Moving& a, const Moving& b, const Moving& c) {
  if (a.texture < b.texture) {
    if (b.texture < c.texture) return b.texture;
    return c.texture <= a.texture ? a.texture : c.texture;
  }
  if (a.texture < c.texture) return a.texture;
  return c.texture <= b.texture ? b.texture : c.texture;
}

void QuickSortRange(Moving* first, Moving* last) {
  for (std::ptrdiff_t n = last - first; n > 16; n = last - first) {
    const std::uint16_t pivot = MedianKey(*first, first[n / 2], *(last - 1));
    Moving* i = first;
    Moving* j = last;
    for (;;) {
      while (i->texture < pivot) ++i;
      do {
        --j;
      } while (pivot < j->texture);
      if (j <= i) break;
      std::swap(*i, *j);
      ++i;
    }
    if (i - first < last - i) {
      QuickSortRange(first, i);
      first = i;
    } else {
      QuickSortRange(i, last);
      last = i;
    }
  }
}

}  // namespace

void SortModelFaces(std::vector<ModelFace>& faces) {
  std::vector<Moving> moving(faces.size());
  for (std::size_t k = 0; k < faces.size(); ++k) {
    const ModelFace& f = faces[k];
    moving[k] = {f.vertices, f.tex_coords, f.texture, f.smoothing_group};
  }
  Moving* first = moving.data();
  Moving* last = first + moving.size();
  if (moving.size() <= 16) {
    InsertionSort(first, last);
  } else {
    QuickSortRange(first, last);
    InsertionSort(first, first + 16);
    for (Moving* i = first + 16; i != last; ++i) {
      const Moving v = *i;
      Moving* p = i;
      for (; v.texture < (p - 1)->texture; --p) *p = *(p - 1);
      *p = v;
    }
  }
  for (std::size_t k = 0; k < faces.size(); ++k) {
    ModelFace& f = faces[k];
    f.vertices = moving[k].vertices;
    f.tex_coords = moving[k].tex_coords;
    f.texture = moving[k].texture;
    f.smoothing_group = moving[k].smoothing_group;
  }
}

bool ComputeModelNormals(ModelMesh& mesh) {
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

bool DecodeModel(std::span<const std::uint8_t> data, Model& out, std::string* error) {
  out = Model{};
  Reader in(data);
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
    if (!ComputeModelNormals(mesh))
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
