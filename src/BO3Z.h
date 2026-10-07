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
		inline constexpr int kFlagInit = 0, kFlagMoved = 11, kPerkShift = 14, kFlagPerksV2 = 23;  // bits: init | crew 1-3 | (old perks 4-10) | moved 11 | slot 12-13 | perks 14-22 | v2 23
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
		std::string_view HeadNodeFor(RE::Actor* a_actor);
		void             LogState();
		void             Web(const RE::NiPoint3& a_at, float a_radius, float a_seconds);   // Widow's Wine
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
		int                   SlotCount();
		float                 AdsBlend();                     // 0..1, for movement and HUD
	}

	namespace Shops
	{
		void Tick(float a_delta);
		void Reset();
		void OnActivate(RE::TESObjectREFR* a_ref);
		std::string BoxTown();
	}

	namespace Perks
	{
		void OnEffect(RE::FormID a_mgef);
		void OnZombieHit(RE::Actor* a_zombie);  // a round zombie hit the player
		void OnReload(float a_emptyFraction);
		void Tick();
		float ReloadMult();
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
			int         points{ 0 };
			bool        gunOut{ false };
			std::string gunName;
			int         clip{ 0 };
			int         reserve{ 0 };
			bool        reloading{ false };
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
