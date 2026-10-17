#include "BO3Z.h"

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

		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
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
				try {
					T("save");
					Save::Guard();
					DevTools::RunQueuedInGame();
					T("intro");
					Intro::Tick();
					T("director");
					Director::Tick(a_delta);
					T("gun");
					Gun::Tick(a_delta);
					T("shops");
					Shops::Tick(a_delta);
					T("perks");
					Perks::Tick();
					T("party");
					Party::Tick(a_delta);
					T("compass");
					Compass::Tick(a_delta);
					T("loot");
					Loot::Tick(a_delta);
					T("movement");
					Movement::Tick(a_delta);
					T("audio");
					Audio::Tick(a_delta);
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
				if (a_event->opening && a_event->menuName == RE::SleepWaitMenu::MENU_NAME && dataLoaded) {
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
					std::erase_if(seen, [&](const auto& e) { return now - e.second > std::chrono::seconds(30); });
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
					}
					return RE::BSEventNotifyControl::kContinue;  // dawn, followers, guards: no points
				}
				const auto [gun, head] = Gun::RecentHit(dying->GetFormID());
				Points::Add(gun ? (head ? gen::kPoints_headshot_kill : gen::kPoints_kill) : gen::kPoints_melee_kill);
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
					}
					if (auto* b = e->AsButtonEvent()) {
						if (b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->GetIDCode() == 0 && b->IsDown()) {
							Hud::OnClick();
						}
						Hud::OnMenuButton(b);
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
					Shops::OnActivate(a_event->objectActivated.get());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// one hit a quarter second on each body (a spell's several effects and its hit all land at once)
		bool Limit(RE::Actor* a_actor)
		{
			static std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> last;
			const auto now = std::chrono::steady_clock::now();
			auto&      t = last[a_actor->GetFormID()];
			if (now - t <= 250ms) {
				return false;
			}
			t = now;
			return true;
		}

		void HurtUndead(RE::Actor* a_actor, float a_damage)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* av = a_actor->AsActorValueOwner();
			av->DamageActorValue(RE::ActorValue::kHealth, a_damage);
			if (av->GetActorValue(RE::ActorValue::kHealth) <= 0) {
				a_actor->KillImpl(player, a_damage, true, false);
			}
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
			static constexpr float kTier[4] = { 1.0f, 1.3f, 1.7f, 2.2f };
			const int   tier = std::clamp(Arcana::TierOf(a_index), 0, 3);
			const float grow = (1.0f + gen::kForgeStep * Arcana::Level(a_index)) * (1.0f + 0.5f * Save::PerkStacks(gen::kPerk_twinstrike));
			if (Director::IsRoundUndead(a_actor)) {
				if (Limit(a_actor)) {
					HurtUndead(a_actor, Director::RoundHealth() * gen::kShareSpell * kTier[tier] * grow);
				}
				return;
			}
			if (Arcana::Damages(a_index) || a_actor->IsPlayerTeammate() || !a_actor->IsHostileToActor(player) || !Limit(a_actor)) {
				return;
			}
			static constexpr float kPart[4] = { 0.07f, 0.09f, 0.12f, 0.16f };
			const float max = a_actor->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth);
			HurtUndead(a_actor, std::max(8.0f, max * kPart[tier] * grow));
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
				if (a_event && player && a_event->cause.get() == player && a_event->target.get() == player) {
					Perks::SelfHit();  // (your own spells never hurt you)
				}
				// Skyrim weapons and spells keep up with the rounds: each hit takes a share of a zombie's round health.
				if (a_event && a_event->cause.get() == player && a_event->target) {
					auto* z = a_event->target->As<RE::Actor>();
					auto* src = RE::TESForm::LookupByID(a_event->source);
					if (const int ci = Arcana::IndexOf(src); z && ci >= 0) {
						CofferHit(z, ci);  // the Coffer's spells: every one of them hurts every enemy
					} else if (z && !z->IsDead() && Director::IsRoundUndead(z)) {
						bool ourSpell = false;
						for (int i = 0; i < static_cast<int>(gen::kSpells.size()); ++i) {
							if (auto* m = Forms::Spell(i); m && src == m) {
								ourSpell = true;
							}
						}
						if (src && !ourSpell && Forms::GunIndexOf(src) < 0 && Limit(z)) {
							float share = gen::kShareSpell;
							if (auto* w = src->As<RE::TESObjectWEAP>()) {
								share = (w->IsBow() || w->IsCrossbow()) ? gen::kShareBow : gen::kShareMelee;
							} else if (src->Is(RE::FormType::Spell)) {
								share *= 1.0f + 0.5f * Save::PerkStacks(gen::kPerk_twinstrike);
							}
							HurtUndead(z, Director::RoundHealth() * share);
						}
					}
				}
				if (a_event && a_event->target.get() == RE::PlayerCharacter::GetSingleton() && a_event->cause) {
					if (auto* z = a_event->cause->As<RE::Actor>(); z && Director::IsRoundUndead(z)) {
						Perks::OnZombieHit(z);
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
					if (Movement::OnSpellCast(a_event->spell)) {
						// (Bond II's Updraft)
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
						CofferHit(a_event->target->As<RE::Actor>(), ci);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// The nights are a little darker (Noah: 'make night time a little bit darker'): every weather's night light -
		// ambient, moon light, the directional ambient, fog, sky and horizon - turned down once at start-up. Day,
		// dusk and dawn are untouched; whatever weather mod is installed is darkened the same way.
		void DarkenNights()
		{
			auto  scale = [](RE::Color& c, float f) { c.red = static_cast<std::uint8_t>(c.red * f), c.green = static_cast<std::uint8_t>(c.green * f), c.blue = static_cast<std::uint8_t>(c.blue * f); };
			using CT = RE::TESWeather::ColorTypes;
			constexpr auto N = RE::TESWeather::ColorTimes::kNight;
			int n = 0;
			for (auto* w : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESWeather>()) {
				if (!w) {
					continue;
				}
				for (auto t : { CT::kAmbient, CT::kSunlight, CT::kEffectLighting, CT::kCloudLODAmbient, CT::kCloudLODDiffuse, CT::kSkyStatics }) {
					scale(w->colorData[t][N], 0.72f);
				}
				for (auto t : { CT::kFogNear, CT::kFogFar, CT::kSkyLower, CT::kHorizon, CT::kSkyUpper }) {
					scale(w->colorData[t][N], 0.85f);
				}
				auto& d = w->directionalAmbientLightingColors[N].directional;
				for (auto* c : { &d.x.max, &d.x.min, &d.y.max, &d.y.min, &d.z.max, &d.z.min }) {
					scale(*c, 0.72f);
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
			logger::info("world: nights darkened, days dimmed, in {} weathers", n);
		}

		void OnDataLoaded()
		{
			// Skyrim's own floating quest markers give way to ours (src/Hud.cpp: drawn in the game's style)
			if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
				if (auto* st = prefs->GetSetting("bShowFloatingQuestMarkers:GamePlay")) {
					st->data.b = false;
					logger::info("quests: Skyrim's floating quest markers off (ours instead)");
				}
			}
			// no blood splattered over the screen when you are hit (the red edges of the HUD say it) - drawn invisible, not
			// switched off: with bBloodSplatterEnabled off the game never builds its splatter node, and opening the map
			// (which hides that node) crashed the game
			if (auto* ini = RE::INISettingCollection::GetSingleton()) {
				int n = 0;
				for (const char* name : { "fBloodSplatterMaxOpacity:ScreenSplatter", "fBloodSplatterMaxOpacity2:ScreenSplatter", "fBloodSplatterMinOpacity:ScreenSplatter",
						 "fBloodSplatterMinOpacity2:ScreenSplatter", "fBloodSplatterCountBase:ScreenSplatter", "fBloodSplatterCountRandomMargin:ScreenSplatter" }) {
					if (auto* st = ini->GetSetting(name)) st->data.f = 0.0f, ++n;
				}
				logger::info("hud: Skyrim's blood splatter on the screen made invisible ({} settings)", n);
			}
			DarkenNights();
			logger::info("data loaded");
			if (!Forms::Resolve()) {
				logger::error("a form the mashup needs is missing; rounds stay off");
			}
			Audio::InstallSteps();
			Arcana::Load();
			Director::HeadlongCombat();
			Settings::ApplyMods();
			if (auto* ini = RE::INIPrefSettingCollection::GetSingleton()) {  // (and no tutorials at all)
				if (auto* t = ini->GetSetting("bShowTutorials:Interface")) {
					t->data.b = false;
				}
			}
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
			logger::info("hooked PlayerCharacter::Update");
			REL::Relocation<std::uintptr_t> hud{ RE::VTABLE_HUDMenu[0] };
			NoticeFilter::func = hud.write_vfunc(0x4, NoticeFilter::thunk);

			auto* events = RE::ScriptEventSourceHolder::GetSingleton();
			events->AddEventSink<RE::TESDeathEvent>(DeathWatch::Get());
			events->AddEventSink<RE::TESLoadGameEvent>(LoadWatch::Get());
			events->AddEventSink<RE::TESActivateEvent>(ActivateWatch::Get());
			events->AddEventSink<RE::TESMagicEffectApplyEvent>(EffectWatch::Get());
			events->AddEventSink<RE::TESSpellCastEvent>(CastWatch::Get());
			events->AddEventSink<RE::TESHitEvent>(HitWatch::Get());
			Intro::Install();
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(InputWatch::Get());
			Hud::Install();
			logger::info("all hooks installed");
		}
	}

	namespace Events
	{
		void InstallMenuWatch()
		{
			RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatch::Get());
			logger::info("watching menus");
		}
	}
}
