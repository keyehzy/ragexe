#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/crt_rand.h"
#include "render/camera.h"
#include "render/effect_prim.h"
#include "render/effect_table.h"

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
  std::uint32_t texture, flags;
};

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

#include "prim_vectors.inc"

constexpr std::uint32_t kSyntheticTextures = 0x7100;  // the script's stand-in pointers
constexpr std::uint32_t kSyntheticSeed = 500;         // srand before the n-th primitive's frames
constexpr std::uint32_t kTranslucent = 1;

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
    default: FAIL("unknown field " << f.field);
  }
}

void CheckTriangles(const std::vector<PrimTriangle>& got, const TriangleVector* want,
                    std::size_t count, const std::map<std::string, std::uint32_t>& ids) {
  REQUIRE(got.size() == count);
  for (std::size_t t = 0; t < count; ++t) {
    INFO("triangle " << t);
    CHECK(want[t].flags == kTranslucent);
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
      const bool more = UpdatePrimitive(p, rand);
      CHECK(more == (fv.more != 0));
      const std::vector<std::uint32_t> state = StateOf(p);
      for (std::size_t i = 0; i < kStateSize; ++i) {
        INFO("state " << i);
        CHECK(state[i] == fv.state[i]);
      }
      std::vector<PrimTriangle> triangles;
      if (more) DrawPrimitive(p, triangles);
      CheckTriangles(triangles, fv.triangles, fv.count, ids);
      ++owner.frame;
    }
  }
}

TEST_CASE("The hit spark plays as the original's", "[effect]") {
  std::map<std::string, std::uint32_t> ids;
  for (const TextureVector& t : kTextures) ids[t.name] = t.id;
  const Projection projection = SetSize(800, 600);
  std::size_t n = 0;
  for (const SparkVector& v : kSparks) {
    INFO("run " << n++);
    EffectWorld world(v.seed);
    RagEffect& effect = world.Launch(1, {F(v.position[0]), F(v.position[1]), F(v.position[2])});
    effect.duration = v.duration;
    const RagEffect* alive = &effect;
    std::map<const EffectPrim*, std::size_t> order;
    Matrix view{};
    std::size_t next_view = 0;
    for (std::size_t f = 0; f < v.frame_count; ++f) {
      INFO("frame " << f);
      if (next_view < v.view_count && std::cmp_equal(v.views[next_view].frame, f)) {
        for (std::size_t i = 0; i < 12; ++i) view[i] = F(v.views[next_view].view[i]);
        ++next_view;
      }
      const SparkFrameVector& fv = v.frames[f];
      world.Step(view, projection);
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
      CheckTriangles(world.Draw(), fv.triangles, fv.count, ids);
    }
    CHECK(world.empty());
  }
}
