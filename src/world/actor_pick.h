#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "world/actor_walk.h"

namespace ragexe::world {

// What a pickable object's drawing leaves to be found under the mouse (0x28 bytes,
// docs/world/pick.md): a screen rectangle, a depth that ranks the rectangles, and who it is.
struct PickRecord {
  float left = 0.0f;       // +0x0
  float top = 0.0f;        // +0x4
  float depth = 0.0f;      // +0x8, the point's 1/z plus the bias
  float right = 0.0f;      // +0xc
  float bottom = 0.0f;     // +0x10
  float depth_2 = 0.0f;    // +0x14, the same again
  std::int32_t id = 0;     // +0x18
  std::int32_t job = 0;    // +0x1c
  std::int32_t kind = 0;   // +0x20, a PickKind
  std::int32_t value = 0;  // +0x24, CGameActor+0x224
};

enum PickKind : std::int32_t {
  kPickActor = 0,
  kPickItem = 1,
  kPickSkill = 2,
  kPickMonster20 = 3,  // a monster (job above 1000) whose +0x1f4 is 20
};

// What ranks an actor's rectangle against the others (CGameActor::OnScreenBounds).
struct PickPriority {
  bool shift = false;      // 00685fec: Shift held (CGameMode 00515d10)
  bool aiming = false;     // 00686020: a skill aimed that IsSkillIdInFlagList (004d9920) names
  bool guild_war = false;  // CWorld+0x60 (ClientState 005aa630)
  std::optional<std::int32_t> player_guild;  // the player's +0x23c; none without a player
};

// The least width and height of a rectangle, Ftol(the renderer's width / 640 times side),
// worked out once: 40 for an actor (0067cf20), 34 for an item (0068602c) and a skill
// (00687fa8).
std::int32_t PickMinimumSize(float width_ratio, float side);

// The bias OnScreenBounds adds to an actor's depth: 400, 200, 100, 50 or 0 by its job and
// the priority (docs/world/pick.md#the-bias).
float ActorPickBias(const GameActor& actor, const PickPriority& priority);

// CGameActor::OnScreenBounds (00502cc0, vf05): the record of a pickable actor from its
// drawing's bounds (left, top, right, bottom) and the 1/z of its point.
PickRecord ActorPick(const GameActor& actor, const std::array<float, 4>& bounds, float inverse_z,
                     const PickPriority& priority, std::int32_t minimum);

// CItem::vf05 (0053ae40): an item's record, its id CItem+0xb0, the bias 300.
PickRecord ItemPick(std::int32_t id, const std::array<float, 4>& bounds, float inverse_z,
                    std::int32_t minimum);

// CSkill::vf05 (005ac5b0): a skill's record, its id CSkill+0x280, the bias 350.
PickRecord SkillPick(std::int32_t id, std::int32_t job, const std::array<float, 4>& bounds,
                     float inverse_z, std::int32_t minimum);

// CActorPickNode, the global g_CActorPickNode (0067ca60): the frame's records under a node
// of the screen, split into four children by its right and bottom halves.
class ActorPickNode {
 public:
  // 004ce8e0: cleared, then the rectangle, and four children below it while depth is above
  // 0. The original recurses without end on a negative depth.
  void Build(int depth, std::int32_t left, std::int32_t top, std::int32_t right,
             std::int32_t bottom);

  // 004ce710: the records and the children gone.
  void Clear();

  // 004ce570: into the leaf under the record's top-left corner.
  void Insert(const PickRecord& record);

  // 004ce5f0: the record under (x, y) with the greatest depth sum, in the leaf under the
  // point, then the first other child's answer; null for none.
  const PickRecord* Find(float x, float y) const;

 private:
  int Quadrant(std::int32_t x, std::int32_t y) const;

  std::array<std::int32_t, 4> rect_{};                      // +0x4: left, top, right, bottom
  std::array<std::unique_ptr<ActorPickNode>, 4> children_;  // +0x14
  std::vector<PickRecord> records_;                         // +0x24, a std::list of pointers
};

}  // namespace ragexe::world
