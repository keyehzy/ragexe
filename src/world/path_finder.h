#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "res/gat.h"

namespace ragexe::world {

// One step of a path: a cell, the direction it was entered by, and the time the walker
// reaches it (docs/world/path.md#the-path).
struct MoveStep {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t dir = 0;  // 0 is +y, counting anticlockwise in eighths; odd ones diagonal
  std::uint32_t time = 0;
};

// MovePath, the member CGameActor+0x170 (0x1c bytes).
struct MovePath {
  std::vector<MoveStep> steps;  // +0x4
  std::int32_t field_0x10 = 0;  // +0x10, zeroed by Reset (004ccf60)
  float start_x = 0.0f;         // +0x14: where in the first cell the walk starts, in cells
  float start_y = 0.0f;         // +0x18

  // GetEndKey (004cd1b0): the last step's time. The original reads before the vector when
  // it is empty.
  std::uint32_t EndTime() const { return steps.back().time; }

  // FindSegment (004cd1f0): the step the walker is heading for at a time, as its time
  // and cell, and the index of the step it left. Before the first step's time: the second
  // step, index 0; at or after the last step's: the last step, index -1. The path must
  // have two steps.
  int FindSegment(std::uint32_t time, std::uint32_t* step_time, float* x, float* y) const;
};

// What FindPath ended with; the original returns true only for kFound, and logs the
// two failures.
enum class PathResult {
  kFound,
  kSameCell,  // the start is the goal: nothing searched, the path untouched
  kNoPath,    // "find path failed : no path found" (0060c1d0)
  kOverflow,  // "find path aborted : buffer overflow" (0060c1f4): 149 cells searched
};

// PathFinder, the global g_PathFinder (00684e30): an A* search over the attribute map's
// cells (docs/world/path.md).
class PathFinder {
 public:
  // The nodes a search can take (004ccb80 refuses the 150th).
  static constexpr int kNodeCount = 0x95;

  // 004ccc50: the map CGameMode sets after loading a world (CWorld+0x40).
  void SetAttrMap(const res::AttrMap* map) { map_ = map; }

  // FindPath (004cc5c0): the path from cell (start_x, start_y), entered at sixteenths
  // (sub_x, sub_y) of a cell, to (goal_x, goal_y), its steps timed from `time` at `speed`
  // milliseconds per cell. Fills the path only when found.
  PathResult FindPath(std::uint32_t time, std::int32_t start_x, std::int32_t start_y,
                      std::int32_t sub_x, std::int32_t sub_y, std::int32_t goal_x,
                      std::int32_t goal_y, std::int32_t speed, MovePath& path);

 private:
  // 0x1c bytes from +0x10.
  struct Node {
    std::int32_t x = 0;
    std::int32_t y = 0;
    Node* parent = nullptr;
    std::int32_t g = 0;      // the cost from the start: 10 a side, 14 a diagonal
    std::int32_t f = 0;      // g and ten times the cells left along x and y
    std::int32_t state = 0;  // 0 unseen, 1 open, 2 closed
    std::int32_t dir = 0;
  };

  void Clear();
  Node* NewNode();
  Node* NodeAt(std::int32_t x, std::int32_t y);
  bool IsWalkable(std::int32_t x, std::int32_t y) const;
  bool CanStep(std::int32_t x, std::int32_t y, std::int32_t to_x, std::int32_t to_y) const;
  bool Visit(Node* from, std::int32_t cost, std::int32_t x, std::int32_t y, std::int32_t dir);
  std::int32_t Estimate(std::int32_t x, std::int32_t y) const;
  void SiftUp(std::size_t hole, Node* value);
  Node* PopOpen();
  void BuildPath(std::uint32_t time, Node* goal, std::int32_t speed, MovePath& path) const;

  const res::AttrMap* map_ = nullptr;    // +0x0
  std::int32_t goal_x_ = 0;              // +0x4
  std::int32_t goal_y_ = 0;              // +0x8
  std::int32_t used_ = 0;                // +0xc
  std::array<Node, 150> nodes_{};        // +0x10
  std::map<std::int32_t, Node*> cells_;  // +0x1078: by width * y + x
  std::vector<Node*> open_;              // +0x1088: a heap on f, least first
};

// CGameActor::FindMovePath (00506b20): FindPath from the actor's last move time
// (CGameActor+0x18c); when that path would already have ended by `now` (the server's
// time, ClientState 00593200), the search again from now at ten thirteenths of the speed,
// and now becomes the move time.
PathResult FindMovePath(PathFinder& finder, MovePath& path, std::uint32_t& move_time,
                        std::uint32_t now, std::int32_t start_x, std::int32_t start_y,
                        std::int32_t sub_x, std::int32_t sub_y, std::int32_t goal_x,
                        std::int32_t goal_y, std::int32_t speed);

// CGameMode::IsPathFound (00511ec0): whether FindPath finds a path, at 150 ms a cell from
// `now` (timeGetTime), into a path thrown away.
bool IsPathFound(PathFinder& finder, std::uint32_t now, std::int32_t start_x, std::int32_t start_y,
                 std::int32_t sub_x, std::int32_t sub_y, std::int32_t goal_x, std::int32_t goal_y);

}  // namespace ragexe::world
