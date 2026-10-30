#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "render/cursor.h"
#include "render/scene.h"

namespace ragexe::render {

// Surfaces and textures drawn onto the screen as rectangles, CSurface's and CTexture's slots
// 3 and 4 (docs/render/texture.md#drawing).

// The offset C3dDevice::CalibrateTexelOffset leaves in texels (006053f4, 006053f8).
struct TexelOffset {
  float u = 0.0f;
  float v = 0.0f;
};

// A CTexture's size (+4, +8) and the share of it its image takes (+0x18, +0x1c).
struct TextureExtent {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  float u_scale = 0.0f;
  float v_scale = 0.0f;
};

// SceneRenderer 00410ab0: a screen rectangle (left, top, right, bottom) of a texture with
// the texture coordinates (u0, v0, u1, v1), in one TRIANGLESTRIP of the 0x1c4 format: top
// left, bottom left, top right, bottom right at z 1e-5 (0x3727c5ac) and rhw 0.99999
// (0x3f7fff58), the colour diffuse and specular 0xffffffff. First ALPHAREF 1,
// ALPHATESTENABLE, ALPHABLENDENABLE and ZENABLE off, stage 0's MAGFILTER and MINFILTER
// POINT, or LINEAR with linear, and the texture when it is not the bound one (bound.stage0);
// after, both filters LINEAR. ALPHAREF stays 1.
void DrawScreenRect(const std::array<float, 4>& rect, const std::array<float, 4>& uv,
                    std::uint32_t texture, std::uint32_t color, bool linear, BoundTextures& bound,
                    ScreenSprites& out);

// CTexture::DrawRegion (00416f10), slot 3: the texture at (x, y), width × height texels of it
// from the texel offset, in the colour, filtered by points.
void DrawTextureRegion(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
                       std::uint32_t color, std::uint32_t texture, const TextureExtent& extent,
                       const TexelOffset& offset, BoundTextures& bound, ScreenSprites& out);

// CTexture::DrawImage (00416fc0), slot 4: the texture's whole image stretched over the
// rectangle from (x, y), white and linearly filtered.
void DrawTextureImage(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
                      std::uint32_t texture, const TextureExtent& extent, const TexelOffset& offset,
                      BoundTextures& bound, ScreenSprites& out);

// A rectangle (left, top, right, bottom), as DirectDraw takes it.
using BlitRect = std::array<std::int32_t, 4>;

// IDirectDrawSurface7::BltFast onto the back buffer: the source's rectangle at (x, y).
struct FastBlit {
  std::int32_t x = 0;
  std::int32_t y = 0;
  BlitRect source{};
  std::uint32_t flags = 0;
};

// CSurface's slot 3 (004163c0), SceneRenderer 0040e610: the surface's width × height from its
// corner at (x, y), clipped to a screen of screen_width × screen_height (the renderer's +0x80
// and +0x84), with its colour key and DDBLTFAST_WAIT (0x11). Nothing when the clipped
// rectangle is empty. The right and bottom are clipped by unsigned comparisons, so a rectangle
// ending left of or above the screen is clipped to a source rectangle wider than it, which
// DirectDraw refuses.
std::optional<FastBlit> BlitSurface(std::int32_t x, std::int32_t y, std::int32_t width,
                                    std::int32_t height, std::uint32_t screen_width,
                                    std::uint32_t screen_height);

// IDirectDrawSurface7::Blt onto the back buffer: the whole source stretched over the
// destination.
struct StretchBlit {
  BlitRect destination{};
  std::uint32_t flags = 0;
};

// CSurface's slot 4 (004163f0), SceneRenderer 0040e6a0: the whole surface stretched over
// (0, 0, width, height) with DDBLT_WAIT (0x1000000); the point given is not read.
StretchBlit StretchSurface(std::int32_t width, std::int32_t height);

}  // namespace ragexe::render
