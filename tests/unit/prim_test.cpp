#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/crt_rand.h"
#include "render/camera.h"
#include "render/effect_prim.h"
#include "render/effect_table.h"
#include "res/act.h"
#include "res/gat.h"
#include "res/sprite.h"

using namespace ragexe::render;

namespace {

struct TableVector {
  int id, minimal;
  std::uint32_t seed;
  int duration;
  const char* name;
};

struct FieldVector {
  std::uint32_t field, value;
  std::uint32_t values[4];  // a key vector's, value of them
};

struct TriangleVector {
  std::uint32_t vertices[24];  // three of x, y, z, rhw, diffuse, specular, u, v
  std::uint32_t texture, src_blend, dest_blend, mode, flags;
};

// The count of a frame whose triangles the script did not record.
constexpr std::size_t kUnchecked = static_cast<std::size_t>(-1);

constexpr std::size_t kStateSize = 31;

struct PrimFrameVector {
  std::uint32_t screen[2];
  int more;
  std::uint32_t state[kStateSize];
  const TriangleVector* triangles;
  std::size_t count;
};

struct PrimVector {
  int id, visible;
  std::uint32_t position[3], offset[3];
  int duration, frame;
  const FieldVector* fields;
  std::size_t field_count;
  const PrimFrameVector* frames;
  std::size_t frame_count;
};

struct SparkPrimVector {
  std::size_t index;
  std::uint32_t state[kStateSize];
};

struct SparkFrameVector {
  int more;
  std::uint32_t screen[2];
  const SparkPrimVector* prims;
  std::size_t prim_count;
  const TriangleVector* triangles;
  std::size_t count;
};

struct ViewVector {
  int frame;
  std::uint32_t view[12];
};

struct SparkVector {
  std::uint32_t seed;
  std::uint32_t position[3];
  int duration;
  const SparkFrameVector* frames;
  std::size_t frame_count;
  const ViewVector* views;
  std::size_t view_count;
};

struct TextureVector {
  std::uint32_t id;
  const char* name;
};

struct QuadVector {
  std::uint32_t desc[8];  // top, left, bottom, right, rhw bottom, rhw top, colour, angle
  int type, image;
  std::uint32_t flags;
};

struct SceneData {
  std::uint32_t view[12];
  std::uint32_t yaw, slope_down, pitch;
};

struct ShapeFrameVector {
  int more;
  const std::uint32_t* state;
  std::size_t state_count;
  const TriangleVector* triangles;
  std::size_t count;
  const QuadVector* quads;
  std::size_t quad_count;
};

struct ShapeVector {
  int id;
  std::uint32_t position[3], target[3], offset[3];
  int duration;
  SceneData scene;
  const FieldVector* fields;
  std::size_t field_count;
  const ShapeFrameVector* frames;
  std::size_t frame_count;
};

struct BurstPrimVector {
  std::size_t index;
  int id;
  const std::uint32_t* state;
  std::size_t count;
};

struct BurstFrameVector {
  int more;
  const BurstPrimVector* prims;
  std::size_t prim_count;
  const TriangleVector* triangles;
  std::size_t count;
  const QuadVector* quads;
  std::size_t quad_count;
};

struct SceneVector {
  int frame;
  SceneData scene;
};

struct BurstVector {
  int id, minimal;
  std::uint32_t seed;
  std::uint32_t position[3];
  std::uint32_t facing;
  int duration;
  const BurstFrameVector* frames;
  std::size_t frame_count;
  const SceneVector* scenes;
  std::size_t scene_count;
};

struct SquareEffectVector {
  int id, level;
  std::uint32_t seed;
  std::uint32_t position[3], player[3], target[3];
  int duration;
  const BurstFrameVector* frames;
  std::size_t frame_count;
  const SceneVector* scenes;
  std::size_t scene_count;
};

struct ImageVector {
  std::int16_t width, height;
  int halved_x, halved_y;
};

struct ItemVector {
  int action, frame, x, y, image;
  std::uint32_t flags;
  int r, g, b, a;
  std::uint32_t scale_x, scale_y;
  int angle, type;
};

#include "prim_vectors.inc"

constexpr std::uint32_t kSyntheticTextures = 0x7100;  // the script's stand-in pointers
constexpr std::uint32_t kSyntheticSeed = 500;         // srand before the n-th primitive's frames
constexpr std::uint32_t kShapeSeed = 700;             // the same for the shapes
constexpr std::uint32_t kSquareSeed = 4000;           // and for the facing squares
constexpr std::uint32_t kMoteSeed = 7000;             // and for the motes

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }
std::uint32_t U(std::int32_t i) { return static_cast<std::uint32_t>(i); }

// The fields the script records, in its order.
std::vector<std::uint32_t> StateOf(const EffectPrim& p) {
  return {U(p.image),
          p.color,
          U(p.period),
          U(p.key),
          p.flags,
          U(p.age),
          U(p.red),
          U(p.green),
          U(p.blue),
          p.fading ? 1u : 0u,
          B(p.position[0]),
          B(p.position[1]),
          B(p.speed),
          B(p.acceleration),
          B(p.alpha_max),
          B(p.heading),
          B(p.angle),
          B(p.alpha),
          B(p.alpha_speed),
          B(p.heading_speed),
          B(p.spin),
          B(p.distance),
          B(p.distance_speed),
          B(p.width),
          B(p.width_speed),
          B(p.height),
          B(p.height_speed),
          B(p.start[0]),
          B(p.start[1]),
          B(p.travel[0]),
          B(p.travel[1])};
}

std::vector<float> Floats(const FieldVector& f) {
  std::vector<float> out(f.value);
  for (std::size_t i = 0; i < f.value; ++i) out[i] = F(f.values[i]);
  return out;
}

constexpr std::uint32_t kRingBase = 0x156b0;  // the ring records, 0xb8 bytes each
constexpr std::uint32_t kRingSize = 0xb8;

std::int16_t Low(std::uint32_t v) { return static_cast<std::int16_t>(v & 0xffff); }
std::int16_t High(std::uint32_t v) { return static_cast<std::int16_t>(v >> 16); }

// A dword of a ring record, at its offset from the first record.
void ApplyRing(std::uint32_t offset, std::uint32_t v, EffectPrim& p) {
  PrimRing& r = p.rings[offset / kRingSize];
  const std::uint32_t o = offset % kRingSize;
  switch (o) {
    case 0:
      r.active = (v & 0xff) != 0;
      r.span = High(v);
      break;
    case 4: r.height = F(v); break;
    case 8: r.age = static_cast<std::int32_t>(v); break;
    case 0xc:
      r.angle = Low(v);
      r.alpha = High(v);
      break;
    case 0x10: r.kind = Low(v); break;
    case 0x68: r.tilt = Low(v); break;
    case 0x6c: r.radius = F(v); break;
    default:
      if (o >= 0x70) {
        for (std::uint32_t b = 0; b < 4 && o - 0x70 + b < r.flags.size(); ++b)
          r.flags[o - 0x70 + b] = static_cast<std::uint8_t>(v >> (8 * b));
      } else {
        r.heights[(o - 0x14) / 4] = F(v);
      }
      break;
  }
}

// The position, the age and the first 0x88 bytes of each ring record, as the script reads
// them.
std::vector<std::uint32_t> RingStateOf(const EffectPrim& p) {
  std::vector<std::uint32_t> out = {B(p.position[0]), B(p.position[1]), B(p.position[2]), U(p.age)};
  const auto pair = [](std::int16_t lo, std::int16_t hi) {
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(lo)) |
           static_cast<std::uint32_t>(static_cast<std::uint16_t>(hi)) << 16;
  };
  for (const PrimRing& r : p.rings) {
    out.push_back(pair(r.active ? 1 : 0, r.span));
    out.push_back(B(r.height));
    out.push_back(U(r.age));
    out.push_back(pair(r.angle, r.alpha));
    out.push_back(pair(r.kind, 0));
    for (float h : r.heights) out.push_back(B(h));
    out.push_back(pair(r.tilt, 0));
    out.push_back(B(r.radius));
    for (std::size_t b = 0; b < 24; b += 4) {
      std::uint32_t word = 0;
      for (std::size_t i = 0; i < 4 && b + i < r.flags.size(); ++i)
        word |= static_cast<std::uint32_t>(r.flags[b + i]) << (8 * i);
      out.push_back(word);
    }
  }
  return out;
}

bool IsSquare(std::int32_t id) {
  switch (id) {
    case 0x1f:
    case 0x28:
    case 0x2b:
    case 0x2d:
    case 0x2f:
    case 0x33:
    case 0x37:
    case 0x38: return true;
    default: return false;
  }
}

// A dword of a record as the facing squares read it, at its offset from the first record.
void ApplySquare(std::uint32_t offset, std::uint32_t v, EffectPrim& p) {
  PrimSquare& s = p.squares[offset / kRingSize];
  switch (offset % kRingSize) {
    case 0:
      s.active = (v & 0xff) != 0;
      s.turn = High(v);
      break;
    case 4: s.range = F(v); break;
    case 8: s.age = static_cast<std::int32_t>(v); break;
    case 0xc:
      s.heading = Low(v);
      s.alpha = High(v);
      break;
    case 0x10: s.view_angle = Low(v); break;
    case 0x14: s.walk_x = F(v); break;
    case 0x18: s.speed = F(v); break;
    case 0x1c: s.climb = F(v); break;
    case 0x20: s.walk_z_goal = F(v); break;
    case 0x2c: s.floor = F(v); break;
    case 0x30: s.mode = F(v); break;
    case 0x34: s.phase = F(v); break;
    case 0x6c: s.size = F(v); break;
    case 0x70:
      s.image = static_cast<std::uint8_t>(v);
      s.state = static_cast<std::uint8_t>(v >> 8);
      s.color = static_cast<std::uint8_t>(v >> 16);
      s.shade = static_cast<std::uint8_t>(v >> 24);
      break;
    case 0x88: s.center[0] = F(v); break;
    case 0x8c: s.center[1] = F(v); break;
    case 0x90: s.center[2] = F(v); break;
    case 0xa4: s.base = F(v); break;
    default: FAIL("unknown square field " << offset); break;
  }
}

// The position, the age and the first 0xa8 bytes of each record as the facing squares
// lay them out, as the script reads them.
std::vector<std::uint32_t> SquareStateOf(const EffectPrim& p) {
  std::vector<std::uint32_t> out = {B(p.position[0]), B(p.position[1]), B(p.position[2]), U(p.age)};
  for (const PrimSquare& s : p.squares) {
    std::array<std::uint8_t, 0xa8> bytes{};
    const auto put = [&bytes](std::size_t at, const auto& value) {
      std::memcpy(bytes.data() + at, &value, sizeof(value));
    };
    put(0, static_cast<std::uint8_t>(s.active ? 1 : 0));
    put(2, s.turn);
    put(4, s.range);
    put(8, s.age);
    put(0xc, s.heading);
    put(0xe, s.alpha);
    put(0x10, s.view_angle);
    put(0x14, s.walk_x);
    put(0x18, s.speed);
    put(0x1c, s.climb);
    put(0x20, s.walk_z_goal);
    put(0x2c, s.floor);
    put(0x30, s.mode);
    put(0x34, s.phase);
    put(0x6c, s.size);
    put(0x70, s.image);
    put(0x71, s.state);
    put(0x72, s.color);
    put(0x73, s.shade);
    put(0x88, s.center);
    put(0xa4, s.base);
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
      std::uint32_t word = 0;
      std::memcpy(&word, bytes.data() + i, 4);
      out.push_back(word);
    }
  }
  return out;
}

bool IsMote(std::int32_t id) { return id == 0x1b || id == 0x1e || id == 0x21; }

// A dword of a record as ids 0x1b, 0x1e and 0x21 read it.
void ApplyMote(std::uint32_t offset, std::uint32_t v, EffectPrim& p) {
  PrimMotes& r = p.motes[offset / kRingSize];
  const std::uint32_t o = offset % kRingSize;
  if (o == 0) {
    r.active = (v & 0xff) != 0;
  } else if (o == 4) {
    r.speed = F(v);
  } else if (o == 8) {
    r.age = static_cast<std::int32_t>(v);
  } else if (o == 0xc) {
    r.alpha = High(v);
  } else if (o == 0x10) {
    r.view_angle = Low(v);
  } else if (o >= 0x14 && o < 0x68) {
    r.values[(o - 0x14) / 4] = F(v);
  } else if (o == 0x6c) {
    r.size = F(v);
  } else if (o >= 0x88) {
    r.points[(o - 0x88) / 0xc][(o - 0x88) % 0xc / 4] = F(v);
  } else {
    FAIL("unknown mote field " << offset);
  }
}

// The position, the age and each whole record as the motes lay them out; 0x21's motes' x
// and z, which its drawing writes, are left out as the script leaves them.
std::vector<std::uint32_t> MotesStateOf(const EffectPrim& p) {
  std::vector<std::uint32_t> out = {B(p.position[0]), B(p.position[1]), B(p.position[2]), U(p.age)};
  for (const PrimMotes& r : p.motes) {
    std::array<std::uint8_t, kRingSize> bytes{};
    const auto put = [&bytes](std::size_t at, const auto& value) {
      std::memcpy(bytes.data() + at, &value, sizeof(value));
    };
    put(0, static_cast<std::uint8_t>(r.active ? 1 : 0));
    put(4, r.speed);
    put(8, r.age);
    put(0xe, r.alpha);
    put(0x10, r.view_angle);
    put(0x14, r.values);
    put(0x6c, r.size);
    for (std::size_t k = 0; k < 4; ++k) {
      Vec3 m = r.points[k];
      if (p.id == 0x21) m[0] = m[2] = 0.0f;
      put(0x88 + 0xc * k, m);
    }
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
      std::uint32_t word = 0;
      std::memcpy(&word, bytes.data() + i, 4);
      out.push_back(word);
    }
  }
  return out;
}

std::vector<std::uint32_t> StateFor(const EffectPrim& p) {
  if (IsSquare(p.id)) return SquareStateOf(p);
  return IsMote(p.id) ? MotesStateOf(p) : RingStateOf(p);
}

void Apply(const FieldVector& f, EffectPrim& p) {
  const auto i = static_cast<std::int32_t>(f.value);
  const float v = F(f.value);
  switch (f.field) {
    case 0x13c: p.flags = f.value; break;
    case 0x144: p.life = i; break;
    case 0x148: p.image_delay = i; break;
    case 0x134: p.period = i; break;
    case 0x150: p.fade_at = i; break;
    case 0x160: p.change_at = i; break;
    case 0x1b4: p.image_loop = i != 0; break;
    case 0x164: p.image_count = i; break;
    case 0x19c: p.red = i; break;
    case 0x1a0: p.green = i; break;
    case 0x1a4: p.blue = i; break;
    case 0x1a8: p.red_speed = i; break;
    case 0x1ac: p.green_speed = i; break;
    case 0x1b0: p.blue_speed = i; break;
    case 0x1ec: p.speed = v; break;
    case 0x1f0: p.acceleration = v; break;
    case 0x23c: p.heading = v; break;
    case 0x244: p.angle = v; break;
    case 0x254: p.alpha = v; break;
    case 0x258: p.alpha_speed = v; break;
    case 0x25c: p.fade_scale = v; break;
    case 0x204: p.alpha_max = v; break;
    case 0x208: p.alpha_min = v; break;
    case 0x268: p.spin = v; break;
    case 0x274: p.spin_rate = v; break;
    case 0x284: p.width = v; break;
    case 0x288: p.width_speed = v; break;
    case 0x28c: p.width_rate = v; break;
    case 0x290: p.height = v; break;
    case 0x294: p.height_speed = v; break;
    case 0x298: p.height_rate = v; break;
    case 0x260: p.heading_speed = v; break;
    case 0x26c: p.heading_rate = v; break;
    case 0x278: p.distance = v; break;
    case 0x27c: p.distance_speed = v; break;
    case 0x280: p.distance_rate = v; break;
    case 0x1f8: p.changes[0] = f.value; break;
    case 0x1fc: p.changes[1] = f.value; break;
    case 0x1cc: p.changes[2] = f.value; break;
    case 0x1f4: p.changes[3] = f.value; break;
    case 0x200: p.changes[4] = f.value; break;
    case 0x1d0: p.changes[5] = f.value; break;
    case 0x2a4: p.key_frames = Floats(f); break;
    case 0x2b4: p.key_values = Floats(f); break;
    case 0x2c4: p.key_limits = Floats(f); break;
    case 0xb4: p.speed_keys = i != 0; break;
    case 0xb8: p.alpha_keys = i != 0; break;
    case 0x138: p.key = i; break;
    case 0x240: p.pitch = v; break;
    case 0x14c: p.move_after = i; break;
    case 0x248: p.scale = v; break;
    case 0x24c: p.scale_speed = v; break;
    case 0x250: p.scale_rate = v; break;
    case 0x38: p.facing = v; break;
    case 0x24: p.action = i; break;
    case 0x28: p.image = i; break;
    case 0x128: p.quad_flags = f.value; break;
    case 0x168: p.coin_sound = i == 1; break;
    case 0x21c: p.fall_speed = v; break;
    case 0x220: p.fall_rate = v; break;
    case 0x154: p.history_count = i; break;
    case 0x224: p.bottom_radius = v; break;
    case 0x228: p.top_radius = v; break;
    case 0x22c: p.bottom_radius_speed = v; break;
    case 0x230: p.top_radius_speed = v; break;
    case 0x234: p.bottom_radius_rate = v; break;
    case 0x238: p.top_radius_rate = v; break;
    case 0x264: p.pitch_speed = v; break;
    case 0x270: p.pitch_rate = v; break;
    case 0x29c: p.max_height = v; break;
    case 0x1b8: p.sweep = v; break;
    default:
      if (f.field >= kRingBase && f.field < kRingBase + 4 * kRingSize) {
        if (IsSquare(p.id)) {
          ApplySquare(f.field - kRingBase, f.value, p);
        } else if (IsMote(p.id)) {
          ApplyMote(f.field - kRingBase, f.value, p);
        } else {
          ApplyRing(f.field - kRingBase, f.value, p);
        }
      } else if (f.field >= 0x2f8 && f.field < 0x304) {
        p.travel[(f.field - 0x2f8) / 4] = F(f.value);
      } else if (f.field >= 0xc4 && f.field < 0xf4) {
        p.matrix[(f.field - 0xc4) / 4] = v;
      } else if (f.field >= 0x1d4 && f.field < 0x1ec) {
        p.step_changes[(f.field - 0x1d4) / 4] = f.value;
      } else {
        FAIL("unknown field " << f.field);
      }
  }
}

// The fields of ids 5, 6 and 0xd the script records, then the matrix and the history.
std::vector<std::uint32_t> StateOf3(const EffectPrim& p) {
  std::vector<std::uint32_t> out = {B(p.position[0]),
                                    B(p.position[1]),
                                    B(p.position[2]),
                                    U(p.image),
                                    p.color,
                                    U(p.period),
                                    p.flags,
                                    U(p.age),
                                    B(p.speed),
                                    B(p.fall_speed),
                                    B(p.bottom_radius),
                                    B(p.top_radius),
                                    B(p.bottom_radius_speed),
                                    B(p.top_radius_speed),
                                    B(p.heading),
                                    B(p.pitch),
                                    B(p.angle),
                                    B(p.scale),
                                    B(p.scale_speed),
                                    B(p.alpha),
                                    B(p.alpha_speed),
                                    B(p.heading_speed),
                                    B(p.pitch_speed),
                                    B(p.height),
                                    B(p.height_speed),
                                    B(p.step[0]),
                                    B(p.step[1]),
                                    B(p.step[2]),
                                    B(p.start[0]),
                                    B(p.start[1]),
                                    B(p.start[2]),
                                    B(p.travel[0]),
                                    B(p.travel[1]),
                                    B(p.travel[2]),
                                    B(p.drift[0]),
                                    B(p.drift[2]),
                                    p.fading ? 1u : 0u};
  for (float m : p.matrix) out.push_back(B(m));
  for (std::int32_t k = 0; k < p.history_count; ++k) {
    const PrimHistory& h = p.history[static_cast<std::size_t>(k)];
    for (float x : h.position) out.push_back(B(x));
    for (float x : h.points) out.push_back(B(x));
    out.push_back(B(h.distance));
    out.push_back(B(h.scale));
    out.push_back(B(h.heading));
    for (float x : h.matrix) out.push_back(B(x));
    out.push_back(U(h.red));
    out.push_back(U(h.green));
    out.push_back(U(h.blue));
    out.push_back(B(h.alpha));
    out.push_back(h.color);
  }
  return out;
}

// The synthetic attribute map of tools/actor_vectors.py.
ragexe::res::AttrMap TestAttr() {
  ragexe::res::AttrMap attr;
  attr.width = 40;
  attr.height = 40;
  attr.cell_size = 5;
  for (int c = 0; c < 40 * 40; ++c) {
    ragexe::res::AttrCell cell;
    for (int k = 0; k < 4; ++k)
      cell.heights[static_cast<std::size_t>(k)] = static_cast<float>((c * 11 + k * 5) % 23 - 15);
    attr.cells.push_back(cell);
  }
  return attr;
}

ragexe::res::Sprite TestSprite() {
  ragexe::res::Sprite s;
  for (const ImageVector& im : kImages8) {
    ragexe::res::SpriteImage i;
    i.width = im.width;
    i.height = im.height;
    i.halved_x = im.halved_x != 0;
    i.halved_y = im.halved_y != 0;
    s.images.push_back(i);
  }
  for (const ImageVector& im : kImages32) {
    ragexe::res::SpriteImage32 i;
    i.width = im.width;
    i.height = im.height;
    i.halved_x = im.halved_x != 0;
    i.halved_y = im.halved_y != 0;
    s.images32.push_back(i);
  }
  return s;
}

ragexe::res::Act TestAct() {
  ragexe::res::Act act;
  for (int frames : kActionFrames) act.actions.emplace_back(static_cast<std::size_t>(frames));
  for (const ItemVector& v : kItems) {
    ragexe::res::ActItem item;
    item.x = v.x;
    item.y = v.y;
    item.image = v.image;
    item.flags = v.flags;
    item.color = {static_cast<std::uint8_t>(v.r), static_cast<std::uint8_t>(v.g),
                  static_cast<std::uint8_t>(v.b), static_cast<std::uint8_t>(v.a)};
    item.scale_x = F(v.scale_x);
    item.scale_y = F(v.scale_y);
    item.angle = v.angle;
    item.image_type = v.type;
    act.actions[static_cast<std::size_t>(v.action)][static_cast<std::size_t>(v.frame)]
        .items.push_back(item);
  }
  return act;
}

PrimScene SceneOf(const SceneData& d, const ragexe::res::AttrMap* attr) {
  PrimScene scene;
  for (std::size_t i = 0; i < 12; ++i) scene.view[i] = F(d.view[i]);
  scene.view_yaw = F(d.yaw);
  scene.view_pitch = F(d.pitch);
  scene.slope_down = F(d.slope_down);
  scene.projection = SetSize(800, 600);
  scene.attr = attr;
  return scene;
}

void CheckState(const std::vector<std::uint32_t>& got, const std::uint32_t* want,
                std::size_t count) {
  REQUIRE(got.size() == count);
  for (std::size_t i = 0; i < count; ++i) {
    INFO("state " << i);
    CHECK(got[i] == want[i]);
  }
}

void CheckQuads(const std::vector<SpriteQuad>& got, const QuadVector* want, std::size_t count) {
  REQUIRE(got.size() == count);
  for (std::size_t q = 0; q < count; ++q) {
    INFO("quad " << q);
    const SpriteQuad& g = got[q];
    const QuadVector& w = want[q];
    CHECK(B(g.top) == w.desc[0]);
    CHECK(B(g.left) == w.desc[1]);
    CHECK(B(g.bottom) == w.desc[2]);
    CHECK(B(g.right) == w.desc[3]);
    CHECK(B(g.rhw_bottom) == w.desc[4]);
    CHECK(B(g.rhw_top) == w.desc[5]);
    CHECK(g.color == w.desc[6]);
    CHECK(B(g.angle) == w.desc[7]);
    CHECK(g.image_type == w.type);
    CHECK(g.image == w.image);
    CHECK(g.flags == w.flags);
  }
}

void CheckTriangles(const std::vector<PrimTriangle>& got, const TriangleVector* want,
                    std::size_t count, const std::map<std::string, std::uint32_t>& ids) {
  if (count == kUnchecked) return;
  REQUIRE(got.size() == count);
  for (std::size_t t = 0; t < count; ++t) {
    INFO("triangle " << t);
    CHECK(got[t].flags == want[t].flags);
    CHECK(got[t].src_blend == want[t].src_blend);
    CHECK(got[t].dest_blend == want[t].dest_blend);
    CHECK(got[t].mode == want[t].mode);
    const auto id = ids.find(got[t].texture);
    REQUIRE(id != ids.end());
    CHECK(id->second == want[t].texture);
    for (std::size_t k = 0; k < 3; ++k) {
      INFO("vertex " << k);
      const ScreenVertex& sv = got[t].vertices[k];
      const std::uint32_t* w = want[t].vertices + k * 8;
      CHECK(B(sv.x) == w[0]);
      CHECK(B(sv.y) == w[1]);
      CHECK(B(sv.z) == w[2]);
      CHECK(B(sv.rhw) == w[3]);
      CHECK(sv.diffuse == w[4]);
      CHECK(sv.specular == w[5]);
      CHECK(B(sv.u) == w[6]);
      CHECK(B(sv.v) == w[7]);
    }
  }
}

}  // namespace

TEST_CASE("The CRT's rand is MSVC's", "[effect]") {
  ragexe::core::CrtRand rand;
  CHECK(rand.Next() == 41);
  CHECK(rand.Next() == 18467);
  CHECK(rand.Next() == 6334);
}

TEST_CASE("Each effect id plays the original's .str and lasts as long", "[effect]") {
  for (const TableVector& v : kTable) {
    INFO("id " << v.id << " minimal " << v.minimal);
    ragexe::core::CrtRand rand(v.seed);
    CHECK(EffectStrName(v.id, v.minimal != 0, rand) == v.name);
    CHECK(EffectDuration(v.id) == v.duration);
  }
  ragexe::core::CrtRand rand;
  CHECK(EffectStrName(kEffectIdCount, false, rand).empty());
  CHECK(EffectDuration(kEffectIdCount) == 0);
}

TEST_CASE("Primitives 0 to 2 step and draw as the original's", "[effect]") {
  std::map<std::string, std::uint32_t> ids;
  for (std::uint32_t i = 0; i < 4; ++i) ids["t" + std::to_string(i)] = kSyntheticTextures + i;
  std::size_t n = 0;
  for (const PrimVector& v : kPrims) {
    INFO("primitive " << n++ << ", id " << v.id);
    RagEffect owner;
    for (std::size_t i = 0; i < 3; ++i) owner.position[i] = F(v.position[i]);
    owner.duration = v.duration;
    owner.frame = v.frame;
    EffectPrim p = MakePrimitive(&owner, v.id, {F(v.offset[0]), F(v.offset[1]), F(v.offset[2])});
    CHECK(p.visible == (v.visible != 0));
    for (std::size_t f = 0; f < v.field_count; ++f) Apply(v.fields[f], p);
    p.textures.clear();
    for (std::int32_t i = 0; i < p.image_count; ++i) p.textures.push_back("t" + std::to_string(i));
    ragexe::core::CrtRand rand(kSyntheticSeed + static_cast<std::uint32_t>(n - 1));
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      const PrimFrameVector& fv = v.frames[f];
      owner.screen_x = F(fv.screen[0]);
      owner.screen_y = F(fv.screen[1]);
      const bool more = UpdatePrimitive(p, rand, PrimScene{});
      CHECK(more == (fv.more != 0));
      const std::vector<std::uint32_t> state = StateOf(p);
      for (std::size_t i = 0; i < kStateSize; ++i) {
        INFO("state " << i);
        CHECK(state[i] == fv.state[i]);
      }
      PrimDrawing drawing;
      if (more) DrawPrimitive(p, PrimScene{}, drawing);
      CheckTriangles(drawing.triangles, fv.triangles, fv.count, ids);
      ++owner.frame;
    }
  }
}

TEST_CASE("The hit spark plays as the original's", "[effect]") {
  std::map<std::string, std::uint32_t> ids;
  for (const TextureVector& t : kTextures) ids[t.name] = t.id;
  PrimScene scene;
  scene.projection = SetSize(800, 600);
  std::size_t n = 0;
  for (const SparkVector& v : kSparks) {
    INFO("run " << n++);
    EffectWorld world(v.seed);
    RagEffect& effect =
        world.Launch(1, {{F(v.position[0]), F(v.position[1]), F(v.position[2])}, 0.0f});
    effect.duration = v.duration;
    const RagEffect* alive = &effect;
    std::map<const EffectPrim*, std::size_t> order;
    std::size_t next_view = 0;
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      if (next_view < v.view_count && std::cmp_equal(v.views[next_view].frame, f)) {
        for (std::size_t i = 0; i < 12; ++i) scene.view[i] = F(v.views[next_view].view[i]);
        ++next_view;
      }
      const SparkFrameVector& fv = v.frames[f];
      world.Step(scene);
      const EffectWorld::Object* effect_object = nullptr;
      std::vector<const EffectPrim*> prims;
      for (const EffectWorld::Object& o : world.objects()) {
        if (o.effect) {
          effect_object = &o;
        } else {
          order.try_emplace(o.prim.get(), order.size());
          prims.push_back(o.prim.get());
        }
      }
      if (alive != nullptr) {
        CHECK((effect_object != nullptr) == (fv.more != 0));
        if (effect_object != nullptr) {
          CHECK(B(effect_object->effect->screen_x) == fv.screen[0]);
          CHECK(B(effect_object->effect->screen_y) == fv.screen[1]);
        } else {
          alive = nullptr;
        }
      }
      REQUIRE(prims.size() == fv.prim_count);
      for (std::size_t i = 0; i < prims.size(); ++i) {
        INFO("primitive " << i);
        CHECK(order[prims[i]] == fv.prims[i].index);
        const std::vector<std::uint32_t> state = StateOf(*prims[i]);
        for (std::size_t k = 0; k < kStateSize; ++k) {
          INFO("state " << k);
          CHECK(state[k] == fv.prims[i].state[k]);
        }
      }
      CheckTriangles(world.Draw(scene).triangles, fv.triangles, fv.count, ids);
    }
    CHECK(world.empty());
  }
}

TEST_CASE("Particles and cylinders step and draw as the original's", "[effect]") {
  std::map<std::string, std::uint32_t> ids;
  for (std::uint32_t i = 0; i < 4; ++i) ids["t" + std::to_string(i)] = kSyntheticTextures + i;
  const ragexe::res::AttrMap attr = TestAttr();
  const ragexe::res::Sprite sprite = TestSprite();
  const ragexe::res::Act act = TestAct();
  std::size_t n = 0;
  for (const ShapeVector& v : kShapes) {
    INFO("shape " << n << ", id " << v.id);
    RagEffect owner;
    for (std::size_t i = 0; i < 3; ++i) {
      owner.position[i] = F(v.position[i]);
      owner.target[i] = F(v.target[i]);
    }
    owner.duration = v.duration;
    EffectPrim p = MakePrimitive(&owner, v.id, {F(v.offset[0]), F(v.offset[1]), F(v.offset[2])});
    for (std::size_t f = 0; f < v.field_count; ++f) Apply(v.fields[f], p);
    p.textures.clear();
    for (std::int32_t i = 0; i < p.image_count; ++i) p.textures.push_back("t" + std::to_string(i));
    p.sprite = {&act, &sprite};
    ResetHistory(p);
    const PrimScene scene = SceneOf(v.scene, &attr);
    ragexe::core::CrtRand rand(kShapeSeed + static_cast<std::uint32_t>(n));
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      const ShapeFrameVector& fv = v.frames[f];
      const bool more = UpdatePrimitive(p, rand, scene);
      CHECK(more == (fv.more != 0));
      CheckState(StateOf3(p), fv.state, fv.state_count);
      PrimDrawing drawing;
      if (more) DrawPrimitive(p, scene, drawing);
      CheckTriangles(drawing.triangles, fv.triangles, fv.count, ids);
      CheckQuads(drawing.quads, fv.quads, fv.quad_count);
      ++owner.frame;
    }
    ++n;
  }
}

TEST_CASE("Effect ids 0 and 2 play as the original's", "[effect]") {
  std::map<std::string, std::uint32_t> ids;
  for (const TextureVector& t : kTextures) ids[t.name] = t.id;
  const ragexe::res::AttrMap attr = TestAttr();
  const ragexe::res::Sprite sprite = TestSprite();
  const ragexe::res::Act act = TestAct();
  std::size_t n = 0;
  for (const BurstVector& v : kBursts) {
    INFO("run " << n++ << ", id " << v.id);
    CHECK(EffectDuration(v.id) == v.duration);
    EffectWorld world(v.seed);
    world.SetMinimal(v.minimal != 0);
    world.SetParticle({&act, &sprite});
    world.Launch(v.id, {{F(v.position[0]), F(v.position[1]), F(v.position[2])}, F(v.facing)});
    std::map<const EffectPrim*, std::size_t> order;
    PrimScene scene;
    std::size_t next_scene = 0;
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      if (next_scene < v.scene_count && std::cmp_equal(v.scenes[next_scene].frame, f))
        scene = SceneOf(v.scenes[next_scene++].scene, &attr);
      const BurstFrameVector& fv = v.frames[f];
      world.Step(scene);
      std::vector<const EffectPrim*> prims;
      bool effect = false;
      for (const EffectWorld::Object& o : world.objects()) {
        if (o.effect) {
          effect = true;
        } else {
          order.try_emplace(o.prim.get(), order.size());
          prims.push_back(o.prim.get());
        }
      }
      CHECK(effect == (fv.more != 0));
      REQUIRE(prims.size() == fv.prim_count);
      for (std::size_t i = 0; i < prims.size(); ++i) {
        INFO("primitive " << i);
        CHECK(order[prims[i]] == fv.prims[i].index);
        CHECK(prims[i]->id == fv.prims[i].id);
        CheckState(StateOf3(*prims[i]), fv.prims[i].state, fv.prims[i].count);
      }
      const PrimDrawing drawing = world.Draw(scene);
      CheckTriangles(drawing.triangles, fv.triangles, fv.count, ids);
      CheckQuads(drawing.quads, fv.quads, fv.quad_count);
    }
    CHECK(world.empty());
  }
}

namespace {

void CheckRingShapes(std::span<const ShapeVector> shapes) {
  std::map<std::string, std::uint32_t> ids;
  for (std::uint32_t i = 0; i < 4; ++i) ids["t" + std::to_string(i)] = kSyntheticTextures + i;
  std::size_t n = 0;
  for (const ShapeVector& v : shapes) {
    INFO("ring primitive " << n++ << ", id " << v.id);
    RagEffect owner;
    for (std::size_t i = 0; i < 3; ++i) owner.position[i] = F(v.position[i]);
    owner.duration = v.duration;
    EffectPrim p = MakePrimitive(&owner, v.id, {0.0f, 0.0f, 0.0f});
    for (std::size_t f = 0; f < v.field_count; ++f) Apply(v.fields[f], p);
    p.textures = {"t0"};
    const PrimScene scene = SceneOf(v.scene, nullptr);
    ragexe::core::CrtRand rand;
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      const ShapeFrameVector& fv = v.frames[f];
      const bool more = UpdatePrimitive(p, rand, scene);
      CHECK(more == (fv.more != 0));
      CheckState(RingStateOf(p), fv.state, fv.state_count);
      PrimDrawing drawing;
      if (more) DrawPrimitive(p, scene, drawing);
      CheckTriangles(drawing.triangles, fv.triangles, fv.count, ids);
      ++owner.frame;
    }
  }
}

void CheckRingEffects(std::span<const BurstVector> effects) {
  std::map<std::string, std::uint32_t> ids;
  for (const TextureVector& t : kTextures) ids[t.name] = t.id;
  for (const BurstVector& v : effects) {
    INFO("effect id " << v.id);
    CHECK(EffectDuration(v.id) == v.duration);
    EffectWorld world(v.seed);
    world.Launch(v.id, {{F(v.position[0]), F(v.position[1]), F(v.position[2])}, 0.0f});
    std::map<const EffectPrim*, std::size_t> order;
    PrimScene scene;
    std::size_t next_scene = 0;
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      if (next_scene < v.scene_count && std::cmp_equal(v.scenes[next_scene].frame, f))
        scene = SceneOf(v.scenes[next_scene++].scene, nullptr);
      const BurstFrameVector& fv = v.frames[f];
      world.Step(scene);
      std::vector<const EffectPrim*> prims;
      bool effect = false;
      for (const EffectWorld::Object& o : world.objects()) {
        if (o.effect) {
          effect = true;
        } else {
          order.try_emplace(o.prim.get(), order.size());
          prims.push_back(o.prim.get());
        }
      }
      CHECK(effect == (fv.more != 0));
      REQUIRE(prims.size() == fv.prim_count);
      for (std::size_t i = 0; i < prims.size(); ++i) {
        INFO("primitive " << i);
        CHECK(order[prims[i]] == fv.prims[i].index);
        CHECK(prims[i]->id == fv.prims[i].id);
        CheckState(RingStateOf(*prims[i]), fv.prims[i].state, fv.prims[i].count);
      }
      CheckTriangles(world.Draw(scene).triangles, fv.triangles, fv.count, ids);
    }
  }
}

}  // namespace

TEST_CASE("Ring walls step and draw as the original's", "[effect]") { CheckRingShapes(kRings); }

TEST_CASE("The ring effects play as the original's", "[effect]") { CheckRingEffects(kRingEffects); }

TEST_CASE("Flat squares step and draw as the original's", "[effect]") {
  CheckRingShapes(kFlatSquares);
}

TEST_CASE("The flat square effects play as the original's", "[effect]") {
  CheckRingEffects(kFlatEffects);
}

TEST_CASE("Facing squares step and draw as the original's", "[effect]") {
  std::map<std::string, std::uint32_t> ids;
  for (std::uint32_t i = 0; i < 4; ++i) ids["t" + std::to_string(i)] = kSyntheticTextures + i;
  std::size_t n = 0;
  for (const ShapeVector& v : kSquares) {
    INFO("square primitive " << n << ", id " << v.id);
    RagEffect owner;
    for (std::size_t i = 0; i < 3; ++i) owner.position[i] = F(v.position[i]);
    owner.duration = v.duration;
    EffectPrim p = MakePrimitive(&owner, v.id, {0.0f, 0.0f, 0.0f});
    for (std::size_t f = 0; f < v.field_count; ++f) Apply(v.fields[f], p);
    p.textures = {"t0", "t1"};
    const PrimScene scene = SceneOf(v.scene, nullptr);
    ragexe::core::CrtRand rand(kSquareSeed + static_cast<std::uint32_t>(n));
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      const ShapeFrameVector& fv = v.frames[f];
      const bool more = UpdatePrimitive(p, rand, scene);
      CHECK(more == (fv.more != 0));
      CheckState(SquareStateOf(p), fv.state, fv.state_count);
      PrimDrawing drawing;
      if (more) DrawPrimitive(p, scene, drawing);
      CheckTriangles(drawing.triangles, fv.triangles, fv.count, ids);
      ++owner.frame;
    }
    ++n;
  }
}

namespace {

void CheckTargetEffects(std::span<const SquareEffectVector> effects) {
  std::map<std::string, std::uint32_t> ids;
  for (const TextureVector& t : kTextures) ids[t.name] = t.id;
  const auto vec = [](const std::uint32_t* v) { return Vec3{F(v[0]), F(v[1]), F(v[2])}; };
  for (const SquareEffectVector& v : effects) {
    INFO("effect id " << v.id);
    CHECK(EffectDuration(v.id) == v.duration);
    EffectWorld world(v.seed);
    world.SetPlayer(vec(v.player));
    RagEffect& e = world.Launch(v.id, {vec(v.position), 0.0f});
    e.target = vec(v.target);
    e.level = v.level;
    std::map<const EffectPrim*, std::size_t> order;
    PrimScene scene;
    std::size_t next_scene = 0;
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      if (next_scene < v.scene_count && std::cmp_equal(v.scenes[next_scene].frame, f))
        scene = SceneOf(v.scenes[next_scene++].scene, nullptr);
      const BurstFrameVector& fv = v.frames[f];
      world.Step(scene);
      std::vector<const EffectPrim*> prims;
      bool effect = false;
      for (const EffectWorld::Object& o : world.objects()) {
        if (o.effect) {
          effect = true;
        } else {
          order.try_emplace(o.prim.get(), order.size());
          prims.push_back(o.prim.get());
        }
      }
      CHECK(effect == (fv.more != 0));
      REQUIRE(prims.size() == fv.prim_count);
      for (std::size_t i = 0; i < prims.size(); ++i) {
        INFO("primitive " << i);
        CHECK(order[prims[i]] == fv.prims[i].index);
        CHECK(prims[i]->id == fv.prims[i].id);
        if (fv.prims[i].state == nullptr) continue;
        CheckState(StateFor(*prims[i]), fv.prims[i].state, fv.prims[i].count);
      }
      CheckTriangles(world.Draw(scene).triangles, fv.triangles, fv.count, ids);
    }
  }
}

}  // namespace

TEST_CASE("The square effects play as the original's", "[effect]") {
  CheckTargetEffects(kSquareEffects);
}

TEST_CASE("Motes step and draw as the original's", "[effect]") {
  std::map<std::string, std::uint32_t> ids;
  for (std::uint32_t i = 0; i < 5; ++i) ids["t" + std::to_string(i)] = kSyntheticTextures + i;
  std::size_t n = 0;
  for (const ShapeVector& v : kMotes) {
    INFO("mote primitive " << n << ", id " << v.id);
    RagEffect owner;
    for (std::size_t i = 0; i < 3; ++i) owner.position[i] = F(v.position[i]);
    owner.duration = v.duration;
    EffectPrim p = MakePrimitive(&owner, v.id, {0.0f, 0.0f, 0.0f});
    for (std::size_t f = 0; f < v.field_count; ++f) Apply(v.fields[f], p);
    p.textures = {"t0", "t1", "t2", "t3", "t4"};
    const PrimScene scene = SceneOf(v.scene, nullptr);
    ragexe::core::CrtRand rand(kMoteSeed + static_cast<std::uint32_t>(n));
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      const ShapeFrameVector& fv = v.frames[f];
      const bool more = UpdatePrimitive(p, rand, scene);
      CHECK(more == (fv.more != 0));
      CheckState(MotesStateOf(p), fv.state, fv.state_count);
      PrimDrawing drawing;
      if (more) DrawPrimitive(p, scene, drawing);
      CheckTriangles(drawing.triangles, fv.triangles, fv.count, ids);
      ++owner.frame;
    }
    ++n;
  }
}

TEST_CASE("The mote effects play as the original's", "[effect]") {
  CheckTargetEffects(kMoteEffects);
}
