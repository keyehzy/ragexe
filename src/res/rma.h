#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/resource.h"

namespace ragexe::res {

// CTokenizer::Tokenize (004e4190) on one line (docs/formats/rma.md).
std::vector<std::string> TokenizeLine(std::string_view line);

// One "#action" line's values, as CRmaRes keeps them in a 0x1c-byte record.
struct RmaAction {
  std::int32_t value_0 = -1;       // the fourth token; -1 marks an action never defined
  std::int32_t value_4 = -1;       // the fifth
  std::int32_t value_8 = 4;        // the third
  std::vector<std::int32_t> list;  // the sixth and later
  std::int32_t rsx = 0;            // index into Rma::rsx_names of the last "#rsxname" before it
};

struct Rma {
  std::vector<std::string> rsx_names;
  std::vector<RmaAction> actions;  // indexed by action number
  float interior_zoom = 1.0f;      // the constructor's defaults (00421c90)
  float shadow_zoom = 1.0f;
  std::int32_t interior_offset_x = 0;
  std::int32_t interior_offset_y = 0;
  std::array<std::int32_t, 2> interior_frames{};
  // The messages the original shows while reading, in order.
  std::vector<std::string> messages;
};

// The port refuses action numbers above this; the original would allocate a record for
// every number up to the largest.
inline constexpr std::int32_t kRmaMaxAction = 0xffff;

// CRmaRes::Load (004220d0) on a whole .rma file.
bool DecodeRma(std::span<const std::uint8_t> data, Rma& out, std::string* error);

// CRmaRes.
class RmaRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const Rma& rma() const { return rma_; }
  const std::string& error() const override { return error_; }

 private:
  Rma rma_;
  std::string error_;
};

// The original's registration (static initializer 00421b10).
void RegisterRmaTypes(ResourceManager& manager);

}  // namespace ragexe::res
