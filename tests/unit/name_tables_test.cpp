#include "res/name_tables.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "render/player_names.h"

using namespace ragexe::res;

namespace {

struct JobNamesVector {
  int service;
  const char* const* names;
  std::size_t count;
};

struct NpcSpriteVector {
  int job, actor_job;
  const char* sprite;
  const char* act;
  std::uint32_t scale;
};

#include "name_table_vectors.inc"

}  // namespace

TEST_CASE("Job names are the original's for every service type", "[name_tables]") {
  for (const JobNamesVector& v : kJobNameTables) {
    INFO("service " << v.service);
    REQUIRE(static_cast<std::size_t>(JobCount()) == v.count);
    for (std::size_t job = 0; job < v.count; ++job) {
      INFO("job " << job);
      CHECK(JobName(static_cast<std::int32_t>(job), v.service) == std::string_view(v.names[job]));
    }
    CHECK(JobName(-1, v.service).empty());
    CHECK(JobName(static_cast<std::int32_t>(v.count), v.service).empty());
  }
}

TEST_CASE("Skill, guild skill and sex names are the original's", "[name_tables]") {
  REQUIRE(static_cast<std::size_t>(SkillNameCount()) == std::size(kSkillNames));
  for (std::size_t i = 0; i < std::size(kSkillNames); ++i)
    CHECK(SkillName(static_cast<std::int32_t>(i)) == std::string_view(kSkillNames[i]));
  CHECK(SkillName(-1).empty());
  CHECK(SkillName(SkillNameCount()).empty());
  CHECK(kZeroVectorSizes[0] == SkillNameCount());
  CHECK(kZeroVectorSizes[1] == SkillNameCount());
  CHECK(static_cast<std::size_t>(kZeroVectorSizes[2]) == std::size(kGuildSkillNames));
  for (std::size_t i = 0; i < std::size(kGuildSkillNames); ++i)
    CHECK(GuildSkillName(static_cast<std::int32_t>(i)) == std::string_view(kGuildSkillNames[i]));
  CHECK(GuildSkillName(-1).empty());
  CHECK(GuildSkillName(static_cast<std::int32_t>(std::size(kGuildSkillNames))).empty());
  REQUIRE(std::size(kSexLetters) == 2);
  CHECK(SexLetter(0) == std::string_view(kSexLetters[0]));
  CHECK(SexLetter(1) == std::string_view(kSexLetters[1]));
  CHECK(SexLetter(2).empty());
  CHECK(SexLetter(-1).empty());
}

TEST_CASE("Shadow scales are the original's for every job", "[name_tables]") {
  REQUIRE(std::size(kShadowScales) == static_cast<std::size_t>(JobCount()));
  for (std::size_t job = 0; job < std::size(kShadowScales); ++job) {
    INFO("job " << job);
    CHECK(std::bit_cast<std::uint32_t>(JobShadowScale(static_cast<std::int32_t>(job))) ==
          kShadowScales[job]);
  }
  CHECK(JobShadowScale(-1) == 1.0f);
  CHECK(JobShadowScale(JobCount()) == 1.0f);
}

TEST_CASE("NPCs and monsters take the original's sprites and shadows", "[name_tables]") {
  std::size_t n = 0;
  for (const NpcSpriteVector& v : kNpcSprites) {
    INFO("case " << n++ << ": job " << v.job << ", the actor's " << v.actor_job);
    const auto names = ragexe::render::NpcSpriteNames(v.job, v.actor_job, 1);
    CHECK(names.sprite + ".spr" == std::string(v.sprite));
    CHECK(names.act + ".act" == std::string(v.act));
    CHECK(std::bit_cast<std::uint32_t>(JobShadowScale(v.job)) == v.scale);
  }
  CHECK(ragexe::render::NpcSpriteNames(JobCount(), 0, 1).sprite.empty());
  CHECK(ragexe::render::NpcSpriteNames(-1, 0, 1).act.empty());
}
