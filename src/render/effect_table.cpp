#include "render/effect_table.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>

namespace ragexe::render {

namespace {

struct EffectStr {
  std::int32_t id;
  const char* name;     // with %d for a numbered variant
  const char* minimal;  // the name with minimal effects on, nullptr for the same
  std::int32_t variants;
};

// InitEffect's switch, ids 10 to 0x150 (the byte table 0054f854 into the jump table
// 0054f690); the names are its strings at 006184..00618b74, some in CP949.
// clang-format off
constexpr EffectStr kEffectStrs[] = {
    {0xa, "Maemor.str", "memor_min.str", 0},
    {0xd, "SafetyWall.str", nullptr, 0},
    {0x17, "StoneCurse.str", nullptr, 0},
    {0x19, "Firewall%d.str", nullptr, 2},
    {0x1d, "Lightning.str", nullptr, 0},
    {0x1e, "ThunderStorm.str", nullptr, 0},
    {0x28, "Cross.str", nullptr, 0},
    {0x29, "Angelus.str", nullptr, 0},
    {0x31, "FireHit%d.str", nullptr, 3},
    {0x34, "WindHit%d.str", nullptr, 3},
    {0x40, "ArrowShot.str", nullptr, 0},
    {0x41, "Invenom.str", nullptr, 0},
    {0x42, "Cure.str", "cure_min.str", 0},
    {0x43, "Provoke.str", nullptr, 0},
    {0x44, "Mvp.str", nullptr, 0},
    {0x45, "SkidTrap.str", nullptr, 0},
    {0x46, "Brandish.str", nullptr, 0},
    {0x4b, "Gloria.str", "gloria_min.str", 0},
    {0x4c, "Magnificat.str", "magnificat_min.str", 0},
    {0x4d, "Resurrection.str", "resurrection_min.str", 0},
    {0x4e, "Recovery.str", nullptr, 0},
    {0x53, "Sanctuary.str", nullptr, 0},
    {0x54, "Impositio.str", nullptr, 0},
    {0x55, "LexAeterna.str", "lexaeterna_min.str", 0},
    {0x56, "Aspersio.str", nullptr, 0},
    {0x57, "LexDivina.str", nullptr, 0},
    {0x58, "Suffragium.str", "suffragium_min.str", 0},
    {0x59, "StormGust.str", nullptr, 0},
    {0x5a, "Lord.str", nullptr, 0},
    {0x5b, "Benedictio.str", nullptr, 0},
    {0x5c, "Meteor%d.str", nullptr, 3},
    {0x5f, "Quagmire.str", nullptr, 0},
    {0x60, "FirePillar.str", nullptr, 0},
    {0x61, "FirePillarBomb.str", nullptr, 0},
    {0x65, "RepairWeapon.str", nullptr, 0},
    {0x66, "CrashEarth.str", nullptr, 0},
    {0x67, "WeaponPerfection.str", "WeaponPerfection_min.str", 0},
    {0x68, "MaximizePower.str", "maximize_min.str", 0},
    {0x6a, "BlastMine.str", nullptr, 0},
    {0x6b, "Claymore.str", nullptr, 0},
    {0x6c, "Freezing.str", nullptr, 0},
    {0x6d, "Bubble%d.str", nullptr, 4},
    {0x6e, "GasPush.str", nullptr, 0},
    {0x6f, "Spring.str", nullptr, 0},
    {0x70, "Kyrie.str", "kyrie_min.str", 0},
    {0x71, "Magnus.str", nullptr, 0},
    {0x7c, "VenomDust.str", nullptr, 0},
    {0x7e, "PoisonReact_1st.str", nullptr, 0},
    {0x7f, "PoisonReact.str", nullptr, 0},
    {0x81, "VenomSplasher.str", nullptr, 0},
    {0x82, "TwoHand.str", nullptr, 0},
    {0x83, "AutoCounter.str", nullptr, 0},
    {0x85, "Freeze.str", nullptr, 0},
    {0x86, "Freezed.str", nullptr, 0},
    {0x87, "IceCrash.str", nullptr, 0},
    {0x8b, "SandMan.str", nullptr, 0},
    {0x8d, "Pneuma%d.str", nullptr, 3},
    {0x8f, "SonicBlow.str", nullptr, 0},
    {0x90, "Brandish2.str", nullptr, 0},
    {0x91, "ShockWave.str", nullptr, 0},
    {0x92, "ShockWaveHit.str", nullptr, 0},
    {0x93, "EarthHit.str", nullptr, 0},
    {0x94, "Pierce.str", nullptr, 0},
    {0x95, "Bowling.str", nullptr, 0},
    {0x96, "SpearStab.str", nullptr, 0},
    {0x97, "SpearBoomerang.str", nullptr, 0},
    {0x98, "HolyHit.str", nullptr, 0},
    {0x99, "Concentration.str", nullptr, 0},
    {0x9a, "bs_RefineSuccess.str", nullptr, 0},
    {0x9b, "bs_RefineFailed.str", nullptr, 0},
    {0x9c, "JobChange.str", nullptr, 0},
    {0x9d, "LevelUP.str", nullptr, 0},
    {0x9e, "JobLvUP.str", nullptr, 0},
    {0xa7, "TamingSuccess.str", nullptr, 0},
    {0xa8, "TamingFailed.str", nullptr, 0},
    {0xa9, "EnergyCoat.str", nullptr, 0},
    {0xaa, "CartRevolution.str", nullptr, 0},
    {0xb5, "MentalBreak.str", nullptr, 0},
    {0xb6, "magical.str", nullptr, 0},
    {0xb7, "sui_explosion.str", nullptr, 0},
    {0xb9, "suicide.str", nullptr, 0},
    {0xba, "yunta_1.str", nullptr, 0},
    {0xbb, "yunta_2.str", nullptr, 0},
    {0xbc, "yunta_3.str", nullptr, 0},
    {0xbd, "yunta_4.str", nullptr, 0},
    {0xbe, "yunta_5.str", nullptr, 0},
    {0xbf, "homing.str", nullptr, 0},
    {0xc0, "poison.str", nullptr, 0},
    {0xc1, "silence.str", nullptr, 0},
    {0xc2, "stun.str", nullptr, 0},
    {0xc3, "StoneCurse.str", nullptr, 0},
    {0xc5, "sleep.str", nullptr, 0},
    {0xc7, "Pong%d.str", nullptr, 3},
    {0xcc, "\273\241\260\243\306\367\274\307.str", nullptr, 0},  // 빨간포션.str
    {0xcd, "\301\326\310\253\306\367\274\307.str", nullptr, 0},  // 주홍포션.str
    {0xce, "\263\353\266\365\306\367\274\307.str", nullptr, 0},  // 노란포션.str
    {0xcf, "\307\317\276\341\306\367\274\307.str", nullptr, 0},  // 하얀포션.str
    {0xd0, "\306\304\266\365\306\367\274\307.str", nullptr, 0},  // 파란포션.str
    {0xd1, "\303\312\267\317\306\367\274\307.str", nullptr, 0},  // 초록포션.str
    {0xd2, "fruit.str", nullptr, 0},
    {0xd3, "fruit_.str", nullptr, 0},
    {0xd5, "Deffender.str", nullptr, 0},
    {0xd6, "Keeping.str", nullptr, 0},
    {0xda, "\301\375\301\337.str", nullptr, 0},  // 집중.str
    {0xdb, "\260\242\274\272.str", nullptr, 0},  // 각성.str
    {0xdc, "\271\366\274\255\305\251.str", nullptr, 0},  // 버서크.str
    {0xfa, "TwoHand.str", nullptr, 0},
    {0x131, "p_success.str", nullptr, 0},
    {0x132, "p_failed.str", nullptr, 0},
    {0x13b, "SafetyWall.str", nullptr, 0},
    {0x14f, "angel.str", nullptr, 0},
    {0x150, "devil.str", nullptr, 0},
};
// clang-format on

// 00617148: one dword per id.
constexpr std::array<std::int32_t, kEffectIdCount> kDurations = {
    50,       50,       50,       50,       50,    50,    100,   50,       80,     80,
    100,      80,       40,       9999,     50,    100,   40,    30,       50,     50,
    100,      100,      270,      9999,     100,   40,    160,   150,      100,    200,
    104,      160,      100,      200,      100,   100,   100,   100,      100,    100,
    9999,     9999,     150,      100,      50,    100,   180,   250,      130,    50,
    50,       50,       40,       50,       40,    40,    40,    40,       40,     40,
    200,      250,      250,      250,      9999,  9999,  9999,  9999,     9999,   9999,
    9999,     250,      500,      250,      250,   9999,  9999,  9999,     9999,   250,
    250,      250,      250,      9999,     9999,  9999,  9999,  9999,     9999,   9999,
    9999,     9999,     9999,     250,      250,   150,   9999,  9999,     250,    250,
    70,       9999,     9999,     9999,     9999,  250,   9999,  9999,     9999,   9999,
    9999,     9999,     9999,     9999,     350,   250,   250,   100,      250,    250,
    250,      40,       250,      250,      9999,  250,   9999,  9999,     250,    9999,
    9999,     9999,     250,      9999,     9999,  9999,  250,   350,      8000,   9999,
    250,      9999,     250,      9999,     9999,  32797, 9999,  9999,     9999,   9999,
    9999,     9999,     9999,     9999,     9999,  9999,  9999,  9999,     9999,   9999,
    9999,     99999999, 99999999, 99999999, 9999,  1000,  9999,  9999,     9999,   9999,
    9999,     9999,     100,      100,      100,   100,   100,   100,      100,    100,
    50,       9999,     9999,     9999,     0,     9999,  9999,  9999,     9999,   9999,
    9999,     9999,     9999,     9999,     9999,  9999,  150,   9999,     0,      9999,
    99999999, 99999999, 99999999, 99999,    9999,  9999,  9999,  9999,     9999,   9999,
    9999,     9999,     500,      9999,     9999,  500,   500,   500,      9999,   9999,
    9999,     150,      200,      9999,     300,   300,   999,   999,      999999, 99999999,
    99999999, 999,      99999,    99999999, 999,   999,   999,   999,      999,    29999,
    29999,    29999,    29999,    99999,    999,   999,   999,   999,      200,    999,
    9999,     150,      200,      100,      99999, 999,   999,   999,      999,    999,
    999,      300,      300,      300,      300,   300,   300,   16,       300,    300,
    300,      300,      300,      300,      300,   300,   300,   29999,    29999,  29999,
    29999,    29999,    29999,    29999,    29999, 29999, 29999, 29999,    29999,  29999,
    29999,    29999,    29999,    29999,    29999, 999,   999,   99999999, 300,    300,
    300,      99999999, 99999,    1000,     1000,  1000,  1000,  36000,    300,    999,
    300,      300,      100,      100,      100,   9999,  200,   9999,     9999,   9999,
    100,      99999999, 36000,    36000,    36000, 100,   340,   340,      1000,   100,
    200,      300,      300,      99999999, 9999,  9999,  9999,  50,
};

}  // namespace

std::string EffectStrName(std::int32_t id, bool minimal, core::CrtRand& rand) {
  const auto* it = std::find_if(std::begin(kEffectStrs), std::end(kEffectStrs),
                                [&](const EffectStr& e) { return e.id == id; });
  if (it == std::end(kEffectStrs)) return {};
  if (it->variants != 0) {
    char name[64];
    std::snprintf(name, sizeof name, it->name, rand.Next() % it->variants + 1);
    return name;
  }
  return minimal && it->minimal != nullptr ? it->minimal : it->name;
}

std::int32_t EffectDuration(std::int32_t id) {
  if (id < 0 || id >= kEffectIdCount) return 0;
  return kDurations[static_cast<std::size_t>(id)];
}

}  // namespace ragexe::render
