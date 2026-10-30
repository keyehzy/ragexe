#include "render/model.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace ragexe::render {

namespace {

constexpr float kPi = 3.1415920257568359375f;            // 005e8818, 005e259c
constexpr float kInverse180 = 0.0055555556900799274f;    // 005e2690
constexpr float kInverse255 = 0.0039215688593685627f;    // 005e88c8
constexpr float kSpeedScale = 0.3333333432674408f;       // 005e8968
constexpr float kTimeScale = 100.0f;                     // 005e2ad8
constexpr float kFar = 1500.0f;                          // 005e881c
constexpr float kNear = 10.0f;                           // 005e8820
constexpr float kFull = 255.0f;                          // 005e26a8
constexpr float kHalf = 128.0f;                          // 005e29dc
constexpr float kSlerpEpsilon = 9.999999974752427e-07f;  // 005e25a0
constexpr float kBoxStart = 100000.0f;                   // 005e88cc's negation
constexpr std::uint32_t kOne = 0x3f800000;

// T is the x87's value at the thread's precision control: core::X87<float> under single
// precision, core::X87<double> under double. Values round to float where the original
// stores them.
template <typename T>
constexpr core::FpuPrecision kPrecision =
    std::is_same_v<T, core::X87<float>> ? core::FpuPrecision::kSingle : core::FpuPrecision::kDouble;

template <typename T>
float Store(T v) {
  return static_cast<float>(v);
}

template <typename T, std::size_t N>
std::array<T, N> Widen(const std::array<float, N>& a) {
  std::array<T, N> r{};
  for (std::size_t i = 0; i < N; ++i) r[i] = static_cast<T>(a[i]);
  return r;
}

template <typename T>
float Radians(float degrees) {
  return Store((T{kPi} * degrees) * kInverse180);
}

// __ftol (005c2084) of the x87's value, keeping the low byte as the colour stores do.
template <typename T>
std::uint32_t FtolByte(T value) {
  const auto v = static_cast<double>(value);
  if (std::isnan(v) || v <= -9.2233720368547758e18 || v >= 9.2233720368547758e18) return 0;
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(v)) & 0xff;
}

std::int32_t Ftol(float v) {
  if (std::isnan(v) || v <= -9.2233720368547758e18f || v >= 9.2233720368547758e18f) return 0;
  return static_cast<std::int32_t>(static_cast<std::int64_t>(v));
}

std::uint32_t Argb(std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
  return (a & 0xff) << 24 | (r & 0xff) << 16 | (g & 0xff) << 8 | (b & 0xff);
}

// _CIacos (005c1770): atan2(sqrt((1 + x)(1 − x)), x). The CRT clears the rounding mode
// and keeps the precision, so the square root and its operands round to the thread's.
template <typename T>
float Acos(float x) {
  const T rest = (T{1} + x) * (T{1} - x);
  return static_cast<float>(std::atan2(static_cast<double>(Sqrt(rest)), static_cast<double>(x)));
}

// The key before which time falls, from 1; the count when none does.
template <typename Key>
std::size_t KeyAfter(const std::vector<Key>& keys, std::int32_t time) {
  std::size_t i = 1;
  while (i < keys.size() && keys[i].frame <= time) ++i;
  return i;
}

template <typename T, typename Key>
float KeyFraction(const Key& a, const Key& b, std::int32_t time) {
  return Store(static_cast<T>(time - a.frame) / static_cast<T>(b.frame - a.frame));
}

// The vertices C3dNode keeps in its model's mesh.
const res::ModelMesh& Mesh(const ModelInstance& instance, const ModelNodeInstance& node) {
  return instance.model->nodes[static_cast<std::size_t>(node.source)].mesh;
}

const res::ModelNode& Source(const ModelInstance& instance, const ModelNodeInstance& node) {
  return instance.model->nodes[static_cast<std::size_t>(node.source)];
}

int AddNode(const res::Model& model, int source, int parent, ModelInstance& out) {
  const res::ModelNode& from = model.nodes[static_cast<std::size_t>(source)];
  ModelNodeInstance node;
  node.source = source;
  node.parent = parent;
  node.alpha = static_cast<float>(from.alpha) * kInverse255;
  node.keys.translation = from.translation_keys;
  if (node.keys.translation.empty()) node.keys.translation.push_back({0, from.position});
  node.keys.rotation = from.rotation_keys;
  if (node.keys.rotation.empty())
    node.keys.rotation.push_back(
        {0, RotationFromAxisAngle(from.rotation_axis, from.rotation_angle)});
  node.keys.scale = from.scale;
  node.faces.resize(from.mesh.faces.size());
  node.tint.resize(from.mesh.faces.size() * 9);
  const int index = static_cast<int>(out.nodes.size());
  out.nodes.push_back(std::move(node));
  for (const int child : from.children) {
    const int added = AddNode(model, child, index, out);
    out.nodes[static_cast<std::size_t>(index)].children.push_back(added);
  }
  return index;
}

void ComputeMatrices(ModelInstance& instance, std::int32_t time, core::FpuPrecision precision) {
  for (ModelNodeInstance& node : instance.nodes)
    node.matrix = NodeMatrixAt(node.keys, time, precision);
  instance.dirty = true;
}

// C3dNode::ExpandBoundingBox (004b7940).
template <typename T>
void ExpandBox(const ModelInstance& instance, int index, const Matrix& parent,
               std::array<float, 6>& box) {
  const ModelNodeInstance& node = instance.nodes[static_cast<std::size_t>(index)];
  const Matrix matrix = Multiply(node.matrix, parent, kPrecision<T>);
  const std::array<T, 12> m = Widen<T>(matrix);
  for (const Vec3& vertex : Mesh(instance, node).vertices) {
    const std::array<T, 3> v = Widen<T>(vertex);
    const float x = Store((((m[0] * v[0]) + (m[6] * v[2])) + (m[3] * v[1])) + m[9]);
    const float y = Store((((m[1] * v[0]) + (m[7] * v[2])) + (m[4] * v[1])) + m[10]);
    const float z = Store((((m[2] * v[0]) + (m[8] * v[2])) + (m[5] * v[1])) + m[11]);
    if (x < box[0]) box[0] = x;
    if (box[3] < x) box[3] = x;
    if (y < box[1]) box[1] = y;
    if (box[4] < y) box[4] = y;
    if (z < box[2]) box[2] = z;
    if (box[5] < z) box[5] = z;
  }
  for (const int child : node.children) ExpandBox<T>(instance, child, matrix, box);
}

// C3dNode::UpdateBoundingBox (004b7a80): the node's vertices' box under its matrices, its
// corners through the world matrix.
template <typename T>
void UpdateNodeBox(ModelInstance& instance, int index, const Matrix& parent, const Matrix& world) {
  ModelNodeInstance& node = instance.nodes[static_cast<std::size_t>(index)];
  const Matrix matrix = Multiply(node.matrix, parent, kPrecision<T>);
  const std::array<T, 12> m = Widen<T>(matrix);
  const std::array<T, 12> w = Widen<T>(world);
  std::array<float, 6> local = {kBoxStart,  kBoxStart,  kBoxStart,
                                -kBoxStart, -kBoxStart, -kBoxStart};
  for (const Vec3& vertex : Mesh(instance, node).vertices) {
    const std::array<T, 3> v = Widen<T>(vertex);
    const float x = Store((((m[0] * v[0]) + (m[6] * v[2])) + (m[3] * v[1])) + m[9]);
    const float y = Store((((m[7] * v[2]) + (m[4] * v[1])) + (m[1] * v[0])) + m[10]);
    const float z = Store((((m[8] * v[2]) + (m[5] * v[1])) + (m[2] * v[0])) + m[11]);
    if (x < local[0]) local[0] = x;
    if (local[3] < x) local[3] = x;
    if (y < local[1]) local[1] = y;
    if (local[4] < y) local[4] = y;
    if (z < local[2]) local[2] = z;
    if (local[5] < z) local[5] = z;
  }
  node.box = {kBoxStart, kBoxStart, kBoxStart, -kBoxStart, -kBoxStart, -kBoxStart};
  for (int c = 0; c < 8; ++c) {
    const T x = (c & 1) != 0 ? local[3] : local[0];
    const T y = (c & 2) != 0 ? local[4] : local[1];
    const T z = (c & 4) != 0 ? local[5] : local[2];
    const float wx = Store((((z * w[6]) + (w[3] * y)) + (x * w[0])) + w[9]);
    const float wy = Store((((z * w[7]) + (x * w[1])) + (y * w[4])) + w[10]);
    const float wz = Store((((z * w[8]) + (x * w[2])) + (y * w[5])) + w[11]);
    if (wx < node.box[0]) node.box[0] = wx;
    if (wy < node.box[1]) node.box[1] = wy;
    if (wz < node.box[2]) node.box[2] = wz;
    if (node.box[3] < wx) node.box[3] = wx;
    if (node.box[4] < wy) node.box[4] = wy;
    if (node.box[5] < wz) node.box[5] = wz;
  }
  for (const int child : node.children) UpdateNodeBox<T>(instance, child, matrix, world);
}

template <typename T>
Matrix PlacedMatrix(const ModelInstance& instance) {
  constexpr core::FpuPrecision p = kPrecision<T>;
  Matrix m = MakeTranslation(instance.centre);
  m = Scale(m, instance.scale, p);
  m = RotateY(m, Radians<T>(instance.rotation[1]), p);
  m = RotateX(m, Radians<T>(instance.rotation[0]), p);
  m = RotateZ(m, Radians<T>(instance.rotation[2]), p);
  return Translate(m, instance.position, p);
}

// ModelInstance 004b98c0.
template <typename T>
void ComputeBox(ModelInstance& instance) {
  std::array<float, 6> box = {kBoxStart, kBoxStart, kBoxStart, -kBoxStart, -kBoxStart, -kBoxStart};
  ExpandBox<T>(instance, 0, kIdentity, box);
  const T min_x = box[0], min_y = box[1], min_z = box[2];
  const T max_x = box[3], max_y = box[4], max_z = box[5];
  instance.centre = {Store((max_x + min_x) * T{-0.5f}), -box[4], Store((max_z + min_z) * T{-0.5f})};
  const Matrix world = PlacedMatrix<T>(instance);
  const std::array<T, 12> w = Widen<T>(world);
  instance.world = world;
  UpdateNodeBox<T>(instance, 0, kIdentity, world);
  instance.bounds = {kBoxStart, kBoxStart, kBoxStart, -kBoxStart, -kBoxStart, -kBoxStart};
  for (int c = 0; c < 8; ++c) {
    const T x = (c & 1) != 0 ? max_x : min_x;
    const T y = (c & 2) != 0 ? max_y : min_y;
    const T z = (c & 4) != 0 ? max_z : min_z;
    Vec3& p = instance.corners[static_cast<std::size_t>(c)];
    p[0] = Store((((w[0] * x) + (y * w[3])) + (z * w[6])) + w[9]);
    p[1] = Store((((w[1] * x) + (w[7] * z)) + (y * w[4])) + w[10]);
    p[2] = Store((((w[2] * x) + (w[8] * z)) + (y * w[5])) + w[11]);
    for (std::size_t k = 0; k < 3; ++k) {
      if (p[k] < instance.bounds[k]) instance.bounds[k] = p[k];
      if (instance.bounds[k + 3] < p[k]) instance.bounds[k + 3] = p[k];
    }
  }
  const T top_x = (((max_y * w[3]) + (max_z * w[6])) + (max_x * w[0])) + w[9];
  const T bottom_x = (((min_y * w[3]) + (min_z * w[6])) + (min_x * w[0])) + w[9];
  instance.box_centre[0] = Store((top_x + bottom_x) * T{0.5f});
  const T bottom_y = Store((((min_y * w[4]) + (min_x * w[1])) + (min_z * w[7])) + w[10]);
  const T bottom_z = Store((((min_y * w[5]) + (min_x * w[2])) + (min_z * w[8])) + w[11]);
  const T top_y = Store((((max_x * w[1]) + (max_y * w[4])) + (max_z * w[7])) + w[10]);
  const T top_z = Store((((max_x * w[2]) + (max_y * w[5])) + (max_z * w[8])) + w[11]);
  instance.box_centre[1] = Store((top_y + bottom_y) * T{0.5f});
  instance.box_centre[2] = Store((top_z + bottom_z) * T{0.5f});
  const T size_x = Store(max_x - min_x);
  const T size_y = Store(max_y - min_y);
  const std::array<T, 3> scale = Widen<T>(instance.scale);
  instance.half_extents = {Store(Abs(size_x * scale[0]) * T{0.5f}),
                           Store(Abs(size_y * scale[1]) * T{0.5f}),
                           Store(Abs((max_z - min_z) * scale[2]) * T{0.5f})};
  constexpr core::FpuPrecision p = kPrecision<T>;
  Matrix r = MakeScale(instance.scale);
  r = RotateY(r, Radians<T>(instance.rotation[1]), p);
  r = RotateX(r, Radians<T>(instance.rotation[0]), p);
  r = RotateZ(r, Radians<T>(instance.rotation[2]), p);
  for (std::size_t a = 0; a < 3; ++a) {
    const std::array<T, 3> v = {r[a * 3], r[a * 3 + 1], r[a * 3 + 2]};
    const float sum = Store(a == 0 ? ((v[1] * v[1]) + (v[2] * v[2])) + (v[0] * v[0])
                                   : ((v[0] * v[0]) + (v[1] * v[1])) + (v[2] * v[2]));
    const T inverse = T{1} / SqrtFloat(sum);
    instance.axes[a] = {Store(inverse * v[0]), Store(inverse * v[1]), Store(inverse * v[2])};
  }
  instance.dirty = true;
}

// The transforms of normals by a matrix whose translation is cleared, in each place's
// order of operations, each component stored.
template <typename T>
Vec3 NormalA(const Matrix& matrix, const Vec3& normal) {
  const std::array<T, 12> m = Widen<T>(matrix);
  const std::array<T, 3> v = Widen<T>(normal);
  return {Store((((m[0] * v[0]) + (m[6] * v[2])) + (m[3] * v[1])) + m[9]),
          Store((((m[7] * v[2]) + (m[4] * v[1])) + (m[1] * v[0])) + m[10]),
          Store((((m[8] * v[2]) + (m[5] * v[1])) + (m[2] * v[0])) + m[11])};
}

template <typename T>
Vec3 NormalB(const Matrix& matrix, const Vec3& normal) {
  const std::array<T, 12> m = Widen<T>(matrix);
  const std::array<T, 3> v = Widen<T>(normal);
  return {Store((((m[6] * v[2]) + (m[3] * v[1])) + (m[0] * v[0])) + m[9]),
          Store((((m[7] * v[2]) + (m[4] * v[1])) + (m[1] * v[0])) + m[10]),
          Store((((m[8] * v[2]) + (m[5] * v[1])) + (m[2] * v[0])) + m[11])};
}

template <typename T>
Vec3 NormalC(const Matrix& matrix, const Vec3& normal) {
  const std::array<T, 12> m = Widen<T>(matrix);
  const std::array<T, 3> v = Widen<T>(normal);
  return {Store((((m[0] * v[0]) + (m[6] * v[2])) + (m[3] * v[1])) + m[9]),
          Store((((m[1] * v[0]) + (m[7] * v[2])) + (m[4] * v[1])) + m[10]),
          Store((((m[2] * v[0]) + (m[8] * v[2])) + (m[5] * v[1])) + m[11])};
}

// v over its length, x and y stored; z too unless keep_z, when it stays on the x87 stack
// for the light's dot product.
template <typename T>
std::array<T, 3> Normalised(const Vec3& vector, bool keep_z) {
  const std::array<T, 3> v = Widen<T>(vector);
  const T r = T{1} / SqrtFloat(Store(((v[0] * v[0]) + (v[2] * v[2])) + (v[1] * v[1])));
  const T z = r * v[2];
  return {Store(r * v[0]), Store(r * v[1]), keep_z ? z : T{Store(z)}};
}

// The diffuse and ambient light on a unit normal, each channel at most 1; red and green
// stored, blue left on the x87 stack.
template <typename T>
std::array<T, 3> Lit(const std::array<T, 3>& n, const GroundLight& light) {
  const std::array<T, 3> d = Widen<T>(light.direction);
  T dot = ((d[2] * n[2]) + (d[0] * n[0])) + (d[1] * n[1]);
  if (!(dot > T{0})) dot = 0;
  std::array<T, 3> c{};
  for (std::size_t k = 0; k < 3; ++k) {
    c[k] = (dot * light.diffuse[k]) + light.ambient[k];
    if (c[k] >= T{1}) c[k] = 1;
  }
  c[0] = Store(c[0]);
  c[1] = Store(c[1]);
  return c;
}

template <typename T>
std::uint32_t LitColor(const std::array<T, 3>& c, std::uint32_t alpha, bool tex_color) {
  const T k = tex_color ? kHalf : kFull;
  return Argb(alpha, FtolByte(c[0] * k), FtolByte(c[1] * k), FtolByte(c[2] * k));
}

template <typename T>
std::uint32_t NodeAlpha(const ModelNodeInstance& node) {
  return FtolByte(T{node.alpha} * kFull);
}

// C3dNode 004b8510: each corner's colour plus the ground's under it, the corner placed by
// the node's own matrix from the instance's position.
template <typename T>
void TintNode(const ModelInstance& instance, ModelNodeInstance& node, const CellColorLookup& cell) {
  const res::ModelMesh& mesh = Mesh(instance, node);
  const std::array<T, 12> n = Widen<T>(node.matrix);
  const std::array<T, 3> p = Widen<T>(instance.position);
  for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
    for (std::size_t c = 0; c < 3; ++c) {
      const std::array<T, 3> v = Widen<T>(mesh.vertices[mesh.faces[f].vertices[c]]);
      T x = 0;
      float y = 0.0f, z = 0.0f;
      if (c == 0) {
        y = Store((((n[4] * v[1]) + (n[1] * v[0])) + (n[7] * v[2])) + n[10]);
        z = Store((((n[5] * v[1]) + (n[2] * v[0])) + (n[8] * v[2])) + n[11]);
        x = (((n[3] * v[1]) + (n[6] * v[2])) + (n[0] * v[0])) + n[9];
      } else if (c == 1) {
        y = Store((((n[4] * v[1]) + (n[7] * v[2])) + (n[1] * v[0])) + n[10]);
        z = Store((((n[5] * v[1]) + (n[8] * v[2])) + (n[2] * v[0])) + n[11]);
        x = (((n[3] * v[1]) + (n[6] * v[2])) + (n[0] * v[0])) + n[9];
      } else {
        y = Store((((n[7] * v[2]) + (n[1] * v[0])) + (n[4] * v[1])) + n[10]);
        z = Store((((n[8] * v[2]) + (n[2] * v[0])) + (n[5] * v[1])) + n[11]);
        x = (((n[6] * v[2]) + (n[3] * v[1])) + (v[0] * n[0])) + n[9];
      }
      const std::array<std::int32_t, 3> rgb =
          cell(Store(p[0] + x), Store(p[1] + y), Store(p[2] + z));
      const std::uint32_t color = node.faces[f].colors[c];
      const std::array<std::int32_t, 3> mine = {static_cast<std::int32_t>(color >> 16 & 0xff),
                                                static_cast<std::int32_t>(color >> 8 & 0xff),
                                                static_cast<std::int32_t>(color & 0xff)};
      for (std::size_t k = 0; k < 3; ++k) {
        std::int32_t& out = node.tint[f * 9 + c * 3 + k];
        out = mine[k] + rgb[k];
        if (out > 0xff) out = 0xff;
      }
    }
  }
}

template <typename T>
void LightNode(ModelInstance& instance, int index, const Matrix& parent, const GroundLight& light,
               const CellColorLookup& cell) {
  ModelNodeInstance& node = instance.nodes[static_cast<std::size_t>(index)];
  const res::ModelMesh& mesh = Mesh(instance, node);
  Matrix m = Multiply(node.matrix, parent, kPrecision<T>);
  m[9] = 0.0f;
  m[10] = 0.0f;
  m[11] = 0.0f;
  const std::size_t faces = mesh.faces.size();
  for (std::size_t f = 0; f < faces; ++f) {
    const res::ModelFace& face = mesh.faces[f];
    FaceShade& shade = node.faces[f];
    const auto tex_color = [&](std::size_t c) {
      return mesh.tex_coords[face.tex_coords[c]].field_0 != 0;
    };
    if (instance.shade_type == 0) {
      const std::uint32_t alpha = NodeAlpha<T>(node);
      for (std::size_t c = 0; c < 3; ++c) shade.colors[c] = Argb(alpha, 0xff, 0xff, 0xff);
    } else if (instance.shade_type == 1) {
      const std::array<T, 3> lit =
          Lit(Normalised<T>(NormalA<T>(m, mesh.face_normals[f]), true), light);
      for (std::size_t c = 0; c < 3; ++c)
        shade.colors[c] = LitColor(lit, NodeAlpha<T>(node), tex_color(c));
    } else if (instance.shade_type == 2) {
      for (std::size_t c = 0; c < 3; ++c) {
        const Vec3& normal = mesh.corner_normals[f][c];
        const Vec3 t = c == 0   ? NormalA<T>(m, normal)
                       : c == 1 ? NormalB<T>(m, normal)
                                : NormalC<T>(m, normal);
        shade.colors[c] =
            LitColor(Lit(Normalised<T>(t, false), light), NodeAlpha<T>(node), tex_color(c));
      }
    }
    for (std::size_t c = 0; c < 3; ++c) {
      const res::ModelTexCoord& tc = mesh.tex_coords[face.tex_coords[c]];
      shade.uv[c * 2] = tc.u;
      shade.uv[c * 2 + 1] = tc.v;
    }
  }
  TintNode<T>(instance, node, cell);
  for (const int child : node.children) LightNode<T>(instance, child, m, light, cell);
}

template <typename T>
std::array<float, 4> Slerp(float t, const std::array<float, 4>& qa,
                           const std::array<float, 4>& qb) {
  const std::array<T, 4> a = Widen<T>(qa);
  const std::array<T, 4> b = Widen<T>(qb);
  const T dot = (((a[2] * b[2]) + (a[1] * b[1])) + (a[0] * b[0])) + (b[3] * a[3]);
  float cosine = Store(dot);
  const bool flip = dot < T{0};
  if (flip) cosine = -cosine;
  T s0 = 0, s1 = 0;
  if (T{kSlerpEpsilon} <= T{1} - cosine) {
    const float omega = Acos<T>(cosine);
    const float phi = Store((T{0} * kPi) + omega);
    const float sine = SinFloat(omega);
    const float pt = Store(T{phi} * t);
    s0 = Store(T{SinFloat(Store(T{omega} - pt))} / sine);
    s1 = T{SinFloat(pt)} / sine;
  } else {
    s0 = T{1} - t;
    s1 = t;
  }
  if (flip) s1 = -s1;
  return {Store((s0 * a[0]) + (s1 * b[0])), Store((s0 * a[1]) + (s1 * b[1])),
          Store((s0 * a[2]) + (s1 * b[2])), Store((s1 * b[3]) + (s0 * a[3]))};
}

template <typename T>
Matrix NodeMatrixAtAs(const NodeKeys& keys, std::int32_t time) {
  constexpr core::FpuPrecision p = kPrecision<T>;
  Matrix translation = kIdentity;
  if (keys.translation.size() == 1) {
    translation = MakeTranslation(keys.translation[0].value);
  } else if (keys.translation.size() > 1) {
    const std::size_t i = KeyAfter(keys.translation, time);
    if (i == keys.translation.size()) {
      translation = MakeTranslation(keys.translation.back().value);
    } else {
      const std::array<T, 3> a = Widen<T>(keys.translation[i - 1].value);
      const std::array<T, 3> b = Widen<T>(keys.translation[i].value);
      const T t = KeyFraction<T>(keys.translation[i - 1], keys.translation[i], time);
      const T s = T{1} - t;
      translation = MakeTranslation({Store((s * a[0]) + (t * b[0])), Store((s * a[1]) + (t * b[1])),
                                     Store((s * a[2]) + (t * b[2]))});
    }
  }
  Matrix rotation = kIdentity;
  if (keys.rotation.size() == 1) {
    rotation = MakeRotationFromKey(keys.rotation[0].value, p);
  } else if (keys.rotation.size() > 1) {
    const std::size_t i = KeyAfter(keys.rotation, time);
    if (i == keys.rotation.size()) {
      rotation = MakeRotationFromKey(keys.rotation.back().value, p);
    } else {
      const res::RotationKey& a = keys.rotation[i - 1];
      const res::RotationKey& b = keys.rotation[i];
      if (std::bit_cast<std::uint32_t>(a.value[3]) != kOne ||
          std::bit_cast<std::uint32_t>(b.value[3]) != kOne)
        rotation = MakeRotationFromKey(Slerp<T>(KeyFraction<T>(a, b, time), a.value, b.value), p);
    }
  }
  const Matrix scale = MakeScale(keys.scale);
  return Multiply(Multiply(scale, rotation, p), translation, p);
}

template <typename T>
void SetUp(ModelInstance& instance) {
  ComputeMatrices(instance, 0, kPrecision<T>);
  ComputeBox<T>(instance);
}

template <typename T>
void Light(ModelInstance& instance, const GroundLight& light, const CellColorLookup& cell) {
  if (instance.dirty) UpdateWorldMatrix(instance, kPrecision<T>);
  LightNode<T>(instance, 0, instance.world, light, cell);
}

struct DrawState {
  const ModelDrawSettings* settings;
  std::uint8_t alpha;
  bool flip;
  bool clip;
  std::vector<ModelPrimitive>* out;
};

std::uint32_t FadedColor(std::uint32_t color, std::uint8_t alpha, const std::array<float, 3>& f) {
  return Argb(alpha, FtolByte(static_cast<float>(color >> 16 & 0xff) * f[0]),
              FtolByte(static_cast<float>(color >> 8 & 0xff) * f[1]),
              FtolByte(static_cast<float>(color & 0xff) * f[2]));
}

void DrawNode(ModelInstance& instance, int index, const Matrix& parent, const DrawState& s) {
  ModelNodeInstance& node = instance.nodes[static_cast<std::size_t>(index)];
  const res::ModelNode& source = Source(instance, node);
  const res::ModelMesh& mesh = source.mesh;
  const ModelDrawSettings& settings = *s.settings;
  const GroundViewport& vp = settings.viewport;
  const Matrix m = Multiply(node.matrix, parent);
  const float depth_scale = kFar / (kFar - kNear);
  std::vector<ScreenVertex> screen(mesh.vertices.size());
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    const Vec3& v = mesh.vertices[i];
    const float y = (((m[7] * v[2]) + (m[1] * v[0])) + (m[4] * v[1])) + m[10];
    float z = (((m[8] * v[2]) + (m[2] * v[0])) + (m[5] * v[1])) + m[11];
    if (z < kNear) z = kNear;
    ScreenVertex& o = screen[i];
    o.rhw = 1.0f / z;
    const float x = (((m[6] * v[2]) + (m[0] * v[0])) + (m[3] * v[1])) + m[9];
    o.x = ((o.rhw * vp.scale_x) * x) + static_cast<float>(vp.center_x);
    o.y = ((y * vp.scale_y) * o.rhw) + static_cast<float>(vp.center_y);
    o.z = (((1.0f / o.rhw) - kNear) * depth_scale) * o.rhw;
  }
  const auto& faces = mesh.faces;
  std::size_t f = 0;
  while (f < faces.size()) {
    const std::uint16_t texture = faces[f].texture;
    ModelPrimitive prim;
    for (; f < faces.size() && faces[f].texture == texture; ++f) {
      const res::ModelFace& face = faces[f];
      const ScreenVertex& p0 = screen[face.vertices[0]];
      const ScreenVertex& p1 = screen[face.vertices[1]];
      const ScreenVertex& p2 = screen[face.vertices[2]];
      if (s.clip) {
        const float min_x = std::fmin(std::fmin(p0.x, p1.x), p2.x);
        if (!(min_x < static_cast<float>(vp.width))) continue;
        const float max_x = std::fmax(std::fmax(p0.x, p1.x), p2.x);
        if (!(0.0f <= max_x)) continue;
        const float min_y = std::fmin(std::fmin(p0.y, p1.y), p2.y);
        if (!(min_y < static_cast<float>(vp.height))) continue;
        const float max_y = std::fmax(std::fmax(p0.y, p1.y), p2.y);
        if (!(0.0f <= max_y)) continue;
      }
      const bool back = ((p2.x - p0.x) * (p1.y - p0.y)) <= ((p1.x - p0.x) * (p2.y - p0.y));
      if (back != s.flip && face.field_10 == 0) continue;
      const FaceShade& shade = node.faces[f];
      for (std::size_t c = 0; c < 3; ++c) {
        ScreenVertex v = c == 0 ? p0 : c == 1 ? p1 : p2;
        v.u = shade.uv[c * 2];
        v.v = shade.uv[c * 2 + 1];
        const std::uint32_t color = shade.colors[c];
        if (settings.alternate_fade) {
          v.diffuse = FadedColor(color, s.alpha, settings.fade.water);
        } else if (node.translucent) {
          v.diffuse = (color & 0x00ffffff) | 0x80000000;
        } else if (settings.ground_tint) {
          const std::int32_t* t = &node.tint[f * 9 + c * 3];
          v.diffuse = Argb(s.alpha, FtolByte(static_cast<float>(t[0]) * settings.fade.ground[0]),
                           FtolByte(static_cast<float>(t[1]) * settings.fade.ground[1]),
                           FtolByte(static_cast<float>(t[2]) * settings.fade.ground[2]));
        } else {
          v.diffuse = FadedColor(color, s.alpha, settings.fade.ground);
        }
        prim.vertices.push_back(v);
      }
    }
    if (prim.vertices.empty() || source.textures.empty()) continue;
    const auto count = static_cast<std::int32_t>(source.textures.size());
    prim.texture =
        source.textures[static_cast<std::size_t>(static_cast<std::int32_t>(texture) % count)];
    const bool alpha_texture =
        prim.texture >= 0 &&
        static_cast<std::size_t>(prim.texture) < settings.texture_alpha.size() &&
        settings.texture_alpha[static_cast<std::size_t>(prim.texture)];
    prim.flags = s.alpha == 0xff && !alpha_texture ? 0 : 1;
    s.out->push_back(std::move(prim));
  }
  node.translucent = false;
  for (const int child : node.children) DrawNode(instance, child, m, s);
}

}  // namespace

std::array<float, 4> RotationFromAxisAngle(const Vec3& axis, float angle) {
  const float half = angle * 0.5f;
  const float s = SinFloat(half);
  return {s * axis[0], s * axis[1], s * axis[2], CosFloat(half)};
}

std::array<float, 4> SlerpRotation(float t, const std::array<float, 4>& a,
                                   const std::array<float, 4>& b, core::FpuPrecision precision) {
  return precision == core::FpuPrecision::kDouble ? Slerp<core::X87<double>>(t, a, b)
                                                  : Slerp<core::X87<float>>(t, a, b);
}

Matrix NodeMatrixAt(const NodeKeys& keys, std::int32_t time, core::FpuPrecision precision) {
  return precision == core::FpuPrecision::kDouble ? NodeMatrixAtAs<core::X87<double>>(keys, time)
                                                  : NodeMatrixAtAs<core::X87<float>>(keys, time);
}

bool CreateModelInstance(const res::Model& model, const Vec3& position, const Vec3& rotation,
                         const Vec3& scale, float rsw_speed, ModelInstance& out,
                         core::FpuPrecision precision) {
  out = ModelInstance{};
  if (model.nodes.empty()) return false;
  out.model = &model;
  out.speed = rsw_speed != 0.0f ? rsw_speed * kSpeedScale : rsw_speed;
  out.position = position;
  out.rotation = rotation;
  out.scale = scale;
  out.shade_type = model.field_190;
  out.length = model.animation_length;
  AddNode(model, 0, -1, out);
  if (precision == core::FpuPrecision::kDouble)
    SetUp<core::X87<double>>(out);
  else
    SetUp<core::X87<float>>(out);
  return true;
}

void UpdateWorldMatrix(ModelInstance& instance, core::FpuPrecision precision) {
  instance.world = precision == core::FpuPrecision::kDouble
                       ? PlacedMatrix<core::X87<double>>(instance)
                       : PlacedMatrix<core::X87<float>>(instance);
  instance.inverse = Invert(instance.world, precision);
  instance.dirty = false;
}

std::array<std::int32_t, 3> GroundCellColor(const GroundMesh& ground, float x, float z,
                                            core::FpuPrecision precision) {
  const auto cell_of = [&](float v, std::int32_t size) {
    const double quotient = precision == core::FpuPrecision::kDouble
                                ? static_cast<double>(v) / static_cast<double>(ground.cell_size)
                                : static_cast<double>(v / ground.cell_size);
    return static_cast<std::int32_t>(std::floor(quotient)) + size / 2;
  };
  const std::int32_t cx = cell_of(x, ground.width);
  const std::int32_t cy = cell_of(z, ground.height);
  if (cx < 0 || cy < 0 || cx >= ground.width || cy >= ground.height) return {0xff, 0xff, 0xff};
  const std::size_t index =
      (static_cast<std::size_t>(cy) * static_cast<std::size_t>(ground.width)) +
      static_cast<std::size_t>(cx);
  const auto& c = ground.cells[index].lightmap_color;
  return {c[0], c[1], c[2]};
}

void LightModel(ModelInstance& instance, const GroundLight& light, const CellColorLookup& cell,
                core::FpuPrecision precision) {
  if (precision == core::FpuPrecision::kDouble)
    Light<core::X87<double>>(instance, light, cell);
  else
    Light<core::X87<float>>(instance, light, cell);
}

void DrawModel(ModelInstance& instance, const Matrix& view, bool clip,
               const ModelDrawSettings& settings, std::vector<ModelPrimitive>& out) {
  bool flip = instance.scale[0] < 0.0f;
  if (instance.scale[1] < 0.0f) flip = !flip;
  if (instance.scale[2] < 0.0f) flip = !flip;
  if (instance.dirty) UpdateWorldMatrix(instance);
  const DrawState state{&settings, instance.alpha, flip, clip, &out};
  DrawNode(instance, 0, Multiply(instance.world, view), state);
  if (instance.fading) {
    if (instance.alpha > 0x5a) instance.alpha = static_cast<std::uint8_t>(instance.alpha - 5);
  } else if (instance.alpha != 0xff) {
    instance.alpha = static_cast<std::uint8_t>(instance.alpha + 5);
  }
  instance.fading = false;
}

void AdvanceAnimation(ModelInstance& instance) {
  if (instance.loop != 1 && instance.loop != 2) return;
  const std::int32_t old = instance.time;
  ComputeMatrices(instance, old, core::FpuPrecision::kSingle);
  const std::int32_t time = Ftol((instance.speed * kTimeScale) + static_cast<float>(old));
  instance.time = time;
  if (instance.loop == 1) {
    if (time >= instance.length) instance.time = old;
    return;
  }
  if (instance.length == 0) instance.length = 1;
  if (time >= instance.length) instance.time = time % instance.length;
}

}  // namespace ragexe::render
