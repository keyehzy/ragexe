#include "world/path_finder.h"

#include <cmath>
#include <cstddef>

namespace ragexe::world {

namespace {

// 005e8a7c: a sixteenth of a cell.
constexpr float kSubCell = 0.0625f;
// 005e8a80: a diagonal step's time over a side's.
constexpr float kDiagonal = 1.414f;

std::int32_t Abs(std::int32_t v) {
  const auto u = static_cast<std::uint32_t>(v);
  const std::uint32_t sign = v < 0 ? 0xffffffffu : 0u;
  return static_cast<std::int32_t>((u ^ sign) - sign);
}

std::int32_t Wrap(std::int64_t v) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}

// __ftol: toward zero.
std::int32_t Truncate(float v) { return static_cast<std::int32_t>(v); }

}  // namespace

int MovePath::FindSegment(std::uint32_t time, std::uint32_t* step_time, float* x, float* y) const {
  if (time < steps[0].time) {
    *step_time = steps[1].time;
    *x = static_cast<float>(steps[1].x);
    *y = static_cast<float>(steps[1].y);
    return 0;
  }
  const int count = static_cast<int>(steps.size());
  int i = 0;
  for (; i < count - 1; ++i) {
    if (steps[i].time <= time && time < steps[i + 1].time) {
      *step_time = steps[i + 1].time;
      *x = static_cast<float>(steps[i + 1].x);
      *y = static_cast<float>(steps[i + 1].y);
      return i;
    }
  }
  *step_time = steps[i].time;
  *x = static_cast<float>(steps[i].x);
  *y = static_cast<float>(steps[i].y);
  return -1;
}

// 004cc4e0: the cell index and the open list emptied. The nodes keep their fields.
void PathFinder::Clear() {
  cells_.clear();
  open_.clear();
}

// 004ccb80.
PathFinder::Node* PathFinder::NewNode() {
  if (used_ > kNodeCount - 1) return nullptr;
  return &nodes_[static_cast<std::size_t>(used_++)];
}

// 004ccba0: the cell's node, a new unseen one when it has none; null when the nodes have
// run out.
PathFinder::Node* PathFinder::NodeAt(std::int32_t x, std::int32_t y) {
  const std::int32_t key = Wrap(static_cast<std::int64_t>(map_->width) * y + x);
  if (const auto it = cells_.find(key); it != cells_.end() && it->second != nullptr)
    return it->second;
  Node* node = NewNode();
  if (node == nullptr) return nullptr;
  node->x = x;
  node->y = y;
  node->state = 0;
  cells_[key] = node;
  return node;
}

bool PathFinder::IsWalkable(std::int32_t x, std::int32_t y) const {
  const std::int32_t attribute =
      map_->cells[static_cast<std::size_t>(map_->width) * static_cast<std::size_t>(y) +
                  static_cast<std::size_t>(x)]
          .attribute;
  return attribute != 1 && attribute != 5;
}

// 004ccc80: whether a walker on (x, y) may step to the next cell (to_x, to_y): it is on the
// map and neither attribute 1 nor 5, and for a diagonal step so are both cells beside it.
bool PathFinder::CanStep(std::int32_t x, std::int32_t y, std::int32_t to_x,
                         std::int32_t to_y) const {
  const std::int32_t width = map_->width;
  const std::int32_t height = map_->height;
  if (to_x < 0 || to_y < 0 || to_x >= width || to_y >= height || !IsWalkable(to_x, to_y))
    return false;
  if (x == to_x || y == to_y) return true;
  return x >= 0 && x < width && IsWalkable(x, to_y) && y >= 0 && y < height && IsWalkable(to_x, y);
}

std::int32_t PathFinder::Estimate(std::int32_t x, std::int32_t y) const {
  return Wrap((static_cast<std::int64_t>(Abs(Wrap(static_cast<std::int64_t>(x) - goal_x_))) +
               Abs(Wrap(static_cast<std::int64_t>(y) - goal_y_))) *
              10);
}

// The heap's sift-up (std::HeapSiftUp 004ce400, and inline in 004cc5c0 and 004cc9d0): the
// value moves up past every parent with a greater f.
void PathFinder::SiftUp(std::size_t hole, Node* value) {
  while (hole > 0) {
    const std::size_t parent = (hole - 1) / 2;
    if (open_[parent]->f <= value->f) break;
    open_[hole] = open_[parent];
    hole = parent;
  }
  open_[hole] = value;
}

// The heap's pop, inline in 004cc5c0: the hole left at the top goes down to the smaller
// child, the right one on a tie, to the last level; the last value then sifts up from it.
PathFinder::Node* PathFinder::PopOpen() {
  Node* top = open_.front();
  Node* value = open_.back();
  open_.back() = top;
  const std::size_t length = open_.size() - 1;
  std::size_t hole = 0;
  std::size_t child = 2;
  for (; child < length; child = 2 * child + 2) {
    if (open_[child - 1]->f < open_[child]->f) --child;
    open_[hole] = open_[child];
    hole = child;
  }
  if (child == length) {
    open_[hole] = open_[length - 1];
    hole = length - 1;
  }
  SiftUp(hole, value);
  open_.pop_back();
  return top;
}

// 004cc9d0: the cell (x, y) reached from `from` by a step of `cost` in direction `dir`. An
// unseen cell opens; a cell reached more cheaply than before takes the new parent and costs,
// and moves up the heap if open, or opens again if closed without going back on the heap.
// False only when a new node is needed and none is left.
bool PathFinder::Visit(Node* from, std::int32_t cost, std::int32_t x, std::int32_t y,
                       std::int32_t dir) {
  const std::int32_t g = Wrap(static_cast<std::int64_t>(from->g) + cost);
  Node* node = NodeAt(x, y);
  if (node == nullptr) return false;
  if (node->state == 0) {
    node->state = 1;
    node->parent = from;
    node->g = g;
    node->f = Wrap(static_cast<std::int64_t>(g) + Estimate(x, y));
    node->dir = dir;
    open_.push_back(node);
    SiftUp(open_.size() - 1, node);
    return true;
  }
  if (node->g <= g) return true;
  node->parent = from;
  node->g = g;
  node->f = Wrap(static_cast<std::int64_t>(g) + Estimate(x, y));
  node->dir = dir;
  if (node->state == 2) {
    node->state = 1;
    return true;
  }
  if (node->state != 1) return true;
  for (std::size_t i = 0; i < open_.size(); ++i) {
    if (open_[i]->x == node->x && open_[i]->y == node->y) {
      SiftUp(i, open_[i]);
      break;
    }
  }
  return true;
}

// 004ccda0: the steps from the start to the goal, the first at `time` and the second when
// the walker has gone straight from the start's point to its cell; after that a side takes
// `speed` and a diagonal 1.414 times it (004cced0).
void PathFinder::BuildPath(std::uint32_t time, Node* goal, std::int32_t speed,
                           MovePath& path) const {
  std::size_t count = 0;
  for (const Node* n = goal; n != nullptr; n = n->parent) ++count;
  path.steps.resize(count);
  if (count < 2) return;
  std::size_t i = count;
  for (const Node* n = goal; n != nullptr; n = n->parent) {
    --i;
    path.steps[i].x = n->x;
    path.steps[i].y = n->y;
    path.steps[i].dir = n->dir;
  }
  const std::int32_t diagonal = Truncate(static_cast<float>(speed) * kDiagonal);
  path.steps[0].time = time;
  const float dx = static_cast<float>(path.steps[1].x) - path.start_x;
  const float dy = static_cast<float>(path.steps[1].y) - path.start_y;
  const float distance = std::sqrt(dx * dx + dy * dy);
  path.steps[1].time =
      time + static_cast<std::uint32_t>(Truncate(distance * static_cast<float>(speed)));
  for (std::size_t k = 2; k < count; ++k) {
    const std::int32_t step = path.steps[k].dir % 2 == 0 ? speed : diagonal;
    path.steps[k].time = path.steps[k - 1].time + static_cast<std::uint32_t>(step);
  }
}

PathResult PathFinder::FindPath(std::uint32_t time, std::int32_t start_x, std::int32_t start_y,
                                std::int32_t sub_x, std::int32_t sub_y, std::int32_t goal_x,
                                std::int32_t goal_y, std::int32_t speed, MovePath& path) {
  goal_x_ = goal_x;
  goal_y_ = goal_y;
  if (start_x == goal_x && start_y == goal_y) return PathResult::kSameCell;
  path.field_0x10 = 0;
  Clear();
  used_ = 0;
  Node* start = NodeAt(start_x, start_y);
  start->state = 1;
  start->parent = nullptr;
  start->g = 0;
  start->f = Estimate(start_x, start_y);
  open_.push_back(start);
  SiftUp(open_.size() - 1, start);

  // The eight cells around, from x+1, y-1 round to x, y-1; the direction each is entered by.
  struct Step {
    std::int32_t dx, dy, cost, dir;
  };
  static constexpr std::array<Step, 8> kSteps{{{1, -1, 14, 5},
                                               {1, 0, 10, 6},
                                               {1, 1, 14, 7},
                                               {0, 1, 10, 0},
                                               {-1, 1, 14, 1},
                                               {-1, 0, 10, 2},
                                               {-1, -1, 14, 3},
                                               {0, -1, 10, 4}}};
  while (!open_.empty()) {
    Node* node = PopOpen();
    if (node->x == goal_x && node->y == goal_y) {
      float x = static_cast<float>(sub_x) * kSubCell;
      if (start_x > 0) x = (static_cast<float>(start_x) - 0.5f) + x;
      path.start_x = x;
      float y = static_cast<float>(sub_y) * kSubCell;
      if (start_y > 0) y = (static_cast<float>(start_y) - 0.5f) + y;
      path.start_y = y;
      BuildPath(time, node, speed, path);
      return PathResult::kFound;
    }
    for (const Step& s : kSteps) {
      const std::int32_t to_x = Wrap(static_cast<std::int64_t>(node->x) + s.dx);
      const std::int32_t to_y = Wrap(static_cast<std::int64_t>(node->y) + s.dy);
      if (CanStep(node->x, node->y, to_x, to_y) && !Visit(node, s.cost, to_x, to_y, s.dir)) {
        return PathResult::kOverflow;
      }
    }
    node->state = 2;
  }
  return PathResult::kNoPath;
}

PathResult FindMovePath(PathFinder& finder, MovePath& path, std::uint32_t& move_time,
                        std::uint32_t now, std::int32_t start_x, std::int32_t start_y,
                        std::int32_t sub_x, std::int32_t sub_y, std::int32_t goal_x,
                        std::int32_t goal_y, std::int32_t speed) {
  const PathResult found =
      finder.FindPath(move_time, start_x, start_y, sub_x, sub_y, goal_x, goal_y, speed, path);
  if (found != PathResult::kFound || path.EndTime() > now) return found;
  move_time = now;
  return finder.FindPath(now, start_x, start_y, sub_x, sub_y, goal_x, goal_y,
                         Wrap(speed * 10LL) / 13, path);
}

bool IsPathFound(PathFinder& finder, std::uint32_t now, std::int32_t start_x, std::int32_t start_y,
                 std::int32_t sub_x, std::int32_t sub_y, std::int32_t goal_x, std::int32_t goal_y) {
  MovePath path;
  return finder.FindPath(now, start_x, start_y, sub_x, sub_y, goal_x, goal_y, 150, path) ==
         PathResult::kFound;
}

}  // namespace ragexe::world
