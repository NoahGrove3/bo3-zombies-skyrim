#include "BO3Z.h"

// New games: Bethesda's own quick start (MQQuickstart = 3 makes MQ101 stage 0 run its skip fragment:
// character creation, choose Hadvar or Ralof, start MQ102, complete MQ101). Then we land outside
// Riverwood with the starting staff (no crew picker: you play your own character).
namespace bo3z
{
	namespace
	{
		int  g_settle = 0;
		bool g_moved = false;  // this game's new character has been moved to Riverwood

		bool FreshQuickstartGame(RE::PlayerCharacter* a_player)
		{
			auto* cal = RE::Calendar::GetSingleton();
			const float days = cal && cal->gameDaysPassed ? cal->gameDaysPassed->value : 99.0f;
			return a_player->GetLevel() <= 1 && days < 2.0f;
		}

		// Survival Mode stays off in The Curse of Alduin: hunger/cold/fatigue fight the nights' own pacing, and its attribute
		// penalties (it keeps them in Variable02-04) fought the mod's old save values. When the game has it on (the
		// Gameplay setting, or a new game's prompt) we switch it off the way its own toggle does.
		void KeepSurvivalOff()
		{
			static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(60);
			if (std::chrono::steady_clock::now() - last < std::chrono::seconds(5)) {
				return;
			}
			last = std::chrono::steady_clock::now();
			auto* enabled = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ModeEnabled");
			if (!enabled || enabled->value < 0.5f) {
				return;
			}
			static auto lastStop = std::chrono::steady_clock::now() - std::chrono::seconds(60);
			if (std::chrono::steady_clock::now() - lastStop < std::chrono::seconds(30)) {
				return;  // the script is still shutting it down
			}
			lastStop = std::chrono::steady_clock::now();
			if (auto* toggle = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ModeToggle")) {
				toggle->value = 0.0f;
			}
			auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SurvivalModeMainQuest");
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!quest || !vm) {
				logger::warn("intro: survival mode is on but its quest or the script machine is missing");
				return;
			}
			const auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(quest->GetFormType(), quest);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			const bool ok = vm->DispatchMethodCall(handle, "Survival_MainScript", "StopSurvivalMode", RE::MakeFunctionArguments(), none);
			logger::info("intro: survival mode was on: switched off ({})", ok ? "its StopSurvivalMode runs" : "call failed");
		}

	}

	namespace Intro
	{
		void Install()
		{
			if (auto* q = Forms::Quickstart(); q && q->value != 3.0f) {
				q->value = 3.0f;  // read by MQ101 stage 0 when a new game starts
				logger::info("intro: MQQuickstart set to 3 (skip Helgen)");
			}
		}

		bool CrewScreenOpen() { return false; }
		bool NewGameThisSession() { return g_moved; }
		void OnMainMenu() { g_moved = false, g_settle = 0; }  // the BO3 crew picker is gone: you play your own character

		void Tick()
		{
			static int dbg = 0;
			if (dbg++ % 1800 == 0) {
				logger::info("intro: {} moved={} crew={}", Forms::IntroDebug(), Save::Flag(Save::kFlagMoved), Save::Crew());
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* intro = Forms::IntroQuest();
			auto* ui = RE::UI::GetSingleton();
			if (player && g_moved && Hud::IntroBusy() && !player->IsDead()) {  // nothing hurts a newcomer who cannot yet see
				auto* av = player->AsActorValueOwner();
				const float mx = av->GetPermanentActorValue(RE::ActorValue::kHealth), hp = av->GetActorValue(RE::ActorValue::kHealth);
				if (hp < mx) {
					av->RestoreActorValue(RE::ActorValue::kHealth, mx - hp);
				}
			}
			if (player && g_moved) {  // a new character's first minute: health and place, once a second, so a death can be traced
				static auto last = std::chrono::steady_clock::now();
				static int  n = 0;
				if (n < 90 && std::chrono::steady_clock::now() - last > std::chrono::seconds(1)) {
					last = std::chrono::steady_clock::now(), ++n;
					auto*      av = player->AsActorValueOwner();
					const auto p = player->GetPosition();
					logger::info("intro: watch hp {:.0f}/{:.0f} at {:.0f},{:.0f},{:.0f} paused={} dead={}", av->GetActorValue(RE::ActorValue::kHealth), av->GetPermanentActorValue(RE::ActorValue::kHealth),
						p.x, p.y, p.z, ui && ui->GameIsPaused(), player->IsDead());
				}
			}
			if (!player || !intro || !ui || ui->GameIsPaused()) {
				return;
			}
			KeepSurvivalOff();
			if (!Save::Flag(Save::kFlagMoved) && Forms::IntroDone()) {
				if (FreshQuickstartGame(player) && !g_moved) {  // (a new game's scripts can wipe our values: never move twice)
					if (++g_settle < 20) {  // let the skip fragment finish (race menu, choose sides) before moving
						return;
					}
					g_moved = true;
					Hud::IntroVeil(3.0f);  // (Skyrim's loading screen covers the move; the outro lifts the black once the world is there)
					if (auto* marker = Forms::RiverwoodMarker()) {
						player->MoveTo(marker);
						logger::info("intro: moved the new character to Riverwood");
					}
				}
				Save::SetFlag(Save::kFlagMoved, true);
			}
			// No crew pick any more: you play your own Skyrim character. First time in, you get the starting wand.
			if (Save::Flag(Save::kFlagMoved) && Forms::IntroDone() && !Save::Flag(Save::kFlagStarted)) {
				if (Save::Crew() >= 0) {
					Save::SetCrew(-1);  // older saves: drop the crew character
				} else {
					Arcana::GiveStarter();  // the Coffer's spells replaced the old ones: a new character starts with one of them
					Gun::TestKitIfFlagged();  // (Noah's test characters: CurseOfAlduin-testkit.flag beside the log)
				}
				Save::SetFlag(Save::kFlagStarted, true);
				logger::info("intro: started (no crew screen)");
			}
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.crewScreen = false;
			h.crew = -1;
		}

		// while the intro film plays every key is its own: Space / Enter / E / pad A held skips it
		bool OnButton(const RE::ButtonEvent* a_b)
		{
			if (!a_b || !Hud::IntroActive()) {
				return false;
			}
			const auto dev = a_b->GetDevice();
			const auto id = a_b->GetIDCode();
			const bool skipKey = (dev == RE::INPUT_DEVICE::kKeyboard && (id == 57 || id == 28 || id == 18)) || (dev == RE::INPUT_DEVICE::kGamepad && id == 0x1000);
			if (skipKey) {
				Hud::IntroSkipHeld(a_b->IsPressed());
			}
			return true;
		}
	}
}
