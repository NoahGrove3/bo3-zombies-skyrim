#include "BO3Z.h"
#include "Perf.h"

namespace bo3z
{
	namespace
	{
		void OnDataLoaded();

		// SkyUI's "N new menu(s) registered" (and its other start-up notes) don't reach the top-left corner
		struct NoticeFilter
		{
			static bool Drop(std::string_view t)
			{
				if (t.starts_with("$SKI")) {
					return true;
				}
				std::string l(t);
				std::ranges::transform(l, l.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return l.find("menu(s) registered") != std::string::npos || l.find("new menu") != std::string::npos ||
				       l.find("menus registered") != std::string::npos || l.find("mcm helper") != std::string::npos ||
				       l.find("skyui") != std::string::npos;
			}
			static RE::UI_MESSAGE_RESULTS thunk(RE::IMenu* a_this, RE::UIMessage& a_msg)
			{
				static REL::Relocation<std::uintptr_t> hudData{ RE::VTABLE_HUDData[0] };
				if (a_msg.type == RE::UI_MESSAGE_TYPE::kUpdate && a_msg.data && *reinterpret_cast<std::uintptr_t*>(a_msg.data) == hudData.address()) {
					auto* d = static_cast<RE::HUDData*>(a_msg.data);
					if (d->type == RE::HUD_MESSAGE_TYPE::kNotification && d->text.c_str() && Drop(d->text.c_str())) {
						logger::info("hid notification: {}", d->text.c_str());
						return RE::UI_MESSAGE_RESULTS::kHandled;
					}
				}
				return func(a_this, a_msg);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// (Noah) no spell ever hurts the one who cast it - you or anyone else: a harmful effect whose caster is its own target
		// (a fireball bursting at your feet, a mage caught in its own blast) is never applied. And (Noah: "walking over an
		// electrocuted body hurts me") nothing hurts you but a living foe: a harmful effect on you from yourself, from no one,
		// from a corpse or a hazard, or from someone not fighting you is dropped (the night's dead always count as foes, even
		// as they burst). Your own spells only hurt those who are fighting you (and the night's dead): townsfolk, followers
		// and anyone not hostile are spared.
		bool SelfHarm(RE::MagicTarget* a_this, const RE::MagicTarget::AddTargetData& a_data)
		{
			auto* me = a_this ? a_this->GetTargetStatsObject() : nullptr;
			if (!me || !a_data.effect || !a_data.effect->baseEffect) {
				return false;
			}
			auto* mg = a_data.effect->baseEffect;
			if (!(mg->IsHostile() || mg->IsDetrimental())) {
				return false;
			}
			if (a_data.caster && a_data.caster == me) {
				return true;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* caster = a_data.caster ? a_data.caster->As<RE::Actor>() : nullptr;
			if (player && me == player && Bows::IsStrikeSpell(a_data.magicItem)) {
				return true;  // (Noah: the charged bow hurt me at night) our bows' spells never touch you, whoever they count as cast by
			}
			if (player && me == player) {
				if (!caster) {
					return true;  // a hazard, a trap, a lingering field with no one behind it
				}
				if (Director::IsRoundUndead(caster) && !caster->IsDead()) {
					if (const float hm = Director::HarmMult(caster); hm < 0.999f) {  // (the harpies' spells, toned down)
						auto& d = const_cast<RE::MagicTarget::AddTargetData&>(a_data);
						d.power *= hm;
						if (d.magnitude > 0) d.magnitude *= hm;
					}
					return false;
				}
				return caster->IsDead() || !Director::IsFoe(caster);
			}
			if (player && caster == player) {
				auto* target = me->As<RE::Actor>();
				return target && !Director::IsFoe(target);
			}
			// (Noah: "the summons are too strong ... they deal too much damage") a summon's own spells barely scratch the
			// night's dead - what its blows and bolts do to them is the share counted on the hit (TESHitEvent below), by the
			// rung of your summon spells; and the night's dead's spells hurt your summons by that same rule the other way
			if (player && caster && caster != player && Director::PlayerSide(caster, player)) {
				if (auto* target = me->As<RE::Actor>(); target && Director::IsRoundUndead(target)) {
					auto& d = const_cast<RE::MagicTarget::AddTargetData&>(a_data);
					d.power *= 0.1f;
					if (d.magnitude > 0) d.magnitude *= 0.1f;
				}
			}
			return false;
		}
		template <int N>
		struct NoSelfHarm
		{
			static bool thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
			{
				if (SelfHarm(a_this, a_data)) {
					static int said = 0;
					if (said++ < 40) {
						logger::info("magic: {} spared its own {}", a_this->GetTargetStatsObject()->GetName(), a_data.effect->baseEffect->GetName());
					}
					return false;
				}
				return func(a_this, a_data);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		// the vtable of a class's MagicTarget part: the one whose object locator says it sits 0x98 into the actor
		template <std::size_t M>
		std::uintptr_t MagicTargetVtbl(const std::array<REL::VariantID, M>& a_vtbls)
		{
			for (const auto& id : a_vtbls) {
				const std::uintptr_t v = id.address();
				if (!v) continue;
				const auto* col = *reinterpret_cast<const std::uint32_t* const*>(v - sizeof(void*));
				if (col && col[1] == 0x98) return v;
			}
			return 0;
		}
		// The Address Library's vtable ids can miss on this game version: the vtables are then read off live objects
		// (the player's own MagicTarget part, and the first plain Character met in the world).
		bool g_harmHooked[2] = { false, false };
		void HookHarm(int a_which, std::uintptr_t a_vtbl)
		{
			if (g_harmHooked[a_which] || !a_vtbl) {
				return;
			}
			g_harmHooked[a_which] = true;
			REL::Relocation<std::uintptr_t> mt{ a_vtbl };
			if (a_which == 0) {
				NoSelfHarm<0>::func = mt.write_vfunc(0x1, NoSelfHarm<0>::thunk);
			} else {
				NoSelfHarm<1>::func = mt.write_vfunc(0x1, NoSelfHarm<1>::thunk);
			}
			logger::info("hooked {} magic (live vtable): no self-harm", a_which == 0 ? "the actors'" : "your");
		}
		void HookHarmLive(RE::PlayerCharacter* a_player)
		{
			if (!g_harmHooked[1] && a_player) {
				HookHarm(1, *reinterpret_cast<std::uintptr_t*>(static_cast<RE::MagicTarget*>(a_player)));
			}
			if (!g_harmHooked[0]) {
				static const std::uintptr_t playerTop = a_player ? *reinterpret_cast<std::uintptr_t*>(a_player) : 0;
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
					auto a = h.get();
					if (a && a.get() != a_player && a->GetFormType() == RE::FormType::ActorCharacter &&
						*reinterpret_cast<std::uintptr_t*>(a.get()) != playerTop) {
						HookHarm(0, *reinterpret_cast<std::uintptr_t*>(static_cast<RE::MagicTarget*>(a.get())));
						break;
					}
				}
			}
		}

		// (Noah: "sometimes when I die I don't get the death screen at all - just the normal Skyrim death") a blow that
		// lands in the instant the essential flag is off (something clears it each frame; it is set back) killed you for
		// real. Skyrim's kill itself is turned aside now: you are left on 1 health and the fall takes it from there.
		struct PlayerKill
		{
			static void thunk(RE::Actor* a_this, RE::Actor* a_attacker, float a_damage, bool a_sendEvent, bool a_ragdollInstant)
			{
				if (a_this && !a_this->IsDead() && Party::HoldsDeath()) {
					auto*       av = a_this->AsActorValueOwner();
					const float hp = av->GetActorValue(RE::ActorValue::kHealth);
					if (hp < 1.0f) av->RestoreActorValue(RE::ActorValue::kHealth, 1.0f - hp);
					a_this->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kEssential);
					if (static int said = 0; said++ < 10) logger::info("death: a killing blow ({} , {:.0f}) turned aside - you fall instead", a_attacker ? a_attacker->GetName() : "none", a_damage);
					return;
				}
				func(a_this, a_attacker, a_damage, a_sendEvent, a_ragdollInstant);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				if (!g_harmHooked[0] || !g_harmHooked[1]) {
					HookHarmLive(a_this);
				}
				// (a trace of the first ticks after each load: if the game ever stops there, the log shows where)
				static int trace = 0;
				if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
					trace = 0;
				}
				const bool tr = trace < 240;
				auto       T = [&](const char* a_what) {
                    if (tr) {
                        logger::info("tick {} {}", trace, a_what);
                    }
				};
				++trace;
				Watchdog::PlayerTicked();
				T("game");
				func(a_this, a_delta);
				// (Noah: crashes at the loading screen after dying) nothing of ours runs while a load is under way (the world
				// is being torn down under it), and once you are dead only the party, the HUD's sound and the audio go on
				if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
					return;
				}
				if (a_this && a_this->IsDead()) {
					try {
						Party::Tick(a_delta);
						Audio::Tick(a_delta);
					} catch (const std::exception& e) {
						logger::error("tick (dead): {}", e.what());
					}
					return;
				}
				try {
					using Perf::Scope;
					static std::uint64_t frames = 0;
					Perf::Report(++frames);
					T("save");
					{ Scope q(Perf::kSave); Save::Guard(); DevTools::RunQueuedInGame(); }
					T("intro");
					{ Scope q(Perf::kIntro); Intro::Tick(); }
					T("director");
					{ Scope q(Perf::kDirector); Director::Tick(a_delta); }
					T("gun");
					{ Scope q(Perf::kGun); Gun::Tick(a_delta); }
					T("shops");
					{ Scope q(Perf::kShops); Shops::Tick(a_delta); }
					{ Scope q(Perf::kClasses); Classes::Tick(); Classes::TickFast(a_delta); }
					T("perks");
					{ Scope q(Perf::kPerks); Perks::Tick(); }
					T("party");
					{ Scope q(Perf::kParty); Party::Tick(a_delta); }
					T("compass");
					{  // (the audit's performance pass) the compass's marks are world bearings - turning is drawn each frame by the
						// HUD - so they are worked out 15 times a second, not every frame
						static float acc = 0;
						Compass::Heading();
						if ((acc += a_delta) >= 1.0f / 15.0f) {
							Scope q(Perf::kCompass);
							Compass::Tick(acc);
							acc = 0;
						}
					}
					T("loot");
					{ Scope q(Perf::kLoot); Loot::Tick(a_delta); }
					T("movement");
					{ Scope q(Perf::kMovement); Movement::Tick(a_delta); }
					T("audio");
					{ Scope q(Perf::kAudio); Audio::Tick(a_delta); }
					T("done");
				} catch (const std::exception& e) {
					logger::error("tick: {}", e.what());
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		class MenuWatch : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuWatch* Get()
			{
				static MenuWatch s;
				return &s;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				Audio::SetMenuMusic();  // the main menu plays our track (read by the game when the menu opens)
				if (a_event->opening && a_event->menuName == RE::TutorialMenu::MENU_NAME) {
					// Skyrim's tutorial pop-ups (low health, low stamina, low magicka...) close the moment they open
					if (auto* q = RE::UIMessageQueue::GetSingleton()) {
						q->AddMessage(RE::TutorialMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
					}
				}
				if (a_event->opening && a_event->menuName == RE::SleepWaitMenu::MENU_NAME && dataLoaded && !Save::Flag(Save::kFlagStarted)) {
					// (Noah: "I can wait all the way through the night, and it resets to day") before a class is chosen the
					// clock holds at day - said so, rather than a night that silently turns back to morning
					Hud::Banner("The nights begin once you choose your class at an Echo Altar - until then it stays day", 5.0f);
				}
				if (a_event->opening && a_event->menuName == RE::SleepWaitMenu::MENU_NAME && dataLoaded && Save::Flag(Save::kFlagStarted)) {
					// (Noah) once the nights have begun the Curse's own rest screen takes the wait menu's place
					if (auto* q = RE::UIMessageQueue::GetSingleton()) q->AddMessage(RE::SleepWaitMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
					SKSE::GetTaskInterface()->AddTask([] { Director::OpenRest(); });
				} else if (a_event->opening && a_event->menuName == RE::SleepWaitMenu::MENU_NAME && dataLoaded) {
					if (std::string why; !Director::MayRest(why)) {
						if (auto* q = RE::UIMessageQueue::GetSingleton()) {
							q->AddMessage(RE::SleepWaitMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
						}
						Hud::Banner(why, 3.5f);
					}
				}
				if (a_event->menuName == RE::MainMenu::MENU_NAME) {
					Audio::MenuUiSounds(a_event->opening);
					Save::ForgetSnapshot();
					Intro::OnMainMenu();
				}
				// "Data loaded" without the Script Extender: the first menu event once the player form exists.
				if (!dataLoaded && RE::TESForm::LookupByID(0x7)) {
					dataLoaded = true;
					OnDataLoaded();
				}
				if (dataLoaded) {
					Intro::Install();  // keep MQQuickstart at 3 for the next new game
				}
				// (Noah) making your character: your armour comes off while the menu is up (you see the body and face you are
				// shaping), and goes back on as it closes
				if (a_event->menuName == RE::RaceSexMenu::MENU_NAME) {
					static std::vector<RE::TESBoundObject*> worn;
					auto* pl = RE::PlayerCharacter::GetSingleton();
					auto* em = RE::ActorEquipManager::GetSingleton();
					if (pl && em && a_event->opening) {
						worn.clear();
						auto* under = Classes::Underwear();
						for (auto& [obj, e] : pl->GetInventory([](RE::TESBoundObject& o) { return o.IsArmor(); })) {
							if (obj && obj != under && e.second && e.second->IsWorn()) {
								worn.push_back(obj);
							}
						}
						for (auto* o : worn) {
							em->UnequipObject(pl, o, nullptr, 1, nullptr, false, false, false, true);
						}
						Classes::DressUnder(true);  // (Noah: her bra and underwear on while you shape her, not bare)
						logger::info("character menu: {} pieces of armour taken off", worn.size());
					} else if (pl && em && !a_event->opening) {
						if (auto* under = Classes::Underwear(); under && !(pl->GetActorBase() && pl->GetActorBase()->IsFemale()) && pl->GetItemCount(under) > 0) {
							em->UnequipObject(pl, under, nullptr, 1, nullptr, false, false, false, true);  // (a man keeps HIMBO's own)
						}
						for (auto* o : worn) {
							em->EquipObject(pl, o, nullptr, 1, nullptr, false, false, false, true);
						}
						logger::info("character menu: {} pieces of armour put back on", worn.size());
						worn.clear();
					}
				}
				// (Noah: too dark to see her) the menu lights you like a portrait: a warm key light in front of your face, a soft
				// fill from the side (Skyrim's own load-screen lights), and midday for as long as the menu is up
				if (a_event->menuName == RE::RaceSexMenu::MENU_NAME) {
					static std::vector<RE::ObjectRefHandle> lamps;
					static float                            hourWas = -1;
					auto*                                   pl = RE::PlayerCharacter::GetSingleton();
					auto*                                   cal = RE::Calendar::GetSingleton();
					for (auto& h : lamps) {
						if (auto r = h.get()) {
							r->Disable();
							r->SetDelete(true);
						}
					}
					lamps.clear();
					if (a_event->opening && pl && pl->Is3DLoaded()) {
						// (Noah: "the character creation lighting is very bright, it's hard to see") no midday and no key light
						// any more: only the soft fill from the side, over RaceMenu's own light
						(void)cal;
						const float        yaw = pl->GetAngleZ();
						const RE::NiPoint3 F{ std::sin(yaw), std::cos(yaw), 0 }, R{ std::cos(yaw), -std::sin(yaw), 0 };
						const RE::NiPoint3 at = pl->GetPosition();
						struct L { RE::FormID id; RE::NiPoint3 off; };
						const L kLamps[1] = { { 0x105305, F * 90.0f - R * 220.0f + RE::NiPoint3{ 0, 0, 120 } } };
						for (const auto& l : kLamps) {
							auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(l.id);
							if (!base) continue;
							if (auto ref = pl->PlaceObjectAtMe(base, false)) {
								ref->SetPosition(at + l.off);
								lamps.push_back(ref->CreateRefHandle());
							}
						}
						logger::info("character menu: lit ({} lights)", lamps.size());
					} else if (!a_event->opening && cal && cal->gameHour && hourWas >= 0) {
						cal->gameHour->value = hourWas;
						hourWas = -1;
					}
				}
				if (a_event->menuName == RE::RaceSexMenu::MENU_NAME) Intro::OnRaceMenu(a_event->opening);
				if (a_event->menuName == RE::RaceSexMenu::MENU_NAME && !a_event->opening) {
					// a character just made in a new game: black from here until the film (no glimpse of the road first)
					auto* pl = RE::PlayerCharacter::GetSingleton();
					auto* cal = RE::Calendar::GetSingleton();
					const float days = cal && cal->gameDaysPassed ? cal->gameDaysPassed->value : 99.0f;
					if (pl && pl->GetLevel() <= 1 && days < 2.0f) {
						if (Intro::NewGameThisSession()) {
							Hud::AfterCharacterMade();  // (already in Riverwood: just a moment of black)
						} else {
							Hud::IntroVeil(6.0f, true);  // (a cap: the move to Riverwood ends it sooner)
							logger::info("intro: character made: black until the move");
						}
					}
				}
				if (a_event->menuName == RE::LoadingMenu::MENU_NAME && a_event->opening) {
					Save::ForgetSnapshot();  // whatever comes next may be another game
					Shops::BeforeLoad();     // (the crash on every in-game load: our hand-made map markers off first)
				}
				if (a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening && dataLoaded) {
					// After a new game, a save from the main menu, or a save loaded in game.
					Save::OnWorldReady();
					static int loads = 0;
					if (loads++ > 0) {
						Watchdog::WorldLoaded();  // (into the world - not the first, which is the main menu's own at start)
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
			bool dataLoaded{ false };
		};

		class LoadWatch : public RE::BSTEventSink<RE::TESLoadGameEvent>
		{
		public:
			static LoadWatch* Get()
			{
				static LoadWatch s;
				return &s;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
			{
				Audio::StopAll();
				Save::ForgetSnapshot();
				Director::Reset();
				Gun::Reset();
				Shops::Reset();
				Movement::Reset();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class DeathWatch : public RE::BSTEventSink<RE::TESDeathEvent>
		{
		public:
			static DeathWatch* Get()
			{
				static DeathWatch s;
				return &s;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				// the first word of a death (dying), not the last (dead): a body crumbles a second after it falls, often
				// before Skyrim would call it fully dead, and the kill must still count. Once per body.
				if (!a_event || !a_event->actorDying) {
					return RE::BSEventNotifyControl::kContinue;
				}
				{
					static std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> seen;
					const auto now = std::chrono::steady_clock::now();
					std::erase_if(seen, [&](const auto& e) { return now - e.second > std::chrono::seconds(3); });  // (a short window: a reused FormID's death must count)
					const auto id = a_event->actorDying->GetFormID();
					if (seen.contains(id)) {
						return RE::BSEventNotifyControl::kContinue;
					}
					seen[id] = now;
				}
				auto* dying = a_event->actorDying->As<RE::Actor>();
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (dying && dying == player) {  // who or what killed you (a new character's first minutes are watched closely)
					auto*       k = a_event->actorKiller.get();
					const auto  p = player->GetPosition();
					auto*       cell = player->GetParentCell();
					logger::info("death: the player died (killer {:08X} {}, at {:.0f},{:.0f},{:.0f} in {}, level {}, hp max {:.0f})", k ? k->GetFormID() : 0, k ? k->GetName() : "none",
						p.x, p.y, p.z, cell ? cell->GetFormEditorID() : "?", player->GetLevel(), player->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth));
					Perks::LoseAll();  // death takes every blessing
				}
				if (dying) {
					Director::OnDying(dying);
				}
				if (dying && dying != player && Director::IsFoe(dying)) {  // (the classes overhaul) a kill by you or your summons trains what you hold
					auto* killer = a_event->actorKiller.get() ? a_event->actorKiller->As<RE::Actor>() : nullptr;
					const bool mine = killer == player;
					const bool summon = killer && killer != player && ((killer->IsCommandedActor() && killer->GetCommandingActor().get() == player) || Classes::IsOwnSummon(killer));
					if (mine || summon) Classes::OnKill(summon, dying, killer);
				}
				if (dying && dying != player && !Director::IsRoundUndead(dying) && a_event->actorKiller.get() == player && Director::IsFoe(dying)) {
					PowerUps::MaybeDrop(dying->GetPosition());  // (Noah: any enemy can drop one - bandits, wolves, draugr too)
				}
				if (!dying || !Director::IsRoundUndead(dying)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				Audio::Voice(dying, Audio::VoiceEvent::kDeath);
				Audio::Flesh(dying, true);
				if (Coop::Client()) {
					PowerUps::MaybeDrop(dying->GetPosition());  // a friend's game rolls its own drops (the host's aren't seen here)
				}
				if (a_event->actorKiller.get() != player) {
					// a party member's kill counts for everyone (each player gets the points: nobody goes without)
					auto* killer = a_event->actorKiller.get() ? a_event->actorKiller->As<RE::Actor>() : nullptr;
					if (Net::InLobby() && Party::IsFriend(killer)) {
						Points::Add(gen::kPoints_kill);
					} else if (killer && ((killer->IsCommandedActor() && killer->GetCommandingActor().get() == player) || Classes::IsOwnSummon(killer) || Echo::IsAlly(killer))) {
						// (the audit: since a summon's kill became the summon's, it paid nothing) your summons' and allies' kills pay
						Points::Add(gen::kPoints_kill);
						Hud::Essence(dying->GetPosition());
					}
					return RE::BSEventNotifyControl::kContinue;  // dawn, followers, guards: no points
				}
				const auto [gun, head] = Gun::RecentHit(dying->GetFormID());
				Points::Add(gun ? (head ? gen::kPoints_headshot_kill : gen::kPoints_kill) : gen::kPoints_melee_kill);
				Hud::Essence(dying->GetPosition());
				{
					const bool bow = Bows::Held() >= 0;
					Stats::OnKill(dying, gun, head, !gun && !bow, bow, dying->GetPosition().GetDistance(player->GetPosition()));
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class InputWatch : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputWatch* Get()
			{
				static InputWatch s;
				return &s;
			}
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (auto* e = *a_event; e; e = e->next) {
					if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
						if (auto* mm = static_cast<RE::MouseMoveEvent*>(e)) {
							Hud::OnMouseMove(static_cast<float>(mm->mouseInputX), static_cast<float>(mm->mouseInputY));
						}
					}
					if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
						Movement::OnStick(static_cast<RE::ThumbstickEvent*>(e));
						if (auto* ts = static_cast<RE::ThumbstickEvent*>(e); ts && ts->IsLeft()) Hud::OnLeftStick(ts->xValue, ts->yValue);
						if (auto* ts = static_cast<RE::ThumbstickEvent*>(e); ts && std::abs(ts->xValue) + std::abs(ts->yValue) > 0.35f) Hud::NotePad();
					}
					if (auto* b = e->AsButtonEvent()) {
						if (b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->GetIDCode() == 0 && b->IsDown()) {
							Hud::OnClick();
						}
						Hud::OnMenuButton(b);
						Director::ScoutKey(b);
						if (Director::RestButton(b)) {  // the rest screen has the keys while it is up
							continue;
						}
						if (Classes::OnButton(b)) {  // the class picker has the keys while it is up
							continue;
						}
						if (!Intro::OnButton(b) && !Party::OnButton(b)) {  // (down or dead: your hands are still)
							if (Shops::OnButton(b) || Loot::OnButton(b)) {
								continue;
							}
							Movement::OnButton(b);
							Gun::OnButton(b);
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class ActivateWatch : public RE::BSTEventSink<RE::TESActivateEvent>
		{
		public:
			static ActivateWatch* Get()
			{
				static ActivateWatch s;
				return &s;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				if (a_event && a_event->actionRef.get() == RE::PlayerCharacter::GetSingleton() && a_event->objectActivated) {
					if (auto* base = a_event->objectActivated->GetBaseObject()) {
						logger::info("activate: {} ({:08X})", base->GetName(), base->GetFormID());
					}
					if (!Echo::OnActivate(a_event->objectActivated.get())) {
						Shops::OnActivate(a_event->objectActivated.get());
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// one hit a quarter second on each body (a spell's several effects and its hit all land at once)
		// (the audit: one window per body swallowed your blows while your summons bit it) one window per attacker and body
		bool Limit(RE::Actor* a_actor, RE::Actor* a_by = nullptr)
		{
			static std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> last;
			const auto now = std::chrono::steady_clock::now();
			if (last.size() > 512) std::erase_if(last, [&](const auto& e) { return now - e.second > 2s; });
			const std::uint64_t key = (static_cast<std::uint64_t>(a_by ? a_by->GetFormID() : 0x14) << 32) | a_actor->GetFormID();
			auto&      t = last[key];
			if (now - t <= 250ms) {
				return false;
			}
			t = now;
			return true;
		}

		void HurtUndead(RE::Actor* a_actor, float a_damage, RE::Actor* a_by = nullptr)
		{
			// (Noah: "the class quests aren't progressing - summon kills don't count") the blow's own maker is the killer: a
			// summon's kill is the summon's (it was always counted as yours, and so never as a summon's)
			auto* player = a_by ? a_by : RE::PlayerCharacter::GetSingleton();
			auto* av = a_actor->AsActorValueOwner();
			if (Undead::IsDancing(a_actor)) a_damage *= 1.5f;  // (the Dance Of Death: half again as hard)
			a_damage *= Director::TakenMult(a_actor);  // (a brute dazed or berserk)
			av->DamageActorValue(RE::ActorValue::kHealth, a_damage);
			if (av->GetActorValue(RE::ActorValue::kHealth) <= 0) {
				a_actor->KillImpl(player, a_damage, true, false);
			}
		}

		// (Noah: "better feedback when I hit enemies") your blade, your bow and your spells mark a hit on one of the dead as
		// the wands do: the cross at the crosshair (red and larger on a kill) and its tick (a kill's own sound)
		void MarkHit(RE::Actor* a_z)
		{
			if (!a_z) return;
			static auto last = std::chrono::steady_clock::time_point{};
			const auto  now = std::chrono::steady_clock::now();
			const bool  kill = a_z->IsDead() || a_z->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0;
			if (!kill && now - last < std::chrono::milliseconds(90)) return;
			last = now;
			Forms::Play(kill ? gen::Sound::headshot : gen::Sound::hitmarker);
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().hitTime = kill ? 0.3f : 0.2f;
			Hud::Edit().hitKill = kill;
			Hud::Edit().hitHead = false;
		}

		// A Coffer spell reached an actor (its hit, or one of its effects taking hold - some, like Paralyze or Magicka
		// Void, never hit). Noah: every spell from the shrine hurts every enemy. The night's undead lose a share of
		// their round health (rarer spells more, a reforge adds its step, each Twin Strike Ale half again - their own
		// magnitudes don't reach the dead); any other enemy is hurt by the spell's own effects, and a spell with none
		// that take health (paralysis, a drain of magicka...) takes a part of its health instead.
		void CofferHit(RE::Actor* a_actor, int a_index)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!a_actor || !player || a_actor == player || a_actor->IsDead()) {
				return;
			}
			// (Noah: the one upgrade curve) its rung and its Ascends are its upgrades
			const int   ups = std::clamp(Arcana::TierOf(a_index), 0, 3) + std::max(0, Arcana::Level(a_index));
			const float grow = UpgradeMult(ups) * (1.0f + 0.5f * Save::PerkStacks(gen::kPerk_twinstrike)) * (0.7f + 0.3f * Arcana::Power());
			// (Noah's plan, Destruction) dual-casting one element: the same spell in both hands hits a third harder
			const auto  hands = Arcana::Hands();
			const float twin = Classes::Get() == 1 && hands[0] == a_index && hands[1] == a_index ? 1.35f : 1.0f;
			if (Director::IsRoundUndead(a_actor)) {
				if (Limit(a_actor)) {
					// (the balance pass) a concentration spell lands four times a second: each touch takes a third of a
					// cast's share, so a stream and a bolt kill about as fast for the magicka
					const float stream = Arcana::Concentration(a_index) ? 0.35f : 1.0f;
					HurtUndead(a_actor, Director::RoundHealth() * gen::kShareSpell * grow * twin * stream);
				}
				return;
			}
			if (Arcana::Damages(a_index) || !Director::IsFoe(a_actor) || !Limit(a_actor)) {
				return;
			}
			const float max = a_actor->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth);
			HurtUndead(a_actor, std::max(8.0f, max * std::min(0.6f, 0.07f * grow * twin)));  // (Skyrim's own foes: a share of their health, on the same curve)
		}

		class HitWatch : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			static HitWatch* Get()
			{
				static HitWatch s;
				return &s;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				static const RE::FormID fangId = RE::TESDataHandler::GetSingleton()->LookupFormID(gen::kFangWeap, gen::kPlugin);
				const bool fangHit = a_event && player && a_event->cause.get() == player && fangId && a_event->source == fangId;
				if (fangHit && a_event->target) {
					if (auto* t = a_event->target->As<RE::Actor>(); t && t != player && Director::IsFoe(t)) {
						Gun::FangHit(t, a_event->flags.any(RE::TESHitEvent::Flag::kPowerAttack));
					}
				}
				// (Noah: "my summons start to kill me if I am damaging them") your blow on your own summon: it doesn't hold it
				// against you (just after the hit, when the fight it picked would begin)
				if (a_event && player && a_event->cause.get() == player && a_event->target) {
					if (auto* sm = a_event->target->As<RE::Actor>(); sm && sm != player && ((sm->IsCommandedActor() && sm->GetCommandingActor().get() == player) || Classes::IsOwnSummon(sm))) {
						Tasks::Add([h = sm->GetHandle()] {
							auto* pc = RE::PlayerCharacter::GetSingleton();
							if (auto a = h.get(); a && pc && !a->IsDead()) {
								if (auto t = a->GetActorRuntimeData().currentCombatTarget.get(); t && Director::PlayerSide(t.get(), pc)) a->StopCombat();
							}
						});
					}
				}
				if (a_event && player && a_event->cause.get() == player && a_event->target && a_event->target.get() != player) {
					if (auto* t = a_event->target->As<RE::Actor>()) Gun::OnStaffHit(t, a_event->source);
					if (auto* t = a_event->target->As<RE::Actor>()) Classes::ConjurerStaffHit(t, a_event->source);
					if (auto* t = a_event->target->As<RE::Actor>()) Classes::NoteHit(t, a_event->source, a_event->flags.any(RE::TESHitEvent::Flag::kPowerAttack));
					auto* src = a_event->source ? RE::TESForm::LookupByID(a_event->source) : nullptr;
					const bool spellHit = src && src->As<RE::MagicItem>();  // (the charge's own lightning never sets off another charge)
					if (auto* t = a_event->target->As<RE::Actor>(); t && !spellHit && Director::IsFoe(t)) {
						Gun::StormHit(t);  // (a blade, a fist or Skyrim's own bow: the Stormblast charge goes off too)
					}
					// (Noah: "nothing I try lets me imbue my sword with a spell") the Spellsword's blade carries the spell in
					// the other hand by itself - no storing, no keys: each blade hit on one of the dead also strikes it with
					// that spell (a moment apart, for free). The Spellsword mod's own "contains one spell" slot is not used.
					if (auto* t = a_event->target->As<RE::Actor>(); t && !spellHit && !t->IsDead() && Director::IsFoe(t) && src && src->As<RE::TESObjectWEAP>() &&
						Classes::Get() == 3) {  // (3: the Spellsword - src/CofferSpells.h)
						static auto last = std::chrono::steady_clock::time_point{};
						const auto  now = std::chrono::steady_clock::now();
						if (now - last > std::chrono::milliseconds(350)) {
							// (the Spellsword review: a shell or cloak in hand was cast at the foe) only the altar's Spellsword spells ride
							// the blade - the one in a hand, or the last held with a two-handed blade
							auto*          bw = src->As<RE::TESObjectWEAP>();
							const int      bi = bw && (bw->IsTwoHandedSword() || bw->IsTwoHandedAxe()) ? -1 : Classes::BladeSpell();  // (a two-handed wonder casts its own)
							RE::SpellItem* sp = bi >= 0 ? Arcana::Spell(bi) : nullptr;
							if (sp && sp->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
								last = now;
								Forms::CastOnce(player, sp, t, 1.0f, player);
								// (Noah: "only the one I'm holding - multiple spells from one weapon as an upgrade") Twin Resonance is
								// earned: a Legendary touch on the blade brings your other touch along, a quarter as strong
								if (Arcana::TierOf(bi) >= 3) {
									for (int other : Arcana::Carried()) {
										if (other == bi || Arcana::ClassOf(other) != 3) continue;
										if (auto* sp2 = Arcana::Spell(other); sp2 && sp2->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
											Forms::CastOnce(player, sp2, t, 0.25f, player);
											break;
										}
									}
								}
								static int told = 0;
								if (told++ < 5) logger::info("spellsword: the blade struck with {}", sp->GetName());
							}
						}
					}
				}
				if (a_event && player && a_event->cause.get() == player && a_event->target.get() == player) {
					Perks::SelfHit();  // (your own spells never hurt you)
				}
				// (the Spellsword's spellshield) a blow on you, blocked
				if (a_event && player && a_event->target.get() == player && a_event->cause && a_event->cause.get() != player && a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked)) {
					auto* src = a_event->source ? RE::TESForm::LookupByID(a_event->source) : nullptr;
					if (auto* att = a_event->cause->As<RE::Actor>()) Classes::OnBlock(att, src && src->As<RE::MagicItem>());
				}
				// (Noah: Insta-Kill means one hit) any blow of yours - blade, fist, Skyrim's bows and spells - drops a wave undead
				if (a_event && player && a_event->cause.get() == player && a_event->target && PowerUps::InstaKill()) {
					if (auto* z = a_event->target->As<RE::Actor>(); z && z != player && !z->IsDead() && Director::IsRoundUndead(z)) {
						z->KillImpl(player, 9999.0f, true, false);
					}
				}
				// Skyrim weapons and spells keep up with the rounds: each hit takes a share of a zombie's round health.
				if (a_event && a_event->cause.get() == player && a_event->target) {
					auto* z = a_event->target->As<RE::Actor>();
					auto* src = RE::TESForm::LookupByID(a_event->source);
					if (Bows::IndexOf(src) >= 0) {
						return RE::BSEventNotifyControl::kContinue;  // the altar's bows: their own arrow is only the look (Bows.inl does the hit)
					}
					if (z && Director::IsRoundUndead(z)) {  // Trial of Steel: was it a blade, an axe, a mace or a fist?
						auto*      w = src ? src->As<RE::TESObjectWEAP>() : nullptr;
						const bool melee = !a_event->projectile && (src ? (w && !w->IsBow() && !w->IsCrossbow() && !w->IsStaff() && Forms::GunIndexOf(src) < 0) : true);
						Curse::NoteHit(z->GetFormID(), melee);
					}
					if (const int ci = Arcana::IndexOf(src); z && ci >= 0) {
						CofferHit(z, ci);  // the Coffer's spells: every one of them hurts every enemy
						if (Director::IsRoundUndead(z)) MarkHit(z);
					} else if (z && !z->IsDead() && Director::IsRoundUndead(z)) {
						bool ourSpell = false;
						for (int i = 0; i < static_cast<int>(gen::kSpells.size()); ++i) {
							if (auto* m = Forms::Spell(i); m && src == m) {
								ourSpell = true;
							}
						}
						if (src && !ourSpell && Forms::GunIndexOf(src) < 0 && !(fangId && src->GetFormID() == fangId) && Limit(z)) {  // (the Fang's blows are FangHit's alone)
							float share = gen::kShareSpell;
							if (auto* w = src->As<RE::TESObjectWEAP>()) {
								// (Noah: the one upgrade curve) a crossbow by its reforges, an altar spell-weapon by its steel
								const int gear = Classes::AltarGearUpgrades(w);
								share = (w->IsBow() || w->IsCrossbow()) ? gen::kShareBow * UpgradeMult(Classes::CrossbowLevel(w)) : gen::kShareMelee * (gear >= 0 ? UpgradeMult(gear) : 1.0f);
							} else if (src->Is(RE::FormType::Spell)) {
								share *= 1.0f + 0.5f * Save::PerkStacks(gen::kPerk_twinstrike);
							}
							HurtUndead(z, Director::RoundHealth() * share * (0.7f + 0.3f * Arcana::Power()));  // (the night's power curve)
							if (auto* w = src->As<RE::TESObjectWEAP>(); w && w->IsCrossbow()) {
								if (const int el = Classes::CrossbowElement(w); el >= 0) Bows::CrossbowStrike(el, Classes::CrossbowLevel(w), z, Director::RoundHealth() * share * 0.5f);
							}
							Undead::OnHit(z);
							MarkHit(z);
						}
					}
				}
				// the wonders' summons (Echo::Allies) keep up with the rounds too: a share of a zombie's round health a blow
				if (a_event && a_event->cause && a_event->target) {
					auto* ally = a_event->cause->As<RE::Actor>();
					auto* z = a_event->target->As<RE::Actor>();
					auto* pl = RE::PlayerCharacter::GetSingleton();
					const bool mySummon = ally && pl && ally != pl && ((ally->IsCommandedActor() && ally->GetCommandingActor().get() == pl) || Classes::IsOwnSummon(ally));  // (Noah's plan: the Summoner's summons scale with the night's round)
					if (ally && z && !z->IsDead() && (Echo::IsAlly(ally) || mySummon) && Director::IsRoundUndead(z) && Limit(z, ally)) {
						// (the balance pass) your summons hit by the highest rung of the summon spells you carry (0.3x of a
						// blade at the first rung, up to 0.51x at the Legendary), a third again with Bonebinder's Brew
						float tierMult = 0.9f;
						if (mySummon) {
							// (the classes' growth) its own family's rung and Ascends - the best you carry if it isn't the altar's
							int best = 1, asc = 0;
							if (const int fam = Classes::SummonFamily(ally); fam >= 0) {
								best = Arcana::RungOf(fam), asc = Arcana::Level(fam);
								for (int i : Arcana::Carried()) if (Arcana::ClassOf(i) == 0 && Arcana::LadderName(i) == Arcana::LadderName(fam)) best = std::max(best, Arcana::RungOf(i)), asc = std::max(asc, Arcana::Level(i));
							} else {
								for (int i : Arcana::Carried()) if (Arcana::ClassOf(i) == 0) best = std::max(best, Arcana::RungOf(i)), asc = std::max(asc, Arcana::Level(i));
							}
							tierMult = 0.3f * UpgradeMult(std::max(0, best - 1) + std::max(0, asc)) * (Perks::SummonMight() ? gen::kPerks[gen::kPerk_bonebinder].value : 1.0f);  // (Noah: the one upgrade curve - rungs past the first and Ascends)  // (the Brew: its own half again, as it says)
						}
						// (Noah: "summons are too strong") a summon lands its share at most once every 0.7 s, however fast it bites
						// or how many bolts it throws, and that share is a third of a blade's at the first rung
						static std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> beat;
						auto&      b = beat[ally->GetFormID()];
						const auto now = std::chrono::steady_clock::now();
						if (beat.size() > 300) beat.clear();
						if (!mySummon || now - b >= std::chrono::milliseconds(700)) {
							b = now;
							HurtUndead(z, Director::RoundHealth() * gen::kShareMelee * tierMult * (0.7f + 0.3f * Arcana::Power()), ally);
						}
					}
				}
				// (Noah: "my summons have way too much health") the night's dead hurt your summons for real: each blow a share
				// of the summon's own health (a brute's, an elite's or a boss's twice that), by the difficulty's damage
				if (a_event && a_event->cause && a_event->target) {
					auto* z = a_event->cause->As<RE::Actor>();
					auto* sm = a_event->target->As<RE::Actor>();
					auto* pl = RE::PlayerCharacter::GetSingleton();
					if (z && sm && pl && sm != pl && !sm->IsDead() && Director::IsRoundUndead(z) && Director::PlayerSide(sm, pl) &&
						((sm->IsCommandedActor() && sm->GetCommandingActor().get() == pl) || Classes::IsOwnSummon(sm)) && Limit(sm)) {
						const int   row = Director::EnemyRow(z);
						const bool  big = Director::IsBoss(z) || Director::IsElite(z) || (row >= 0 && gen::kEnemies[row].kind == 1);
						auto*       sav = sm->AsActorValueOwner();
						// (the Summoner review: every summon fell in a dozen blows whatever its rung, and the Brew's health counted for
						// nothing) a higher pact takes its blows better, and Bonebinder's Brew's half again of health is real
						int best = 1;
						for (int i : Arcana::Carried()) if (Arcana::ClassOf(i) == 0) best = std::max(best, Arcana::RungOf(i));
						const float might = Perks::SummonMight() ? gen::kPerks[gen::kPerk_bonebinder].value : 1.0f;
						const float hurt = sav->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.08f / (1.0f + 0.35f * static_cast<float>(best - 1)) / might * (big ? 2.0f : 1.0f) * Settings::UndeadDamageMult();
						sav->DamageActorValue(RE::ActorValue::kHealth, hurt);
						if (sav->GetActorValue(RE::ActorValue::kHealth) <= 0 && !sm->IsDead()) sm->KillImpl(z, hurt, true, false);
					}
				}
				if (a_event && a_event->target.get() == RE::PlayerCharacter::GetSingleton() && a_event->cause) {
					if (auto* z = a_event->cause->As<RE::Actor>(); z && z != RE::PlayerCharacter::GetSingleton() && Director::IsFoe(z)) {
						const bool ours = Director::IsRoundUndead(z);
						static int saidOurs = 0, saidOther = 0;
						if ((ours ? saidOurs++ : saidOther++) < 15) logger::info("hurt: hit by {} (ours {}, foe {})", z->GetName(), ours, Director::IsFoe(z));
					}
					if (auto* z = a_event->cause->As<RE::Actor>(); z && Director::IsRoundUndead(z)) {
						// (Noah: the damage at the end of the contact swing) held until the claws meet you
						if (const float wait = Undead::HitDelay(z); wait > 0.02f) Director::QueueZombieHit(z, wait);
						else Perks::OnZombieHit(z);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// The player's shout spells: one of the cities' Thu'ums was spoken (its word count is the spell)
		class CastWatch : public RE::BSTEventSink<RE::TESSpellCastEvent>
		{
		public:
			static CastWatch* Get()
			{
				static CastWatch s;
				return &s;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (a_event && a_event->object && a_event->object.get() == RE::PlayerCharacter::GetSingleton()) {
					int level = 0;
					// (the Volatiles) the night hears you: a shout most of all, any spell a little
					{
						auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(a_event->spell);
						const bool shout = sp && sp->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower;
						Director::Noise(shout ? 35.0f : 5.0f);
					}
					Compass::OnCast(a_event->spell);
					Gun::OnStaffCast(a_event->spell);
					Classes::NoteCast(a_event->spell);
					if (Classes::OnCast(a_event->spell)) {
						// (the Tome of the Six Fires)
					} else if (Director::DanceOfDeath(a_event->spell)) {
						// (the Dance Of Death)
					} else if (Movement::OnSpellCast(a_event->spell)) {
						// (Bond II's Updraft)
					} else if (Eggs::OnSpellCast(a_event->spell)) {
						// (a hidden quest's summon, the Eye's beam)
					} else if (const int sh = Gun::ShoutOfSpell(a_event->spell, level); sh >= 0) {
						Gun::Shout(sh, level);
					} else if (Arcana::IndexOf(RE::TESForm::LookupByID(a_event->spell)) >= 0) {
						static int casts = 0;
						Perks::OnCast(++casts);  // Stormcrown counts the Coffer's spells
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class EffectWatch : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
		{
		public:
			static EffectWatch* Get()
			{
				static EffectWatch s;
				return &s;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
			{
				if (a_event && a_event->target.get() == RE::PlayerCharacter::GetSingleton()) {
					Perks::OnEffect(a_event->magicEffect);
				} else if (a_event && a_event->caster.get() == RE::PlayerCharacter::GetSingleton() && a_event->target) {
					if (const int ci = Arcana::IndexOfEffect(a_event->magicEffect); ci >= 0) {
						// (the audit: a kill inside the engine's own effect-apply can dispel the list it is walking) the hit
						// lands just after, on the main thread's task queue
						if (auto* t = a_event->target->As<RE::Actor>()) {
							Tasks::Add([h = t->GetHandle(), ci] { if (auto a = h.get(); a && !a->IsDeleted()) CofferHit(a.get(), ci); });
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// The nights are a little darker (Noah: 'make night time a little bit darker'): every weather's night light -
		// ambient, moon light, the directional ambient, fog, sky and horizon - turned down once at start-up. Day,
		// dusk and dawn are untouched; whatever weather mod is installed is darkened the same way.
		// (Noah: "use Load Screen Compendium for all the loading screens - random") with its plugin on, every other loading
		// screen (Skyrim's, the DLCs', other mods', our own) gets a condition that is never true, so the engine picks
		// at random among the Compendium's alone
		void OnlyCompendiumScreens()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh || !dh->LookupModByName("LoadScreenCompendium.esp")) return;
			int off = 0, on = 0;
			for (auto* ls : dh->GetFormArray<RE::TESLoadScreen>()) {
				if (!ls) continue;
				auto* f = ls->GetFile(0);
				if (f && std::string_view(f->GetFilename()) == "LoadScreenCompendium.esp") {
					++on;
					continue;
				}
				auto* never = new RE::TESConditionItem();
				never->data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetRandomPercent;
				never->data.functionData.params[0] = nullptr, never->data.functionData.params[1] = nullptr;
				never->data.comparisonValue.f = -1.0f;
				never->data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kLessThan;
				never->data.flags.isOR = false;
				never->data.object = RE::CONDITIONITEMOBJECT::kSelf;
				never->next = ls->conditions.head;
				ls->conditions.head = never;
				++off;
			}
			logger::info("world: loading screens - {} from the Compendium, {} others set aside", on, off);
		}
		void DarkenNights()
		{
			auto  scale = [](RE::Color& c, float f) { c.red = static_cast<std::uint8_t>(c.red * f), c.green = static_cast<std::uint8_t>(c.green * f), c.blue = static_cast<std::uint8_t>(c.blue * f); };
			// (Noah: "make nighttime a little brighter - it's really hard to see even at 1.5 brightness") the night's light
			// raised and given a cool moonlit floor, so the dark still reads as night but the ground and the dead show
			auto  lift = [](RE::Color& c, float f, int floorR, int floorG, int floorB) {
				c.red = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.red * f) + floorR, 0, 255));
				c.green = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.green * f) + floorG, 0, 255));
				c.blue = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.blue * f) + floorB, 0, 255));
			};
			using CT = RE::TESWeather::ColorTypes;
			constexpr auto N = RE::TESWeather::ColorTimes::kNight;
			int n = 0;
			for (auto* w : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESWeather>()) {
				if (!w) {
					continue;
				}
				for (auto t : { CT::kAmbient, CT::kEffectLighting, CT::kCloudLODAmbient }) {
					lift(w->colorData[t][N], 1.45f, 16, 20, 30);
				}
				for (auto t : { CT::kSunlight, CT::kCloudLODDiffuse, CT::kSkyStatics }) {
					lift(w->colorData[t][N], 1.35f, 10, 12, 18);
				}
				for (auto t : { CT::kFogNear, CT::kFogFar, CT::kSkyLower, CT::kHorizon }) {
					lift(w->colorData[t][N], 1.15f, 4, 5, 8);
				}
				auto& d = w->directionalAmbientLightingColors[N].directional;
				for (auto* c : { &d.x.max, &d.x.min, &d.y.max, &d.y.min, &d.z.max, &d.z.min }) {
					lift(*c, 1.45f, 14, 18, 26);
				}
				// and the day a little dimmer too (it was glaring with the graphics mods)
				for (auto tm : { RE::TESWeather::ColorTimes::kDay, RE::TESWeather::ColorTimes::kSunrise, RE::TESWeather::ColorTimes::kSunset }) {
					const float f = tm == RE::TESWeather::ColorTimes::kDay ? 0.82f : 0.9f;
					for (auto t : { CT::kAmbient, CT::kSunlight, CT::kEffectLighting, CT::kCloudLODAmbient, CT::kCloudLODDiffuse }) {
						scale(w->colorData[t][tm], f);
					}
					auto& dd = w->directionalAmbientLightingColors[tm].directional;
					for (auto* c : { &dd.x.max, &dd.x.min, &dd.y.max, &dd.y.min, &dd.z.max, &dd.z.min }) {
						scale(*c, f);
					}
				}
				++n;
			}
			logger::info("world: nights brightened, days dimmed, in {} weathers", n);
		}

		// (Noah: full movement while drawing any bow) the bow's movement types (Skyrim slows you while it is drawn) move at
		// the default type's speeds - every bow, the player's and everyone's
		void FreeBowMovement()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				return;
			}
			const RE::BGSMovementType* def = RE::TESForm::LookupByID<RE::BGSMovementType>(0x03580D);  // Skyrim.esm NPC_Default
			for (auto* mt : dh->GetFormArray<RE::BGSMovementType>()) {
				if (mt && mt->movementTypeData.typeName.c_str()) {
					const std::string_view n(mt->movementTypeData.typeName.c_str());
					if (!def && (n == "NPC_Default_MT" || n == "NPC_Default" || n == "Default" || n == "NPCDefault")) {
						def = mt;
					}
				}
			}
			if (!def) {  // (Noah: the default's name differs here) a bow held but not drawn moves at full pace: drawing matches it
				for (auto* mt : dh->GetFormArray<RE::BGSMovementType>()) {
					if (mt && mt->movementTypeData.typeName.c_str() && std::string_view(mt->movementTypeData.typeName.c_str()) == "NPCBow") {
						def = mt;
					}
				}
			}
			for (auto* mt : dh->GetFormArray<RE::BGSMovementType>()) {
				if (!mt || !mt->movementTypeData.typeName.c_str()) {
					continue;
				}
				const std::string_view n(mt->movementTypeData.typeName.c_str());
				if (def && mt != def && n.find("Bow") != std::string_view::npos && n.find("Horse") == std::string_view::npos && n.find("Sneak") == std::string_view::npos) {
					auto& sp = mt->movementTypeData.defaultData.speeds;
					const float was = sp[RE::Movement::SPEED_DIRECTIONS::kForward][RE::Movement::MaxSpeeds::kRun];
					for (int d = 0; d < RE::Movement::SPEED_DIRECTIONS::kRotations; ++d) {
						for (int m = 0; m < RE::Movement::MaxSpeeds::kTotal; ++m) {
							sp[d][m] = std::max(sp[d][m], def->movementTypeData.defaultData.speeds[d][m]);
						}
					}
					logger::info("bows: {} no longer slows you (run {:.0f} -> {:.0f})", n, was, sp[RE::Movement::SPEED_DIRECTIONS::kForward][RE::Movement::MaxSpeeds::kRun]);
				}
			}
			if (!def) {
				logger::warn("bows: no default movement type found - drawing a bow still slows you");
			}
		}

		// (Noah: "the spells I was using went invisible when I switched from one spell to the other") Skyrim drops a
		// hand's magic art now and then when a spell is swapped in: a moment after any spell goes into your hand it is put
		// in again (the same spell, the same hand), which brings its art back. (Its own re-equip is not answered.)
		class EquipWatch : public RE::BSTEventSink<RE::TESEquipEvent>
		{
		public:
			static EquipWatch* Get()
			{
				static EquipWatch w;
				return &w;
			}
			static inline std::atomic_bool s_mine{ false };
			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				if (!a_event || !a_event->equipped || s_mine.load() || !a_event->actor || !a_event->actor->IsPlayerRef()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(a_event->baseObject);
				if (!sp || sp->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const RE::FormID id = sp->GetFormID();
				std::thread([id] {
					std::this_thread::sleep_for(std::chrono::milliseconds(300));
					Tasks::Add([id] {
						auto* pl = RE::PlayerCharacter::GetSingleton();
						auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(id);
						auto* em = RE::ActorEquipManager::GetSingleton();
						if (!pl || !sp || !em || pl->IsDead()) return;
						for (bool left : { false, true }) {
							if (pl->GetEquippedObject(left) != sp) continue;
							auto* slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(left ? 0x013F43 : 0x013F42);
							s_mine = true;
							em->EquipSpell(pl, sp, slot);
							s_mine = false;
						}
					});
				}).detach();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void OnDataLoaded()
		{
			FreeBowMovement();
			Undead::InstallHooks();
			Hud::InstallInputGate();
			// (Noah: "the hoarfrost casting animation is very bright, hard to see when casting") Mysticism's Hoarfrost
			// wears Skyrim's own Frostbite in the hands (its art and its light), not its blinding own
			if (auto* dh = RE::TESDataHandler::GetSingleton()) {
				auto* frost = RE::TESForm::LookupByID<RE::SpellItem>(0x0002B96C);  // Frostbite
				RE::BGSArtObject* art = nullptr;
				RE::TESObjectLIGH* light = nullptr;
				if (frost) {
					for (auto* e : frost->effects) {
						if (e && e->baseEffect) { art = e->baseEffect->data.castingArt, light = e->baseEffect->data.light; break; }
					}
				}
				if (auto* hf = dh->LookupForm<RE::SpellItem>(0x445FFD, "MysticismMagic.esp"); hf && frost) {
					int n = 0;
					for (auto* e : hf->effects) {
						if (!e || !e->baseEffect) continue;
						e->baseEffect->data.castingArt = art, e->baseEffect->data.light = light, ++n;
					}
					logger::info("spells: Hoarfrost's hands dimmed to Frostbite's ({} effects)", n);
				}
			}
			// Skyrim's own floating quest markers give way to ours (src/Hud.cpp: drawn in the game's style)
			if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
				if (auto* st = prefs->GetSetting("bShowFloatingQuestMarkers:GamePlay")) {
					st->data.b = Hud::kEdgeHud;  // (Edge UI: Skyrim's own markers; they were off for ours)
					logger::info("quests: Skyrim's floating quest markers {}", Hud::kEdgeHud ? "on (Edge UI)" : "off (ours instead)");
				}
			}
			// (no blood splattered over the screen when you are hit - the red edges of the HUD say it: wiped each frame in
			// Movement::Tick, never switched off - with bBloodSplatterEnabled off the game never builds its splatter node, and
			// opening the map, which hides that node, crashed the game)
			DarkenNights();
			OnlyCompendiumScreens();
			// (Noah: "Obtain the Spellsword Manual" still in the journal) the Spellsword mod's start quest never starts on a
			// new game at all (stopping it once it ran didn't hold) - the class gives the Spellsword's kit
			if (auto* q = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x009A24, "Spellsword.esp")) {
				q->data.flags.reset(RE::QuestFlag::kStartsEnabled);
				logger::info("quests: the Spellsword mod's manual quest no longer starts with a new game");
			}
			logger::info("data loaded");
			if (!Forms::Resolve()) {
				logger::error("a form the mashup needs is missing; rounds stay off");
			}
			Audio::InstallSteps();
			Fantasy::Inject();
			Arcana::Load();
			Fantasy::HushAltarEchoes();
			Fantasy::TameSpellSounds();
			Director::HeadlongCombat();
			Settings::ApplyMods();
			if (auto* ini = RE::INIPrefSettingCollection::GetSingleton()) {  // (and no tutorials at all)
				if (auto* t = ini->GetSetting("bShowTutorials:Interface")) {
					t->data.b = false;
				}
			}
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
			PlayerKill::func = vtbl.write_vfunc(0x10E, PlayerKill::thunk);
			logger::info("hooked PlayerCharacter::Update");
			REL::Relocation<std::uintptr_t> hud{ RE::VTABLE_HUDMenu[0] };
			NoticeFilter::func = hud.write_vfunc(0x4, NoticeFilter::thunk);
			if (const auto v = MagicTargetVtbl(RE::VTABLE_Character)) {
				HookHarm(0, v);
			} else {
				logger::warn("no Character MagicTarget vtable found: reading it off a live actor instead");
			}
			if (const auto v = MagicTargetVtbl(RE::VTABLE_PlayerCharacter)) {
				HookHarm(1, v);
			} else {
				logger::warn("no PlayerCharacter MagicTarget vtable found: reading it off you instead");
			}

			auto* events = RE::ScriptEventSourceHolder::GetSingleton();
			events->AddEventSink<RE::TESDeathEvent>(DeathWatch::Get());
			events->AddEventSink<RE::TESLoadGameEvent>(LoadWatch::Get());
			events->AddEventSink<RE::TESActivateEvent>(ActivateWatch::Get());
			events->AddEventSink<RE::TESMagicEffectApplyEvent>(EffectWatch::Get());
			events->AddEventSink<RE::TESSpellCastEvent>(CastWatch::Get());
			events->AddEventSink<RE::TESHitEvent>(HitWatch::Get());
			events->AddEventSink<RE::TESEquipEvent>(EquipWatch::Get());
			Intro::Install();
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(InputWatch::Get());
			Hud::Install();
			logger::info("all hooks installed");
		}
	}

	namespace
	{
		// (Noah: every save crashed as it loaded) a save can hold a reference's extra data twice over (an ExtraGlobal the
		// reference already got from its plugin): the game's loader adds a new one, the list refuses it and hands back
		// nothing, and the loader writes through that nothing. Refused, the one already there is handed back instead -
		// the loader fills that in, and the spare is freed.
		struct LoadExtraAdd
		{
			static RE::BSExtraData* thunk(RE::ExtraDataList* a_list, RE::BSExtraData* a_new)
			{
				auto* r = func(a_list, a_new);
				if (!r && a_list && a_new) {
					const auto type = a_new->GetType();
					if (auto* have = a_list->GetByType(type)) {
						static int said = 0;
						if (said++ < 10) logger::warn("load: a saved extra (type {}) was already on its reference - the one there is used", static_cast<int>(type));
						delete a_new;
						return have;
					}
				}
				if (!r && a_new) {
					// (Noah: "my game crashed after I died" - in this loader, writing through nothing: the list refused the
					// new one and had none of its kind) the new one is handed back unadded - the loader fills it in and
					// it is simply left aside, rather than written through nothing
					static int said = 0;
					if (said++ < 10) logger::warn("load: a saved extra (type {}) had no list to go into - set aside", static_cast<int>(a_new->GetType()));
					return a_new;
				}
				return r;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		// (Noah: the game crashed loading back after every death) the same loader's other branch: the save says the
		// reference already carries its ExtraGlobal, the loader asks the list for it, gets nothing, and writes through
		// that nothing. Missing, a fresh one is made and added for the loader to fill in.
		struct LoadExtraGet
		{
			static RE::BSExtraData* thunk(RE::ExtraDataList* a_list, RE::ExtraDataType a_type)
			{
				auto* r = func(a_list, a_type);
				if (!r && !a_list) {  // (no list at all: the loader fills a spare that belongs to nothing)
					static RE::BSExtraData* spare = RE::BSExtraData::Create<RE::ExtraGlobal>();
					return spare;
				}
				if (!r && a_list) {
					auto* e = RE::BSExtraData::Create<RE::ExtraGlobal>();
					if (e) {
						e->global = nullptr;
						if (!a_list->Add(e)) {
							static int said2 = 0;
							if (said2++ < 10) logger::warn("load: a fresh ExtraGlobal was refused by its list - the loader fills a spare");
						}
						r = e;
						static int said = 0;
						if (said++ < 10) logger::warn("load: a saved ExtraGlobal was missing from its reference (type {}) - a fresh one made", static_cast<int>(a_type));
					}
				}
				return r;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	namespace Events
	{
		void InstallLoadGuard()
		{
			// the ExtraGlobal branch of the reference loader (ID 12095): its call that adds the new extra
			const std::uintptr_t at = REL::ID(12095).address() + 0x55A;
			if (*reinterpret_cast<const std::uint8_t*>(at) != 0xE8) {
				logger::warn("load: the extra-data loader isn't where it was expected - no guard");
				return;
			}
			SKSE::AllocTrampoline(256);  // (the load guard's two calls, and the animation graph's hook: src/Undead.cpp)
			LoadExtraAdd::func = SKSE::GetTrampoline().write_call<5>(at, LoadExtraAdd::thunk);
			// ... and its call that fetches the one already there (+0x522)
			const std::uintptr_t get = REL::ID(12095).address() + 0x522;
			if (*reinterpret_cast<const std::uint8_t*>(get) == 0xE8) {
				LoadExtraGet::func = SKSE::GetTrampoline().write_call<5>(get, LoadExtraGet::thunk);
				logger::info("load: guard on saved extra data installed (add and fetch)");
			} else {
				logger::info("load: guard on saved extra data installed (add only)");
			}
		}
		void InstallMenuWatch()
		{
			RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatch::Get());
			logger::info("watching menus");
		}
	}
}
