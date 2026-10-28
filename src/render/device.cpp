#include "render/device.h"

namespace ragexe::render {

void DeviceState::SetRenderState(std::uint32_t state, std::uint32_t value) {
  render_[state] = value;
}

void DeviceState::SetTextureStageState(int stage, std::uint32_t state, std::uint32_t value) {
  if (state == d3d7::kAddress) {
    stages_[{stage, d3d7::kAddressU}] = value;
    stages_[{stage, d3d7::kAddressV}] = value;
    return;
  }
  stages_[{stage, state}] = value;
}

std::optional<std::uint32_t> DeviceState::render_state(std::uint32_t state) const {
  const auto it = render_.find(state);
  if (it == render_.end()) return std::nullopt;
  return it->second;
}

std::optional<std::uint32_t> DeviceState::stage_state(int stage, std::uint32_t state) const {
  const auto it = stages_.find({stage, state});
  if (it == stages_.end()) return std::nullopt;
  return it->second;
}

DeviceState InitialDeviceState(bool point_filter) {
  using namespace d3d7;
  DeviceState s;
  s.SetRenderState(kCullMode, kCullNone);
  s.SetRenderState(kZFunc, kCmpLessEqual);
  s.SetRenderState(kTexturePerspective, 1);
  s.SetRenderState(kAlphaFunc, kCmpGreaterEqual);
  s.SetRenderState(kAlphaRef, 0xf0);
  s.SetRenderState(kColorKeyEnable, 1);
  s.SetRenderState(kZEnable, 1);
  s.SetRenderState(kZWriteEnable, 1);
  s.SetRenderState(kSrcBlend, kBlendSrcAlpha);
  s.SetRenderState(kDestBlend, kBlendInvSrcAlpha);

  s.SetTextureStageState(0, kTexCoordIndex, 0);
  s.SetTextureStageState(0, kColorOp, kOpModulate);
  s.SetTextureStageState(0, kColorArg1, kArgTexture);
  s.SetTextureStageState(0, kColorArg2, kArgDiffuse);
  s.SetTextureStageState(0, kAlphaOp, kOpModulate);
  s.SetTextureStageState(0, kAlphaArg1, kArgTexture);
  s.SetTextureStageState(0, kAlphaArg2, kArgDiffuse);
  s.SetTextureStageState(1, kTexCoordIndex, 1);
  s.SetTextureStageState(1, kColorArg1, kArgTexture);
  s.SetTextureStageState(1, kColorOp, kOpModulateAlphaAddColor);
  s.SetTextureStageState(1, kColorArg2, kArgCurrent);
  s.SetTextureStageState(1, kAlphaOp, kOpDisable);
  s.SetTextureStageState(2, kColorOp, kOpDisable);
  s.SetTextureStageState(2, kAlphaOp, kOpDisable);

  const std::uint32_t filter = point_filter ? kFilterPoint : kFilterLinear;
  for (const int stage : {0, 1}) {
    s.SetTextureStageState(stage, kMinFilter, filter);
    s.SetTextureStageState(stage, kMagFilter, filter);
  }
  // ADDRESS then replaces stage 0's ADDRESSV.
  s.SetTextureStageState(0, kAddressV, kAddressWrap);
  s.SetTextureStageState(0, kAddress, kAddressClamp);
  s.SetTextureStageState(1, kAddress, kAddressBorder);
  s.SetTextureStageState(1, kBorderColor, 0xff000000);
  return s;
}

PixelChannel ChannelFromMask(std::uint32_t mask) {
  PixelChannel c;
  c.mask = mask;
  std::uint32_t m = mask;
  for (; m != 0 && (m & 1) == 0; m >>= 1) ++c.shift;
  for (; m != 0; m >>= 1) --c.loss;
  return c;
}

PixelChannels ChannelsFromFormat(std::uint32_t bit_count, std::uint32_t red, std::uint32_t green,
                                 std::uint32_t blue, std::uint32_t alpha) {
  return {bit_count, ChannelFromMask(red), ChannelFromMask(green), ChannelFromMask(blue),
          ChannelFromMask(alpha)};
}

std::int32_t ExpandChannel(const PixelChannel& channel, std::uint32_t pixel) {
  const std::uint32_t bits = (channel.mask & pixel & 0xffff) >> (channel.shift & 31);
  return static_cast<std::int32_t>(bits << (channel.loss & 31));
}

PixelModeChoice SelectPixelMode(const PixelChannels& channels) {
  switch (channels.red.mask) {
    case 0xf800: return {PixelMode::kRgb565, {}};
    case 0x7c00: return {PixelMode::kRgb555, {}};
    case 0x1f: return {PixelMode::kRgb565, "BGR 565 MODE. NOT SUPPORTED"};  // 006054a0
    default: break;
  }
  if (channels.bit_count == 32) return {PixelMode::kRgb32, {}};
  return {PixelMode::kUnsupported, "NOT SUPPORTED MODE"};  // 0060548c
}

std::optional<TextureSize> FitTextureSize(std::uint32_t width, std::uint32_t height,
                                          const TextureLimits& limits) {
  // The doubling never reaches a side above 2^31: it wraps to 0 and goes on forever.
  constexpr std::uint32_t kTop = 0x80000000u;
  if (width > kTop || height > kTop) return std::nullopt;
  const auto fit = [](std::uint32_t side, std::uint32_t min, std::uint32_t max) {
    std::uint32_t p = 1;
    while (p < side) p <<= 1;
    if (p <= min) p = min;
    if (max <= p) p = max;
    return p;
  };
  std::uint32_t w = fit(width, limits.min_width, limits.max_width);
  std::uint32_t h = fit(height, limits.min_height, limits.max_height);
  const std::uint32_t aspect = limits.max_aspect_ratio;
  if (aspect != 0) {
    if (h == 0) return std::nullopt;
    if (aspect < w / h) h = w / aspect;
    if (w == 0) return std::nullopt;
    if (aspect < h / w) return TextureSize{h / aspect, h};
  }
  return TextureSize{w, h};
}

MipmapFlags SelectMipmapFlags(std::uint32_t trilinear_setting, std::uint32_t texture_filter_caps) {
  constexpr std::uint32_t kLinearMipLinear = 0x20;  // D3DPTFILTERCAPS_LINEARMIPLINEAR
  if (trilinear_setting != 0 && (texture_filter_caps & kLinearMipLinear) != 0) {
    return {0x20000, 0x400008};  // DDSD_MIPMAPCOUNT; DDSCAPS_MIPMAP | DDSCAPS_COMPLEX
  }
  return {};
}

bool CanBlendTwoStages(std::uint16_t max_texture_blend_stages, std::uint32_t texture_op_caps) {
  constexpr std::uint32_t kModulateAlphaAddColor = 0x20000;  // D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR
  return max_texture_blend_stages >= 2 && (texture_op_caps & kModulateAlphaAddColor) != 0;
}

std::int32_t TextureDivisor(std::uint32_t texture_mode) {
  if (texture_mode == 0) return 4;
  if (texture_mode == 1) return 2;
  return 1;
}

}  // namespace ragexe::render
