#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

namespace ragexe::render {

// The Direct3D 7 values the original's device set-up uses (docs/render/device.md): the
// render states (D3DRENDERSTATETYPE), the texture stage states
// (D3DTEXTURESTAGESTATETYPE) and the values given to them. The port's renderer
// reproduces what they select.
namespace d3d7 {

inline constexpr std::uint32_t kTexturePerspective = 4;
inline constexpr std::uint32_t kZEnable = 7;
inline constexpr std::uint32_t kZWriteEnable = 14;
inline constexpr std::uint32_t kAlphaTestEnable = 15;
inline constexpr std::uint32_t kSrcBlend = 19;
inline constexpr std::uint32_t kDestBlend = 20;
inline constexpr std::uint32_t kCullMode = 22;
inline constexpr std::uint32_t kZFunc = 23;
inline constexpr std::uint32_t kAlphaRef = 24;
inline constexpr std::uint32_t kAlphaFunc = 25;
inline constexpr std::uint32_t kAlphaBlendEnable = 27;
inline constexpr std::uint32_t kColorKeyEnable = 41;

inline constexpr std::uint32_t kColorOp = 1;
inline constexpr std::uint32_t kColorArg1 = 2;
inline constexpr std::uint32_t kColorArg2 = 3;
inline constexpr std::uint32_t kAlphaOp = 4;
inline constexpr std::uint32_t kAlphaArg1 = 5;
inline constexpr std::uint32_t kAlphaArg2 = 6;
inline constexpr std::uint32_t kTexCoordIndex = 11;
inline constexpr std::uint32_t kAddress = 12;
inline constexpr std::uint32_t kAddressU = 13;
inline constexpr std::uint32_t kAddressV = 14;
inline constexpr std::uint32_t kBorderColor = 15;
inline constexpr std::uint32_t kMagFilter = 16;
inline constexpr std::uint32_t kMinFilter = 17;

inline constexpr std::uint32_t kCullNone = 1;
inline constexpr std::uint32_t kCmpLessEqual = 4;
inline constexpr std::uint32_t kCmpGreaterEqual = 7;
inline constexpr std::uint32_t kBlendSrcAlpha = 5;
inline constexpr std::uint32_t kBlendInvSrcAlpha = 6;
inline constexpr std::uint32_t kOpDisable = 1;
inline constexpr std::uint32_t kOpModulate = 4;
inline constexpr std::uint32_t kOpModulateAlphaAddColor = 18;
inline constexpr std::uint32_t kArgDiffuse = 0;
inline constexpr std::uint32_t kArgCurrent = 1;
inline constexpr std::uint32_t kArgTexture = 2;
inline constexpr std::uint32_t kAddressWrap = 1;
inline constexpr std::uint32_t kAddressClamp = 3;
inline constexpr std::uint32_t kAddressBorder = 4;
inline constexpr std::uint32_t kFilterPoint = 1;
inline constexpr std::uint32_t kFilterLinear = 2;

}  // namespace d3d7

// The render and texture stage states set on a device. A state never set keeps the
// device's own default, which is not recorded here.
class DeviceState {
 public:
  void SetRenderState(std::uint32_t state, std::uint32_t value);
  // D3DTSS_ADDRESS sets D3DTSS_ADDRESSU and D3DTSS_ADDRESSV, as Direct3D 7 defines it.
  void SetTextureStageState(int stage, std::uint32_t state, std::uint32_t value);

  std::optional<std::uint32_t> render_state(std::uint32_t state) const;
  std::optional<std::uint32_t> stage_state(int stage, std::uint32_t state) const;

  const std::map<std::uint32_t, std::uint32_t>& render_states() const { return render_; }
  const std::map<std::pair<int, std::uint32_t>, std::uint32_t>& stage_states() const {
    return stages_;
  }

  friend bool operator==(const DeviceState&, const DeviceState&) = default;

 private:
  std::map<std::uint32_t, std::uint32_t> render_;
  std::map<std::pair<int, std::uint32_t>, std::uint32_t> stages_;
};

// The states C3dDevice::Init (00404410) sets once the device exists. point_filter is
// the device's +0x1e4, which is still 0 then: InitAudioAndDevice sets it afterwards.
DeviceState InitialDeviceState(bool point_filter = false);

// One channel of a pixel format as Init derives it from the primary surface: the mask,
// the bits below it, and 8 minus the bits up to its top, the left shift that scales the
// channel to 8 bits.
struct PixelChannel {
  std::uint32_t mask = 0;
  std::int32_t shift = 0;
  std::int32_t loss = 8;
};

struct PixelChannels {
  std::uint32_t bit_count = 0;
  PixelChannel red, green, blue, alpha;
};

PixelChannel ChannelFromMask(std::uint32_t mask);
PixelChannels ChannelsFromFormat(std::uint32_t bit_count, std::uint32_t red, std::uint32_t green,
                                 std::uint32_t blue, std::uint32_t alpha);

// ExpandRed, ExpandGreen, ExpandBlue (00405df0, 00405e20, 00405e50): the channel of a
// pixel scaled to 8 bits. Only the low 16 bits of the pixel count, so in a 32-bit format
// red is always 0.
std::int32_t ExpandChannel(const PixelChannel& channel, std::uint32_t pixel);

// The pixel mode CreateRenderer (004054b0) gives the SceneRenderer, from the primary
// surface's format.
enum class PixelMode : std::int32_t {
  kUnsupported = -1,
  kRgb565 = 2,
  kRgb555 = 3,
  kRgb32 = 4,
};

struct PixelModeChoice {
  PixelMode mode;
  // What the client shows through ShowErrorMessage, or empty.
  std::string_view message;
};

PixelModeChoice SelectPixelMode(const PixelChannels& channels);

// The texture sizes the device accepts, from D3DDEVICEDESC7 (+0x7c to +0x90).
struct TextureLimits {
  std::uint32_t min_width = 1;
  std::uint32_t min_height = 1;
  std::uint32_t max_width = 2048;
  std::uint32_t max_height = 2048;
  std::uint32_t max_aspect_ratio = 0;  // 0: any
};

struct TextureSize {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// FitTextureSize (004056e0): each side up to a power of two within the limits, then the
// aspect ratio limit. nullopt where the original never returns (a side above 2^31) or
// divides by zero (a side limited to 0 with an aspect ratio limit).
std::optional<TextureSize> FitTextureSize(std::uint32_t width, std::uint32_t height,
                                          const TextureLimits& limits);

// SelectMipmapFlags (00405f20): what CTexture::CreateSurface adds to a texture's surface
// description, DDSD_MIPMAPCOUNT and DDSCAPS_MIPMAP | DDSCAPS_COMPLEX, when the
// TRILINEARFILTER setting is on and the device filters with D3DPTFILTERCAPS_LINEARMIPLINEAR.
struct MipmapFlags {
  std::uint32_t description_flags = 0;
  std::uint32_t caps = 0;

  friend bool operator==(const MipmapFlags&, const MipmapFlags&) = default;
};

MipmapFlags SelectMipmapFlags(std::uint32_t trilinear_setting, std::uint32_t texture_filter_caps);

// CreateRenderer's two-stage test (0x6053f0): at least two texture blend stages and
// D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR.
bool CanBlendTwoStages(std::uint16_t max_texture_blend_stages, std::uint32_t texture_op_caps);

// LoadVideoSettings' TEXTUREMODE (0x688500, 2 when absent): what CreateTexture divides a
// texture's width and height by (0x61ec50).
std::int32_t TextureDivisor(std::uint32_t texture_mode);

}  // namespace ragexe::render
