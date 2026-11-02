#include "world/ground_item.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>

#include "core/x87.h"
#include "render/effect_prim.h"

namespace ragexe::world {

namespace {

constexpr float kHalf = 0.5f;                                      // 005e25b8
constexpr float kSixteenth = 0.0625f;                              // 005e8a7c
constexpr float kDropHeight = 15.0f;                               // 005e98dc
constexpr float kDropSpeed = std::bit_cast<float>(0xbf19999au);    // -0.6
constexpr float kFall = std::bit_cast<float>(0x3da9fbe7u);         // 005e98d4, 0.083
constexpr float kShadowShare = std::bit_cast<float>(0x3ecccccdu);  // 005e98d0, 0.4
constexpr float kOpaque = 255.0f;                                  // 005e26a8
constexpr std::uint32_t kItemKeyColor = 0xffffff00;
constexpr std::int32_t kBlinkPeriod = 92;  // ticks
constexpr std::int32_t kBlinkFrom = 90;
constexpr std::int32_t kKeep = -1;                            // SetColorChannels: the channel kept
constexpr char kItemFolder[] = "\xbe\xc6\xc0\xcc\xc5\xdb\\";  // 0061d628, 아이템\ in CP949
// sprintf's buffer in CItem::SendMsg: a longer label overruns it in the client.
constexpr std::size_t kLabelBuffer = 0x80;

// CGameMode::CellToWorld (00511e50): ((cell - side × 0.5) + sixteenths / 16) × the cell size,
// each step in single precision.
std::array<float, 2> CellToWorld(const res::AttrMap& attr, std::int32_t cell_x, std::int32_t cell_y,
                                 std::int32_t sub_x, std::int32_t sub_y) {
  const auto size = static_cast<double>(attr.cell_size);
  const auto along = [size](std::int32_t cell, std::int32_t side, std::int32_t sub) {
    const float from_edge =
        core::RoundedSum(-core::RoundedProduct(kHalf, static_cast<double>(side)), cell);
    return core::RoundedProduct(from_edge + core::RoundedProduct(kSixteenth, sub), size);
  };
  return {along(cell_x, attr.width, sub_x), along(cell_y, attr.height, sub_y)};
}

// GetItemInfo (004d5b00): the item the id names, or the fallback (00689140).
const res::ItemInfo& ItemInfoOf(const res::ItemTables& tables, std::string_view name) {
  const auto found = tables.items.find(res::ParseLeadingInt(name));
  return found == tables.items.end() ? tables.unknown_item : found->second;
}

// GetActionFrameCount (00421a80): 1 for an action of fewer than two frames.
std::int32_t FrameCount(const res::Act& act, std::int32_t action) {
  if (action < 0 || static_cast<std::size_t>(action) >= act.actions.size()) return 1;
  const std::size_t n = act.actions[static_cast<std::size_t>(action)].size();
  return n < 2 ? 1 : static_cast<std::int32_t>(n);
}

}  // namespace

GroundItem MakeGroundItem(std::uint32_t now) {
  GroundItem item;
  render::RenderObject& o = item.object;
  o.keyed_at = now;
  o.pickable = true;
  o.shadow = true;
  o.key_color = kItemKeyColor;
  return item;
}

void ArriveGroundItem(GroundItem& item, const GroundItemArrival& record, const res::AttrMap& attr,
                      const GroundItemNames& names, std::uint32_t now) {
  render::RenderObject& o = item.object;
  item.id = record.id;
  const std::array<float, 2> at =
      CellToWorld(attr, record.cell_x, record.cell_y, record.sub_x, record.sub_y);
  o.position = {at[0], res::AttrHeight(attr, at[0], at[1]), at[1]};
  if (record.dropped != 0) {
    o.position[1] -= kDropHeight;
    item.falling = true;
    item.fall_speed = kDropSpeed;
  } else {
    item.falling = false;
  }
  item.drop_height = o.position[1];
  // ClientState::GetItemInfoString and ItemRecord::BuildDisplayName on a record of type 0,
  // which takes neither the refine nor the cards: the IdNum2 tables' names identified, the
  // Num2 tables' otherwise.
  const res::ItemInfo& info = ItemInfoOf(*names.tables, record.name);
  const std::string resource =
      kItemFolder + (record.identified ? info.identified_resource : info.resource);
  item.sprite_name = resource + ".spr";
  item.act_name = resource + ".act";
  const std::string& name = record.identified ? info.identified_name : info.name;
  const std::string format(names.label_format);
  std::array<char, kLabelBuffer> label{};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  std::snprintf(label.data(), label.size(), format.c_str(), name.c_str(), record.count);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  item.label = label.data();
  o.start_time = now;
}

void UpdateGroundItem(GroundItem& item, const res::AttrMap& attr, std::uint32_t now) {
  render::RenderObject& o = item.object;
  const float t = render::SpanTicks(now - o.start_time);
  o.keyed = core::Ftol(t) % kBlinkPeriod >= kBlinkFrom;
  if (!item.falling) return;
  o.position[1] = (t * kFall + item.fall_speed) * t + item.drop_height;
  const float ground = res::AttrHeight(attr, o.position[0], o.position[2]);
  if (ground < o.position[1]) {
    o.position[1] = ground;
    item.falling = false;
  }
}

std::optional<float> GroundItemShadowScale(const GroundItem& item) {
  if (!item.object.visible) return std::nullopt;
  return item.object.scale * kShadowShare;
}

CorpseObject MakeCorpse(const Corpse& corpse, std::uint32_t now) {
  CorpseObject made;
  render::RenderObject& o = made.object;
  o.keyed_at = now;
  o.depth_up = true;
  o.start_time = now;
  o.position = corpse.position;
  made.act_name = corpse.act;
  made.sprite_name = corpse.sprite;
  o.base_action = corpse.action;
  o.action = corpse.action;
  o.frame = corpse.frame;
  o.facing = corpse.facing;
  return made;
}

bool UpdateCorpse(CorpseObject& corpse, const res::Act& act, float camera_yaw, std::uint32_t now) {
  render::RenderObject& o = corpse.object;
  const float t = render::SpanTicks(now - o.start_time);
  o.action = render::ActorDirection(o.facing, camera_yaw) + o.base_action;
  o.frame = FrameCount(act, o.action) - 1;
  render::SetColorChannels(o.color, core::Ftol(kOpaque - t), kKeep, kKeep, kKeep);
  return t < kOpaque;
}

}  // namespace ragexe::world
