#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/crt_rand.h"
#include "render/player.h"
#include "render/player_names.h"
#include "res/act.h"
#include "res/gat.h"
#include "world/actor_walk.h"
#include "world/path_finder.h"

namespace ragexe::world {

// A message an actor sends the game mode (CGameMode::SendMsg, vf05), or itself
// (CGameActor::SendMsg, vf02, from no sender): its id and three values.
struct ModeMessage {
  std::int32_t id = 0;
  std::int32_t a = 0;
  std::int32_t b = 0;
  std::int32_t c = 0;
  bool operator==(const ModeMessage&) const = default;
};

// What the own player's message 0x42 asks for a skill on a cell (CPlayer::SendMsg, 0054c5d0):
// the game mode's 0x4d, or for skill 0x7d window 0x54 (UIWindowMgr 004ad670) sent 0x44 (its
// vf37), each with the address of this record.
struct CellSkill {
  bool window = false;
  std::int32_t skill = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t level = 0;
  bool operator==(const CellSkill&) const = default;
};

// A sound an actor plays (PlayWave, 004352a0): its file and where it is from the player, the
// height 0 and the rest 250, 40 and 1.
struct Sound {
  std::string_view name;
  float x = 0.0f;
  float z = 0.0f;
};

// What made an actor's effect, a CMsgEffect.
enum class EffectMaker : std::uint8_t {
  kSprite,     // CGameMode::AddSpriteEffectByType (005106a0)
  kEmotion,    // CGameMode::AddEmotionEffect (005111b0)
  kNumber,     // CGameActor::ShowNumberEffect (0050beb0), one digit
  kMsgSprite,  // CGameMode::AddMsgSpriteEffect (00510440), msg.spr
};

// An effect made for an actor, as the actor's messages see it.
struct ActorEffect {
  EffectMaker maker = EffectMaker::kSprite;
  std::int32_t type = 0;      // AddSpriteEffectByType's type, which picks the sprite, the emotion,
                              // or ShowNumberEffect's, its +0x10c
  std::int32_t actor_id = 0;  // the actor's id (+0x1b4) when it was made
  const render::Vec3* position = nullptr;  // the actor's position, which it is drawn at
  float field_0x44 = 0.0f;                 // +0x44
  float field_0x3c = 0.0f;                 // +0x3c: 0.5 for a number of type 0x16
  std::uint32_t color = 0;                 // +0x7c: a number's, or AddMsgSpriteEffect's value
};

// A message to an effect (its vf02): 0x32 from the actor, to follow it, 0x35 from none, to
// end, and the others with their arguments, `at` standing for a position's address.
struct EffectMessage {
  std::int32_t effect = 0;  // its number
  std::int32_t id = 0;
  std::int32_t a = 0;
  std::int32_t b = 0;
  std::int32_t c = 0;
  const render::Vec3* at = nullptr;
  const GameActor* sender = nullptr;  // the actor that sends it, or none
  bool operator==(const EffectMessage&) const = default;
};

// The effects made for actors, which they name by number, their place in `made` from 1, what
// they were sent, in order, and those the world deleted (CWorld::DeleteGameObject, 004c4bc0).
struct ActorEffects {
  std::vector<ActorEffect> made;
  std::vector<EffectMessage> messages;
  std::vector<std::int32_t> deleted;
};

// What CGameActor::vf34 (00503f30) leaves of an actor that has died: CGameMode::AddCorpse
// (00510030) with its position, its body's act and sprite (CRes::GetName, empty without one),
// its base action, frame and facing.
struct Corpse {
  render::Vec3 position{};
  std::string_view act;
  std::string_view sprite;
  std::int32_t action = 0;
  std::int32_t frame = 0;
  float facing = 0.0f;
};

// An arrow the game mode shoots from an actor (CGameMode::AddArrowEffect, 005101f0, which makes
// a CArrowEffect only for a target): the position it leaves from, the target's id and its
// speed.
struct Arrow {
  const render::Vec3* from = nullptr;
  std::int32_t target_id = 0;
  float speed = 0.0f;
};

// What a pet says when it talks (CGameActor::ProcessState, standing): ClientState's pet
// (+0x7d0) and the two lines of its tables the game mode hears, the first from 005aa3e0, the
// second from the table at +0x824 by 005a9f40's and 005a9fa0's levels.
struct PetTalk {
  std::int32_t pet_id = 0;
  std::int32_t first = 0;
  std::int32_t second = 0;
};

// What an actor does to one of its windows (docs/world/windows.md), beyond queueing it for
// removal (UIWindowMgr 004a90c0, StateContext::closed_windows).
enum class WindowCallKind : std::uint8_t {
  kCreate,     // UIWindow 004a58e0 with its width and height, once made
  kMove,       // vf18 with x and y
  kAdd,        // UIWindowMgr 004a7d60, which shows it
  kAmount,     // UIRechargeGage::SetAmount (00442e40) with the part done and the whole
  kFirstBar,   // UIPcGage::SetFirstBar (004431b0) with its two words
  kSecondBar,  // UIPcGage::SetSecondBar (004431d0)
};

struct WindowCall {
  WindowCallKind kind = WindowCallKind::kMove;
  std::int32_t window = 0;  // its number
  std::int32_t a = 0;
  std::int32_t b = 0;
  bool operator==(const WindowCall&) const = default;
};

// The actors' windows as the port keeps them, which GameActor names by number, their place
// in `sizes` from 1: each one's width and height (UIWindow +0x1c, +0x20) and what they were
// told, in order.
struct ActorWindows {
  std::vector<std::array<std::int32_t, 2>> sizes;
  std::vector<WindowCall> calls;
};

// The order message 0 carries (CGameActor::vf02): when, and from which cell (with the
// sixteenths in it) to which.
struct MoveOrder {
  std::uint32_t time = 0;
  std::int32_t start_x = 0;
  std::int32_t start_y = 0;
  std::int32_t sub_x = 0;
  std::int32_t sub_y = 0;
  std::int32_t goal_x = 0;
  std::int32_t goal_y = 0;
  bool operator==(const MoveOrder&) const = default;
};

// What the actors' states read beyond the actor (docs/world/states.md).
struct StateContext {
  const res::AttrMap* attr = nullptr;                   // CWorld+0x40
  const res::Act* act = nullptr;                        // the actor's body, CRenderObject+0x9c
  float camera_yaw = 0.0f;                              // CGameMode+0xb8 (the CView) +0x20
  std::int32_t service_type = 0;                        // g_serviceType (0067c9d4)
  std::vector<ModeMessage>* messages = nullptr;         // what goes to the game mode, when kept
  std::vector<std::int32_t>* closed_windows = nullptr;  // UIWindowMgr 004a90c0's, when kept
  std::int32_t* mode_field_0x2a0 = nullptr;  // CGameMode+0x2a0, which a player standing clears
  const PetTalk* pet = nullptr;              // the player's pet, when there is one
  core::CrtRand* rand = nullptr;             // the C runtime's rand, which a pet's talk draws on
  const render::Vec3* target = nullptr;      // where CWorld::GetActorById finds the actor
                                             // +0x1a8 names, when it does
  std::vector<ModeMessage>* actor_messages = nullptr;  // what the actor sends itself, when kept
  std::vector<Sound>* sounds = nullptr;                // what it plays, when kept
  const render::Vec3* player = nullptr;  // CWorld+0x3c, the player, whom the sounds are heard by
  std::int32_t own_id = 0;               // ClientState+0xcdc, the player's id
  std::int32_t* ground_water_fade = nullptr;  // g_groundWaterFade (00679f9c)
  // The braces let contexts be brace-initialized short of these without
  // -Wmissing-field-initializers.
  // NOLINTBEGIN(readability-redundant-member-init)
  std::string_view server_name{};  // ClientState+0x4e4, the character server chosen at login
  std::function<bool(const std::string&)> resource_exists{};  // ResourceManager::Exists
                                                              // (004e0190); without it every
                                                              // file is there
  ActorEffects* effects{};         // the effects made; without it the messages that make and end
                                   // them do nothing
  std::vector<Corpse>* corpses{};  // the corpses left, when kept
  std::string_view act_name{};     // the names of the body's act (+0x9c) and sprite (+0x98),
  std::string_view sprite_name{};  // which a corpse takes
  GameActor* target_actor{};       // the actor CWorld::GetActorById finds for +0x1a8, which an
                                   // attack's hits are queued on, for a hit's attacker, or for
                                   // the own player's pursuit; `target` is its position
  GameActor* player_actor{};       // the player (CWorld+0x3c) as an actor, which the hits aimed
                                   // at own_id are queued on and whom own_id's hits are from;
                                   // `player` is its position
  std::vector<Arrow>* arrows{};    // the arrows shot, when kept
  std::vector<std::int32_t>* launched{};  // the effects CAbleToMakeEffect::LaunchEffect
                                          // (00501e10) is asked for, by id, which the port
                                          // doesn't make, when kept
  std::vector<EffectMessage>* launched_messages{};  // what those it makes are sent, each by
                                                    // its place in `launched` from 1, when
                                                    // kept
  bool effects_on{};     // 00687ea8: LaunchEffect makes any effect, not only its own few
  bool msg_sprites{};    // 00687ec0, which CGameMode 00516a80 toggles: message 0x59 shows one
  bool guild_war_map{};  // CWorld+0x60 (ClientState::IsGuildWarMap, 005aa630)
  const std::vector<std::int32_t>* own_statuses{};  // the player's statuses (the vector at
                                                    // 00641f98), which packet 0x0196 keeps
  std::int32_t target_weapon{-1};  // vf31 of target_actor and of player_actor: -1 for a
  std::int32_t player_weapon{-1};  // CGameActor, a CPc's weapon (+0x2a0)
  ActorWindows* windows{};         // the windows made and what they were told; without it
                                   // none is made and those an actor has are not told
  std::int32_t screen_width{640};  // g_pSceneRenderer (00625e98) +0x24 and +0x28
  std::int32_t screen_height{480};
  // The world's own player's (docs/world/pursuit.md):
  std::vector<MoveOrder>* own_moves{};  // the orders of the moves (message 0) it sends itself,
                                        // when kept; actor_messages has each as 0 with 0s
  std::uint32_t ping{};             // ClientState+0x524, the mean of the last three round trips of
                                    // packet 0x007f (ClientState::AddPingSample, 0058e280)
  std::int32_t attack_range{};      // ClientState+0x640, in cells (packets 0x0139 and 0x013a)
  const render::Vec3* item{};       // where the item CWorld::GetItemById (004c4d20) finds lies
  const GameActor* skill_object{};  // the skill's object CWorld::GetSkillById (004c4db0) finds
  std::vector<std::int32_t>* picked{};  // CGameMode+0x198 (its size +0x19c): the items asked to
                                        // be picked up, by id
  std::int32_t own_speed{};    // ClientState+0xd94 (00687404), the player's speed (packet 0x00b0)
  std::int32_t skill_range{};  // CGameMode+0x364, the range of the skill being aimed (its record's
                               // +0x18, CGameMode::SendMsg 00521cde; 5 after packet 0x019e)
  std::vector<CellSkill>* cell_skills{};  // what message 0x42 asks for, when kept
  // The ids IsInAdminListB (004cad40) looks among (0067c978, the clientinfo's admins), whose
  // players' bodies and weapons are the administrators' own.
  const std::vector<std::int32_t>* admins{};
  // CPlayer::SendMsg for the messages the own player sends itself, the id and two words; without
  // it they are kept in actor_messages.
  std::function<void(std::int32_t, std::int32_t, std::int32_t)> send_own{};
  // NOLINTEND(readability-redundant-member-init)
};

// The CPlayer fields beyond a CPc's: the world's own player's, whom the user moves
// (docs/world/pursuit.md). CPlayer::SendMsg (0054c5d0) sets them; the initial values are
// 0054b4a0's. A CPlayer reports neither its cell nor its walking (GameActor::reports false).
struct OwnPlayer {
  std::int32_t goal_x = 0;          // +0x308 and +0x30c, the cell it last asked to walk to; -1
  std::int32_t goal_y = 0;          // after SetState but to walking
  std::uint32_t acted_at = 0;       // +0x310, timeGetTime when it last acted on its target
  std::uint32_t premove_at = 0;     // +0x314, when message 9 set PreMove
  std::int32_t pursuit = 0;         // +0x318, what it pursues: 1 and 5 an actor to attack (5
                                    // one faster than it), 2 an item to pick up, 3 an actor and
                                    // 4 a cell to cast a skill on, 6 a skill's object; 0 none
  std::int32_t premove = 0;         // +0x31c, message 9's: a move sent ahead of the server's
  std::int32_t attack_mode = 0;     // +0x320, message 0xa's second word: 2 attacks once
  std::int32_t acted = 0;           // +0x324, set once it acts, until the next order
  std::int32_t walk_asked = 0;      // +0x328, set once message 0x5b asks the game mode to walk,
                                    // cleared by a move (message 0)
  std::int32_t move_queued = 0;     // +0x32c: a move to +0x358, +0x35c waits for an attack, a
                                    // flinch or a fall (states 2, 5 and 6) to end
  std::int32_t target_id = 0;       // +0x330, the actor, item or skill's object pursued
  std::int32_t acts = 0;            // +0x334, the times it acted on the target
  std::uint32_t asked_at = 0;       // +0x338, when it last asked to walk toward an attack's
                                    // target
  std::uint32_t walk_asked_at = 0;  // +0x33c, when message 0x5b last asked
  std::int32_t skill = 0;           // +0x344, the skill to cast
  std::int32_t range = 1;           // +0x348, its range in cells
  std::int32_t level = 0;           // +0x34c, its level
  std::int32_t cell_x = 0;          // +0x350 and +0x354, the cell to cast on
  std::int32_t cell_y = 0;
  std::int32_t queued_x = -1;  // +0x358 and +0x35c
  std::int32_t queued_y = -1;
};

// A player's look (docs/world/look.md): what CPc's vf14 and vf15 dress it in, and the names its
// layers' resources were asked for by, as given (ResourceManager::GetResource, 004dff60), ""
// none.
struct PlayerLook {
  std::int32_t hair = 0;                   // +0x298
  std::int32_t hair_color = 0;             // +0x29c
  std::array<std::int32_t, 3> headgear{};  // +0x2a4, +0x2a8 and +0x2ac: the top, middle and
                                           // bottom
  std::int32_t shield = 0;                 // +0x2b0, a kind
  std::int32_t body_color = 0;             // +0x34 (CRenderObject), the clothes' colour
  std::array<render::PlayerSpriteNames, render::kPlayerLayerCount> layers{};  // +0x2f8, +0x2e8
  render::PlayerSpriteNames body{};  // the object's own actions and sprite (+0x9c, +0x98)
  std::string imf;                   // +0x27c
  std::string head_palette;          // +0x2cc
  std::string body_palette;          // +0x10
};

// The CPc fields a player's states read and write beyond a CGameActor's.
struct PlayerActor : GameActor {
  std::int32_t field_0x28c = 0;  // +0x28c, vf36's argument: below 0 it shows emotion 0x22
  std::int32_t saved_frame = 0;  // +0x294: the frame standing goes back to
  std::int32_t weapon = 0;       // +0x2a0 (CPc::SetWeapon, 005464b0), a kind or an item's id
  std::int32_t gauge = 0;        // +0x2dc, its gauge (UIPcGage), by number in ActorWindows
  std::int32_t field_0x2e0 = 0;  // +0x2e0 and +0x2e4, message 0x72's second and first words
  std::int32_t field_0x2e4 = 0;
  PlayerLook look;
  std::optional<OwnPlayer> own;  // a CPlayer's fields: the world's own player, whose vf10 is
                                 // CPlayer::SetState (0054c550)
};

// CPc's constructor (00541fb0): MakeGameActor, then CPc::ResetPcFields (005422a0), whose fields
// the port has at 0, +0x240 among them. The weapon (+0x2a0) is left as the memory had it.
PlayerActor MakePlayerActor(std::uint32_t now, std::uint32_t server_time);

// CGameMode::GetAttrCell (00511d50): the attribute cell under world (x, z), floor(x / cell
// size + width / 2) and floor(z / cell size + height / 2), the halves C's.
std::array<std::int32_t, 2> AttrCellAt(const res::AttrMap& attr, float x, float z);

// A cell and where in it, in sixteenths of a cell.
struct CellPoint {
  std::int32_t cell_x = 0;
  std::int32_t cell_y = 0;
  std::int32_t sub_x = 0;
  std::int32_t sub_y = 0;
};

// CGameMode 00511c30: the cell under world (x, z) as AttrCellAt finds it, and the sixteenths
// of it, floor((width / 2 + x / cell size - cell) * 16) modulo 16, C's remainder.
CellPoint CellPointAt(const res::AttrMap& attr, float x, float z);

// IsSpecialActorIdOnKoreaService (004d8e10): on the Korean service (0), ids 0x2c8c7d to
// 0x2c8c80 and 0x2c8c82.
bool IsSpecialActorIdOnKoreaService(std::int32_t service_type, std::int32_t id);

// CGameActor::vf07 (00502ff0): CRenderObject::SetAction, then not pushed and +0x1ac -1.
void SetGameActorAction(GameActor& actor, const res::Act& act, std::int32_t action,
                        std::int32_t play_mode);

// CGameActor::SetState (005034e0, vf10) at timeGetTime `now`. Nothing happens in state 3 or
// with +0xd0 set, nor for 7 in state 6; leaving state 4 puts the actor on its target. Then
// the state's work (docs/world/states.md):
//  - standing (0) and walking (1): their actions and messages;
//  - attacking (2, 7 and 9): action 0x50 (0x58 for 9) played once, the turn toward the actor
//    +0x1a8 names, an arrow for job 0x505, +0x1ac the action's attack frame (vf33), and the
//    cell reported;
//  - dying (3): lies down (+0x80), ends a frozen body's ice (0x3b and 0x3e to itself) and
//    plays action 0x40 once;
//  - 4: closes the cast's gauge (0x53), plays action 0x30 once and reports the cell;
//  - 5: action 0x18 played once, clearing +0xcc and +0x82; 11 action 0x20 and 12 action 0x28
//    played once, 12 reporting the cell;
//  - 13: held, action 0x58 or 0x50 by AttacksWithSecondAction for jobs below 0x16, otherwise
//    0x10, played once;
//  - 6, 8, 10 and those past 13: none.
// Last the action's direction, the state and its time.
void SetGameActorState(GameActor& actor, std::int32_t state, const StateContext& context,
                       std::uint32_t now);

// CPc::SetState (00543020, vf10), around CGameActor's: standing goes back to the saved frame,
// takes the ready stance (action 32) after states 2, 4 and 8 or with +0x224 set, and clears
// CGameMode+0x2a0; walking clears +0xcc and, but for +0x218 1 and 2, +0x82, closes the
// cast's gauge (message 0x53, CGameActor::SendMsg) and forgets the saved frame. 6 reports
// the cell and plays action 0x10 once, finished at once, on the saved frame; 8 reports it and
// plays action 0x60 once, finished at once. 2 and 9 start as walking does, then are
// CGameActor's 9 or 2 by AttacksWithSecondAction, +0x1ac PlayerAttackFrame's, and for a bow
// (2) or weapons 0xd and 0xe (9) an arrow and 8 / +0x44 more on +0x1ac; the state stays the
// one asked for. 7 plays action 0x20 (jobs 0x13, 0x14, 0xe, and 0xf with +0x1bc 0) or 0x60
// once and sets +0x1ac to 6. The rest are CGameActor's, after leaving state 4 puts the player
// on its target.
// For the own player (`own`), CPlayer::SetState (0054c550) first: nothing while dying; but for
// walking, the goal (+0x308, +0x30c) forgotten (-1); then SendQueuedMove. Its state 3's game
// mode message 5 and effect 0x150 (CGameMode::LaunchEffectAt, 0050ff10) are not ported.
void SetPlayerState(PlayerActor& player, std::int32_t state, const StateContext& context,
                    std::uint32_t now);

// What an update of an actor's state came to.
enum class StateResult {
  kFaded,  // returned 0: the fade (+0x234) is over
  kDone,   // returned 1
  kGone,   // returned 0: it died, and left its corpse
};

// CGameActor::ProcessState (005038c0, vf09) in the actor's state, at timeGetTime `now` and the
// server's time. Walking is ProcessWalk, standing at the path's end. Otherwise the hold and the
// fade, then the state's work, and the times:
//  - 0: an action played once and finished stands again (SetState(0)), or the player's pet
//    (jobs 1000 to 0x57f, +0x1f4 20, its id the pet's) may talk: 3 s after +0x1d4, a
//    rand() % 100 below 25 sends the game mode 0xa1 with each of its lines, and +0x1d4 is
//    cleared. Then the frame (vf08).
//  - 2, 5, 7, 9 and 12: the frame and, the action finished, standing again; 5 and 7 clear
//    +0xcc first.
//  - 3: clears +0x82 and +0xcc and steps the frame. The action finished with nothing queued
//    (+0x20c), the game mode hears 0xd with the id; a job past 0x16 then leaves its corpse
//    (vf34) and the update returns 0 (kGone) without the times.
//  - 4: clears +0xcc, steps the frame, and slides toward the target (+0x228, +0x22c) by the
//    time since the last update over what is left of 200 ms from the state's start; the
//    action finished, it is on the target and stands. Set on the ground when it moved.
//  - 8: clears +0xcc, steps the frame, and faces the actor +0x1a8 names (context.target) on
//    every 34th tick since the state began.
//  - 10: clears +0xcc; 16 and 17: the frame; the rest nothing.
//  - 13, casting (docs/world/windows.md): with +0xcc clear, sets it and plays SetState's action
//    again, held; once timeGetTime reaches the cast's end (+0x1d8), clears +0xcc and +0x82,
//    stands and closes the gauge (0x53). Then the frame.
StateResult ProcessGameActorState(GameActor& actor, const StateContext& context, std::uint32_t now,
                                  std::uint32_t server_time);

// CPc::ProcessState (00542eb0, vf09): the hold and the fade, then, without the times, 2 and 7
// the frame and, the action finished, standing again; 3 the frame, clearing +0x82 first; 6 the
// frame; 8 the frame and facing the target as CGameActor's state 8, keeping +0xcc. The rest
// are CGameActor's, standing again through CPc::SetState.
StateResult ProcessGameActorState(PlayerActor& player, const StateContext& context,
                                  std::uint32_t now, std::uint32_t server_time);

// CGameActor::ReportCell (00503110, vf11): the game mode told 0xc with the actor's id and the
// attribute cell under it.
void ReportGameActorCell(const GameActor& actor, const StateContext& context);

// CGameActor::SendMsg's message 0x1c (00505266), which packet 0x0080 sends when the actor has
// died: a monster's id (jobs 1001 to 0x57f) becomes 100000 less it; then dying (SetState(3)),
// unless something is queued (+0x20c) and the job is 0x16 or past, when +0x210 is set and it
// dies once that is played.
void KillGameActor(GameActor& actor, const StateContext& context, std::uint32_t now);
void KillGameActor(PlayerActor& player, const StateContext& context, std::uint32_t now);

// Message 0xc (00505300), which the actor sends itself once what was queued has played:
// dying, unless it already is.
void KillGameActorAfterQueue(GameActor& actor, const StateContext& context, std::uint32_t now);
void KillGameActorAfterQueue(PlayerActor& player, const StateContext& context, std::uint32_t now);

// CGameActor::vf02's message 0 (00505dd3): the move time from the order, FindMovePath at
// the actor's speed; found, the goal kept and walking (segment 0), otherwise standing.
PathResult MoveGameActor(GameActor& actor, PathFinder& finder, const MoveOrder& order,
                         std::uint32_t server_time, const StateContext& context, std::uint32_t now);

// CGameActor::SendMsg's message 0x13 (005062b3): a move from where the actor stands, at the
// server's time, to (goal_x, goal_y). A speed of 0 is 2 (0x37 with +0xd8) and pushes the
// actor; any other holds it (+0x82).
PathResult MoveGameActorTo(GameActor& actor, PathFinder& finder, std::int32_t goal_x,
                           std::int32_t goal_y, std::int32_t speed, std::uint32_t server_time,
                           const StateContext& context, std::uint32_t now);
PathResult MoveGameActorTo(PlayerActor& player, PathFinder& finder, std::int32_t goal_x,
                           std::int32_t goal_y, std::int32_t speed, std::uint32_t server_time,
                           const StateContext& context, std::uint32_t now);

// Message 0 to a player, whose vf10 is CPc::SetState.
PathResult MoveGameActor(PlayerActor& player, PathFinder& finder, const MoveOrder& order,
                         std::uint32_t server_time, const StateContext& context, std::uint32_t now);

}  // namespace ragexe::world
