#include "world/world_objects.h"

#include <algorithm>
#include <string>

namespace ragexe::world {

namespace {

// The actor an object is, or none.
GameActor* ActorOf(WorldObject& object) {
  if (auto* npc = std::get_if<GameActor>(&object)) return npc;
  return std::get_if<PlayerActor>(&object);
}

std::int32_t Word(std::span<const std::uint8_t, kItemPacketSize> packet, std::size_t at) {
  return static_cast<std::int16_t>(packet[at] | (packet[at + 1] << 8));
}

std::int32_t Dword(std::span<const std::uint8_t, kItemPacketSize> packet, std::size_t at) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(packet[at]) |
                                   (static_cast<std::uint32_t>(packet[at + 1]) << 8) |
                                   (static_cast<std::uint32_t>(packet[at + 2]) << 16) |
                                   (static_cast<std::uint32_t>(packet[at + 3]) << 24));
}

// What packets 0x009d and 0x009e share: the id, the item's number (unsigned, as _itoa's
// decimal text), identified and the cell.
GroundItemArrival ItemRecord(std::span<const std::uint8_t, kItemPacketSize> packet) {
  GroundItemArrival record;
  record.id = Dword(packet, 2);
  record.name = std::to_string(packet[6] | (packet[7] << 8));
  record.identified = packet[8] != 0;
  record.cell_x = Word(packet, 9);
  record.cell_y = Word(packet, 0xb);
  return record;
}

}  // namespace

template <typename T>
T& WorldObjects::Append(T object) {
  objects_.push_back(std::make_unique<WorldObject>(std::move(object)));
  return std::get<T>(*objects_.back());
}

WorldObject& WorldObjects::Add(WorldObject object) {
  objects_.push_back(std::make_unique<WorldObject>(std::move(object)));
  return *objects_.back();
}

GameActor& WorldObjects::AddActor(GameActor actor) {
  GameActor& added = Append(std::move(actor));
  actors_.push_back(&added);
  return added;
}

PlayerActor& WorldObjects::AddActor(PlayerActor actor) {
  PlayerActor& added = Append(std::move(actor));
  actors_.push_back(&added);
  return added;
}

GroundItem& WorldObjects::AddItem(GroundItem item) {
  GroundItem& added = Append(std::move(item));
  items_.push_back(&added);
  return added;
}

PlayerActor& WorldObjects::CreatePlayer(PlayerActor player) {
  PlayerActor& added = Append(std::move(player));
  player_ = &added;
  return added;
}

void WorldObjects::Update(const std::function<bool(WorldObject&)>& update) {
  for (auto it = objects_.begin(); it != objects_.end();) {
    WorldObject& object = **it;
    if (update(object)) {
      ++it;
      continue;
    }
    if (GameActor* actor = ActorOf(object)) {
      if (const auto found = std::ranges::find(actors_, actor); found != actors_.end())
        actors_.erase(found);
      if (actor == player_) player_ = nullptr;
    }
    if (auto* item = std::get_if<GroundItem>(&object)) std::erase(items_, item);
    it = objects_.erase(it);
  }
}

GameActor* WorldObjects::FindActor(std::int32_t id) const {
  const auto found = std::ranges::find(actors_, id, &GameActor::id);
  return found == actors_.end() ? nullptr : *found;
}

GroundItem* WorldObjects::FindItem(std::int32_t id) const {
  const auto found = std::ranges::find(items_, id, &GroundItem::id);
  return found == items_.end() ? nullptr : *found;
}

// CWorld::DeleteGameObject: the object let go when the list has it.
void WorldObjects::Release(const WorldObject* object) {
  const auto found = std::ranges::find(objects_, object, &std::unique_ptr<WorldObject>::get);
  if (found != objects_.end()) objects_.erase(found);
}

bool WorldObjects::DeleteActor(std::int32_t id) {
  const auto found = std::ranges::find(actors_, id, &GameActor::id);
  if (found == actors_.end()) return false;
  GameActor* actor = *found;
  actors_.erase(found);
  for (const auto& object : objects_) {
    if (ActorOf(*object) == actor) {
      Release(object.get());
      break;
    }
  }
  return true;
}

bool WorldObjects::DeleteItem(std::int32_t id) {
  const auto found = std::ranges::find(items_, id, &GroundItem::id);
  if (found == items_.end()) return false;
  const GroundItem* item = *found;
  items_.erase(found);
  for (const auto& object : objects_) {
    if (std::get_if<GroundItem>(object.get()) == item) {
      Release(object.get());
      break;
    }
  }
  return true;
}

void WorldObjects::Clear() {
  player_ = nullptr;
  actors_.clear();
  items_.clear();
  objects_.clear();
}

GroundItemArrival ItemInView(std::span<const std::uint8_t, kItemPacketSize> packet) {
  GroundItemArrival record = ItemRecord(packet);
  record.count = Word(packet, 0xd);
  record.sub_x = packet[0xf];
  record.sub_y = packet[0x10];
  return record;
}

GroundItemArrival ItemDropped(std::span<const std::uint8_t, kItemPacketSize> packet) {
  GroundItemArrival record = ItemRecord(packet);
  record.sub_x = packet[0xd];
  record.sub_y = packet[0xe];
  record.count = Word(packet, 0xf);
  record.dropped = 1;
  return record;
}

GroundItem& ShowGroundItem(WorldObjects& world, const GroundItemArrival& record,
                           const res::AttrMap& attr, const GroundItemNames& names,
                           std::uint32_t now) {
  GroundItem* item = world.FindItem(record.id);
  if (item == nullptr) item = &world.AddItem(MakeGroundItem(now));
  ArriveGroundItem(*item, record, attr, names, now);
  return *item;
}

void RemoveGroundItem(WorldObjects& world, std::vector<std::int32_t>* picked, std::int32_t id) {
  world.DeleteItem(id);
  if (picked == nullptr) return;
  if (const auto found = std::ranges::find(*picked, id); found != picked->end())
    picked->erase(found);
}

}  // namespace ragexe::world
