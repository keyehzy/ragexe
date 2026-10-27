#include "res/gnd.h"

#include <algorithm>
#include <cstring>

#include "res/data_file.h"

namespace ragexe::res {

namespace {

constexpr std::size_t kHeaderSize = 26;
constexpr std::size_t kLightmapHeaderSize = 16;
constexpr std::size_t kLightmapSize = 256;
constexpr std::size_t kSurfaceSize = 40;
constexpr std::size_t kShortCellSize = 0x18;
constexpr std::size_t kCellSize = 0x1c;

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

std::int32_t I32(const std::uint8_t* p) {
  std::int32_t v = 0;
  std::memcpy(&v, p, 4);
  return v;
}

std::int16_t I16(const std::uint8_t* p) {
  std::int16_t v = 0;
  std::memcpy(&v, p, 2);
  return v;
}

// Reads `count` records of `size` bytes at `pos`, advancing it; false when they run past
// the end.
bool Take(std::span<const std::uint8_t> data, std::size_t& pos, std::int32_t count,
          std::size_t size, const std::uint8_t*& start) {
  if (count < 0) return false;
  if (static_cast<std::size_t>(count) > (data.size() - pos) / size) return false;
  start = data.data() + pos;
  pos += static_cast<std::size_t>(count) * size;
  return true;
}

}  // namespace

bool DecodeGround(std::span<const std::uint8_t> data, Ground& out, std::string* error) {
  out = Ground{};
  if (data.size() < 4 || std::memcmp(data.data(), "GRGN", 4) != 0) {
    // ParseUnversioned (004292e0) leaves the version byte that GetCell switches on unset.
    return Fail(error, "an unversioned ground, whose cells the client cannot read");
  }
  if (data.size() < kHeaderSize) return Fail(error, "the file is truncated");
  out.major = data[4];
  out.minor = data[5];
  if (out.minor > 7) return Fail(error, "an unsupported version");
  out.width = I32(&data[6]);
  out.height = I32(&data[10]);
  std::memcpy(&out.cell_size, &data[14], 4);
  const std::int32_t texture_count = I32(&data[18]);
  const std::int32_t name_size = I32(&data[22]);
  if (out.width < 0 || out.height < 0) return Fail(error, "the ground has a negative size");

  // Each name is a pointer into the file, which the texture cache reads as a C string.
  std::size_t pos = kHeaderSize;
  const std::uint8_t* p = nullptr;
  if (name_size < 0 || !Take(data, pos, texture_count, std::max<std::size_t>(name_size, 1), p))
    return Fail(error, "the texture names run past the file");
  pos = kHeaderSize;
  for (std::int32_t i = 0; i < texture_count; ++i) {
    const auto name = data.subspan(pos, static_cast<std::size_t>(name_size));
    const auto nul = std::find(name.begin(), name.end(), std::uint8_t{0});
    if (nul == name.end()) return Fail(error, "a texture name without a NUL in its record");
    out.textures.emplace_back(name.begin(), nul);
    pos += static_cast<std::size_t>(name_size);
  }

  if (data.size() - pos < kLightmapHeaderSize) return Fail(error, "the file is truncated");
  const std::int32_t lightmap_count = I32(&data[pos]);
  pos += kLightmapHeaderSize;  // the count, then three dwords the loader drops
  if (!Take(data, pos, lightmap_count, kLightmapSize, p))
    return Fail(error, "the lightmaps run past the file");
  out.lightmaps.resize(static_cast<std::size_t>(lightmap_count));
  for (GroundLightmap& lightmap : out.lightmaps) {
    std::memcpy(lightmap.alpha.data(), p, 64);
    std::memcpy(lightmap.color.data(), p + 64, 192);
    p += kLightmapSize;
  }

  if (data.size() - pos < 4) return Fail(error, "the file is truncated");
  const std::int32_t surface_count = I32(&data[pos]);
  pos += 4;
  if (!Take(data, pos, surface_count, kSurfaceSize, p))
    return Fail(error, "the surfaces run past the file");
  out.surfaces.resize(static_cast<std::size_t>(surface_count));
  for (GroundSurface& surface : out.surfaces) {
    std::memcpy(surface.u.data(), p, 16);
    std::memcpy(surface.v.data(), p + 16, 16);
    surface.texture = I16(p + 32);
    surface.lightmap = static_cast<std::uint16_t>(I16(p + 34));
    std::memcpy(&surface.color, p + 36, 4);
    p += kSurfaceSize;
  }

  const std::size_t cell_size = out.minor == 7 ? kCellSize : kShortCellSize;
  const std::size_t count =
      static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height);
  if (out.width != 0 && static_cast<std::size_t>(out.height) >
                            (data.size() - pos) / cell_size / static_cast<std::size_t>(out.width)) {
    return Fail(error, "the cells run past the file");
  }
  out.cells.resize(count);
  p = data.data() + pos;
  for (GroundCell& cell : out.cells) {
    std::memcpy(cell.heights.data(), p, 16);
    for (std::size_t k = 0; k < 3; ++k) {
      // Minor versions below 7 store the indices as shorts, which GetCell widens
      // (GndCell::SetFromRecord 00429070, SetFromRecordV6 004290b0).
      cell.surfaces[k] = cell_size == kCellSize ? I32(p + 16 + 4 * k) : I16(p + 16 + 2 * k);
    }
    p += cell_size;
  }
  return true;
}

std::unique_ptr<Resource> GndRes::Clone() const { return std::make_unique<GndRes>(); }

bool GndRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "cannot open " + file.path();
    return false;
  }
  error_.clear();
  return DecodeGround(file.data(), ground_, &error_);
}

void GndRes::Reset() {
  // Slot 4 (00429270) empties the names and drops the pointers to the other sections; the
  // size and the counts stay.
  ground_.textures.clear();
  ground_.lightmaps.clear();
  ground_.surfaces.clear();
  ground_.cells.clear();
}

void RegisterGroundTypes(ResourceManager& manager) {
  manager.RegisterType("gnd", "", std::make_unique<GndRes>());
}

}  // namespace ragexe::res
