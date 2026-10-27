#include "res/wav.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "res/data_files.h"
#include "res/resource.h"

using namespace ragexe::res;

namespace {

using Bytes = std::vector<std::uint8_t>;

void Put(Bytes& out, std::uint32_t v, int size) {
  for (int k = 0; k < size; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}

void Append(Bytes& out, std::string_view text) { out.insert(out.end(), text.begin(), text.end()); }

Bytes Chunk(std::string_view tag, const Bytes& body) {
  Bytes out;
  Append(out, tag);
  Put(out, static_cast<std::uint32_t>(body.size()), 4);
  out.insert(out.end(), body.begin(), body.end());
  if (body.size() % 2 != 0) out.push_back(0);
  return out;
}

Bytes Fmt(std::uint16_t tag, std::uint16_t channels, std::uint16_t bits, std::uint16_t align) {
  Bytes body;
  Put(body, tag, 2);
  Put(body, channels, 2);
  Put(body, 22050, 4);
  Put(body, 22050u * align, 4);
  Put(body, align, 2);
  Put(body, bits, 2);
  return Chunk("fmt ", body);
}

Bytes Riff(const std::vector<Bytes>& chunks) {
  Bytes body;
  Append(body, "WAVE");
  for (const Bytes& c : chunks) body.insert(body.end(), c.begin(), c.end());
  Bytes out;
  Append(out, "RIFF");
  Put(out, static_cast<std::uint32_t>(body.size()), 4);
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

Bytes Fact(std::uint32_t samples) {
  Bytes body;
  Put(body, samples, 4);
  return Chunk("fact", body);
}

// The block header of one channel: a first sample and a step index.
void PutHeader(Bytes& out, std::int16_t sample, std::uint8_t index) {
  Put(out, static_cast<std::uint16_t>(sample), 2);
  out.push_back(index);
  out.push_back(0);
}

std::int16_t Sample(const Wave& wave, std::size_t n) {
  std::int16_t v = 0;
  std::memcpy(&v, &wave.sample[44 + 2 * n], 2);
  return v;
}

std::uint32_t Field(const Wave& wave, std::size_t pos, int size) {
  std::uint32_t v = 0;
  for (int k = 0; k < size; ++k) v |= static_cast<std::uint32_t>(wave.sample[pos + k]) << (8 * k);
  return v;
}

}  // namespace

TEST_CASE("PCM and other waves are kept as they are") {
  Bytes samples;
  Put(samples, 0x1234, 2);
  const Bytes pcm = Riff({Fmt(1, 1, 16, 2), Chunk("data", samples)});
  Wave wave;
  std::string error;
  REQUIRE(DecodeWave(pcm, wave, &error));
  CHECK(wave.type == WaveFileType::kPcm);
  CHECK(wave.sample == pcm);
  const Bytes ms_adpcm = Riff({Fmt(2, 1, 4, 256), Chunk("data", samples)});
  REQUIRE(DecodeWave(ms_adpcm, wave, &error));
  CHECK(wave.type == WaveFileType::kOther);
  CHECK(wave.sample == ms_adpcm);
  REQUIRE(DecodeWave(Bytes{}, wave, &error));
  CHECK(wave.sample.empty());
}

TEST_CASE("IMA ADPCM is decoded to a 16-bit PCM wave") {
  // One mono block of 8 bytes: the header (sample 0, index 0), then nibbles 4, 0xc, 7, 0.
  Bytes data;
  PutHeader(data, 0, 0);
  for (int b : {0xc4, 0x07, 0x00, 0x00}) data.push_back(static_cast<std::uint8_t>(b));
  const Bytes file = Riff({Fmt(0x11, 1, 4, 8), Fact(9), Chunk("data", data)});
  Wave wave;
  std::string error;
  REQUIRE(DecodeWave(file, wave, &error));
  CHECK(wave.type == WaveFileType::kImaAdpcm);
  REQUIRE(wave.sample.size() == 44u + 18u);
  CHECK(std::memcmp(wave.sample.data(), "RIFF", 4) == 0);
  CHECK(Field(wave, 4, 4) == 18u + 36u);
  CHECK(Field(wave, 20, 2) == 1u);  // PCM
  CHECK(Field(wave, 22, 2) == 1u);  // channels
  CHECK(Field(wave, 28, 4) == 44100u);
  CHECK(Field(wave, 32, 2) == 2u);
  CHECK(Field(wave, 34, 2) == 16u);
  CHECK(Field(wave, 40, 4) == 18u);
  CHECK(Sample(wave, 0) == 0);
  // Step 7: nibble 4 adds 7 >> 3 + 7, and the index rises by 2 to step 9.
  CHECK(Sample(wave, 1) == 7);
  // Step 9: nibble 0xc subtracts 9 >> 3 + 9; the index rises by 2 to 4, step 11.
  CHECK(Sample(wave, 2) == -3);
  // Step 11: nibble 7 adds 11 >> 3 + 11 + 5 + 2.
  CHECK(Sample(wave, 3) == 16);
  // Nibble 0 still adds step >> 3: 23 >> 3 at index 12.
  CHECK(Sample(wave, 4) == 18);
}

TEST_CASE("IMA ADPCM: saturation, stereo, and samples the data does not reach") {
  Bytes data;
  PutHeader(data, 32760, 88);
  for (int k = 0; k < 4; ++k) data.push_back(0x77);
  PutHeader(data, -32760, 88);
  for (int k = 0; k < 4; ++k) data.push_back(0xff);
  const Bytes file = Riff({Fmt(0x11, 1, 4, 8), Fact(20), Chunk("data", data)});
  Wave wave;
  std::string error;
  REQUIRE(DecodeWave(file, wave, &error));
  CHECK(Sample(wave, 1) == 32767);
  CHECK(Sample(wave, 9) == -32760);
  CHECK(Sample(wave, 10) == -32768);
  CHECK(Sample(wave, 18) == 0);  // past the data: still zero
  // Stereo: a header per channel, then a word of each, interleaved by sample.
  Bytes stereo;
  PutHeader(stereo, 100, 0);
  PutHeader(stereo, -100, 0);
  for (int b : {0x04, 0, 0, 0, 0x0c, 0, 0, 0}) stereo.push_back(static_cast<std::uint8_t>(b));
  const Bytes two = Riff({Fmt(0x11, 2, 4, 16), Fact(9), Chunk("data", stereo)});
  REQUIRE(DecodeWave(two, wave, &error));
  CHECK(Sample(wave, 0) == 100);
  CHECK(Sample(wave, 1) == -100);
  CHECK(Sample(wave, 2) == 107);
  CHECK(Sample(wave, 3) == -107);
}

TEST_CASE("IMA ADPCM with no samples has no sample; a bad index fails") {
  Bytes data;
  PutHeader(data, 0, 0);
  for (int k = 0; k < 4; ++k) data.push_back(0x11);
  Wave wave;
  std::string error;
  REQUIRE(DecodeWave(Riff({Fmt(0x11, 1, 4, 8), Fact(0), Chunk("data", data)}), wave, &error));
  CHECK(wave.type == WaveFileType::kImaAdpcm);
  CHECK(wave.sample.empty());
  data[2] = 89;
  CHECK_FALSE(DecodeWave(Riff({Fmt(0x11, 1, 4, 8), Fact(9), Chunk("data", data)}), wave, &error));
  // Without "fact", the count comes from the blocks: 1 + 8 per word, per block.
  data[2] = 0;
  REQUIRE(DecodeWave(Riff({Fmt(0x11, 1, 4, 8), Chunk("data", data)}), wave, &error));
  CHECK(wave.sample.size() == 44u + 18u);
}

TEST_CASE("the file type: MPEG syncs, signatures, and tags read without regard to case") {
  Bytes mp3 = {'I', 'D', '3', 0, 0, 0, 0, 0, 0, 0, 0xff, 0xfb, 0x90, 0x00};
  mp3.resize(600, 0);
  CHECK(ClassifyWave(mp3) == WaveFileType::kMpegLayer3);
  Bytes layer2;
  for (int k = 0; k < 50; ++k) {
    for (int b : {0xff, 0xfd, 0x90, 0x00}) layer2.push_back(static_cast<std::uint8_t>(b));
    layer2.resize(layer2.size() + 100, 0);
  }
  CHECK(ClassifyWave(layer2) == WaveFileType::kOther);
  CHECK(ClassifyWave(Bytes(layer2.begin(), layer2.begin() + 416)) == std::nullopt);
  Bytes voc;
  Append(voc, "Creative Voice File");
  voc.insert(voc.end(), mp3.begin(), mp3.end());
  CHECK(ClassifyWave(voc) == WaveFileType::kOther);
  Bytes samples(10, 0);
  Bytes lower = Riff({Fmt(1, 1, 16, 2), Chunk("data", samples)});
  std::memcpy(lower.data() + 8, "wave", 4);
  CHECK(ClassifyWave(lower) == WaveFileType::kPcm);
  std::memcpy(lower.data(), "JUNK", 4);  // RIFF itself is never checked
  CHECK(ClassifyWave(lower) == WaveFileType::kPcm);
  CHECK(ClassifyWave(Bytes{'R', 'I', 'F', 'F', 16, 0, 0, 0}) == std::nullopt);
  Wave wave;
  std::string error;
  CHECK_FALSE(DecodeWave(Bytes{'R', 'I', 'F', 'F', 16, 0, 0, 0}, wave, &error));
  // A chunk size of 0xfffffff8 steps the walk by 0, so Miles walks forever.
  Bytes loops;
  Append(loops, "RIFF");
  Put(loops, 100, 4);
  Append(loops, "WAVELIST");
  Put(loops, 0xfffffff8u, 4);
  loops.resize(loops.size() + 16, 0);
  CHECK(ClassifyWave(loops) == std::nullopt);
}

TEST_CASE("WaveRes loads from wav\\ through the resource manager") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_wav_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "wav");
  const Bytes pcm = Riff({Fmt(1, 1, 16, 2), Chunk("data", Bytes(8, 1))});
  {
    std::ofstream f(dir / "data" / "wav" / "hit.wav", std::ios::binary);
    f.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(pcm.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterWaveTypes(manager);
  auto* hit = static_cast<WaveRes*>(manager.Get("Hit.wav"));
  REQUIRE(hit != nullptr);
  CHECK(hit->wave().sample == pcm);
  CHECK(manager.Get("miss.wav") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
