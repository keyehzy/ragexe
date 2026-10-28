#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "render/fixed_function.h"

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUCommandBuffer;

namespace ragexe::render {

// A transformed vertex with both coordinate sets, as the shaders take it.
struct GpuVertex {
  float x = 0.0f, y = 0.0f, z = 0.0f, rhw = 0.0f;
  std::uint32_t diffuse = 0;  // ARGB
  std::uint32_t specular = 0;
  float u0 = 0.0f, v0 = 0.0f;
  float u1 = 0.0f, v1 = 0.0f;
};

// The vertices of a draw's primitive, by its id.
using VertexLookup = std::function<std::span<const GpuVertex>(std::uint32_t primitive)>;

// The SDL_GPU device that draws what the original's Direct3D 7 device drew
// (docs/render/gpu.md): textures by id, and frames of DrawRecords.
class RenderDevice {
 public:
  // nullptr, with error set, when SDL_GPU has no device. Without a window the device only
  // renders to images.
  static std::unique_ptr<RenderDevice> Create(SDL_Window* window, std::string* error);
  ~RenderDevice();
  RenderDevice(const RenderDevice&) = delete;
  RenderDevice& operator=(const RenderDevice&) = delete;

  // The id the device keeps its white texture under.
  static constexpr std::uint32_t kWhiteId = 0xffffffff;

  // A texture of width × height ARGB texels, top row first, under id (not 0 or kWhiteId).
  bool SetTexture(std::uint32_t id, std::uint32_t width, std::uint32_t height,
                  std::span<const std::uint32_t> argb);

  // Clears the window and its depth, draws the records with vertices placed for a
  // width × height viewport, and presents.
  bool DrawFrame(const std::vector<DrawRecord>& draws, const VertexLookup& vertices,
                 std::uint32_t width, std::uint32_t height, std::uint32_t clear_argb);

  // The same frame into a width × height image instead, read back as ARGB texels, top row
  // first, with alpha 0xff.
  bool RenderToImage(const std::vector<DrawRecord>& draws, const VertexLookup& vertices,
                     std::uint32_t width, std::uint32_t height, std::uint32_t clear_argb,
                     std::vector<std::uint32_t>& argb);

  const std::string& error() const { return error_; }
  const char* driver() const;

 private:
  RenderDevice(SDL_Window* window, SDL_GPUDevice* device) : window_(window), device_(device) {}

  bool CreateShaders();
  // Uploads the draws' vertices on cmd and records the render pass into target.
  bool Record(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, std::uint32_t target_width,
              std::uint32_t target_height, const std::vector<DrawRecord>& draws,
              const VertexLookup& vertices, std::uint32_t width, std::uint32_t height,
              std::uint32_t clear_argb);
  SDL_GPUGraphicsPipeline* Pipeline(const PipelineKey& key);
  SDL_GPUSampler* Sampler(const SamplerKey& key);
  bool EnsureDepth(std::uint32_t width, std::uint32_t height);
  bool EnsureVertexBuffer(std::uint32_t bytes);

  SDL_Window* window_;
  SDL_GPUDevice* device_;
  SDL_GPUShader* vertex_shader_ = nullptr;
  SDL_GPUShader* fragment_shader_ = nullptr;
  std::uint32_t swapchain_format_ = 0;
  std::map<std::array<std::uint32_t, 9>, SDL_GPUGraphicsPipeline*> pipelines_;
  std::map<SamplerKey, SDL_GPUSampler*> samplers_;
  std::map<std::uint32_t, SDL_GPUTexture*> textures_;
  SDL_GPUTexture* white_ = nullptr;
  SDL_GPUTexture* depth_ = nullptr;
  std::uint32_t depth_width_ = 0, depth_height_ = 0;
  SDL_GPUBuffer* vertex_buffer_ = nullptr;
  SDL_GPUTransferBuffer* vertex_transfer_ = nullptr;
  std::uint32_t vertex_capacity_ = 0;
  std::string error_;
};

}  // namespace ragexe::render
