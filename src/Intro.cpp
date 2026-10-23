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
		bool  g_skip = false;        // Helgen skipped this game: once it is done, the move to Riverwood's gate
		bool  g_skipRace = false;    // ... waiting for the character creator to close first
		bool  reachedHelgen = false;  // the cart has rolled into Helgen: no more skip
		bool  g_raceSeen = false;    // the character creator has been and gone this game
		int   g_raceAfterSkip = 0;   // (Noah: skip first, then the creator at Riverwood) 1 wanted, 2 shown
		float g_skipHold = 0;        // H held this long
		bool  g_skipHeld = false;
		void Papyrus(RE::TESForm* a_form, const char* a_class, const char* a_fn, RE::BSScript::IFunctionArguments* a_args)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || !a_form) return;
			const auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(a_form->GetFormType(), a_form);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			vm->DispatchMethodCall(handle, a_class, a_fn, a_args, none);
		}
		void Stage(RE::TESQuest* a_q, int a_stage)
		{
			if (!a_q) return;
			Papyrus(a_q, "Quest", "SetCurrentStageID", RE::MakeFunctionArguments(static_cast<std::int32_t>(a_stage)));  // (the VM owns the arguments)
		}
		void DoSkip()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			const auto mq = Forms::MQ102();  // MQ102A (Hadvar), MQ102B (Ralof), MQ102
			Stage(mq[2], 200);
			Stage(mq[0], 200);
			Stage(Forms::IntroQuest(), 1000);
			g_skip = true;  // Tick moves you once the quests are done
			if (p) {
				auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
				if (vm) {
					vm->DispatchStaticCall("Game", "EnablePlayerControls", RE::MakeFunctionArguments(true, true, true, true, true, true, true, true, static_cast<std::int32_t>(0)), none);
				}
				p->NotifyAnimationGraph("IdleForceDefaultState");  // (the bound hands, the cart's seat: let go)
			}
			Hud::Banner("Helgen skipped - you wake at Riverwood's gate", 4.0f);
			logger::info("intro: Helgen skipped (MQ102 200, MQ102A 200, MQ101 1000)");
		}

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
		// (Noah: the real Helgen again, with a skip) MQQuickstart 0: a new game opens in the cart. All through Helgen a
		// hold of H (Back on a pad) skips it: the character creator first if it hasn't come yet, then Helgen and Before
		// the Storm are set done and you wake at Riverwood's gate
		void Install()
		{
			if (auto* q = Forms::Quickstart(); q && q->value != 0.0f) {
				q->value = 0.0f;  // read by MQ101 stage 0 when a new game starts
				logger::info("intro: MQQuickstart set to 0 (Helgen plays; H skips it)");
			}
		}

		bool CrewScreenOpen() { return false; }
		bool NewGameThisSession() { return g_moved; }
		void OnMainMenu() { g_moved = false, g_settle = 0, g_skip = false, g_skipRace = false, g_raceSeen = false, reachedHelgen = false, g_skipHold = 0, g_skipHeld = false, g_raceAfterSkip = 0; }  // the BO3 crew picker is gone: you play your own character

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
			if (!player || !intro || !ui) {
				return;
			}
			// (Noah: "Ulfric gets stuck saying the same sentence after I've already looted Gunjar with QuickLoot") Unbound's
			// "Loot Gunjar's body" moves on only when his body is opened: QuickLoot takes the gear without opening it, so
			// once his body holds no more armour or weapons it is opened for you (and the empty window shut at once)
			{
				static int  look = 0;
				static bool opened = false;
				static int  shut = 0;
				if (shut > 0 && --shut == 0) {
					if (auto* q = RE::UIMessageQueue::GetSingleton(); q && ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) q->AddMessage(RE::ContainerMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
				if (!intro->IsRunning() || intro->GetCurrentStageID() >= 900 || intro->GetCurrentStageID() < 250) {
					opened = false;
				} else if (!opened && ++look % 30 == 0 && !ui->GameIsPaused()) {
					RE::TESObjectREFR* gunjar = nullptr;
					RE::TES::GetSingleton()->ForEachReferenceInRange(player, 1500.0f, [&](RE::TESObjectREFR* r) {
						auto* a = r ? r->As<RE::Actor>() : nullptr;
						if (a && a->IsDead() && a->GetName() && std::string_view(a->GetName()) == "Gunjar") {
							gunjar = a;
							return RE::BSContainer::ForEachResult::kStop;
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
					if (gunjar) {
						int gear = 0;
						for (auto& [obj, data] : gunjar->GetInventory()) {
							if (data.first > 0 && obj && (obj->IsArmor() || obj->IsWeapon())) gear += data.first;
						}
						if (gear == 0) {
							opened = true;
							gunjar->ActivateRef(player, 0, nullptr, 0, false);
							shut = 2;
							logger::info("intro: Gunjar's body looted with QuickLoot - opened for Unbound (stage {})", intro->GetCurrentStageID());
						}
					}
				}
			}
			// the skip: the creator first if it hasn't been, then Helgen set done
			const bool raceOpen = ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
			// (Noah: when the class and its kit come) Helgen skipped: right after the creator - the skip moves you to Riverwood's
			// gate and the picker opens as you arrive (the start step below); played through: as you walk out of the cave
			// into Skyrim (MQ101 done, Before the Storm begun: the same start step)
			if (raceOpen) g_raceSeen = true;
			if (g_skipRace && g_raceSeen && !raceOpen) {
				g_skipRace = false;
				DoSkip();
			}
			// (Noah) the skip is offered on the cart ride only: it ends for good once the cart rolls into Helgen
			if (!reachedHelgen) {
				for (auto* loc = player->GetCurrentLocation(); loc; loc = loc->parentLoc) {
					if (const char* n = loc->GetFullName(); n && std::string_view(n) == "Helgen") {
						reachedHelgen = true;
						logger::info("intro: the cart is in Helgen - the skip is over");
						break;
					}
					if (loc == loc->parentLoc) break;
				}
				if (g_raceSeen) reachedHelgen = true;
			}
			const bool canSkip = !reachedHelgen && !Save::Flag(Save::kFlagMoved) && !Forms::IntroDone() && intro->IsRunning() && !g_skip && !g_skipRace;
			{  // (Noah: the skip never showed) a standing line at the foot of the screen while Helgen can be skipped - and the hold's progress
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().skipHint = canSkip && !raceOpen && !ui->GameIsPaused() && !Hud::IntroActive();
				Hud::Edit().skipFrac = canSkip ? std::clamp(g_skipHold / 1.5f, 0.0f, 1.0f) : 0.0f;
			}
			if (static int said = 0; said++ % 600 == 0) {
				logger::info("intro: skip {} (moved {} done {} running {} skip {} race {})", canSkip, Save::Flag(Save::kFlagMoved), Forms::IntroDone(), intro->IsRunning(), g_skip, g_skipRace);
			}
			if (canSkip) {
				if (g_skipHeld && !raceOpen && !ui->GameIsPaused()) {
					g_skipHold += 1.0f / 60.0f;
					if (g_skipHold > 1.5f) {
						g_skipHeld = false, g_skipHold = 0;
						// (Noah) skipped: the loading screen, Riverwood's gate, and only then the creator (if it hasn't been yet),
						// then the class and its kit
						g_raceAfterSkip = g_raceSeen ? 0 : 1;
						logger::info("intro: Helgen skipped{}", g_raceAfterSkip ? " - the creator opens at Riverwood" : "");
						DoSkip();
					}
				} else {
					g_skipHold = 0;
				}
			}
			if (ui->GameIsPaused()) {
				return;
			}
			KeepSurvivalOff();
			// (Noah: "my game crashed after dying in the Helgen cave") Helgen can't kill you: until you are out of it you fall
			// to your knees instead and rise with your health back (Skyrim's own reload after a death there crashed)
			{
				static bool held = false;
				const bool inHelgen = !Save::Flag(Save::kFlagMoved) && intro->IsRunning();
				auto& flags = player->GetActorRuntimeData().boolFlags;
				if (inHelgen && !held) {
					held = true;
					flags.set(RE::Actor::BOOL_FLAGS::kEssential);
				} else if (!inHelgen && held) {
					held = false;
					if (!Net::InLobby() && !Coop::Client()) flags.reset(RE::Actor::BOOL_FLAGS::kEssential);
				}
				if (held && player->AsActorState()->IsBleedingOut()) {
					static float down = 0;
					if ((down += 1.0f / 60.0f) > 3.0f) {
						down = 0;
						auto* av = player->AsActorValueOwner();
						av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth));
						player->AsActorState()->actorState1.lifeState = RE::ACTOR_LIFE_STATE::kAlive;
						player->NotifyAnimationGraph("BleedOutStop");
						player->NotifyAnimationGraph("IdleForceDefaultState");
						logger::info("intro: back on your feet in Helgen");
					}
				}
			}
			// (Noah: "it should stay daytime until the player leaves the cave from Helgen") until the class is chosen - out of
			// the cave, or at Riverwood after a skip - the clock holds at day
			if (!Save::Flag(Save::kFlagStarted)) {
				if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->gameHour && (cal->gameHour->value < 8.0f || cal->gameHour->value > 16.0f)) {
					cal->gameHour->value = 11.0f;
				}
			}
			if (!Save::Flag(Save::kFlagMoved) && Forms::IntroDone()) {
				if (g_skip && FreshQuickstartGame(player) && !g_moved) {  // (played through: you walk out of Helgen yourself; skipped: Riverwood's gate)
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
			if (Save::Flag(Save::kFlagMoved) && Forms::IntroDone() && !Save::Flag(Save::kFlagStarted) && g_raceAfterSkip) {
				// the creator at Riverwood's gate, once the world is there (the move's loading screen gone); the class waits for it to close
				if (g_raceAfterSkip == 1 && player->Is3DLoaded() && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) && !Hud::IntroBusy()) {
					static int settle = 0;
					if (++settle > 60) {
						settle = 0, g_raceAfterSkip = 2;
						auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
						RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
						if (vm) vm->DispatchStaticCall("Game", "ShowRaceMenu", RE::MakeFunctionArguments(), none);
						logger::info("intro: at Riverwood - the creator");
					}
					return;
				}
				if (g_raceAfterSkip == 2 && (ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) || !g_raceSeen)) return;
				g_raceAfterSkip = 0;  // (made: on to the class)
			}
			// (Noah: "the class pop-up came at the end of the cave, not when I left it") only out under the sky
			const bool outside = player->GetParentCell() && !player->GetParentCell()->IsInteriorCell() && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
			if (Save::Flag(Save::kFlagMoved) && Forms::IntroDone() && !Save::Flag(Save::kFlagStarted) && outside) {
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
			if (a_b && !Save::Flag(Save::kFlagMoved) && !g_skip && ((a_b->GetDevice() == RE::INPUT_DEVICE::kKeyboard && a_b->GetIDCode() == 0x23) ||
				(a_b->GetDevice() == RE::INPUT_DEVICE::kGamepad && a_b->GetIDCode() == 0x1000))) {
				// (H / pad A held: Tick counts it; Back was Skyrim's Wait) - a fresh press, never the A still held from skipping the film
				if (!a_b->IsPressed()) g_skipHeld = false;
				else if (a_b->IsDown() && !Hud::IntroActive()) g_skipHeld = true;
			}
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
