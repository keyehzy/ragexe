#pragma once

#include <cstdint>
#include <string_view>

namespace ragexe::res {

// The tables ClientState builds into itself rather than reading them from the data files
// (docs/formats/client_tables.md#built-in-tables).

// ClientState::InitJobAndSkillNames (0058ac70), +0x754: a job's name, 0x580 of them, 1_ETC_01 where
// none is given. The NPCs' and the monsters' names are their sprites' (CNpc::vf16, 0053fae0).
// Job 2 is Mage on service type 1 (0067c9d4), Magician on the others. Nothing outside the
// table.
std::string_view JobName(std::int32_t job, std::int32_t service);
std::int32_t JobCount();

// The same function's skill names (+0x410) by skill id, 0 Unknown Skill, and the guild
// skills' (+0x490); nothing outside the tables.
std::string_view SkillName(std::int32_t skill);
std::int32_t SkillNameCount();
std::string_view GuildSkillName(std::int32_t index);

// Its sexes' letters (+0x764): F for 0, M for 1, nothing otherwise.
std::string_view SexLetter(std::int32_t sex);

// ClientState::InitShadowScales (0059a5e0), read by GetShadowScale (0059b750): a job's
// shadow scale (CRenderObject+0x40), 1 unless given. The original reads past its 0x580
// entries for a job outside them; here those are 1 too.
float JobShadowScale(std::int32_t job);

}  // namespace ragexe::res
