#include "render/gpu_device.h"

#include <SDL3/SDL.h>

#include <cstring>

#include "scene_shaders.h"

namespace ragexe::render {

namespace {

// D3DBLEND to SDL_GPU's factors.
SDL_GPUBlendFactor BlendFactor(std::uint32_t blend) {
  switch (blend) {
    case 1: return SDL_GPU_BLENDFACTOR_ZERO;
    case 2: return SDL_GPU_BLENDFACTOR_ONE;
    case 3: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case 4: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case 6: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case 8: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case 10: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case 11: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
    default: return SDL_GPU_BLENDFACTOR_ONE;
  }
}

// D3DCMP and SDL_GPUCompareOp share their order, NEVER (1) to ALWAYS (8).
SDL_GPUCompareOp CompareOp(std::uint32_t func) {
  if (func < 1 || func > 8) return SDL_GPU_COMPAREOP_ALWAYS;
  return static_cast<SDL_GPUCompareOp>(func);
}

SDL_GPUPrimitiveType PrimitiveType(std::uint32_t d3d) {
  switch (d3d) {
    case 1: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
    case 2: return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case 3: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
    case 5: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    default: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;  // lists, and fans made into lists
  }
}

SDL_GPUSamplerAddressMode AddressMode(std::uint32_t d3d) {
  switch (d3d) {
    case 2: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    case 3:
    case 4: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;  // no border colour in SDL_GPU
    default: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  }
}

std::array<std::uint32_t, 9> KeyOf(const PipelineKey& k) {
  return {k.blend ? 1u : 0u,     k.src_blend, k.dest_blend, k.z_test ? 1u : 0u,
          k.z_write ? 1u : 0u,   k.z_func,    k.cull,       k.primitive == 6 ? 4u : k.primitive,
          k.two_coords ? 1u : 0u};
}

}  // namespace

std::unique_ptr<RenderDevice> RenderDevice::Create(SDL_Window* window, std::string* error) {
  SDL_GPUDevice* device =
      SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
  if (device == nullptr) {
    if (error != nullptr) *error = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
    return nullptr;
  }
  if (window != nullptr && !SDL_ClaimWindowForGPUDevice(device, window)) {
    if (error != nullptr) *error = std::string("SDL_ClaimWindowForGPUDevice: ") + SDL_GetError();
    SDL_DestroyGPUDevice(device);
    return nullptr;
  }
  std::unique_ptr<RenderDevice> out(new RenderDevice(window, device));
  out->swapchain_format_ = window != nullptr ? SDL_GetGPUSwapchainTextureFormat(device, window)
                                             : SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  if (!out->CreateShaders()) {
    if (error != nullptr) *error = out->error_;
    return nullptr;
  }
  const std::uint32_t white = 0xffffffff;
  if (!out->SetTexture(0, 1, 1, std::span(&white, 1))) {
    if (error != nullptr) *error = out->error_;
    return nullptr;
  }
  out->white_ = out->textures_[0];
  return out;
}

RenderDevice::~RenderDevice() {
  SDL_WaitForGPUIdle(device_);
  for (auto& [key, p] : pipelines_) SDL_ReleaseGPUGraphicsPipeline(device_, p);
  for (auto& [key, s] : samplers_) SDL_ReleaseGPUSampler(device_, s);
  for (auto& [id, t] : textures_) SDL_ReleaseGPUTexture(device_, t);
  if (depth_ != nullptr) SDL_ReleaseGPUTexture(device_, depth_);
  if (vertex_buffer_ != nullptr) SDL_ReleaseGPUBuffer(device_, vertex_buffer_);
  if (vertex_transfer_ != nullptr) SDL_ReleaseGPUTransferBuffer(device_, vertex_transfer_);
  if (vertex_shader_ != nullptr) SDL_ReleaseGPUShader(device_, vertex_shader_);
  if (fragment_shader_ != nullptr) SDL_ReleaseGPUShader(device_, fragment_shader_);
  if (window_ != nullptr) SDL_ReleaseWindowFromGPUDevice(device_, window_);
  SDL_DestroyGPUDevice(device_);
}

const char* RenderDevice::driver() const { return SDL_GetGPUDeviceDriver(device_); }

bool RenderDevice::CreateShaders() {
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device_);
  const bool msl = (formats & SDL_GPU_SHADERFORMAT_MSL) != 0;
  const auto create = [&](SDL_GPUShaderStage stage, const unsigned char* spirv,
                          std::size_t spirv_size, const unsigned char* metal,
                          std::size_t metal_size, std::uint32_t samplers) {
    SDL_GPUShaderCreateInfo info{};
    info.code = msl ? metal : spirv;
    info.code_size = msl ? metal_size : spirv_size;
    info.entrypoint = msl ? "main0" : "main";
    info.format = msl ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = 1;
    return SDL_CreateGPUShader(device_, &info);
  };
  vertex_shader_ = create(SDL_GPU_SHADERSTAGE_VERTEX, kSceneVertSpirv, kSceneVertSpirv_size,
                          kSceneVertMsl, kSceneVertMsl_size, 0);
  fragment_shader_ = create(SDL_GPU_SHADERSTAGE_FRAGMENT, kSceneFragSpirv, kSceneFragSpirv_size,
                            kSceneFragMsl, kSceneFragMsl_size, 2);
  if (vertex_shader_ == nullptr || fragment_shader_ == nullptr) {
    error_ = std::string("SDL_CreateGPUShader: ") + SDL_GetError();
    return false;
  }
  return true;
}

SDL_GPUGraphicsPipeline* RenderDevice::Pipeline(const PipelineKey& key) {
  const auto k = KeyOf(key);
  if (auto it = pipelines_.find(k); it != pipelines_.end()) return it->second;
  SDL_GPUVertexBufferDescription buffer{};
  buffer.slot = 0;
  buffer.pitch = sizeof(GpuVertex);
  buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  const SDL_GPUVertexAttribute attributes[] = {
      {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, x)},
      {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuVertex, diffuse)},
      {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuVertex, specular)},
      {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuVertex, u0)},
      {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuVertex, u1)},
  };
  SDL_GPUColorTargetDescription color{};
  color.format = static_cast<SDL_GPUTextureFormat>(swapchain_format_);
  color.blend_state.enable_blend = key.blend;
  color.blend_state.src_color_blendfactor = BlendFactor(key.src_blend);
  color.blend_state.dst_color_blendfactor = BlendFactor(key.dest_blend);
  color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  color.blend_state.src_alpha_blendfactor = BlendFactor(key.src_blend);
  color.blend_state.dst_alpha_blendfactor = BlendFactor(key.dest_blend);
  color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vertex_shader_;
  info.fragment_shader = fragment_shader_;
  info.vertex_input_state.vertex_buffer_descriptions = &buffer;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attributes;
  info.vertex_input_state.num_vertex_attributes = 5;
  info.primitive_type = PrimitiveType(key.primitive);
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  // Direct3D culls by the winding as the triangle appears on screen, and so does SDL_GPU:
  // CW culls the clockwise ones, keeping the counter-clockwise ones as front faces.
  info.rasterizer_state.cull_mode = key.cull == 1 ? SDL_GPU_CULLMODE_NONE : SDL_GPU_CULLMODE_BACK;
  info.rasterizer_state.front_face =
      key.cull == 2 ? SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE : SDL_GPU_FRONTFACE_CLOCKWISE;
  info.depth_stencil_state.enable_depth_test = key.z_test;
  info.depth_stencil_state.enable_depth_write = key.z_test && key.z_write;
  info.depth_stencil_state.compare_op = CompareOp(key.z_func);
  info.target_info.color_target_descriptions = &color;
  info.target_info.num_color_targets = 1;
  info.target_info.has_depth_stencil_target = true;
  info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
  SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
  if (pipeline == nullptr) error_ = std::string("SDL_CreateGPUGraphicsPipeline: ") + SDL_GetError();
  pipelines_[k] = pipeline;
  return pipeline;
}

SDL_GPUSampler* RenderDevice::Sampler(const SamplerKey& key) {
  if (auto it = samplers_.find(key); it != samplers_.end()) return it->second;
  SDL_GPUSamplerCreateInfo info{};
  info.mag_filter = key.mag_filter == 1 ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
  info.min_filter = key.min_filter == 1 ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = AddressMode(key.address_u);
  info.address_mode_v = AddressMode(key.address_v);
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device_, &info);
  samplers_[key] = sampler;
  return sampler;
}

bool RenderDevice::SetTexture(std::uint32_t id, std::uint32_t width, std::uint32_t height,
                              std::span<const std::uint32_t> argb) {
  if (static_cast<std::size_t>(width) * height != argb.size() || width == 0 || height == 0) {
    error_ = "a texture's size does not match its texels";
    return false;
  }
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  // An ARGB dword is B, G, R, A in memory.
  info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = width;
  info.height = height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &info);
  if (texture == nullptr) {
    error_ = std::string("SDL_CreateGPUTexture: ") + SDL_GetError();
    return false;
  }
  const auto bytes = static_cast<std::uint32_t>(argb.size_bytes());
  SDL_GPUTransferBufferCreateInfo transfer_info{};
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = bytes;
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
  void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
  std::memcpy(mapped, argb.data(), bytes);
  SDL_UnmapGPUTransferBuffer(device_, transfer);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo source{};
  source.transfer_buffer = transfer;
  source.pixels_per_row = width;
  source.rows_per_layer = height;
  SDL_GPUTextureRegion region{};
  region.texture = texture;
  region.w = width;
  region.h = height;
  region.d = 1;
  SDL_UploadToGPUTexture(copy, &source, &region, false);
  SDL_EndGPUCopyPass(copy);
  const bool ok = SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device_, transfer);
  if (auto it = textures_.find(id); it != textures_.end())
    SDL_ReleaseGPUTexture(device_, it->second);
  textures_[id] = texture;
  return ok;
}

bool RenderDevice::EnsureDepth(std::uint32_t width, std::uint32_t height) {
  if (depth_ != nullptr && depth_width_ == width && depth_height_ == height) return true;
  if (depth_ != nullptr) SDL_ReleaseGPUTexture(device_, depth_);
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  info.width = width;
  info.height = height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  depth_ = SDL_CreateGPUTexture(device_, &info);
  depth_width_ = width;
  depth_height_ = height;
  return depth_ != nullptr;
}

bool RenderDevice::EnsureVertexBuffer(std::uint32_t bytes) {
  if (bytes <= vertex_capacity_) return true;
  std::uint32_t capacity = vertex_capacity_ == 0 ? 1u << 16 : vertex_capacity_;
  while (capacity < bytes) capacity *= 2;
  if (vertex_buffer_ != nullptr) SDL_ReleaseGPUBuffer(device_, vertex_buffer_);
  if (vertex_transfer_ != nullptr) SDL_ReleaseGPUTransferBuffer(device_, vertex_transfer_);
  SDL_GPUBufferCreateInfo buffer{};
  buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  buffer.size = capacity;
  vertex_buffer_ = SDL_CreateGPUBuffer(device_, &buffer);
  SDL_GPUTransferBufferCreateInfo transfer{};
  transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer.size = capacity;
  vertex_transfer_ = SDL_CreateGPUTransferBuffer(device_, &transfer);
  vertex_capacity_ = capacity;
  return vertex_buffer_ != nullptr && vertex_transfer_ != nullptr;
}

bool RenderDevice::Record(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target,
                          std::uint32_t target_width, std::uint32_t target_height,
                          const std::vector<DrawRecord>& draws, const VertexLookup& vertices,
                          std::uint32_t width, std::uint32_t height, std::uint32_t clear_argb) {
  // Every draw's vertices in one buffer, fans made into lists.
  std::vector<GpuVertex> all;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
  for (const DrawRecord& d : draws) {
    const std::span<const GpuVertex> v = vertices(d.primitive);
    const auto first = static_cast<std::uint32_t>(all.size());
    if (d.pipeline.primitive == 6) {
      for (std::size_t k = 2; k < v.size(); ++k) {
        all.push_back(v[0]);
        all.push_back(v[k - 1]);
        all.push_back(v[k]);
      }
    } else {
      all.insert(all.end(), v.begin(), v.end());
    }
    ranges.emplace_back(first, static_cast<std::uint32_t>(all.size()) - first);
  }
  if (!all.empty()) {
    const auto bytes = static_cast<std::uint32_t>(all.size() * sizeof(GpuVertex));
    if (!EnsureVertexBuffer(bytes)) {
      error_ = std::string("vertex buffer: ") + SDL_GetError();
      return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(device_, vertex_transfer_, true);
    std::memcpy(mapped, all.data(), bytes);
    SDL_UnmapGPUTransferBuffer(device_, vertex_transfer_);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation source{vertex_transfer_, 0};
    SDL_GPUBufferRegion region{vertex_buffer_, 0, bytes};
    SDL_UploadToGPUBuffer(copy, &source, &region, true);
    SDL_EndGPUCopyPass(copy);
  }
  if (!EnsureDepth(target_width, target_height)) {
    error_ = std::string("depth buffer: ") + SDL_GetError();
    return false;
  }
  SDL_GPUColorTargetInfo color{};
  color.texture = target;
  color.clear_color = {static_cast<float>(clear_argb >> 16 & 0xff) / 255.0f,
                       static_cast<float>(clear_argb >> 8 & 0xff) / 255.0f,
                       static_cast<float>(clear_argb & 0xff) / 255.0f, 1.0f};
  color.load_op = SDL_GPU_LOADOP_CLEAR;
  color.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPUDepthStencilTargetInfo depth{};
  depth.texture = depth_;
  depth.clear_depth = 1.0f;
  depth.load_op = SDL_GPU_LOADOP_CLEAR;
  depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
  depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
  const float viewport[4] = {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f};
  SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof viewport);
  SDL_GPUGraphicsPipeline* bound = nullptr;
  if (!all.empty()) {
    SDL_GPUBufferBinding binding{vertex_buffer_, 0};
    for (std::size_t i = 0; i < draws.size(); ++i) {
      const DrawRecord& d = draws[i];
      if (ranges[i].second == 0) continue;
      SDL_GPUGraphicsPipeline* pipeline = Pipeline(d.pipeline);
      if (pipeline == nullptr) continue;
      if (pipeline != bound) {
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        bound = pipeline;
      }
      SDL_GPUTextureSamplerBinding samplers[2]{};
      for (std::size_t s = 0; s < 2; ++s) {
        auto it = textures_.find(d.textures[s]);
        samplers[s].texture = it == textures_.end() ? white_ : it->second;
        samplers[s].sampler = Sampler(d.samplers[s]);
      }
      SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);
      SDL_PushGPUFragmentUniformData(cmd, 0, &d.uniforms, sizeof d.uniforms);
      SDL_DrawGPUPrimitives(pass, ranges[i].second, 1, ranges[i].first, 0);
    }
  }
  SDL_EndGPURenderPass(pass);
  return true;
}

bool RenderDevice::DrawFrame(const std::vector<DrawRecord>& draws, const VertexLookup& vertices,
                             std::uint32_t width, std::uint32_t height, std::uint32_t clear_argb) {
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
  if (cmd == nullptr) {
    error_ = std::string("SDL_AcquireGPUCommandBuffer: ") + SDL_GetError();
    return false;
  }
  SDL_GPUTexture* swapchain = nullptr;
  std::uint32_t target_width = 0, target_height = 0;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swapchain, &target_width,
                                             &target_height)) {
    error_ = std::string("SDL_WaitAndAcquireGPUSwapchainTexture: ") + SDL_GetError();
    SDL_CancelGPUCommandBuffer(cmd);
    return false;
  }
  if (swapchain == nullptr) return SDL_SubmitGPUCommandBuffer(cmd);  // minimised
  if (!Record(cmd, swapchain, target_width, target_height, draws, vertices, width, height,
              clear_argb)) {
    SDL_CancelGPUCommandBuffer(cmd);
    return false;
  }
  return SDL_SubmitGPUCommandBuffer(cmd);
}

bool RenderDevice::RenderToImage(const std::vector<DrawRecord>& draws, const VertexLookup& vertices,
                                 std::uint32_t width, std::uint32_t height,
                                 std::uint32_t clear_argb, std::vector<std::uint32_t>& argb) {
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = static_cast<SDL_GPUTextureFormat>(swapchain_format_);
  info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = width;
  info.height = height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  SDL_GPUTexture* target = SDL_CreateGPUTexture(device_, &info);
  const std::uint32_t bytes = width * height * 4;
  SDL_GPUTransferBufferCreateInfo transfer_info{};
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  transfer_info.size = bytes;
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
  bool ok = target != nullptr && transfer != nullptr && cmd != nullptr &&
            Record(cmd, target, width, height, draws, vertices, width, height, clear_argb);
  if (ok) {
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region{};
    region.texture = target;
    region.w = width;
    region.h = height;
    region.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = transfer;
    destination.pixels_per_row = width;
    destination.rows_per_layer = height;
    SDL_DownloadFromGPUTexture(copy, &region, &destination);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    ok = fence != nullptr && SDL_WaitForGPUFences(device_, true, &fence, 1);
    if (fence != nullptr) SDL_ReleaseGPUFence(device_, fence);
    if (ok) {
      const auto* texels =
          static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(device_, transfer, false));
      argb.assign(static_cast<std::size_t>(width) * height, 0);
      const bool rgba = swapchain_format_ == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      for (std::size_t i = 0; i < argb.size(); ++i) {
        const std::uint8_t* p = texels + i * 4;
        const std::uint32_t r = rgba ? p[0] : p[2];
        const std::uint32_t b = rgba ? p[2] : p[0];
        argb[i] = 0xff000000u | r << 16 | static_cast<std::uint32_t>(p[1]) << 8 | b;
      }
      SDL_UnmapGPUTransferBuffer(device_, transfer);
    }
  } else if (cmd != nullptr) {
    SDL_CancelGPUCommandBuffer(cmd);
  }
  if (!ok && error_.empty()) error_ = std::string("rendering to an image: ") + SDL_GetError();
  if (transfer != nullptr) SDL_ReleaseGPUTransferBuffer(device_, transfer);
  if (target != nullptr) SDL_ReleaseGPUTexture(device_, target);
  return ok;
}

}  // namespace ragexe::render
