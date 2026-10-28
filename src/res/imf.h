#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/resource.h"

namespace ragexe::res {

// One frame of one action of one layer (docs/formats/imf.md).
struct ImfFrame {
  // CImfRes::FindEntryWithValue (0042a220) looks for the layer whose value here is the one
  // asked for: the order layers are drawn in.
  std::int32_t order = 0;
  std::int32_t x = 0;  // CImfRes::GetRecordPoint (0042a2d0) gives x and y as a point
  std::int32_t y = 0;
};

struct Imf {
  float version = 0.0f;
  std::uint32_t field_4 = 0;  // read and dropped
  // Layers, each a list of actions, each a list of frames.
  std::vector<std::vector<std::vector<ImfFrame>>> layers;
};

// The client's count tables hold this many layers and actions per layer.
inline constexpr std::size_t kImfMaxLayers = 15;
inline constexpr std::size_t kImfMaxActions = 104;

// CImfRes::Load (00429c40) on a whole .imf file.
bool DecodeImf(std::span<const std::uint8_t> data, Imf& out, std::string* error);

// CImfRes.
class ImfRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const Imf& imf() const { return imf_; }
  const std::string& error() const override { return error_; }

 private:
  Imf imf_;
  std::string error_;
};

// The original's registration (static initializer 004299a0).
void RegisterImfTypes(ResourceManager& manager);

}  // namespace ragexe::res
