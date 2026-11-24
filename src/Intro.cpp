#include "CurseOfAlduin.h"

// New games: Bethesda's own quick start (MQQuickstart = 3 makes MQ101 stage 0 run its skip fragment:
// character creation, choose Hadvar or Ralof, start MQ102, complete MQ101). Then we land outside
// Riverwood with the starting staff (no crew picker: you play your own character).
namespace coa
{
	namespace
	{
		int  g_settle = 0;
		bool g_moved = false;  // this game's new character has been moved to Riverwood
		float g_emptyHandsFor = 0;  // seconds left telling the hands they hold nothing (the quick start's axe taken away)
		bool  g_skip = false;        // Helgen skipped this game: once it is done, the move to Riverwood's gate
		bool  g_skipRace = false;    // ... waiting for the character creator to close first
		bool  reachedHelgen = false;
		bool  g_freshGame = false;  // (Noah: a new game made after loading another character stayed in Helgen's cave) Helgen seen unplayed since the main menu: a new game, whatever its level or clock say
		bool  g_creatorDeferred = false;  // the quick start's own creator closed, to open at Riverwood  // the cart has rolled into Helgen: no more skip
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
		// Before the Storm's first two objectives (talk to the friend in Riverwood, travel there with them) never belong to a
		// survivor who wakes at the gate: taken off the journal
		void HideFriendObjectives(RE::TESQuest* a_q)
		{
			if (!a_q) return;
			for (int o : { 10, 20 }) {
				Papyrus(a_q, "Quest", "SetObjectiveDisplayed", RE::MakeFunctionArguments(static_cast<std::int32_t>(o), false, false));
			}
		}
		void DoSkip()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			// (Noah: "my quests from Helgen aren't up to date") the main quest where a Helgen survivor at Riverwood would have
			// it: Unbound completed (its stage 900 completes it, with its journal entry), Before the Storm on "Talk to the
			// Jarl of Whiterun" (stage 30) - the Jarl then moves it on and gives Bleak Falls Barrow as Skyrim does.
			// (Before: Before the Storm was set to its last stage 200, which neither completes it nor ever starts Bleak Falls)
			// (start just before speaking to the Jarl: the friend's helper quest at 10, then Before the Storm at 30 - ApplySide)
			Stage(Forms::IntroQuest(), 900);
			Stage(Forms::IntroQuest(), 1000);
			// (Noah, 2026-11-23: "choose stormcloak or the other option") Before the Storm's friend - Ralof's helper quest or
			// Hadvar's - waits for the setup's choice after the creator (ApplySide)
			g_skip = true;  // Tick moves you once the quests are done
			if (p) {
				auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
				if (vm) {
					vm->DispatchStaticCall("Game", "EnablePlayerControls", RE::MakeFunctionArguments(true, true, true, true, true, true, true, true, static_cast<std::int32_t>(0)), none);
				}
				p->NotifyAnimationGraph("IdleForceDefaultState");  // (the bound hands, the cart's seat: let go)
			}
			logger::info("intro: Helgen skipped (MQ101 900/1000; Before the Storm waits for the side chosen)");   // (no banner: the new game's loading screen covers all of this)
		}

		bool FreshQuickstartGame(RE::PlayerCharacter* a_player)
		{
			auto* cal = RE::Calendar::GetSingleton();
			const float days = cal && cal->gameDaysPassed ? cal->gameDaysPassed->value : 99.0f;
			return g_freshGame || (a_player->GetLevel() <= 1 && days < 2.0f);
		}

		// (Noah, 2026-11-22: "my character for male and female have an axe at their hip, and when I create a character I see
		// Axe Received, but it's not in my inventory") the quick start hands a new character Skyrim's Iron War Axe as Helgen is
		// skipped - and it hung at the hip through the creator until the pack was emptied at the start. Until the game has
		// started, any weapon a new character holds or carries (not a quest's) is taken out of the hands and the pack at once,
		// and the body made again so nothing stays sheathed at the hip
		void StripStartWeapons(RE::PlayerCharacter* a_p)
		{
			static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(5);
			if (std::chrono::steady_clock::now() - last < std::chrono::milliseconds(500)) return;
			last = std::chrono::steady_clock::now();
			auto* em = RE::ActorEquipManager::GetSingleton();
			bool  held = false;
			int   gone = 0;
			for (bool left : { false, true }) {
				if (auto* eq = a_p->GetEquippedObject(left); eq && em && (eq->Is(RE::FormType::Weapon) || eq->Is(RE::FormType::Ammo))) {
					if (a_p->AsActorState()->IsWeaponDrawn()) a_p->DrawWeaponMagicHands(false);
					em->UnequipObject(a_p, eq->As<RE::TESBoundObject>(), nullptr, 1, nullptr, false, true, false, true);
					held = true;
				}
			}
			for (auto& [obj, data] : a_p->GetInventory()) {
				if (!obj || data.first <= 0 || !(obj->Is(RE::FormType::Weapon) || obj->Is(RE::FormType::Ammo))) continue;
				if (data.second && data.second->IsQuestObject()) continue;
				a_p->RemoveItem(obj, data.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				gone += data.first;
			}
			if (held || gone) {
				if (a_p->Is3DLoaded()) a_p->DoReset3D(false);
				logger::info("intro: the start's weapons taken ({} held, {} carried) - nothing at the hip", held ? "one" : "none", gone);
			}
		}

		// (Noah, 2026-11-23: "the option to choose [Stormcloak or Imperial] and survival mode yes or no after Helgen when a new
		// player loads in") after the creator: who brought you out of Helgen, then Skyrim's Survival Mode - chosen once
		std::atomic<bool> g_setupOpen{ false };
		std::atomic<int>  g_setupStep{ 0 }, g_setupSel{ 0 };
		std::chrono::steady_clock::time_point g_setupClosed{};
		void ApplySide(bool a_imperial)
		{
			const auto mq = Forms::MQ102();   // MQ102A (Hadvar's), MQ102B (Ralof's), MQ102 Before the Storm
			// (Skyrim's own new-game question - "Who did you befriend while escaping from Helgen?" - is answered Ralof before you
			// are made (Hud's AnswerStartPrompts: MQ101's fragment sets MQ102B 1 and Ralof outside the keep); Hadvar chosen here,
			// Ralof's helper quest is stopped and Hadvar's run the way that answer would have: 1, then on to Riverwood)
			if (a_imperial) {
				Papyrus(mq[1], "Quest", "Stop", RE::MakeFunctionArguments());
				Stage(mq[0], 1);
			}
			Stage(mq[a_imperial ? 0 : 1], 10);
			Stage(mq[2], 30);   // "Talk to the Jarl of Whiterun" - its journal names Alvor or Gerdur after the helper quest
			HideFriendObjectives(mq[2]);
			logger::info("intro: Before the Storm with {} ({} 10, MQ102 30)", a_imperial ? "Hadvar - the Imperial Legion" : "Ralof - the Stormcloaks", a_imperial ? "MQ102A" : "MQ102B");
		}
		void StartSurvival()
		{
			if (auto* toggle = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ModeToggle")) toggle->value = 1.0f;
			auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SurvivalModeMainQuest");
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!quest || !vm) {
				logger::warn("intro: survival mode chosen, but its quest or the script machine is missing");
				return;
			}
			const auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(quest->GetFormType(), quest);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			const bool ok = vm->DispatchMethodCall(handle, "Survival_MainScript", "StartSurvivalMode", RE::MakeFunctionArguments(), none);
			logger::info("intro: survival mode chosen: switched on ({})", ok ? "its StartSurvivalMode runs" : "call failed");
		}
		void SetupChoose(int a_sel)
		{
			if (!g_setupOpen) return;
			if (g_setupStep == 0) {
				const bool imperial = a_sel == 1;
				Save::SetSetup((Save::Setup() & ~Save::kSetupImperial) | (imperial ? Save::kSetupImperial : 0));
				ApplySide(imperial);
				g_setupStep = 1, g_setupSel = 0;
				return;
			}
			const bool survival = a_sel == 0;
			Save::SetSetup(Save::Setup() | Save::kSetupDone | (survival ? Save::kSetupSurvival : 0));
			if (survival) StartSurvival();
			g_setupOpen = false;
			g_setupClosed = std::chrono::steady_clock::now();
			logger::info("intro: setup done - {}, survival {}", (Save::Setup() & Save::kSetupImperial) ? "Imperial" : "Stormcloak", survival ? "on" : "off");
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
			if (Save::Setup() & Save::kSetupSurvival) return;   // (Noah, 2026-11-23: chosen for this character - Skyrim's own, as it is)
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
		// (Noah: "the skip never moved me - remove the Helgen part for now and start every player at Riverwood's gate like
		// before") Bethesda's own quick start again: the creator, then Riverwood's gate, then the class. Helgen and its skip
		// come back with this set true
		constexpr bool kPlayHelgen = false;
		void Install()
		{
			const float want = kPlayHelgen ? 0.0f : 3.0f;
			if (auto* q = Forms::Quickstart(); q && q->value != want) {
				q->value = want;  // read by MQ101 stage 0 when a new game starts
				logger::info("intro: MQQuickstart set to {} ({})", want, kPlayHelgen ? "Helgen plays; H skips it" : "no Helgen: the creator, then Riverwood's gate");
			}
		}

		// (Noah: "the race menu still opens a second time") the game's Tick never sees the creator - it runs paused - so the
		// menu's own open/close tells it: a creator Skyrim opens at Riverwood's gate is ours (none follows); one opened in
		// Helgen before the move is closed (it opens at the gate)
		void OnRaceMenu(bool a_opening)
		{
			if (!a_opening) {
				return;
			}
			if (g_raceAfterSkip == 1 && Save::Flag(Save::kFlagMoved)) {
				g_raceAfterSkip = 2;
				logger::info("intro: Skyrim opened the creator at Riverwood itself - ours won't follow");
			}
			g_raceSeen = true;
			if (!Save::Flag(Save::kFlagMoved) && !Save::Flag(Save::kFlagStarted)) g_freshGame = true;  // (a creator before the move: a new game's character)
			if (!kPlayHelgen && !Save::Flag(Save::kFlagMoved)) {  // (Noah: the creator opened in Helgen's cave with a broken face, before the move: Helgen is already marked done by then)
				g_raceSeen = false;
				if (g_raceAfterSkip == 0) g_raceAfterSkip = 1;
				if (auto* q = RE::UIMessageQueue::GetSingleton()) q->AddMessage(RE::RaceSexMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				logger::info("intro: a creator in Helgen closed - it opens at Riverwood's gate");
			}
		}
		bool NewGameThisSession() { return g_moved; }
		bool SetupOpen() { return g_setupOpen.load(); }
		int  SetupStep() { return g_setupStep.load(); }
		int  SetupSel() { return g_setupSel.load(); }
		std::atomic<bool> g_setupOver{ false };   // (the mouse is on a card: a click chooses it)
		void SetupHover(int a_i, bool a_moved)
		{
			g_setupOver = a_i >= 0 && a_i < 2;
			if (g_setupOpen && a_moved && g_setupOver) g_setupSel = a_i;
		}
		bool SetupEats(RE::InputEvent* a_e)
		{
			if (!a_e || !a_e->AsButtonEvent()) return false;
			return g_setupOpen || std::chrono::steady_clock::now() - g_setupClosed < std::chrono::milliseconds(350);
		}
		bool SetupButton(const RE::ButtonEvent* a_b)
		{
			if (!g_setupOpen || !a_b) return false;
			if (!a_b->IsDown()) return true;
			const auto code = a_b->GetIDCode();
			const int  sel = g_setupSel;
			if (a_b->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
				if (code == 0x02 || code == 0x03) { g_setupSel = static_cast<int>(code) - 0x02; SetupChoose(g_setupSel); return true; }  // 1-2
				if (code == 0xCB || code == 0x1E || code == 0xCD || code == 0x20) g_setupSel = 1 - sel;   // Left / A, Right / D
				else if (code == 0x1C || code == 0x9C || code == 0x12) SetupChoose(sel);   // Enter / E
				return true;
			}
			if (a_b->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
				if (code == 0x0004 || code == 0x0008) g_setupSel = 1 - sel;   // D-pad left / right
				else if (code == 0x1000) SetupChoose(sel);   // A
				return true;
			}
			if (a_b->GetDevice() == RE::INPUT_DEVICE::kMouse && code == 0) {
				if (g_setupOver) SetupChoose(sel);
				return true;
			}
			return true;
		}
		bool CreatorAtRiverwood()
		{
			auto* ui = RE::UI::GetSingleton();
			return g_raceAfterSkip == 2 && ui && ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
		}
		bool FreshGame() { auto* p = RE::PlayerCharacter::GetSingleton(); return p && FreshQuickstartGame(p); }
		void OnMainMenu() { g_freshGame = false, g_moved = false, g_settle = 0, g_skip = false, g_skipRace = false, g_raceSeen = false, reachedHelgen = false, g_creatorDeferred = false, g_skipHold = 0, g_skipHeld = false, g_raceAfterSkip = 0, g_setupOpen = false; }  // the BO3 crew picker is gone: you play your own character

		void Tick()
		{
			static int dbg = 0;
			if (dbg++ % 1800 == 0) {
				logger::info("intro: {} moved={}", Forms::IntroDebug(), Save::Flag(Save::kFlagMoved));
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* intro = Forms::IntroQuest();
			auto* ui = RE::UI::GetSingleton();
			static auto movedAt = std::chrono::steady_clock::now();
			if (!g_moved) movedAt = std::chrono::steady_clock::now();
			if (player && !Save::Flag(Save::kFlagStarted) && Classes::Get() < 0 && FreshQuickstartGame(player)) StripStartWeapons(player);
			// (the axe that wasn't there) while the hands hold nothing, the animation graph is told so: no weapon type in
			// either hand, so the first-person arms stand empty instead of gripping an axe that's gone
			if (player && g_emptyHandsFor > 0) {
				static auto last = std::chrono::steady_clock::now();
				const auto  nowT = std::chrono::steady_clock::now();
				g_emptyHandsFor -= std::chrono::duration<float>(nowT - last).count();
				last = nowT;
				if (!player->GetEquippedObject(false) && !player->GetEquippedObject(true) && !player->AsActorState()->IsWeaponDrawn()) {
					player->SetGraphVariableInt("iRightHandType", 0);
					player->SetGraphVariableInt("iLeftHandType", 0);
					player->SetGraphVariableInt("iRightHandEquipped", 0);
					player->SetGraphVariableInt("iLeftHandEquipped", 0);
				}
			}
			// (Noah: "I still don't take damage from the Curse's enemies") the newcomer's shield ends after half a minute, whatever
			// the black screen's state says
			if (player && g_moved && Hud::IntroBusy() && !player->IsDead() && std::chrono::steady_clock::now() - movedAt < std::chrono::seconds(30)) {  // nothing hurts a newcomer who cannot yet see
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
			// (Noah: "the character creation is at the end of the cave - I should spawn at Riverwood's gates to create my
			// character") the quick start's creator, opened where Helgen ends, is closed at once; it opens again at the gate
			// (Noah: "the race menu opened again after I created my character") a creator Skyrim opens itself once we're at
			// Riverwood's gate (the quick start's own) is the one we were about to open: it counts, and ours never follows
			if (raceOpen && g_raceAfterSkip == 1 && Save::Flag(Save::kFlagMoved)) {
				g_raceAfterSkip = 2;
				logger::info("intro: Skyrim opened the creator at Riverwood itself - ours won't follow");
			}
			if (!kPlayHelgen && raceOpen && !Save::Flag(Save::kFlagMoved)) {
				g_creatorDeferred = true;
				g_raceSeen = false;
				g_raceAfterSkip = 1;
				Hud::IntroVeil(4.0f);
				if (auto* q = RE::UIMessageQueue::GetSingleton()) q->AddMessage(RE::RaceSexMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				logger::info("intro: the quick start's creator closed - it opens at Riverwood's gate");
			}
			if (g_skipRace && g_raceSeen && !raceOpen) {
				g_skipRace = false;
				DoSkip();
			}
			if (!g_freshGame && !Forms::IntroDone() && intro->IsRunning() && intro->GetCurrentStageID() < 1000 && !Save::Flag(Save::kFlagMoved)) {
				g_freshGame = true;
				logger::info("intro: a new game (Helgen unplayed) - its character goes to Riverwood");
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
			const bool canSkip = kPlayHelgen && !reachedHelgen && !Save::Flag(Save::kFlagMoved) && !Forms::IntroDone() && intro->IsRunning() && !g_skip && !g_skipRace;
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
			// (Noah: "the spawn and character creation changes weren't made" - a new game opened on the cart: Bethesda's quick
			// start didn't take) no Helgen: a new game found on the cart (or anywhere in Helgen) is skipped at once, under the
			// black - Helgen and Before the Storm set done, the move to Riverwood's gate, and the creator there
			if (!kPlayHelgen && !Save::Flag(Save::kFlagMoved) && !Forms::IntroDone() && intro->IsRunning() && !g_skip && !g_skipRace && !raceOpen &&
				FreshQuickstartGame(player) && player->Is3DLoaded() && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
				static int wait = 0;
				Hud::IntroVeil(3.0f, wait == 0);  // (black at once: the cart never shows)
				if (++wait > 90) {  // (a moment for the new game's scripts to settle)
					wait = 0;
					g_raceAfterSkip = g_raceSeen ? 0 : 1;
					logger::info("intro: a new game began in Helgen (stage {}) - skipped: Riverwood's gate, then the creator", intro->GetCurrentStageID());
					g_settle = 0;  // (the move counts its wait from here)
					DoSkip();
				}
			}
			// (a character made with the old skip: Before the Storm stuck on its last stage, Unbound never completed) put right once
			if (static bool fixed = false; !fixed && Save::Flag(Save::kFlagMoved) && player->Is3DLoaded() && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
				fixed = true;
				auto* bts = Forms::MQ102()[2];
				auto* bfb = RE::TESForm::LookupByID<RE::TESQuest>(0x000D0800);  // MQ103 Bleak Falls Barrow
				if (bts && bfb && !bts->IsCompleted() && bts->GetCurrentStageID() >= 200 && !bfb->IsRunning() && !bfb->IsCompleted()) {
					Papyrus(bts, "Quest", "Reset", RE::MakeFunctionArguments());
					Stage(Forms::MQ102()[1], 10);
					Stage(bts, 30);
					HideFriendObjectives(bts);
					if (!intro->IsCompleted()) Stage(intro, 900);
					logger::info("intro: the old skip's quests put right - Unbound completed, Before the Storm on 'Talk to the Jarl of Whiterun'");
				}
			}
			// (Noah: "Before the Storm gets stuck - even when I talk to Gerdur it doesn't check it off") a survivor who woke at
			// Riverwood's gate never had Ralof bring them to Gerdur, so her own lines never close that objective: while Before
			// the Storm waits on the Jarl (stage 30 on), Riverwood's two objectives stand done and the Jarl's is the one shown
			if (static float bts = 0; (bts += 1.0f / 60.0f) > 5.0f && Save::Flag(Save::kFlagMoved)) {
				bts = 0;
				if (auto* q = Forms::MQ102()[2]; q && q->IsRunning() && !q->IsCompleted() && q->GetCurrentStageID() >= 30 && q->GetCurrentStageID() < 200) {
					int fixedN = 0;
					for (auto* o : q->objectives) {
						if (!o) continue;
						if ((o->index == 10 || o->index == 20) && o->state.get() != RE::QUEST_OBJECTIVE_STATE::kCompletedDisplayed && o->state.get() != RE::QUEST_OBJECTIVE_STATE::kDormant) {
							o->state = RE::QUEST_OBJECTIVE_STATE::kCompletedDisplayed, ++fixedN;
						}
						if (o->index == 30 && o->state.get() == RE::QUEST_OBJECTIVE_STATE::kDormant) o->state = RE::QUEST_OBJECTIVE_STATE::kDisplayed, ++fixedN;
					}
					if (fixedN) logger::info("intro: Before the Storm - Riverwood's objectives done, the Jarl's shown ({} set)", fixedN);
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
				if ((g_skip || !kPlayHelgen) && FreshQuickstartGame(player) && !g_moved) {  // (played through: you walk out of Helgen yourself; skipped: Riverwood's gate)
					// (Noah: skipped on the cart, the cart flew and Helgen's soldiers fought, the move never happened) off the
					// cart's seat first - a seated player isn't moved - then to Riverwood, and checked: tried again until there
					if (player->GetOccupiedFurniture()) {
						player->StopInteractingQuick(true);
						player->NotifyAnimationGraph("IdleForceDefaultState");
					}
					// (Noah: "it crashed when starting a new character", twice - in Skyrim's own water building as the move
					// began, 1.8 s into the new game) the world is let settle first: nothing loading, you in it, then a second more
					if (auto* ui0 = RE::UI::GetSingleton(); ui0 && ui0->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
						return;
					}
					if (!player->Is3DLoaded() || !player->GetParentCell() || !player->GetParentCell()->IsAttached()) {
						return;
					}
					if (++g_settle < 330 || player->GetOccupiedFurniture()) {  // (5.5 s: the water of Riverwood is built from a template Skyrim loads in the background)
						return;
					}
					auto* marker = Forms::RiverwoodMarker();
					static int tries = 0;
					if (marker && player->GetPosition().GetDistance(marker->GetPosition()) > 3000.0f) {
						if ((g_settle - 330) % 120 == 0 && tries < 8) {  // (every 2 s, the move's loading screen in between)
							++tries;
							Hud::IntroVeil(3.0f);  // (Skyrim's loading screen covers the move; the outro lifts the black once the world is there)
							player->MoveTo(marker);
							logger::info("intro: moving the new character to Riverwood (try {})", tries);
						}
						if (tries < 8) return;
					}
					g_moved = true, tries = 0;
					logger::info("intro: the new character is at Riverwood");
				}
				Save::SetFlag(Save::kFlagMoved, true);
			}
			// No crew pick any more: you play your own Skyrim character. First time in, you get the starting wand.
			if (Save::Flag(Save::kFlagMoved) && Forms::IntroDone() && !Save::Flag(Save::kFlagStarted) && g_raceAfterSkip) {
				// the creator at Riverwood's gate, once the world is there (the move's loading screen gone); the class waits for it to close
				if (g_raceAfterSkip == 1 && !(player->Is3DLoaded() && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) && !Hud::IntroBusy())) {
					return;  // (Noah: the class came first, with no creator) still loading in: the creator waits, and the class after it
				}
				if (g_raceAfterSkip == 1) {
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
				if (g_raceAfterSkip == 2 && (ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) || !g_raceSeen) && Classes::Get() < 0) return;
				g_raceAfterSkip = 0;  // (made: on to the class)
			}
			// the setup: once the creator at Riverwood is done and the screen is clear
			if (!g_setupOpen && !(Save::Setup() & Save::kSetupDone) && (g_raceSeen || !g_moved) && g_raceAfterSkip == 0 && Save::Flag(Save::kFlagMoved) && !Save::Flag(Save::kFlagStarted) &&
				FreshQuickstartGame(player) && player->Is3DLoaded() && !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) && !Hud::IntroBusy()) {
				g_setupOpen = true, g_setupStep = 0, g_setupSel = 0;
				logger::info("intro: the new character's setup - the side, then survival");
			}
			if (g_setupOpen) return;
			// (Noah: "the class pop-up came at the end of the cave, not when I left it") only out under the sky
			const bool outside = player->GetParentCell() && !player->GetParentCell()->IsInteriorCell() && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
			const bool setupWaits = !(Save::Setup() & Save::kSetupDone) && FreshQuickstartGame(player);   // (a new character's choices come first)
			if (Save::Flag(Save::kFlagMoved) && Forms::IntroDone() && !Save::Flag(Save::kFlagStarted) && outside && !setupWaits) {
				// (Noah: "I should start with nothing but the Curse of Alduin lore book, and what comes with my race") whatever
				// the quick start and other mods put in the pack goes (quest items and the underwear stay); the lore book
				// follows at once (src/Curse.cpp), the race's powers are spells and stay
				if (FreshQuickstartGame(player) && Classes::Get() < 0) {  // (never the class kit)
					auto* dirge = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectBOOK>(gen::kDirgeBook, gen::kPlugin);
					auto* under = Classes::Underwear();
					int   gone = 0;
					// (Noah: "when I first spawn in with nothing it thinks I'm holding an axe") the quick start's axe is
					// taken out of the hands properly first - sheathed, unequipped - so the hands' animation forgets it
					auto* em = RE::ActorEquipManager::GetSingleton();
					if (player->AsActorState()->IsWeaponDrawn()) player->DrawWeaponMagicHands(false);
					for (bool left : { false, true }) {
						if (auto* eq = player->GetEquippedObject(left); eq && em) {
							if (auto* b = eq->As<RE::TESBoundObject>()) em->UnequipObject(player, b, nullptr, 1, nullptr, false, true, false, true);
						}
					}
					for (auto& [obj, data] : player->GetInventory()) {
						if (!obj || data.first <= 0 || obj == dirge || obj == under) continue;
						if (data.second && data.second->IsQuestObject()) continue;
						player->RemoveItem(obj, data.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						gone += data.first;
					}
					logger::info("intro: the new character's pack emptied ({} items) - the lore book and the race's own powers only", gone);
					g_emptyHandsFor = 6.0f;  // (and for a few seconds the hands are told they hold nothing: TickEmptyHands)
				}
				Arcana::GiveStarter();  // the Coffer's spells replaced the old ones: a new character starts with one of them
				Gun::TestKitIfFlagged();  // (Noah's test characters: CurseOfAlduin-testkit.flag beside the log)
				Save::SetFlag(Save::kFlagStarted, true);
				logger::info("intro: started");
			}
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
