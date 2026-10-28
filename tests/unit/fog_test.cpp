#include "render/fog.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "res/client_tables.h"

using namespace ragexe::render;

namespace {

struct HexVector {
  const char* text;
  std::size_t length;
  std::uint32_t value;
};

struct MapFogVector {
  const char* name;
  int on;                    // what 00516570 passes SetFogEnabled with the flag on
  int off;                   // and with it off
  std::uint32_t globals[4];  // colour, density, start, end; 0xdeadbeef where left alone
};

struct VertexFogVector {
  std::uint32_t start;
  std::uint32_t end;
  const std::uint32_t* specular;
};

struct SetupVector {
  int enabled;
  int vertex_fog;
  std::uint32_t state[6];  // mode, colour, range fog, density, start, end
  const std::uint32_t* calls;
  std::size_t count;
  int errors;
};

#include "fog_vectors.inc"

constexpr std::uint32_t kSentinel = 0xdeadbeef;

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }

}  // namespace

TEST_CASE("ParseHexDigits reads colours as the original does", "[fog]") {
  for (const HexVector& v : kHexVectors) {
    const std::string_view text(v.text, v.length);
    INFO("text " << text);
    CHECK(ParseHexDigits(text) == v.value);
  }
}

TEST_CASE("FindMapFog finds a map's fog as the original does", "[fog]") {
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(kFogTable);
  const ragexe::res::LineRecords table =
      ragexe::res::ParseFogParameters(std::span(bytes, sizeof(kFogTable) - 1));
  for (const MapFogVector& v : kMapFogVectors) {
    INFO("map " << v.name);
    CHECK(v.off == 0);
    FogState fog;
    fog.color = kSentinel;
    fog.density = F(kSentinel);
    fog.start = F(kSentinel);
    fog.end = F(kSentinel);
    CHECK(FindMapFog(table, v.name, fog) == (v.on != 0));
    CHECK(fog.color == v.globals[0]);
    CHECK(B(fog.density) == v.globals[1]);
    CHECK(B(fog.start) == v.globals[2]);
    CHECK(B(fog.end) == v.globals[3]);
  }
}

TEST_CASE("VertexFog gives the specular colour the original does", "[fog]") {
  for (const VertexFogVector& v : kVertexFogVectors) {
    FogState fog;
    fog.start = F(v.start);
    fog.end = F(v.end);
    INFO("start " << fog.start << ", end " << fog.end);
    for (std::size_t i = 0; i < std::size(kVertexFogRhw); ++i) {
      INFO("rhw " << F(kVertexFogRhw[i]));
      CHECK(VertexFog(F(kVertexFogRhw[i]), fog) == v.specular[i]);
    }
  }
}

TEST_CASE("SetupFog sets the fog states the original does", "[fog]") {
  std::size_t n = 0;
  for (const SetupVector& v : kSetupVectors) {
    INFO("case " << n++);
    const FogState fog{v.state[0],    v.state[1],    v.state[2],
                       F(v.state[3]), F(v.state[4]), F(v.state[5])};
    const FogSetup setup = SetupFog(fog, v.enabled != 0, v.vertex_fog != 0);
    std::vector<SceneCall> want;
    want.reserve(v.count);
    for (std::size_t i = 0; i < v.count; ++i)
      want.push_back({SceneCall::Kind::kRenderState, -1, v.calls[i * 2], v.calls[i * 2 + 1], 0});
    CHECK(setup.calls == want);
    CHECK(setup.missing_vertex_fog == (v.errors != 0));
  }
}
