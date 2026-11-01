#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "res/gat.h"
#include "world/path_finder.h"

using ragexe::res::AttrMap;
using ragexe::world::FindMovePath;
using ragexe::world::IsPathFound;
using ragexe::world::MovePath;
using ragexe::world::PathFinder;
using ragexe::world::PathResult;

namespace {

struct PathMapVector {
  const char* name;
  int width, height;
  const char* cells;
};

struct PathQueryVector {
  int map;
  const char* query;
  const char* result;  // as ragres find-paths prints it
  const char* steps;   // x y dir time per step, after F and M
};

#include "path_vectors.inc"

AttrMap MakeMap(const PathMapVector& v) {
  AttrMap map;
  map.major = 1;
  map.minor = 2;
  map.width = v.width;
  map.height = v.height;
  map.cells.resize(static_cast<std::size_t>(v.width) * static_cast<std::size_t>(v.height));
  for (std::size_t k = 0; k < map.cells.size(); ++k) {
    switch (v.cells[k]) {
      case '#': map.cells[k].attribute = 1; break;
      case 'o': map.cells[k].attribute = 5; break;
      case '3': map.cells[k].attribute = 3; break;
      case '6': map.cells[k].attribute = 6; break;
      default: break;
    }
  }
  return map;
}

const char* ResultName(PathResult r) {
  switch (r) {
    case PathResult::kFound: return "found";
    case PathResult::kSameCell: return "same";
    case PathResult::kNoPath: return "none";
    case PathResult::kOverflow: return "overflow";
  }
  return "?";
}

std::uint64_t Fnv1a64(const std::vector<std::uint32_t>& values) {
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (std::uint32_t v : values) {
    for (int k = 0; k < 4; ++k) h = (h ^ ((v >> (8 * k)) & 0xff)) * 0x100000001b3ull;
  }
  return h;
}

std::string Steps(const MovePath& path) {
  std::string out;
  for (const auto& s : path.steps) {
    if (!out.empty()) out += ' ';
    out += std::to_string(static_cast<std::uint32_t>(s.x)) + ' ' +
           std::to_string(static_cast<std::uint32_t>(s.y)) + ' ' +
           std::to_string(static_cast<std::uint32_t>(s.dir)) + ' ' + std::to_string(s.time);
  }
  return out;
}

std::string PathLine(PathResult r, const std::string& move_time, const MovePath& path) {
  std::vector<std::uint32_t> values;
  for (const auto& s : path.steps) {
    values.push_back(static_cast<std::uint32_t>(s.x));
    values.push_back(static_cast<std::uint32_t>(s.y));
    values.push_back(static_cast<std::uint32_t>(s.dir));
    values.push_back(s.time);
  }
  char line[128];
  std::snprintf(line, sizeof line, "%s %s %d %08x %08x %zu %016llx", ResultName(r),
                move_time.c_str(), path.field_0x10, std::bit_cast<std::uint32_t>(path.start_x),
                std::bit_cast<std::uint32_t>(path.start_y), path.steps.size(),
                static_cast<unsigned long long>(Fnv1a64(values)));
  return line;
}

}  // namespace

TEST_CASE("The path finder searches, times and reports as the original does", "[path]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const PathQueryVector& v : kPathQueries) {
    if (v.map != current) {
      current = v.map;
      map = MakeMap(kPathMaps[current]);
      finder = PathFinder();
      finder.SetAttrMap(&map);
      path = MovePath();
    }
    INFO(kPathMaps[current].name << ": " << v.query);
    std::istringstream fields(v.query);
    char kind = 0;
    fields >> kind;
    std::uint32_t time = 0, now = 0;
    std::int32_t sx = 0, sy = 0, sub_x = 0, sub_y = 0, gx = 0, gy = 0, speed = 0;
    std::string got;
    if (kind == 'F') {
      fields >> time >> sx >> sy >> sub_x >> sub_y >> gx >> gy >> speed;
      got = PathLine(finder.FindPath(time, sx, sy, sub_x, sub_y, gx, gy, speed, path), "-", path);
    } else if (kind == 'M') {
      fields >> time >> now >> sx >> sy >> sub_x >> sub_y >> gx >> gy >> speed;
      const PathResult r =
          FindMovePath(finder, path, time, now, sx, sy, sub_x, sub_y, gx, gy, speed);
      got = PathLine(r, std::to_string(time), path);
    } else if (kind == 'I') {
      fields >> now >> sx >> sy >> sub_x >> sub_y >> gx >> gy;
      got = IsPathFound(finder, now, sx, sy, sub_x, sub_y, gx, gy) ? "1" : "0";
    } else if (kind == 'Z') {
      fields >> path.field_0x10;
      got = std::to_string(path.field_0x10);
    } else {
      fields >> time;
      if (path.steps.size() < 2) {
        got = "-";
      } else {
        std::uint32_t step_time = 0;
        float x = 0.0f, y = 0.0f;
        const int index = path.FindSegment(time, &step_time, &x, &y);
        char line[64];
        std::snprintf(line, sizeof line, "%d %u %08x %08x", index, step_time,
                      std::bit_cast<std::uint32_t>(x), std::bit_cast<std::uint32_t>(y));
        got = line;
      }
    }
    CHECK(got == v.result);
    if (kind == 'F' || kind == 'M') CHECK(Steps(path) == v.steps);
  }
}

TEST_CASE("The path finder gives up when a search needs a 150th node", "[path]") {
  AttrMap map;
  map.width = 30;
  map.height = 30;
  map.cells.resize(900);
  map.cells[28 * 30 + 28].attribute = 1;
  PathFinder finder;
  finder.SetAttrMap(&map);
  MovePath path;
  CHECK(finder.FindPath(0, 1, 1, 8, 8, 6, 1, 150, path) == PathResult::kFound);
  CHECK(path.steps.size() == 6);
  CHECK(finder.FindPath(0, 1, 1, 8, 8, 28, 28, 150, path) == PathResult::kOverflow);
  CHECK(path.steps.size() == 6);
  CHECK(path.field_0x10 == 0);
}
