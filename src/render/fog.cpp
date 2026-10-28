#include "render/fog.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>

#include "res/c_runtime.h"

namespace ragexe::render {

namespace {

constexpr float kNear = 10.0f;   // 005e269c
constexpr float kFar = 1500.0f;  // 005e2698
constexpr std::uint32_t kLinear = 3;

constexpr std::uint32_t kFogEnable = 28;       // D3DRENDERSTATE_FOGENABLE
constexpr std::uint32_t kFogColor = 34;        // D3DRENDERSTATE_FOGCOLOR
constexpr std::uint32_t kFogTableMode = 35;    // D3DRENDERSTATE_FOGTABLEMODE
constexpr std::uint32_t kFogStart = 36;        // D3DRENDERSTATE_FOGSTART
constexpr std::uint32_t kFogEnd = 37;          // D3DRENDERSTATE_FOGEND
constexpr std::uint32_t kFogDensity = 38;      // D3DRENDERSTATE_FOGDENSITY
constexpr std::uint32_t kRangeFog = 48;        // D3DRENDERSTATE_RANGEFOGENABLE
constexpr std::uint32_t kFogVertexMode = 140;  // D3DRENDERSTATE_FOGVERTEXMODE

// The characters of a std::string up to its first NUL, as the client's char* view of it.
std::string_view CString(const std::string& s) { return {s.c_str(), std::strlen(s.c_str())}; }

float ParseFloat(const std::string& s) { return static_cast<float>(res::AtoF(CString(s))); }

}  // namespace

std::uint32_t ParseHexDigits(std::string_view text) {
  if (text.size() < 3) return 0;
  std::uint32_t value = 0;
  std::uint32_t shift = 0;
  for (std::size_t i = text.size() - 1; i >= 2; --i, shift += 4) {
    const char c = text[i];
    std::uint32_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<std::uint32_t>(c - '0');
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<std::uint32_t>(c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<std::uint32_t>(c - 'a' + 10);
    } else {
      continue;
    }
    value += digit << (shift & 31);
  }
  return value;
}

bool FindMapFog(const res::LineRecords& table, std::string_view rsw_name, FogState& fog) {
  // std::lower_bound with StringLess (00599400), which compares as strcmp does.
  const std::string name(rsw_name);
  const std::string_view key = CString(name);
  const auto it = std::lower_bound(
      table.begin(), table.end(), key,
      [](const auto& record, std::string_view k) { return CString(record.first) < k; });
  if (it == table.end() || CString(it->first) != key) return false;
  const std::vector<std::string>& values = it->second;
  fog.start = ParseFloat(values[0]);
  fog.end = ParseFloat(values[1]);
  fog.color = ParseHexDigits(CString(values[2]));
  fog.density = ParseFloat(values[3]);
  return true;
}

std::uint32_t VertexFog(float rhw, const FogState& fog) {
  const float depth = (1.0f / rhw - kNear) / (kFar - kNear);
  float f = (depth - fog.start) / (fog.end - fog.start);
  if (!(f >= 0.0f)) {
    f = 0.0f;
  } else if (f > 1.0f) {
    f = 1.0f;
  }
  const auto alpha = static_cast<std::int32_t>((1.0f - f) * 255.0f);
  return static_cast<std::uint32_t>(alpha) << 24;
}

FogSetup SetupFog(const FogState& fog, bool enabled, bool vertex_fog) {
  FogSetup out;
  if (!enabled) return out;
  const auto state = [&](std::uint32_t s, std::uint32_t v) {
    out.calls.push_back({SceneCall::Kind::kRenderState, -1, s, v, 0});
  };
  const float start = (kFar - kNear) * fog.start + kNear;
  const float end = (kFar - kNear) * fog.end + kNear;
  state(kFogColor, fog.color);
  if (fog.mode == kLinear) {
    if (!vertex_fog) {
      state(kFogEnable, 0);
    } else {
      state(kFogStart, std::bit_cast<std::uint32_t>(start));
      state(kFogEnd, std::bit_cast<std::uint32_t>(end));
      state(kFogDensity, std::bit_cast<std::uint32_t>(fog.density));
      state(kFogTableMode, 0);
      state(kFogVertexMode, fog.mode);
    }
  } else {
    if (!vertex_fog) {
      out.missing_vertex_fog = true;
    } else {
      state(kFogTableMode, 0);
      state(kFogVertexMode, fog.mode);
    }
    state(kFogDensity, std::bit_cast<std::uint32_t>(fog.density));
  }
  if (vertex_fog && fog.range != 0) state(kRangeFog, 1);
  return out;
}

}  // namespace ragexe::render
