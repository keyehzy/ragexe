#include "render/model.h"

#include <bit>
#include <cmath>

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

float Radians(float degrees) { return (kPi * degrees) * kInverse180; }

// __ftol (005c2084), keeping the low byte as the colour stores do.
std::uint32_t FtolByte(float v) {
  if (std::isnan(v) || v <= -9.2233720368547758e18f || v >= 9.2233720368547758e18f) return 0;
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
// and keeps the precision, so the square root's operands round to single precision.
float Acos(float x) {
  const float rest = (1.0f + x) * (1.0f - x);
  return static_cast<float>(
      std::atan2(static_cast<double>(std::sqrt(rest)), static_cast<double>(x)));
}

// The key before which time falls, from 1; the count when none does.
template <typename Key>
std::size_t KeyAfter(const std::vector<Key>& keys, std::int32_t time) {
  std::size_t i = 1;
  while (i < keys.size() && keys[i].frame <= time) ++i;
  return i;
}

template <typename Key>
float KeyFraction(const Key& a, const Key& b, std::int32_t time) {
  return static_cast<float>(time - a.frame) / static_cast<float>(b.frame - a.frame);
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

void ComputeMatrices(ModelInstance& instance, std::int32_t time) {
  for (ModelNodeInstance& node : instance.nodes) node.matrix = NodeMatrixAt(node.keys, time);
  instance.dirty = true;
}

// C3dNode::ExpandBoundingBox (004b7940).
void ExpandBox(const ModelInstance& instance, int index, const Matrix& parent,
               std::array<float, 6>& box) {
  const ModelNodeInstance& node = instance.nodes[static_cast<std::size_t>(index)];
  const Matrix m = Multiply(node.matrix, parent);
  for (const Vec3& v : Mesh(instance, node).vertices) {
    const float x = (((m[0] * v[0]) + (m[6] * v[2])) + (m[3] * v[1])) + m[9];
    const float y = (((m[1] * v[0]) + (m[7] * v[2])) + (m[4] * v[1])) + m[10];
    const float z = (((m[2] * v[0]) + (m[8] * v[2])) + (m[5] * v[1])) + m[11];
    if (x < box[0]) box[0] = x;
    if (box[3] < x) box[3] = x;
    if (y < box[1]) box[1] = y;
    if (box[4] < y) box[4] = y;
    if (z < box[2]) box[2] = z;
    if (box[5] < z) box[5] = z;
  }
  for (const int child : node.children) ExpandBox(instance, child, m, box);
}

// C3dNode::UpdateBoundingBox (004b7a80): the node's vertices' box under its matrices, its
// corners through the world matrix.
void UpdateNodeBox(ModelInstance& instance, int index, const Matrix& parent, const Matrix& world) {
  ModelNodeInstance& node = instance.nodes[static_cast<std::size_t>(index)];
  const Matrix m = Multiply(node.matrix, parent);
  std::array<float, 6> local = {kBoxStart,  kBoxStart,  kBoxStart,
                                -kBoxStart, -kBoxStart, -kBoxStart};
  for (const Vec3& v : Mesh(instance, node).vertices) {
    const float x = (((m[0] * v[0]) + (m[6] * v[2])) + (m[3] * v[1])) + m[9];
    const float y = (((m[7] * v[2]) + (m[4] * v[1])) + (m[1] * v[0])) + m[10];
    const float z = (((m[8] * v[2]) + (m[5] * v[1])) + (m[2] * v[0])) + m[11];
    if (x < local[0]) local[0] = x;
    if (local[3] < x) local[3] = x;
    if (y < local[1]) local[1] = y;
    if (local[4] < y) local[4] = y;
    if (z < local[2]) local[2] = z;
    if (local[5] < z) local[5] = z;
  }
  node.box = {kBoxStart, kBoxStart, kBoxStart, -kBoxStart, -kBoxStart, -kBoxStart};
  for (int c = 0; c < 8; ++c) {
    const float x = (c & 1) != 0 ? local[3] : local[0];
    const float y = (c & 2) != 0 ? local[4] : local[1];
    const float z = (c & 4) != 0 ? local[5] : local[2];
    const float wx = (((z * world[6]) + (world[3] * y)) + (x * world[0])) + world[9];
    const float wy = (((z * world[7]) + (x * world[1])) + (y * world[4])) + world[10];
    const float wz = (((z * world[8]) + (x * world[2])) + (y * world[5])) + world[11];
    if (wx < node.box[0]) node.box[0] = wx;
    if (wy < node.box[1]) node.box[1] = wy;
    if (wz < node.box[2]) node.box[2] = wz;
    if (node.box[3] < wx) node.box[3] = wx;
    if (node.box[4] < wy) node.box[4] = wy;
    if (node.box[5] < wz) node.box[5] = wz;
  }
  for (const int child : node.children) UpdateNodeBox(instance, child, m, world);
}

Matrix PlacedMatrix(const ModelInstance& instance) {
  Matrix m = MakeTranslation(instance.centre);
  m = Scale(m, instance.scale);
  m = RotateY(m, Radians(instance.rotation[1]));
  m = RotateX(m, Radians(instance.rotation[0]));
  m = RotateZ(m, Radians(instance.rotation[2]));
  return Translate(m, instance.position);
}

Vec3 Normalised(Vec3 v) {
  const float r = 1.0f / SqrtFloat(((v[0] * v[0]) + (v[2] * v[2])) + (v[1] * v[1]));
  return {r * v[0], r * v[1], r * v[2]};
}

// ModelInstance 004b98c0.
void ComputeBox(ModelInstance& instance) {
  std::array<float, 6> box = {kBoxStart, kBoxStart, kBoxStart, -kBoxStart, -kBoxStart, -kBoxStart};
  ExpandBox(instance, 0, kIdentity, box);
  const float min_x = box[0], min_y = box[1], min_z = box[2];
  const float max_x = box[3], max_y = box[4], max_z = box[5];
  instance.centre = {(max_x + min_x) * -0.5f, -max_y, (max_z + min_z) * -0.5f};
  const Matrix w = PlacedMatrix(instance);
  instance.world = w;
  UpdateNodeBox(instance, 0, kIdentity, w);
  instance.bounds = {kBoxStart, kBoxStart, kBoxStart, -kBoxStart, -kBoxStart, -kBoxStart};
  for (int c = 0; c < 8; ++c) {
    const float x = (c & 1) != 0 ? max_x : min_x;
    const float y = (c & 2) != 0 ? max_y : min_y;
    const float z = (c & 4) != 0 ? max_z : min_z;
    Vec3& p = instance.corners[static_cast<std::size_t>(c)];
    p[0] = (((w[0] * x) + (y * w[3])) + (z * w[6])) + w[9];
    p[1] = (((w[1] * x) + (w[7] * z)) + (y * w[4])) + w[10];
    p[2] = (((w[2] * x) + (w[8] * z)) + (y * w[5])) + w[11];
    for (std::size_t k = 0; k < 3; ++k) {
      if (p[k] < instance.bounds[k]) instance.bounds[k] = p[k];
      if (instance.bounds[k + 3] < p[k]) instance.bounds[k + 3] = p[k];
    }
  }
  const float top_x = (((max_y * w[3]) + (max_z * w[6])) + (max_x * w[0])) + w[9];
  const float bottom_x = (((min_y * w[3]) + (min_z * w[6])) + (min_x * w[0])) + w[9];
  instance.box_centre[0] = (top_x + bottom_x) * 0.5f;
  const float bottom_y = (((min_y * w[4]) + (min_x * w[1])) + (min_z * w[7])) + w[10];
  const float bottom_z = (((min_y * w[5]) + (min_x * w[2])) + (min_z * w[8])) + w[11];
  const float top_y = (((max_x * w[1]) + (max_y * w[4])) + (max_z * w[7])) + w[10];
  const float top_z = (((max_x * w[2]) + (max_y * w[5])) + (max_z * w[8])) + w[11];
  instance.box_centre[1] = (top_y + bottom_y) * 0.5f;
  instance.box_centre[2] = (top_z + bottom_z) * 0.5f;
  instance.half_extents = {std::fabs((max_x - min_x) * instance.scale[0]) * 0.5f,
                           std::fabs((max_y - min_y) * instance.scale[1]) * 0.5f,
                           std::fabs((max_z - min_z) * instance.scale[2]) * 0.5f};
  Matrix r = MakeScale(instance.scale);
  r = RotateY(r, Radians(instance.rotation[1]));
  r = RotateX(r, Radians(instance.rotation[0]));
  r = RotateZ(r, Radians(instance.rotation[2]));
  for (std::size_t a = 0; a < 3; ++a) {
    const Vec3 v = {r[a * 3], r[a * 3 + 1], r[a * 3 + 2]};
    const float sum = a == 0 ? ((v[1] * v[1]) + (v[2] * v[2])) + (v[0] * v[0])
                             : ((v[0] * v[0]) + (v[1] * v[1])) + (v[2] * v[2]);
    const float inverse = 1.0f / SqrtFloat(sum);
    instance.axes[a] = {inverse * v[0], inverse * v[1], inverse * v[2]};
  }
  instance.dirty = true;
}

// The transforms of normals by a matrix whose translation is cleared, in each place's
// order of operations.
Vec3 NormalA(const Matrix& m, const Vec3& v) {
  return {(((m[0] * v[0]) + (m[6] * v[2])) + (m[3] * v[1])) + m[9],
          (((m[7] * v[2]) + (m[4] * v[1])) + (m[1] * v[0])) + m[10],
          (((m[8] * v[2]) + (m[5] * v[1])) + (m[2] * v[0])) + m[11]};
}

Vec3 NormalB(const Matrix& m, const Vec3& v) {
  return {(((m[6] * v[2]) + (m[3] * v[1])) + (m[0] * v[0])) + m[9],
          (((m[7] * v[2]) + (m[4] * v[1])) + (m[1] * v[0])) + m[10],
          (((m[8] * v[2]) + (m[5] * v[1])) + (m[2] * v[0])) + m[11]};
}

Vec3 NormalC(const Matrix& m, const Vec3& v) {
  return {(((m[0] * v[0]) + (m[6] * v[2])) + (m[3] * v[1])) + m[9],
          (((m[1] * v[0]) + (m[7] * v[2])) + (m[4] * v[1])) + m[10],
          (((m[2] * v[0]) + (m[8] * v[2])) + (m[5] * v[1])) + m[11]};
}

// The diffuse and ambient light on a unit normal, each channel at most 1.
std::array<float, 3> Lit(const Vec3& n, const GroundLight& light) {
  float dot =
      ((light.direction[2] * n[2]) + (light.direction[0] * n[0])) + (light.direction[1] * n[1]);
  if (!(dot > 0.0f)) dot = 0.0f;
  std::array<float, 3> c{};
  for (std::size_t k = 0; k < 3; ++k) {
    c[k] = (dot * light.diffuse[k]) + light.ambient[k];
    if (c[k] >= 1.0f) c[k] = 1.0f;
  }
  return c;
}

std::uint32_t LitColor(const std::array<float, 3>& c, std::uint32_t alpha, bool tex_color) {
  const float k = tex_color ? kHalf : kFull;
  return Argb(alpha, FtolByte(c[0] * k), FtolByte(c[1] * k), FtolByte(c[2] * k));
}

// C3dNode 004b8510: each corner's colour plus the ground's under it, the corner placed by
// the node's own matrix from the instance's position.
void TintNode(const ModelInstance& instance, ModelNodeInstance& node, const CellColorLookup& cell) {
  const res::ModelMesh& mesh = Mesh(instance, node);
  const Matrix& n = node.matrix;
  const Vec3& p = instance.position;
  for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
    for (std::size_t c = 0; c < 3; ++c) {
      const Vec3& v = mesh.vertices[mesh.faces[f].vertices[c]];
      float x = 0.0f, y = 0.0f, z = 0.0f;
      if (c == 0) {
        y = (((n[4] * v[1]) + (n[1] * v[0])) + (n[7] * v[2])) + n[10];
        z = (((n[5] * v[1]) + (n[2] * v[0])) + (n[8] * v[2])) + n[11];
        x = p[0] + ((((n[3] * v[1]) + (n[6] * v[2])) + (n[0] * v[0])) + n[9]);
      } else if (c == 1) {
        y = (((n[4] * v[1]) + (n[7] * v[2])) + (n[1] * v[0])) + n[10];
        z = (((n[5] * v[1]) + (n[8] * v[2])) + (n[2] * v[0])) + n[11];
        x = p[0] + ((((n[3] * v[1]) + (n[6] * v[2])) + (n[0] * v[0])) + n[9]);
      } else {
        y = (((n[7] * v[2]) + (n[1] * v[0])) + (n[4] * v[1])) + n[10];
        z = (((n[8] * v[2]) + (n[2] * v[0])) + (n[5] * v[1])) + n[11];
        x = p[0] + ((((n[6] * v[2]) + (n[3] * v[1])) + (v[0] * n[0])) + n[9]);
      }
      const std::array<std::int32_t, 3> rgb = cell(x, p[1] + y, p[2] + z);
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

void LightNode(ModelInstance& instance, int index, const Matrix& parent, const GroundLight& light,
               const CellColorLookup& cell) {
  ModelNodeInstance& node = instance.nodes[static_cast<std::size_t>(index)];
  const res::ModelMesh& mesh = Mesh(instance, node);
  Matrix m = Multiply(node.matrix, parent);
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
      const std::uint32_t alpha = FtolByte(node.alpha * kFull);
      for (std::size_t c = 0; c < 3; ++c) shade.colors[c] = Argb(alpha, 0xff, 0xff, 0xff);
    } else if (instance.shade_type == 1) {
      const std::array<float, 3> lit = Lit(Normalised(NormalA(m, mesh.face_normals[f])), light);
      for (std::size_t c = 0; c < 3; ++c)
        shade.colors[c] = LitColor(lit, FtolByte(node.alpha * kFull), tex_color(c));
    } else if (instance.shade_type == 2) {
      for (std::size_t c = 0; c < 3; ++c) {
        const Vec3& normal = mesh.corner_normals[f][c];
        const Vec3 t = c == 0   ? NormalA(m, normal)
                       : c == 1 ? NormalB(m, normal)
                                : NormalC(m, normal);
        shade.colors[c] =
            LitColor(Lit(Normalised(t), light), FtolByte(node.alpha * kFull), tex_color(c));
      }
    }
    for (std::size_t c = 0; c < 3; ++c) {
      const res::ModelTexCoord& tc = mesh.tex_coords[face.tex_coords[c]];
      shade.uv[c * 2] = tc.u;
      shade.uv[c * 2 + 1] = tc.v;
    }
  }
  TintNode(instance, node, cell);
  for (const int child : node.children) LightNode(instance, child, m, light, cell);
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
                                   const std::array<float, 4>& b) {
  float cosine = (((a[2] * b[2]) + (a[1] * b[1])) + (a[0] * b[0])) + (b[3] * a[3]);
  const bool flip = cosine < 0.0f;
  if (flip) cosine = -cosine;
  float s0 = 0.0f, s1 = 0.0f;
  if (kSlerpEpsilon <= 1.0f - cosine) {
    const float omega = Acos(cosine);
    const float phi = (0.0f * kPi) + omega;
    const float sine = SinFloat(omega);
    const float pt = phi * t;
    s0 = SinFloat(omega - pt) / sine;
    s1 = SinFloat(pt) / sine;
  } else {
    s0 = 1.0f - t;
    s1 = t;
  }
  if (flip) s1 = -s1;
  return {(s0 * a[0]) + (s1 * b[0]), (s0 * a[1]) + (s1 * b[1]), (s0 * a[2]) + (s1 * b[2]),
          (s1 * b[3]) + (s0 * a[3])};
}

Matrix NodeMatrixAt(const NodeKeys& keys, std::int32_t time) {
  Matrix translation = kIdentity;
  if (keys.translation.size() == 1) {
    translation = MakeTranslation(keys.translation[0].value);
  } else if (keys.translation.size() > 1) {
    const std::size_t i = KeyAfter(keys.translation, time);
    if (i == keys.translation.size()) {
      translation = MakeTranslation(keys.translation.back().value);
    } else {
      const res::TranslationKey& a = keys.translation[i - 1];
      const res::TranslationKey& b = keys.translation[i];
      const float t = KeyFraction(a, b, time);
      const float s = 1.0f - t;
      translation =
          MakeTranslation({(s * a.value[0]) + (t * b.value[0]), (s * a.value[1]) + (t * b.value[1]),
                           (s * a.value[2]) + (t * b.value[2])});
    }
  }
  Matrix rotation = kIdentity;
  if (keys.rotation.size() == 1) {
    rotation = MakeRotationFromKey(keys.rotation[0].value);
  } else if (keys.rotation.size() > 1) {
    const std::size_t i = KeyAfter(keys.rotation, time);
    if (i == keys.rotation.size()) {
      rotation = MakeRotationFromKey(keys.rotation.back().value);
    } else {
      const res::RotationKey& a = keys.rotation[i - 1];
      const res::RotationKey& b = keys.rotation[i];
      if (std::bit_cast<std::uint32_t>(a.value[3]) != kOne ||
          std::bit_cast<std::uint32_t>(b.value[3]) != kOne)
        rotation = MakeRotationFromKey(SlerpRotation(KeyFraction(a, b, time), a.value, b.value));
    }
  }
  const Matrix scale = MakeScale(keys.scale);
  return Multiply(Multiply(scale, rotation), translation);
}

bool CreateModelInstance(const res::Model& model, const Vec3& position, const Vec3& rotation,
                         const Vec3& scale, float rsw_speed, ModelInstance& out) {
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
  ComputeMatrices(out, 0);
  ComputeBox(out);
  return true;
}

void UpdateWorldMatrix(ModelInstance& instance) {
  instance.world = PlacedMatrix(instance);
  instance.inverse = Invert(instance.world);
  instance.dirty = false;
}

std::array<std::int32_t, 3> GroundCellColor(const GroundMesh& ground, float x, float z) {
  const auto cell_of = [&](float v, std::int32_t size) {
    return static_cast<std::int32_t>(std::floor(static_cast<double>(v / ground.cell_size))) +
           size / 2;
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

void LightModel(ModelInstance& instance, const GroundLight& light, const CellColorLookup& cell) {
  if (instance.dirty) UpdateWorldMatrix(instance);
  LightNode(instance, 0, instance.world, light, cell);
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
  ComputeMatrices(instance, old);
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
