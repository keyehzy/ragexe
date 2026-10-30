#include "render/blit.h"

#include <bit>
#include <cmath>
#include <cstddef>

#include "core/x87.h"

namespace ragexe::render {

namespace {

constexpr std::uint32_t kZEnable = 7;
constexpr std::uint32_t kAlphaTestEnable = 15;
constexpr std::uint32_t kAlphaRef = 24;
constexpr std::uint32_t kAlphaBlendEnable = 27;
constexpr std::uint32_t kMinFilter = 16;
constexpr std::uint32_t kMagFilter = 17;
constexpr std::uint32_t kPoint = 1;
constexpr std::uint32_t kLinear = 2;
constexpr std::uint32_t kTriangleStrip = 5;
constexpr std::uint32_t kTransformedFormat = 0x1c4;
constexpr std::uint32_t kRectZ = 0x3727c5ac;    // 1e-5
constexpr std::uint32_t kRectRhw = 0x3f7fff58;  // 0.99999
constexpr std::uint32_t kBltFastSourceKeyWait = 0x11;
constexpr std::uint32_t kBltWait = 0x1000000;

void RenderState(ScreenSprites& out, std::uint32_t state, std::uint32_t value) {
  out.calls.push_back({SceneCall::Kind::kRenderState, -1, state, value, 0});
}

void StageState(ScreenSprites& out, std::uint32_t state, std::uint32_t value) {
  out.calls.push_back({SceneCall::Kind::kStageState, 0, state, value, 0});
}

// The screen rectangle of (x, y) and the size: each int (the sums wrapping) as a float.
std::array<float, 4> Rect(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  const auto right =
      static_cast<std::int32_t>(static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(width));
  const auto bottom =
      static_cast<std::int32_t>(static_cast<std::uint32_t>(y) + static_cast<std::uint32_t>(height));
  return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(right),
          static_cast<float>(bottom)};
}

// The x87's answer for 0 ÷ 0, its indefinite NaN with the sign set, whatever NaN this
// machine makes.
float X87(float f) { return std::isnan(f) ? std::bit_cast<float>(0xffc00000u) : f; }

}  // namespace

void DrawScreenRect(const std::array<float, 4>& rect, const std::array<float, 4>& uv,
                    std::uint32_t texture, std::uint32_t color, bool linear, BoundTextures& bound,
                    ScreenSprites& out) {
  RenderState(out, kAlphaRef, 1);
  RenderState(out, kAlphaTestEnable, 1);
  RenderState(out, kAlphaBlendEnable, 1);
  RenderState(out, kZEnable, 0);
  const std::uint32_t filter = linear ? kLinear : kPoint;
  StageState(out, kMagFilter, filter);
  StageState(out, kMinFilter, filter);
  if (bound.stage0 != texture) {
    out.calls.push_back({SceneCall::Kind::kTexture, 0, 0, texture, 0});
    bound.stage0 = texture;
  }
  // Top left, bottom left, top right, bottom right.
  const std::array<std::array<std::size_t, 2>, 4> corners = {{{0, 1}, {0, 3}, {2, 1}, {2, 3}}};
  std::array<ScreenVertex, 4> quad{};
  for (std::size_t k = 0; k < quad.size(); ++k) {
    const auto [across, down] = corners[k];
    quad[k].x = rect[across];
    quad[k].y = rect[down];
    quad[k].z = std::bit_cast<float>(kRectZ);
    quad[k].rhw = std::bit_cast<float>(kRectRhw);
    quad[k].diffuse = color;
    quad[k].specular = 0xffffffff;
    quad[k].u = uv[across];
    quad[k].v = uv[down];
  }
  out.calls.push_back({SceneCall::Kind::kDraw, -1, kTriangleStrip,
                       static_cast<std::uint32_t>(out.quads.size()), kTransformedFormat});
  out.quads.push_back(quad);
  StageState(out, kMagFilter, kLinear);
  StageState(out, kMinFilter, kLinear);
}

void DrawTextureRegion(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
                       std::uint32_t color, std::uint32_t texture, const TextureExtent& extent,
                       const TexelOffset& offset, BoundTextures& bound, ScreenSprites& out) {
  // The width is divided by as the integer fild loads, the height after storing it as a float.
  const auto texture_width = static_cast<double>(extent.width);
  const auto texture_height = static_cast<float>(extent.height);
  const std::array<float, 4> uv = {
      X87(core::RoundedQuotient(offset.u, texture_width)),
      X87(offset.v / texture_height),
      X87(core::RoundedQuotient(core::RoundedSum(offset.u, static_cast<double>(width)),
                                texture_width)),
      X87(core::RoundedSum(offset.v, static_cast<double>(height)) / texture_height),
  };
  DrawScreenRect(Rect(x, y, width, height), uv, texture, color, false, bound, out);
}

void DrawTextureImage(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
                      std::uint32_t texture, const TextureExtent& extent, const TexelOffset& offset,
                      BoundTextures& bound, ScreenSprites& out) {
  const float u0 = X87(core::RoundedQuotient(offset.u, static_cast<double>(extent.width)));
  const float v0 = X87(offset.v / static_cast<float>(extent.height));
  const std::array<float, 4> uv = {u0, v0, X87(u0 + extent.u_scale), X87(v0 + extent.v_scale)};
  DrawScreenRect(Rect(x, y, width, height), uv, texture, 0xffffffff, true, bound, out);
}

std::optional<FastBlit> BlitSurface(std::int32_t x, std::int32_t y, std::int32_t width,
                                    std::int32_t height, std::uint32_t screen_width,
                                    std::uint32_t screen_height) {
  FastBlit blit;
  BlitRect& source = blit.source;
  source = {0, 0, width, height};
  const auto ux = static_cast<std::uint32_t>(x);
  const auto uy = static_cast<std::uint32_t>(y);
  if (ux + static_cast<std::uint32_t>(width) > screen_width)
    source[2] = static_cast<std::int32_t>(screen_width - ux);
  if (uy + static_cast<std::uint32_t>(height) > screen_height)
    source[3] = static_cast<std::int32_t>(screen_height - uy);
  blit.x = x;
  blit.y = y;
  if (x < 0) {
    source[0] = static_cast<std::int32_t>(0u - ux);
    blit.x = 0;
  }
  if (y < 0) {
    source[1] = static_cast<std::int32_t>(0u - uy);
    blit.y = 0;
  }
  if (source[2] <= source[0] || source[3] <= source[1]) return std::nullopt;
  blit.flags = kBltFastSourceKeyWait;
  return blit;
}

StretchBlit StretchSurface(std::int32_t width, std::int32_t height) {
  return {{0, 0, width, height}, kBltWait};
}

}  // namespace ragexe::render
