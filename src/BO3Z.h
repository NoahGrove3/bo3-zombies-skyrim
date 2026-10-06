#pragma once
// BO3 Zombies x Skyrim: step 1 systems. Every number comes from Generated.h (built from sheets/*.json).
#include "Generated.h"

namespace bo3z
{
	inline constexpr auto kVersion = "0.1.0";

	// Forms read from the player's own Skyrim (sheets: enemies, sounds, quests).
	namespace Forms
	{
		bool                     Resolve();  // after data loaded; false if a required form is missing
		RE::TESBoundObject*      Enemy(std::size_t a_index);
		RE::TESQuest*            IntroQuest();
		void                     Play(gen::Sound a_sound);
	}

	// State kept on the player (sheet: persistence).
	namespace Save
	{
		int  Get(int a_av);
		void Set(int a_av, int a_value);
		void OnWorldReady();  // after every load / new game: gives the start kit once
	}

	namespace Points
	{
		void Add(int a_points);
		int  Get();
	}

	namespace Director
	{
		void Tick(float a_delta);
		void Reset();                         // load game
		bool IsRoundUndead(RE::Actor* a_actor);
		std::string_view HeadNodeFor(RE::Actor* a_actor);
	}

	namespace Gun
	{
		void Tick(float a_delta);
		void Reset();
		bool OnButton(const RE::ButtonEvent* a_event);
		// Last gun hit on this actor, for the death event: {recent, headshot}
		std::pair<bool, bool> RecentHit(RE::FormID a_actor);
	}

	namespace Hud
	{
		struct State
		{
			bool        inGame{ false };
			int         round{ 0 };
			int         points{ 0 };
			bool        gunOut{ false };
			int         clip{ 0 };
			int         reserve{ 0 };
			bool        reloading{ false };
			std::string banner;
			float       bannerTime{ 0 };
			int         lastGain{ 0 };
			float       gainTime{ 0 };
			float       hitTime{ 0 };
			bool        hitKill{ false };
		};
		State& Edit();           // main thread, under Lock()
		std::mutex& Lock();
		void Banner(std::string a_text, float a_seconds = 4.0f);
		void Install();          // Present hook
	}

	namespace Events
	{
		void InstallMenuWatch();  // from the boot thread once UI exists
	}
}
