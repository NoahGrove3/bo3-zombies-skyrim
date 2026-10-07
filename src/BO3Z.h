#pragma once
// BO3 Zombies x Skyrim. Every number comes from Generated.h (built from sheets/*.json).
#include "Generated.h"

namespace bo3z
{
	inline constexpr auto kVersion = "0.2.0";

	// Forms read from the player's own Skyrim and from BO3Zombies.esp.
	namespace Forms
	{
		bool                 Resolve();  // after data loaded
		RE::TESBoundObject*  Enemy(std::size_t a_index);
		RE::TESQuest*        IntroQuest();
		bool                 IntroDone();
		std::string          IntroDebug();
		RE::TESGlobal*       Quickstart();
		RE::TESObjectREFR*   RiverwoodMarker();
		RE::TESObjectWEAP*   Gun(int a_gun);
		int                  GunIndexOf(const RE::TESForm* a_form);  // -1 if not one of ours
		RE::AlchemyItem*     PerkPotion(int a_perk);
		int                  PerkIndexOfEffect(RE::FormID a_mgef);    // -1
		RE::TESObjectACTI*   Object(int a_object);
		int                  ObjectIndexOf(const RE::TESForm* a_base); // -1
		RE::TESBoundObject*  Ours(std::uint32_t a_localId);              // any record in our plugin
		RE::SpellItem*       GunSpell(int a_gun);                        // tomes/grimoires: the spell held in the hand
		RE::MagicItem*       Spell(int a_spell);                         // sheet: spells (a WEAP row = its enchantment)
		void                 Play(gen::Sound a_sound);
		void                 PlayAt(gen::Sound a_sound, RE::TESObjectREFR* a_at);  // 3D, follows the reference
		inline gen::Sound    Brew(int a_perk) { return static_cast<gen::Sound>(static_cast<int>(gen::Sound::brew_trollshide) + a_perk); }
	}

	// Plugins.txt: make sure BO3Zombies.esp is enabled (called from DllMain, before the game reads it).
	namespace PluginList
	{
		void Ensure();
	}

	// State kept on the player (sheet: persistence).
	namespace Save
	{
		struct Slot
		{
			int gun{ -1 };
			int clip{ 0 };
			int reserve{ 0 };
			int level{ 0 };  // Atronach Reforge level 0-3
		};
		int  Get(int a_av);
		void Set(int a_av, int a_value);
		Slot GetSlot(int a_slot);
		void SetSlot(int a_slot, const Slot& a_value);
		int  Flags();
		void SetFlags(int a_flags);
		bool Flag(int a_bit);
		void SetFlag(int a_bit, bool a_on);
		int  Crew();  // -1 none
		void SetCrew(int a_crew);
		bool HasPerk(int a_perk);
		void SetPerk(int a_perk, bool a_on);
		int  PerkCount();
		int  PerkBits();
		int  ActiveSlot();
		void SetActiveSlot(int a_slot);
		void OnWorldReady();
		inline constexpr int kFlagInit = 0, kFlagReserveV2 = 4, kFlagStarted = 5, kFlagMoved = 11, kPerkShift = 14, kFlagPerksV2 = 23;  // bits: init | crew 1-3 | (old perks 4-10) | moved 11 | slot 12-13 | perks 14-22 | v2 23
	}

	namespace Points
	{
		void Add(int a_points);
		bool Spend(int a_points);
		int  Get();
	}

	namespace Director
	{
		void             Tick(float a_delta);
		void             Reset();
		bool             IsRoundUndead(RE::Actor* a_actor);
		bool             NightNow();  // night hours (the undead are out)
		float            RoundHealth();  // a zombie's full health this round (Skyrim units)
		bool             IsElite(RE::Actor* a_actor);
		std::string_view HeadNodeFor(RE::Actor* a_actor);
		void             LogState();
		void             Web(const RE::NiPoint3& a_at, float a_radius, float a_seconds, float a_amount = 60.0f, bool a_frostFx = false);   // Widow's Wine / wand slows
		void             Shock(const RE::NiPoint3& a_at, float a_radius, float a_bo3Damage);  // Electric Cherry
	}

	namespace Intro
	{
		void Install();                 // data loaded: set MQQuickstart
		void Tick();
		bool CrewScreenOpen();
		bool OnButton(const RE::ButtonEvent* a_event);  // true = consumed
	}

	namespace Gun
	{
		void                  Tick(float a_delta);
		void                  Reset();
		bool                  OnButton(const RE::ButtonEvent* a_event);
		std::pair<bool, bool> RecentHit(RE::FormID a_actor);  // {gun kill, headshot}
		bool                  Give(int a_gun);                // buy/box: put in a slot, equip it
		bool                  Owns(int a_gun);
		void                  RefillAmmo(int a_gun);
		bool                  Reforge();      // Atronach Reforge on the weapon in hand
		int                   ReforgeCost();  // -1 no weapon, 0 maxed
		int                   SlotCount();
		float                 AdsBlend();                     // 0..1, for movement and HUD
	}

	namespace Shops
	{
		void Tick(float a_delta);
		void Reset();
		void OnActivate(RE::TESObjectREFR* a_ref);
		bool OnButton(const RE::ButtonEvent* a_event);  // brew machine menu keys (true = used)
		std::string BoxTown();
	}

	namespace Perks
	{
		void OnEffect(RE::FormID a_mgef);
		void OnZombieHit(RE::Actor* a_zombie);  // a round zombie hit the player
		void OnCast(int a_shots);  // Stormcrown
		void Tick();
		float ManaMult();          // Quicksilver: magicka cost/regen
		float FireDelayMult();
		float DamageMult();
		float HeadMult();
	}

	namespace Movement
	{
		void OnButton(const RE::ButtonEvent* a_event);  // crouch/jump keys (Skyrim ignores crouch while sprinting)
		void Tick(float a_delta);
		void Reset();
	}

	namespace Hud
	{
		struct State
		{
			bool        inGame{ false };
			int         round{ 0 };
			bool        nightActive{ false };  // a night (round) is being fought; otherwise round = nights survived
			int         points{ 0 };
			bool        gunOut{ false };
			std::string gunName;
			int         gunLevel{ 0 };
			int         clip{ 0 };
			int         reserve{ 0 };
			bool        reloading{ false };
			int         gunKind{ 0 };     // 0 wand (infinite), 1 tome, 2 grimoire (magicka)
			float       manaCost{ 0 };    // per shot
			std::string gunElement;       // wand ability / element line under the name
			std::uint32_t gunColor{ 0 };  // element colour RRGGBB
			float       ads{ 0 };
			std::string banner;
			float       bannerTime{ 0 };
			int         lastGain{ 0 };
			float       gainTime{ 0 };
			float       hitTime{ 0 };
			bool        hitKill{ false };
			int         perks{ 0 };      // bitmask
			int         crew{ -1 };
			bool        crewScreen{ false };
			int         crewHighlight{ 0 };
			float       countdown{ 0 };  // seconds until the next night starts (0 = none)
			float       hp{ 0 }, hpMax{ 0 }, mp{ 0 }, mpMax{ 0 }, sp{ 0 }, spMax{ 0 };
			float       hurtTime{ 0 };   // red vignette after taking a hit
			bool        perkMenu{ false };
			std::string bossName;  // empty = no boss
			float       bossHp{ 0 };
			bool        bossEnraged{ false };
			int         perkSel{ 0 };
		};
		State&      Edit();
		std::mutex& Lock();
		void        Banner(std::string a_text, float a_seconds = 4.0f);
		void        Install();
	}

	namespace DevTools
	{
		void PollFromRender();
		void CaptureIfRequested(ID3D11DeviceContext* a_context, IDXGISwapChain* a_swap);
		void RunQueuedOnMain();
		void RunQueuedInGame();
	}

	namespace Events
	{
		void InstallMenuWatch();
	}
}
