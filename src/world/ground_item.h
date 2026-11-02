#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "render/actor.h"
#include "res/act.h"
#include "res/gat.h"
#include "res/item_tables.h"
#include "world/actor_state.h"

namespace ragexe::world {

// A CItem (vtable 005e98ac, docs/world/items.md): an item lying on the ground.
struct GroundItem {
  render::RenderObject object;
  std::string label;         // +0xa0, the name shown: message 0xb7 with the name and the count
  std::int32_t id = 0;       // +0xb0, which its pick record carries (OnScreenBounds)
  bool falling = false;      // +0xb4, dropped and not yet on the ground
  float fall_speed = 0.0f;   // +0xb8, in height per tick, upward negative
  float drop_height = 0.0f;  // +0xbc, the height the fall starts from
  std::string sprite_name;   // what CRenderObject::SetSprite (005018a0)
  std::string act_name;      // and SetAct (00501910) were given
};

// The record of CItem's message 4: an item that comes into view.
struct GroundItemArrival {
  std::string name;          // +0, the item's id as text, by which the tables know it
  std::int32_t id = 0;       // +0x20
  std::int32_t cell_x = 0;   // +0x24
  std::int32_t cell_y = 0;   // +0x28
  std::int32_t sub_x = 0;    // +0x2c, sixteenths of a cell from its corner
  std::int32_t sub_y = 0;    // +0x30
  std::int32_t dropped = 0;  // +0x34: not 0, it falls
  std::int32_t count = 0;    // +0x38
  bool identified = false;   // +0x3c, a byte
};

// What message 4 names the item by: the tables (g_itemInfoById, LoadItemTables) and message
// 0xb7 of msgStringTable (GetMsgString, 004d8df0), the format of the label.
struct GroundItemNames {
  const res::ItemTables* tables = nullptr;
  std::string_view label_format;
};

// CItem's constructor (0053abb0): a CRenderObject's (00500400, keyed_at now) with
// CItem::SetItemDefaults (0053acf0) after it: the scale 1 again, pickable (+0x89), with a shadow
// (+0x88), and the key colour 0xffffff00.
GroundItem MakeGroundItem(std::uint32_t now);

// CItem::SendMsg (0053afb0, vf02)'s message 4, the one it handles; the others go to
// CRenderObject::SendMsg (00500e40), which does nothing. The id; the place
// (CGameMode::CellToWorld, 00511e50) on the ground (C3dAttr::GetHeight), or 15 above it
// (005e98dc) falling at -0.6 with `dropped`; the sprite and act "아이템\" (0061d628) and the
// resource name (ClientState::GetItemResourceName, 00597580) with ".spr" and ".act"; the label,
// the format with the name by the identified byte and the count; the start now.
void ArriveGroundItem(GroundItem& item, const GroundItemArrival& record, const res::AttrMap& attr,
                      const GroundItemNames& names, std::uint32_t now);

// CItem::Update (0053ad60, vf01), which always returns 1: keyed (+0x83) for the 90th and
// 91st of every 92 ticks since the start; falling, the height (0.083 t (005e98d4) + the speed)
// t + the drop height, until it passes the ground's, where it stops.
void UpdateGroundItem(GroundItem& item, const res::AttrMap& attr, std::uint32_t now);

// CItem::Render (0053ad20, vf03), the drawing: none unless shown (+0x84); then the shadow
// (CRenderObject::DrawShadow, 00501550) at 0.4 (005e98d0) of the scale, and the body
// (DrawBody, 00500f70). The shadow's scale, or none.
std::optional<float> GroundItemShadowScale(const GroundItem& item);

// A CCorpse (vtable 005e99f0): what CGameMode::AddCorpse (00510030) makes of a Corpse.
struct CorpseObject {
  render::RenderObject object;
  std::string act_name;
  std::string sprite_name;
};

// AddCorpse: CCorpse's constructor (00540b70, then SetCorpseDefaults, 00540c40: shown again, depth
// from the other slope (+0x80), the start now), then the corpse's position, act and sprite, its
// action as the base action and the action, its frame and facing.
CorpseObject MakeCorpse(const Corpse& corpse, std::uint32_t now);

// CCorpse::Update (00540c70, vf01): the action the base action plus the direction
// (CGameObject::vf04), its last frame, and the alpha 255 (005e26a8) less the ticks since the start
// (SetColorChannels, so kept at -1). True while fewer than 255 ticks have passed; at false the
// world lets it go.
bool UpdateCorpse(CorpseObject& corpse, const res::Act& act, float camera_yaw, std::uint32_t now);

}  // namespace ragexe::world
