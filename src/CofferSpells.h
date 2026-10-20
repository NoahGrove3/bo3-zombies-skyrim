// Generated from sheets/coffer_spells.json (Noah's chosen altar spells). Do not edit.
#pragma once
#include <array>
#include <cstdint>

namespace bo3z::coffer
{
	enum Element : std::uint8_t { kFire, kFrost, kShock, kPoison, kDisease, kSun, kBlood, kArcane, kElements };
	inline constexpr const char* kElementName[kElements] = { "fire", "frost", "shock", "poison", "disease", "sun", "blood", "arcane" };
	struct Spell { const char* plugin; std::uint32_t id; const char* name; Element element; std::uint8_t tier; std::uint16_t cost; bool concentration; float power; };   // power > 1: an Empowered copy (made in game)
	inline constexpr std::array<Spell, 17> kSpells{ {
		{ "Apocalypse - Magic of Skyrim.esp", 0x022DA6, "Ember Bolt", kFire, 0, 13, false, 1.0f },
		{ "Apocalypse - Magic of Skyrim.esp", 0x024361, "Dragon's Teeth", kFire, 0, 13, false, 1.0f },
		{ "Apocalypse - Magic of Skyrim.esp", 0x0279BD, "Creeping Cold", kFrost, 0, 6, true, 1.0f },
		{ "Apocalypse - Magic of Skyrim.esp", 0x02CAE9, "Frozen Orb - Ice Bolt", kFrost, 0, 13, false, 1.0f },
		{ "Ordinator - Perks of Skyrim.esp", 0x07440E, "Skeleton Shock", kShock, 0, 13, false, 1.0f },
		{ "ForgottenMagic_Redone.esp", 0x01EB91, "Frost Bomb", kFrost, 1, 25, false, 1.0f },
		{ "MysticismMagic.esp", 0x440E99, "Leech Life", kBlood, 1, 9, true, 1.0f },
		{ "Apocalypse - Magic of Skyrim.esp", 0x15E73E, "Ice Prism", kFrost, 1, 9, true, 1.0f },
		{ "Apocalypse - Magic of Skyrim.esp", 0x027F2E, "Sleet Storm", kFrost, 1, 9, true, 1.0f },
		{ "Apocalypse - Magic of Skyrim.esp", 0x0294D4, "Bombardment", kFire, 1, 9, true, 1.0f },
		{ "mihailhighfantasypack.esp", 0x09A109, "Harpy Tornado", kArcane, 2, 38, false, 1.0f },
		{ "StormCalling.esl", 0x00080E, "Lightning Flood", kShock, 2, 38, false, 1.0f },
		{ "Apocalypse - Magic of Skyrim.esp", 0x002878, "Lightning Strike", kShock, 2, 38, false, 1.0f },
		{ "Ordinator - Perks of Skyrim.esp", 0x029A2F, "Nova Charge", kArcane, 2, 38, false, 1.0f },
		{ "Ordinator - Perks of Skyrim.esp", 0x1B3B2B, "Sun Blast", kSun, 2, 12, true, 1.0f },
		{ "mihailesobonecolossus.esp", 0x000822, "Sapphire Inferno", kFire, 3, 45, false, 1.0f },
		{ "Apocalypse - Magic of Skyrim.esp", 0x044264, "Forbidden Sun", kFire, 3, 45, false, 1.0f },
	} };
}
