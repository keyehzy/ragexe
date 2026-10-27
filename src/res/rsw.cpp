#include "res/rsw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "res/data_file.h"
#include "res/record_reader.h"

namespace ragexe::res {

namespace {

constexpr std::size_t kNameSize = 40;
constexpr std::size_t kModelSize = 0xc4;
constexpr std::size_t kModelNameSize = 80;
constexpr std::size_t kLightSize = 0x6c;
constexpr std::size_t kOldSoundSize = 0xbc;
constexpr std::size_t kBoundsCount = 1 + 4 + 16 + 64 + 256 + 1024;  // depth 0 to 5

constexpr float kPi = 3.14159203f;            // 005e2aa8, a little below pi
constexpr float kOneOver180 = 1.0f / 180.0f;  // 005e2690

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

// The C string at the start of a record; false when it has no NUL there.
bool CString(const std::uint8_t* record, std::size_t size, std::string& out) {
  const auto* nul = std::find(record, record + size, std::uint8_t{0});
  if (nul == record + size) return false;
  out.assign(record, nul);
  return true;
}

// Rows 1 and 3 (the translation) of Matrix::Multiply (00409d90) of two 4 x 3 matrices, in
// its order of operations, each sum rounded to float as the original stores it.
using Matrix = std::array<float, 12>;

std::array<float, 6> MultiplyRows1And3(const Matrix& a, const Matrix& b) {
  const auto f = [](double v) { return static_cast<float>(v); };
  const auto d = [](float v) { return static_cast<double>(v); };
  return {
      f(d(a[4]) * b[3] + d(a[3]) * b[0] + d(a[5]) * b[6]),
      f(d(a[5]) * b[7] + d(a[4]) * b[4] + d(a[3]) * b[1]),
      f(d(b[8]) * a[5] + d(b[5]) * a[4] + d(a[3]) * b[2]),
      f(d(a[10]) * b[3] + d(a[9]) * b[0] + d(a[11]) * b[6] + b[9]),
      f(d(a[11]) * b[7] + d(a[10]) * b[4] + d(a[9]) * b[1] + b[10]),
      f(d(b[8]) * a[11] + d(b[5]) * a[10] + d(a[9]) * b[2] + b[11]),
  };
}

float Sin(float a) { return static_cast<float>(std::sin(static_cast<double>(a))); }
float Cos(float a) { return static_cast<float>(std::cos(static_cast<double>(a))); }

float Radians(std::int32_t degrees) {
  return static_cast<float>(static_cast<double>(degrees) * kPi * kOneOver180);
}

}  // namespace

std::array<float, 3> WorldLightVector(std::int32_t angle_y, std::int32_t angle_x) {
  const float x = Radians(angle_x);
  const float y = Radians(angle_y);
  // MakeRotationX (004095d0) and MakeRotationY (00409620), from the identity.
  Matrix rx = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  rx[4] = rx[8] = Cos(x);
  rx[5] = Sin(x);
  rx[7] = -rx[5];
  Matrix ry = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  ry[0] = ry[8] = Cos(y);
  ry[6] = Sin(y);
  ry[2] = -ry[6];
  const auto m = MultiplyRows1And3(rx, ry);  // RotateY (00409770)
  return {m[3] + m[0], m[4] + m[1], m[5] + m[2]};
}

bool DecodeWorld(std::span<const std::uint8_t> data, bool lightmaps, World& out,
                 std::string* error) {
  out = World{};
  RecordReader in(data);
  char magic[4] = {};
  if (!in.Bytes(magic, 4) || std::memcmp(magic, "GRSW", 4) != 0)
    return Fail(error, "Invalid Format");  // 006065c4
  if (!in.Value(out.major) || !in.Value(out.minor)) return Fail(error, "the file is truncated");
  const std::uint8_t major = out.major;
  const std::uint8_t minor = out.minor;
  if ((major == 2 && minor > 1) || major > 2)
    return Fail(error, "C3dWorldRes :: Unsupported Version");  // 006065a0
  const auto at_least = [&](std::uint8_t ma, std::uint8_t mi) {
    return major > ma || (major == ma && minor >= mi);
  };

  // The loader reads every name into one 40-byte buffer; a model of a version below 1.3
  // takes its name from whatever that buffer last held.
  std::uint8_t name[kNameSize];
  std::string last_name;
  const auto read_name = [&](std::string* to) {
    if (!in.Bytes(name, kNameSize)) return Fail(error, "the file is truncated");
    if (!CString(name, kNameSize, last_name)) return Fail(error, "a name without a NUL");
    if (to != nullptr) *to = last_name;
    return true;
  };
  if (!read_name(nullptr) || !read_name(&out.ground)) return false;
  if (at_least(1, 4) && !read_name(&out.attr)) return false;
  if (!read_name(&out.extra)) return false;

  bool ok = true;
  if (at_least(1, 3)) ok = ok && in.Value(out.water_level);
  if (at_least(1, 8)) {
    ok = ok && in.Value(out.water_type) && in.Value(out.water_param_170) &&
         in.Value(out.water_phase_step) && in.Value(out.water_param_178);
  }
  if (at_least(1, 9)) ok = ok && in.Value(out.water_frame_interval);
  if (at_least(1, 5)) {
    ok = ok && in.Value(out.light_angle_y) && in.Value(out.light_angle_x) &&
         in.Bytes(out.diffuse.data(), 12) && in.Bytes(out.ambient.data(), 12);
  }
  if (at_least(1, 7)) ok = ok && in.Skip(4);
  if (at_least(1, 6)) ok = ok && in.Bytes(out.field_1b0.data(), 16);
  std::int32_t count = 0;
  ok = ok && in.Value(count);
  if (!ok) return Fail(error, "the file is truncated");
  out.light_vector = WorldLightVector(out.light_angle_y, out.light_angle_x);

  // Every object starts with a 4-byte type.
  if (count > 0 && static_cast<std::size_t>(count) > in.remaining() / 4)
    return Fail(error, "the objects run past the file");
  for (std::int32_t i = 0; i < count; ++i) {
    std::int32_t type = 0;
    if (!in.Value(type)) return Fail(error, "the file is truncated");
    switch (type) {
      case 1: {
        WorldModel model;
        model.name = last_name;
        if (at_least(1, 3)) {
          if (!read_name(&model.name)) return false;
          if (!in.Value(model.field_ec) || !in.Value(model.animation_speed) ||
              !in.Value(model.field_f0)) {
            return Fail(error, "the file is truncated");
          }
          // Above 100 (005e2ad8), or not above 0 (005e2660), NaN included: 1.0.
          if (model.animation_speed > 100.0f || !(model.animation_speed > 0.0f))
            model.animation_speed = 1.0f;
        }
        std::uint8_t record[kModelSize];
        if (!in.Bytes(record, kModelSize)) return Fail(error, "the file is truncated");
        if (!CString(record, kModelNameSize, model.file) ||
            !CString(record + kModelNameSize, kModelNameSize, model.node)) {
          return Fail(error, "a model name without a NUL");
        }
        std::memcpy(model.position.data(), record + 0xa0, 12);
        std::memcpy(model.rotation.data(), record + 0xac, 12);
        std::memcpy(model.scale.data(), record + 0xb8, 12);
        out.models.push_back(std::move(model));
        break;
      }
      case 2:
        if (!in.Skip(kLightSize)) return Fail(error, "the file is truncated");
        break;
      case 3: {
        std::array<std::uint8_t, 0xc0> sound{};
        if (major >= 2) {
          if (!in.Bytes(sound.data(), sound.size())) return Fail(error, "the file is truncated");
        } else {
          // Two names, cut to 79 characters by strncpy, the rest as read, and 4.0.
          std::uint8_t record[kOldSoundSize];
          if (!in.Bytes(record, kOldSoundSize)) return Fail(error, "the file is truncated");
          for (std::size_t field : {0u, 0x50u}) {
            const std::uint8_t* first = record + field;
            std::copy(first, std::find(first, first + 0x4f, std::uint8_t{0}), sound.data() + field);
          }
          std::memcpy(sound.data() + 0xa0, record + 0xa0, 0x1c);
          const float four = 4.0f;
          std::memcpy(sound.data() + 0xbc, &four, 4);
        }
        out.sounds.push_back(sound);
        break;
      }
      case 4: {
        std::array<std::uint8_t, 0x74> effect{};
        if (!in.Bytes(effect.data(), effect.size())) return Fail(error, "the file is truncated");
        out.effects.push_back(effect);
        break;
      }
      default:
        // "not implemented type == %d (%d/%d)" (0060657c), and nothing more is read.
        break;
    }
  }

  if (major == 2 && minor == 1) {
    out.bounds.resize(kBoundsCount);
    for (WorldBounds& node : out.bounds) {
      if (!in.Bytes(node.max.data(), 12) || !in.Bytes(node.min.data(), 12) ||
          !in.Bytes(node.half_extent.data(), 12) || !in.Bytes(node.centre.data(), 12)) {
        return Fail(error, "the file is truncated");
      }
    }
  }

  if (!lightmaps) {
    for (float& c : out.ambient) {
      const double brighter = static_cast<double>(c) * 1.5;  // 005e2ad0
      c = brighter > 1.0 ? 1.0f : static_cast<float>(brighter);
    }
  }
  return true;
}

std::unique_ptr<Resource> RswRes::Clone() const { return std::make_unique<RswRes>(lightmaps_); }

bool RswRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "cannot open " + file.path();
    return false;
  }
  error_.clear();
  return DecodeWorld(file.data(), lightmaps_, world_, &error_);
}

void RswRes::Reset() {
  // Slot 4 (00435c90) frees the objects and the scene graph; the header values stay.
  world_.models.clear();
  world_.sounds.clear();
  world_.effects.clear();
  world_.bounds.clear();
}

void RegisterWorldTypes(ResourceManager& manager, bool lightmaps) {
  manager.RegisterType("rsw", "", std::make_unique<RswRes>(lightmaps));
}

}  // namespace ragexe::res
