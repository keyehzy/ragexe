#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/resource.h"

namespace ragexe::res {

// What Miles' AIL_file_type (Mss32.dll 6.1a) says of a file, as far as CWave::Load tells
// the answers apart (docs/formats/wav.md).
enum class WaveFileType {
  kOther,       // any other answer: the client keeps the file as it is
  kPcm,         // AILFILETYPE_PCM_WAV
  kImaAdpcm,    // AILFILETYPE_ADPCM_WAV: IMA ADPCM, 4 bits a sample
  kMpegLayer3,  // AILFILETYPE_MPEG_L3_AUDIO
};

// AIL_file_type (211277a0); nullopt where it reads past the data or never ends.
std::optional<WaveFileType> ClassifyWave(std::span<const std::uint8_t> data);

// What AIL_WAV_info (21114ad0) reads from a wave file.
struct WaveInfo {
  std::uint16_t format = 0;  // the format tag
  std::uint16_t channels = 0;
  std::uint32_t rate = 0;
  std::uint16_t bits = 0;
  std::uint16_t block_align = 0;
  std::uint32_t data_offset = 0;  // from the start of the file
  std::uint32_t data_size = 0;    // as recorded; may run past the file
  std::uint32_t samples = 0;      // per channel: from "fact" for IMA ADPCM, else computed
};

// The result of AIL_WAV_info: no info when it does not find a wave. It faults where it
// reads past the data or divides by zero, and where its chunk walk never ends.
struct WaveInfoResult {
  bool faults = false;
  std::optional<WaveInfo> info;
};
WaveInfoResult ReadWaveInfo(std::span<const std::uint8_t> data);

struct Wave {
  WaveFileType type = WaveFileType::kOther;
  // What the client plays, at +0x110:
  // - the file as it is, unless it is IMA ADPCM or MPEG Layer III;
  // - for IMA ADPCM, the 16-bit PCM wave AIL_decompress_ADPCM makes, empty when it makes
  //   none;
  // - for MPEG Layer III, the file: the client decodes it through Mp3dec.asi and the port
  //   leaves that to the sound layer.
  std::vector<std::uint8_t> sample;
};

// CWave::Load (004354a0) on a whole file.
bool DecodeWave(std::span<const std::uint8_t> data, Wave& out, std::string* error);

// CWave.
class WaveRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const Wave& wave() const { return wave_; }
  const std::string& error() const override { return error_; }

 private:
  Wave wave_;
  std::string error_;
};

// The original's registration (static initializer 00435300).
void RegisterWaveTypes(ResourceManager& manager);

}  // namespace ragexe::res
