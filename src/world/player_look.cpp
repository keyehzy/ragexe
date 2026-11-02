#include "world/player_look.h"

#include <algorithm>
#include <string>

#include "core/x87.h"
#include "render/world_load.h"
#include "res/name_tables.h"
#include "world/actor_arrival.h"
#include "world/actor_attack.h"
#include "world/actor_effects.h"
#include "world/actor_hit.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kStand = 0;
constexpr std::int32_t kWalk = 1;
constexpr std::int32_t kDead = 3;
constexpr std::int32_t kSit = 6;
constexpr std::int32_t kMaleId = 0x2100e;  // 00503020: always male
constexpr std::int32_t kBard = 0x13;       // UpdateBody: always male
constexpr std::int32_t kDancer = 0x14;     // and always female
constexpr std::int32_t kMaxHair = 19;
constexpr std::int32_t kDefaultHair = 13;
constexpr std::int32_t kHighIds = 2000;        // a shield past it is an item's id
constexpr std::int32_t kNoKindSprite = 0x63f;  // vf20: an id whose kind is not shown
constexpr std::int32_t kGaugeWidth = 0x3c;
constexpr std::int32_t kGaugeHeight = 5;
constexpr std::int32_t kGaugeLeft = 0x1e;
constexpr std::int32_t kArrivalEffect = 6;
constexpr std::int32_t kEmotion = 0x15;
constexpr std::int32_t kEmotionInSlot = 0x22;
constexpr std::int32_t kEmotionDeleted = 0x76;
constexpr float kSixteenth = 0.0625f;  // 005e8a7c
constexpr std::uint32_t kHiddenFromAll = 1u << 6;

constexpr std::int32_t kBody = 0;
constexpr std::int32_t kHead = 1;
constexpr std::int32_t kWeapon = 5;
constexpr std::int32_t kGlow = 6;
constexpr std::int32_t kShield = 7;

// The administrators' bodies and weapons (00615820 to 00615a00): [the second set][male], the
// second set for IsSpecialActorIdOnKoreaService's ids.
constexpr const char* kAdminBodies[2][2] = {
    {"\xc0\xce\xb0\xa3\xc1\xb7\\\xb8\xf6\xc5\xeb\\\xbf\xa9\\\xbf\xee\xbf\xb5\xc0\xda_\xbf\xa9",
     "\xc0\xce\xb0\xa3\xc1\xb7\\\xb8\xf6\xc5\xeb\\\xb3\xb2\\\xbf\xee\xbf\xb5\xc0\xda_\xb3\xb2"},
    {"\xc0\xce\xb0\xa3\xc1\xb7\\\xb8\xf6\xc5\xeb\\\xbf\xa9\\\xbf\xee\xbf\xb5\xc0\xda"
     "2_\xbf\xa9",
     "\xc0\xce\xb0\xa3\xc1\xb7\\\xb8\xf6\xc5\xeb\\\xb3\xb2\\\xbf\xee\xbf\xb5\xc0\xda"
     "2_\xb3\xb2"}};  // 인간족\몸통\<sex>\운영자[2]_<sex>
constexpr const char* kAdminWeapons[2][2] = {
    {"\xc0\xce\xb0\xa3\xc1\xb7\\\xbf\xee\xbf\xb5\xc0\xda\\\xbf\xee\xbf\xb5\xc0\xda_\xbf\xa9_"
     "\xb0\xcb",
     "\xc0\xce\xb0\xa3\xc1\xb7\\\xbf\xee\xbf\xb5\xc0\xda\\\xbf\xee\xbf\xb5\xc0\xda_\xb3\xb2_"
     "\xb0\xcb"},
    {"\xc0\xce\xb0\xa3\xc1\xb7\\\xbf\xee\xbf\xb5\xc0\xda\\\xbf\xee\xbf\xb5\xc0\xda"
     "2_\xbf\xa9_\xb0\xcb",
     "\xc0\xce\xb0\xa3\xc1\xb7\\\xbf\xee\xbf\xb5\xc0\xda\\\xbf\xee\xbf\xb5\xc0\xda"
     "2_\xb3\xb2_\xb0\xcb"}};  // 인간족\운영자\운영자[2]_<sex>_검

std::int32_t Wrap(std::int64_t v) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}

bool IsAdmin(const StateContext& context, std::int32_t id) {
  return context.admins != nullptr &&
         std::find(context.admins->begin(), context.admins->end(), id) != context.admins->end();
}

// CPc::SetLayerAct and SetLayerSprite (00546290, 00546070): the layer's resource by the name, but
// for an administrator's body, weapon (its own) and glow (none).
void SetLayer(PlayerActor& player, std::int32_t layer, bool act, const std::string& name,
              const StateContext& context) {
  render::PlayerSpriteNames& names = player.look.layers[static_cast<std::size_t>(layer)];
  std::string& kept = act ? names.act : names.sprite;
  kept = name;
  if (!IsAdmin(context, player.id)) return;
  const int second = IsSpecialActorIdOnKoreaService(context.service_type, player.id) ? 1 : 0;
  const int male = player.field_0x1bc != 0 ? 1 : 0;
  const char* extension = act ? ".Act" : ".Spr";
  if (layer == kBody) kept = std::string(kAdminBodies[second][male]) + extension;
  if (layer == kWeapon) kept = std::string(kAdminWeapons[second][male]) + extension;
  if (layer == kGlow) kept.clear();
}

// Both of a layer's names, from a name without its extension ("" none).
void SetLayer(PlayerActor& player, std::int32_t layer, const render::PlayerSpriteNames& base,
              const StateContext& context) {
  SetLayer(player, layer, true, base.act.empty() ? std::string() : base.act + ".act", context);
  SetLayer(player, layer, false, base.sprite.empty() ? std::string() : base.sprite + ".spr",
           context);
}

void ClearLayer(PlayerActor& player, std::int32_t layer, const StateContext& context) {
  SetLayer(player, layer, render::PlayerSpriteNames{}, context);
}

std::int32_t Sex(const PlayerActor& player) { return player.field_0x1bc; }

// CPc::UpdateBody (vf16, 00542320).
void UpdateBody(PlayerActor& player, const StateContext& context) {
  if (GameActorJob(player) == kBard && player.field_0x1bc == 0) player.field_0x1bc = 1;
  if (GameActorJob(player) == kDancer && player.field_0x1bc == 1) player.field_0x1bc = 0;
  player.object.shadow_scale = res::JobShadowScale(GameActorJob(player));
  const std::string body = render::PlayerBodyName(GameActorJob(player), Sex(player));
  SetLayer(player, kBody, {body, body}, context);
  player.look.body = player.look.layers[kBody];
}

// CPc::SetHair (vf17, 00542400): a style outside 0 to 19 is 13 (GetHeadActName writes it back).
void SetHair(PlayerActor& player, std::int32_t hair, const StateContext& context) {
  player.look.hair = hair < 0 || hair > kMaxHair ? kDefaultHair : hair;
  const std::string head = render::PlayerHeadName(player.look.hair, Sex(player));
  SetLayer(player, kHead, {head, head}, context);
}

// CPc::SetWeaponKind (vf19, 005426a0).
void SetWeaponKind(PlayerActor& player, std::int32_t kind, const StateContext& context) {
  player.weapon = kind;
  const auto names = render::PlayerWeaponNames(GameActorJob(player), Sex(player), kind);
  SetLayer(player, kWeapon, names[0], context);
  SetLayer(player, kGlow, names[1], context);
}

// vf21 to vf23 (005424f0, 00542580, 00542610).
void SetHeadgear(PlayerActor& player, std::int32_t slot, std::int32_t headgear,
                 const StateContext& context) {
  player.look.headgear[static_cast<std::size_t>(slot)] = headgear;
  SetLayer(player, render::kPlayerHeadgearTop + slot,
           render::PlayerHeadgearNames(Sex(player), headgear), context);
}

// CPc::SetShield (vf24, 005428d0): an id past 2000 becomes its kind.
void SetShield(PlayerActor& player, std::int32_t shield, const StateContext& context) {
  if (shield > kHighIds) shield = render::ShieldKindOf(shield);
  player.look.shield = shield;
  SetLayer(player, kShield, render::PlayerShieldNames(GameActorJob(player), Sex(player), shield),
           context);
}

// CPc::UpdateImfName (vf26, 00542960).
void UpdateImf(PlayerActor& player) {
  player.look.imf = render::PlayerImfName(GameActorJob(player), Sex(player));
}

// vf27 and vf28 (00542a40, 00542b30): the palettes, each kept as it was for colour 0.
void UpdatePalettes(PlayerActor& player) {
  const std::int32_t job = GameActorJob(player);
  PlayerLook& look = player.look;
  if (look.hair_color != 0)
    look.head_palette = render::PlayerHeadPaletteName(look.hair, job, Sex(player), look.hair_color);
  if (look.body_color != 0)
    look.body_palette = render::PlayerBodyPaletteName(job, Sex(player), look.body_color);
}

// The message to itself from no sender, through CPc::SendMsg, then CGameActor's.
void SendSelf(PlayerActor& player, std::int32_t id, std::int32_t a, const StateContext& context,
              std::uint32_t now) {
  if (context.actor_messages != nullptr) context.actor_messages->push_back({id, a, 0, 0});
  if (SendPlayerMessage(player, id, a, 0, context, now)) return;
  SendGameActorEffectMessage(player, id, 0, a, context, now);
}

// CPc::vf37 (00543510) through vf36 (005434c0): below 0 emotion 0x22 shows (0x15), otherwise
// it goes (0x76); +0x28c and +0x290 keep the two.
void SetEmotionWords(PlayerActor& player, std::int32_t first, std::int32_t second,
                     const StateContext& context, std::uint32_t now) {
  if (first < 0) {
    SendSelf(player, kEmotion, kEmotionInSlot, context, now);
  } else {
    SendSelf(player, kEmotionDeleted, 0, context, now);
  }
  player.field_0x28c = first;
  player.repeated_at = static_cast<std::uint32_t>(second);
}

// The stand-in arrival's state by the record's: standing, dead or sitting; any other stands.
void SetArrivalState(PlayerActor& player, std::int32_t pc_state, const StateContext& context,
                     std::uint32_t now) {
  const std::int32_t state = pc_state == 1 ? kDead : pc_state == 2 ? kSit : kStand;
  SetPlayerState(player, state, context, now);
}

// What the four of a kind share before the state: the dress, the status, +0x224, the emblem
// and vf37's words. The job the record's effect state mounts (CGameActor 00504190, 7 as 0xd and
// 0xe as 0x15) SetJob takes as its own again.
template <class Record>
void TakeRecord(PlayerActor& player, const Record& record, bool by_item,
                const StateContext& context, std::uint32_t now) {
  PlayerDress dress{.job = record.job,
                    .sex = record.sex,
                    .hair = record.hair,
                    .weapon = record.weapon,
                    .headgear = {record.headgear_top, record.headgear_mid, record.headgear_bottom},
                    .shield = record.shield,
                    .hair_color = record.hair_color,
                    .body_color = record.body_color};
  DressPlayer(player, dress, by_item, context);
  SetPlayerStatus(player, record.body_state, record.effect_state, record.health_state, context,
                  now);
  player.field_0x224 = record.field_0x224;
  SetGameActorEmblem(player, record.guild, record.emblem, context);
  SetEmotionWords(player, record.field_0x28c, record.field_0x290, context, now);
}

}  // namespace

std::int32_t TwoWeaponsKind(std::int32_t first, std::int32_t second) {
  const auto in = [](std::int32_t id, std::int32_t from) { return id >= from && id < from + 0x32; };
  if (first == 0) {
    if (second == 0) return 0;
    first = second;
    second = 0;
  }
  if (in(first, 0x44c)) {
    if (in(second, 0x44c)) return 0x12;
    if (in(second, 0x514)) return 0x16;
    return 2;
  }
  if (in(first, 0x4b0)) {
    if (in(second, 0x4b0)) return 0x11;
    if (in(second, 0x44c)) return 0x14;
    if (in(second, 0x514)) return 0x15;
    return 1;
  }
  if (in(first, 0x514)) return in(second, 0x514) ? 0x13 : 6;
  return 0;
}

void SetPlayerWeapon(PlayerActor& player, std::int32_t id, const StateContext& context) {
  player.weapon = id;
  if (id == 0) {
    ClearLayer(player, kWeapon, context);
    ClearLayer(player, kGlow, context);
    return;
  }
  const std::int32_t job = GameActorJob(player);
  const std::int32_t first = id & 0xffff;
  const std::int32_t second = static_cast<std::int32_t>(static_cast<std::uint32_t>(id) >> 16);
  if (WeaponKind(second) > 0) {
    const std::string name =
        render::PlayerWeaponKindName(job, Sex(player), TwoWeaponsKind(first, second));
    SetLayer(player, kWeapon, {name, name}, context);
    return;
  }
  std::int32_t kind = 0;
  const std::string own = render::PlayerWeaponItemName(job, Sex(player), first);
  if (!context.resource_exists || context.resource_exists(own + ".act")) {
    SetLayer(player, kWeapon, {own, own}, context);
    kind = WeaponKind(player.weapon);
  } else {
    kind = WeaponKind(first);
    if (job == 6 && (kind == 8 || kind == 6 || kind == 7)) kind = 2;
    const bool none = kind == -1 || kind == 0 || first == kNoKindSprite;
    const bool unheld = ((job == 0xc || job == 0x11) && kind == 8) || (job == 8 && kind == 0xc);
    if (none || unheld) {
      ClearLayer(player, kWeapon, context);
      ClearLayer(player, kGlow, context);
      return;
    }
    const std::string name = render::PlayerWeaponKindName(job, Sex(player), kind);
    SetLayer(player, kWeapon, {name, name}, context);
  }
  if (!render::WeaponShowsGlow(kind)) {
    ClearLayer(player, kGlow, context);
    return;
  }
  const std::string glow = render::PlayerWeaponGlowName(job, Sex(player), kind);
  SetLayer(player, kGlow, {glow, glow}, context);
}

void DressPlayer(PlayerActor& player, const PlayerDress& dress, bool by_item,
                 const StateContext& context) {
  // CGameActor 00503020: the sex's low bit.
  player.field_0x1bc = player.id == kMaleId ? 1 : dress.sex & 1;
  player.look.hair_color = dress.hair_color;
  player.look.body_color = dress.body_color;
  // CGameActor 00503440: the job, a mounted one as its own.
  player.job = dress.job == 0xd ? 7 : dress.job == 0x15 ? 0xe : dress.job;
  UpdateBody(player, context);
  SetHair(player, dress.hair, context);
  std::int32_t shield = dress.shield;
  if (by_item) {
    SetPlayerWeapon(player, dress.weapon & 0xffff, context);
    const std::int32_t second =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(dress.weapon) >> 16);
    shield = WeaponKind(second) == -1 ? second : 0;
  } else {
    SetWeaponKind(player, dress.weapon, context);
  }
  for (std::int32_t slot = 0; slot < 3; ++slot)
    SetHeadgear(player, slot, dress.headgear[static_cast<std::size_t>(slot)], context);
  SetShield(player, shield, context);
  UpdateImf(player);
  UpdatePalettes(player);
}

void ArrivePlayer(PlayerActor& player, std::int32_t message, const PlayerStandingArrival& record,
                  std::int32_t how, const StateContext& context, std::uint32_t now) {
  const bool counted = message == 4 || message == 0x79;
  render::RenderObject& o = player.object;
  if (counted) player.id = record.id;
  const std::array<float, 2> centre =
      render::CellCentre(*context.attr, record.cell_x, record.cell_y);
  o.position = {centre[0], res::AttrHeight(*context.attr, centre[0], centre[1]), centre[1]};
  o.facing = static_cast<float>(Wrap((std::int64_t{record.direction} + 4) * 45));
  if (counted) player.speed = record.speed;
  player.field_0x1ec = record.field_0x1ec;
  player.field_0x1f0 = record.field_0x1f0;
  player.goal_x = record.cell_x;
  player.goal_y = record.cell_y;
  player.saved_frame = record.head;
  player.field_0x1f4 = record.hair;
  TakeRecord(player, record, message == 0x79 || message == 0x7c, context, now);
  SetArrivalState(player, record.pc_state, context, now);
  if (!counted) return;
  if (how == 2 && (player.effect_state & kHiddenFromAll) == 0)
    LaunchGameActorEffect(player, kArrivalEffect, context);
}

PathResult ArriveWalkingPlayer(PlayerActor& player, PathFinder& finder, std::int32_t message,
                               const PlayerWalkingArrival& record, std::uint32_t server_time,
                               const StateContext& context, std::uint32_t now) {
  const bool placed = message == 6 || message == 0x7a;
  player.id = record.id;
  player.speed = record.speed;
  player.move_time = record.order.time;
  player.saved_frame = 0;
  TakeRecord(player, record, message == 0x7a || message == 0x7b, context, now);
  const MoveOrder& order = record.order;
  if (placed) {
    render::RenderObject& o = player.object;
    const std::array<float, 2> centre =
        render::CellCentre(*context.attr, order.start_x, order.start_y);
    const float step =
        core::RoundedProduct(kSixteenth, static_cast<double>(context.attr->cell_size));
    const float x = core::RoundedProduct(step, static_cast<double>(order.sub_x - 8)) + centre[0];
    const float z = core::RoundedProduct(step, static_cast<double>(order.sub_y - 8)) + centre[1];
    o.position = {x, res::AttrHeight(*context.attr, x, z), z};
  }
  const PathResult found =
      FindMovePath(finder, player.path, player.move_time, server_time, order.start_x, order.start_y,
                   order.sub_x, order.sub_y, order.goal_x, order.goal_y, record.speed);
  if (found != PathResult::kFound) {
    if (placed) SetPlayerState(player, kStand, context, now);
    return found;
  }
  player.goal_x = order.goal_x;
  player.goal_y = order.goal_y;
  SetPlayerState(player, kWalk, context, now);
  player.segment = 0;
  return found;
}

bool SendPlayerMessage(PlayerActor& player, std::int32_t message, std::int32_t a, std::int32_t b,
                       const StateContext& context, std::uint32_t /*now*/) {
  switch (message) {
    case 0x22: {
      if (context.windows == nullptr) return true;
      ActorWindows& windows = *context.windows;
      if (player.gauge == 0) {
        windows.sizes.push_back({kGaugeWidth, kGaugeHeight});
        player.gauge = static_cast<std::int32_t>(windows.sizes.size());
        windows.calls.push_back({WindowCallKind::kCreate, player.gauge, kGaugeWidth, kGaugeHeight});
        windows.calls.push_back({WindowCallKind::kAdd, player.gauge, 0, 0});
      }
      windows.calls.push_back({WindowCallKind::kFirstBar, player.gauge, a, b});
      windows.calls.push_back({WindowCallKind::kSecondBar, player.gauge, 0, 0});
      const render::RenderObject& o = player.object;
      const std::int32_t rise = Wrap(std::int64_t{context.screen_height} * 12) / 480;
      windows.calls.push_back({WindowCallKind::kMove, player.gauge,
                               Wrap(std::int64_t{o.screen_x} - kGaugeLeft),
                               Wrap(std::int64_t{rise} + o.screen_y)});
      return true;
    }
    case 0x23:
      if (player.gauge != 0) {
        if (context.closed_windows != nullptr) context.closed_windows->push_back(player.gauge);
        player.gauge = 0;
      }
      return true;
    case 0x4d: return true;
    case 0x64:
      if (player.state == kStand || player.state == kSit) player.object.frame = a;
      return true;
    case 0x69:
    case 0x6a:
      UpdateBody(player, context);
      UpdateImf(player);
      SetWeaponKind(player, player.weapon, context);
      SetShield(player, player.look.shield, context);
      UpdatePalettes(player);
      return true;
    case 0x72:
      player.field_0x2e0 = b;
      player.field_0x2e4 = a;
      return true;
    default: return false;
  }
}

}  // namespace ragexe::world
