#include "res/imf.h"

#include <utility>

#include "res/data_file.h"
#include "res/record_reader.h"

namespace ragexe::res {

namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

}  // namespace

bool DecodeImf(std::span<const std::uint8_t> data, Imf& out, std::string* error) {
  out = Imf{};
  RecordReader in(data);
  const char* truncated = "the file is truncated";
  if (!in.Value(out.version)) return Fail(error, truncated);
  // Below 1.01 (005e29a8), NaN included. 006060d8, in CP949, to the debug log: "%s:
  // reading failed! an old version's format. Save it again with the new version."
  if (!(out.version >= 1.01f)) return Fail(error, "an old version's format");
  std::int32_t last_layer = 0;
  if (!in.Value(out.field_4) || !in.Value(last_layer)) return Fail(error, truncated);
  // The layers are numbered 0 to last_layer.
  if (std::cmp_greater_equal(last_layer, kImfMaxLayers))
    return Fail(error, "more layers than the client's tables hold");
  out.layers.resize(last_layer < 0 ? 0 : static_cast<std::size_t>(last_layer) + 1);
  for (auto& actions : out.layers) {
    std::int32_t action_count = 0;
    if (!in.Value(action_count)) return Fail(error, truncated);
    if (std::cmp_greater(action_count, kImfMaxActions))
      return Fail(error, "more actions than the client's tables hold");
    // A negative count reads nothing, as in the original.
    actions.resize(action_count < 0 ? 0 : static_cast<std::size_t>(action_count));
    for (auto& frames : actions) {
      std::int32_t frame_count = 0;
      if (!in.Value(frame_count)) return Fail(error, truncated);
      if (frame_count > 0 && static_cast<std::size_t>(frame_count) > in.remaining() / 12)
        return Fail(error, truncated);
      frames.resize(frame_count < 0 ? 0 : static_cast<std::size_t>(frame_count));
      for (ImfFrame& frame : frames) {
        in.Value(frame.order);
        in.Value(frame.x);
        in.Value(frame.y);
      }
    }
  }
  return true;
}

std::unique_ptr<Resource> ImfRes::Clone() const { return std::make_unique<ImfRes>(); }

bool ImfRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    // 00606120, in CP949, to the debug log: "hey, the file %s seems to be missing."
    error_ = "cannot open " + file.path();
    return false;
  }
  error_.clear();
  return DecodeImf(file.data(), imf_, &error_);
}

void ImfRes::Reset() {
  // The base class's slot 4 (00420890), which CImfRes keeps, is empty.
}

void RegisterImfTypes(ResourceManager& manager) {
  manager.RegisterType("imf", "imf\\", std::make_unique<ImfRes>());
}

}  // namespace ragexe::res
