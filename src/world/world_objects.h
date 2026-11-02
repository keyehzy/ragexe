#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "res/gat.h"
#include "world/actor_state.h"
#include "world/ground_item.h"

namespace ragexe::world {

// An object of the world's list (docs/world/lists.md): an NPC or a monster (CNpc), a player
// (CPc, or the own player, CPlayer), an item on the ground (CItem) or a corpse (CCorpse).
using WorldObject = std::variant<GameActor, PlayerActor, GroundItem, CorpseObject>;

// CWorld's lists of its objects. The list owns them; the others point into it.
class WorldObjects {
 public:
  // +8 (its head +0xc, its count +0x10): every object, in the order the world updates and
  // draws them.
  [[nodiscard]] const std::list<std::unique_ptr<WorldObject>>& objects() const { return objects_; }
  // +0x14 (+0x18, +0x1c): the actors the server sent, which GetActorById looks among.
  [[nodiscard]] const std::list<GameActor*>& actors() const { return actors_; }
  // +0x20 (+0x24, +0x28): the items on the ground, which GetItemById looks among.
  [[nodiscard]] const std::list<GroundItem*>& items() const { return items_; }
  // +0x3c: the own player, in the list but not among the actors.
  [[nodiscard]] PlayerActor* player() const { return player_; }

  // CGameMode::AddToWorldObjects (00511150): at the end of the list alone, as the corpses
  // (AddCorpse) and the effects join it.
  WorldObject& Add(WorldObject object);
  // An actor the server sends (packets 0x0078 and the like): at the end of the list, then of
  // the actors'.
  GameActor& AddActor(GameActor actor);
  PlayerActor& AddActor(PlayerActor actor);
  // An item (packets 0x009d and 0x009e): at the end of the list, then of the items'.
  GroundItem& AddItem(GroundItem item);
  // CWorld::CreatePlayer (004c4e60): the player +0x3c and at the end of the list. One there
  // before stays in the list.
  PlayerActor& CreatePlayer(PlayerActor player);

  // CWorld::UpdateGameObjects (004c4a10)'s objects: each in order, those that join the end
  // meanwhile too, through `update` (vf01). One whose update returns false is let go: out of
  // the actors' list (CWorld::RemoveActor, 004c4ab0, the first that is it) and of the list.
  // An item's update (CItem::Update) always returns 1; were it to return 0, the original would
  // leave it in +0x20, and the own player's in +0x3c, freed: the port takes them out of
  // those too.
  void Update(const std::function<bool(WorldObject&)>& update);

  // CWorld::GetActorById (004c4cf0) and GetItemById (004c4d20): the first of the list with the
  // id (+0x1b4, +0xb0), or none.
  [[nodiscard]] GameActor* FindActor(std::int32_t id) const;
  [[nodiscard]] GroundItem* FindItem(std::int32_t id) const;

  // CWorld::DeleteActorById (004c4b00) and DeleteItemById (004c4b60): the first with the id let
  // go (CWorld::DeleteGameObject, 004c4bc0) and out of its list. False without one.
  bool DeleteActor(std::int32_t id);
  bool DeleteItem(std::int32_t id);

  // CWorld::ClearGameObjects (004c4c20): every object let go in order, +0x3c 0, the lists
  // emptied.
  void Clear();

 private:
  template <typename T>
  T& Append(T object);
  void Release(const WorldObject* object);

  std::list<std::unique_ptr<WorldObject>> objects_;
  std::list<GameActor*> actors_;
  std::list<GroundItem*> items_;
  PlayerActor* player_ = nullptr;
};

// The length of packets 0x009d and 0x009e.
inline constexpr std::size_t kItemPacketSize = 17;

// The record of message 4 that CGameMode::HandlePacket_009D (00529780) makes of packet 0x009d,
// an item lying in view: the id +2, the item's number +6 as decimal text (_itoa), identified
// +8, the cell +9 and +0xb, the count +0xd, the sixteenths +0xf and +0x10; not dropped.
GroundItemArrival ItemInView(std::span<const std::uint8_t, kItemPacketSize> packet);

// HandlePacket_009E (00529900)'s of packet 0x009e, an item dropped: as 0x009d's but the
// sixteenths +0xd and +0xe and the count +0xf; dropped.
GroundItemArrival ItemDropped(std::span<const std::uint8_t, kItemPacketSize> packet);

// The rest of both: the item of the record's id (GetItemById) or, without one, a new CItem
// (MakeGroundItem at `now`) at the end of the list and of the items'; then message 4 to it.
GroundItem& ShowGroundItem(WorldObjects& world, const GroundItemArrival& record,
                           const res::AttrMap& attr, const GroundItemNames& names,
                           std::uint32_t now);

// CGameMode::HandlePacket_00A1 (0052a070), an item gone: DeleteItemById, then the id out of
// the items asked to be picked up (CGameMode::RemoveIdFromList, 00511940, the first).
void RemoveGroundItem(WorldObjects& world, std::vector<std::int32_t>* picked, std::int32_t id);

}  // namespace ragexe::world
