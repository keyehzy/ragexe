#include "res/grf.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>

#include "res/grf_cipher.h"

namespace ragexe::res {

namespace {

constexpr char kMagic[16] = "Master of Magic";

std::uint32_t U32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

bool Fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

std::size_t RoundUpToBlock(std::size_t n) {
  return (n + grf::kBlockSize - 1) / grf::kBlockSize * grf::kBlockSize;
}

// The record fields after the name (004dde13).
void ReadFields(const std::uint8_t* p, GrfEntry& e) {
  e.size = U32(p + 8);
  e.compressed_size = static_cast<std::int32_t>(U32(p) - e.size - 0x2cb);
  e.stored_size = U32(p + 4) - 0x92cb;
  e.flags = p[12];
  e.offset = U32(p + 13);
}

constexpr std::size_t kFieldsSize = 17;

}  // namespace

std::optional<std::vector<GrfEntry>> ParseGrfTable(std::uint32_t version, std::uint32_t count,
                                                   std::vector<std::uint8_t> table,
                                                   std::string* error) {
  std::vector<GrfEntry> entries;
  std::size_t pos = 0;
  const std::size_t end = table.size();
  // Entries are at least 4 + kFieldsSize bytes, which bounds a corrupt count.
  if (count > end / (4 + kFieldsSize)) {
    Fail(error, "the entry count does not fit in the table");
    return std::nullopt;
  }
  entries.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    if (end - pos < 4) {
      Fail(error, "the table ends inside entry " + std::to_string(i));
      return std::nullopt;
    }
    const std::uint32_t len = U32(table.data() + pos);
    GrfEntry e;
    std::size_t fields;
    if (version >= 0x101) {
      // The name is bytes 2 to len - 4 of the len-byte name field (004ddd0d).
      if (len < 6 || len > end - pos - 4 || end - pos - 4 - len < kFieldsSize) {
        Fail(error, "entry " + std::to_string(i) + " runs past the table");
        return std::nullopt;
      }
      const std::size_t name_size = len - 6;
      std::vector<std::uint8_t> name(RoundUpToBlock(name_size));
      std::memcpy(name.data(), table.data() + pos + 6, name_size);
      grf::SwapNibbles(name);
      grf::DecodeMixed(name, 1);
      name.resize(name_size);
      auto nul = std::find(name.begin(), name.end(), std::uint8_t{0});
      e.name.assign(name.begin(), nul);
      fields = pos + 4 + len;
    } else {
      // Version 0x100: a NUL-terminated name with its nibbles swapped; the fields follow
      // the record at len, not 4 + len (004dde01).
      if (len > end - pos || end - pos - len < kFieldsSize) {
        Fail(error, "entry " + std::to_string(i) + " runs past the table");
        return std::nullopt;
      }
      std::vector<std::uint8_t> name(table.begin() + static_cast<std::ptrdiff_t>(pos + 4),
                                     table.begin() + static_cast<std::ptrdiff_t>(pos + std::max<std::size_t>(len, 4)));
      grf::SwapNibbles(name);
      auto nul = std::find(name.begin(), name.end(), std::uint8_t{0});
      e.name.assign(name.begin(), nul);
      fields = pos + len;
    }
    ReadFields(table.data() + fields, e);
    e.key = HashedFileName(e.name);
    entries.push_back(std::move(e));
    pos = fields + kFieldsSize;
  }
  return entries;
}

std::unique_ptr<GrfArchive> GrfArchive::Open(const std::filesystem::path& path, std::string* error) {
  std::unique_ptr<GrfArchive> grf(new GrfArchive());
  grf->file_.open(path, std::ios::binary);
  if (!grf->file_) {
    Fail(error, "cannot open " + path.string());
    return nullptr;
  }
  grf->file_.seekg(0, std::ios::end);
  grf->file_size_ = static_cast<std::uint64_t>(grf->file_.tellg());

  std::uint8_t header[kHeaderSize];
  if (!grf->ReadAt(0, header, kHeaderSize) || std::memcmp(header, kMagic, sizeof kMagic) != 0) {
    Fail(error, "not a GRF archive (no \"Master of Magic\" header)");
    return nullptr;
  }
  const std::uint32_t table_offset = U32(header + 0x1e);
  const std::uint32_t seed = U32(header + 0x22);
  const std::uint32_t count = U32(header + 0x26) - seed - 7;
  grf->version_ = U32(header + 0x2a);
  if (grf->version_ < 0x100 || grf->version_ > 0x103) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "unsupported GRF version 0x%x", grf->version_);
    Fail(error, buf);
    return nullptr;
  }
  const std::uint64_t table_start = std::uint64_t{kHeaderSize} + table_offset;
  if (table_start > grf->file_size_) {
    Fail(error, "the table offset is past the end of the file");
    return nullptr;
  }
  std::vector<std::uint8_t> table(static_cast<std::size_t>(grf->file_size_ - table_start));
  if (!grf->ReadAt(table_start, table.data(), table.size())) {
    Fail(error, "cannot read the table");
    return nullptr;
  }
  auto entries = ParseGrfTable(grf->version_, count, std::move(table), error);
  if (!entries) return nullptr;
  grf->entries_ = std::move(*entries);

  grf->sorted_.resize(grf->entries_.size());
  for (std::uint32_t i = 0; i < grf->sorted_.size(); ++i) grf->sorted_[i] = i;
  // Stable, so that of two equal names the first in the table is found.
  std::stable_sort(grf->sorted_.begin(), grf->sorted_.end(), [&](std::uint32_t a, std::uint32_t b) {
    return grf->entries_[a].key < grf->entries_[b].key;
  });
  return grf;
}

const GrfEntry* GrfArchive::Find(std::string_view name) const {
  const HashedFileName key(name);
  auto it = std::lower_bound(sorted_.begin(), sorted_.end(), key,
                             [&](std::uint32_t i, const HashedFileName& k) { return entries_[i].key < k; });
  if (it == sorted_.end() || !(entries_[*it].key == key)) return nullptr;
  return &entries_[*it];
}

std::vector<std::string> GrfArchive::DuplicateNames() const {
  std::vector<std::string> names;
  for (std::size_t i = 1; i < sorted_.size(); ++i) {
    if (entries_[sorted_[i]].key == entries_[sorted_[i - 1]].key &&
        (names.empty() || names.back() != entries_[sorted_[i]].key.name)) {
      names.push_back(entries_[sorted_[i]].key.name);
    }
  }
  return names;
}

bool GrfArchive::ReadAt(std::uint64_t offset, std::uint8_t* out, std::size_t n) const {
  if (offset > file_size_ || n > file_size_ - offset) return false;
  std::lock_guard lock(mutex_);
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(offset));
  file_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n));
  return static_cast<std::size_t>(file_.gcount()) == n;
}

std::optional<std::vector<std::uint8_t>> GrfArchive::Read(const GrfEntry& entry, std::string* error,
                                                         bool* damaged) const {
  if (damaged != nullptr) *damaged = false;
  const std::uint64_t start = std::uint64_t{kHeaderSize} + entry.offset;
  if (start > file_size_ || entry.stored_size > file_size_ - start) {
    Fail(error, entry.name + ": the data runs past the end of the archive");
    return std::nullopt;
  }
  // Decoding works on whole blocks; a partial last block takes the bytes after it in
  // the file, as the client's mapped view would give, and zeros past the end.
  std::vector<std::uint8_t> stored(RoundUpToBlock(entry.stored_size));
  const std::size_t available =
      static_cast<std::size_t>(std::min<std::uint64_t>(stored.size(), file_size_ - start));
  if (!ReadAt(start, stored.data(), available)) {
    Fail(error, entry.name + ": cannot read the data");
    return std::nullopt;
  }

  const bool first_blocks = grf::HasFirstBlocksExtension(entry.name);
  if (first_blocks) {
    if (entry.compressed()) grf::DecodeFirstBlocks(stored);
  } else {
    grf::DecodeMixed(stored, grf::DecimalDigits(entry.compressed_size));
  }

  std::vector<std::uint8_t> out(entry.size);
  if (!entry.compressed()) {
    if (entry.size > entry.stored_size) {
      Fail(error, entry.name + ": the size is larger than the stored data");
      return std::nullopt;
    }
    std::memcpy(out.data(), stored.data(), entry.size);
    return out;
  }
  // The client ignores uncompress's result (004de947, 004dea1d) and keeps the recorded
  // size, so a stream that breaks off gives what inflated before the break and zeros
  // after it (VirtualAlloc memory). data\wav\worm_tail_damage.wav is one: its stored
  // block lacks the last byte and the checksum.
  uLongf out_size = entry.size;
  uLong in_size = entry.stored_size;
  const int z = uncompress2(out.data(), &out_size, stored.data(), &in_size);
  if (z != Z_OK) {
    if (z == Z_MEM_ERROR || out_size > entry.size) {
      Fail(error, entry.name + ": zlib error " + std::to_string(z));
      return std::nullopt;
    }
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(out_size), out.end(), std::uint8_t{0});
    if (damaged != nullptr) *damaged = true;
    return out;
  }
  out.resize(out_size);
  return out;
}

}  // namespace ragexe::res
