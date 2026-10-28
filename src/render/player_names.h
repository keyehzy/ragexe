#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace ragexe::render {

// The names of a player character's sprites and palettes (docs/render/player.md#the-names),
// built as ClientState builds them from the tables InitNameTables (00588ba0) fills. They
// are CP949 paths, the sprites' relative to sprite\ and without their extension, the
// palettes' relative to palette\. A job is CGameActor::GetJob's (00503490), 0 the novice;
// a sex is 0 female, 1 male.

// GetBodyActName and GetBodySpriteName (00593920, 00593960): 인간족\몸통\<sex>\<job>_<sex>.
std::string PlayerBodyName(std::int32_t job, std::int32_t sex);

// GetHeadActName and GetHeadSpriteName (005939a0, 00593a20): 인간족\머리통\<sex>\<head>_<sex>,
// the head the hair style names for the sex; a style outside 0 to 19 is 13.
std::string PlayerHeadName(std::int32_t hair, std::int32_t sex);

// GetImfName (00593b70): <job>_<sex>.imf.
std::string PlayerImfName(std::int32_t job, std::int32_t sex);

// A layer's actions and sprite; an empty name leaves the layer undrawn.
struct PlayerSpriteNames {
  std::string act;
  std::string sprite;
};

// CPc::vf21 to vf23 (layers 2 to 4) through 00593bb0 and 00594020:
// 악세사리\<sex>\<sex><headgear>, nothing for headgear 0. The actions are also nothing for
// headgear 0x12 and 0x13 on a male, which leaves those undrawn.
PlayerSpriteNames PlayerHeadgearNames(std::int32_t sex, std::int32_t headgear);

// ClientState 005932a0: whether a job shows a weapon of this kind.
bool CanShowWeapon(std::int32_t job, std::int32_t kind);

// CPc::vf19 (005426a0), the weapon by its kind: layer 5 is 인간족\<job>\<job>_<sex><weapon>
// when the job shows it, and layer 6 the same with _검광, the glow, for kinds 1 to 7 and
// 0x10 to 0x16.
std::array<PlayerSpriteNames, 2> PlayerWeaponNames(std::int32_t job, std::int32_t sex,
                                                   std::int32_t kind);

// ClientState::MapHighIdToKind (005aae70): the shields' item ids 0x835 to 0x83f as kinds 1
// to 4, others 0.
std::int32_t ShieldKindOf(std::int32_t id);

// ClientState::IsValueInRangeForKind (0059b940): whether a job shows a shield of this kind
// (an id above 2000 is mapped first).
bool CanShowShield(std::int32_t job, std::int32_t kind);

// CPc::vf24 (005428d0) through 0059ba10 and 0059ba80: 방패\<job>\<job>_<sex><shield> when
// the job shows it, an id above 2000 taken as its kind.
PlayerSpriteNames PlayerShieldNames(std::int32_t job, std::int32_t sex, std::int32_t shield);

// CPc::vf27 (00542a40) through 0059b890: 머리\머리<head>_<sex>_<palette>.pal, nothing for
// palette 0. Hair style 0 stands for the job's own (ResolveZeroIndex, 00593560).
std::string PlayerHeadPaletteName(std::int32_t hair, std::int32_t job, std::int32_t sex,
                                  std::int32_t palette);

// CPc::vf28 (00542b30) through 0059b900: 몸\<job>_<sex>_<palette>.pal, nothing for palette 0.
std::string PlayerBodyPaletteName(std::int32_t job, std::int32_t sex, std::int32_t palette);

}  // namespace ragexe::render
