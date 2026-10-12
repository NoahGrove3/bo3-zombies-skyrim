#include "BO3Z.h"

namespace bo3z
{
	namespace
	{
		void OnDataLoaded();

		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				try {
					Save::Guard();
					DevTools::RunQueuedInGame();
					Intro::Tick();
					Director::Tick(a_delta);
					Gun::Tick(a_delta);
					Shops::Tick(a_delta);
					Perks::Tick();
					Movement::Tick(a_delta);
					Audio::Tick(a_delta);
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
				if (a_event->menuName == RE::LoadingMenu::MENU_NAME && a_event->opening) {
					Save::ForgetSnapshot();  // whatever comes next may be another game
				}
				if (a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening && dataLoaded) {
					// After a new game, a save from the main menu, or a save loaded in game.
					Save::OnWorldReady();
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
				if (!a_event || !a_event->dead || !a_event->actorDying) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* dying = a_event->actorDying->As<RE::Actor>();
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!dying || !Director::IsRoundUndead(dying)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				Audio::Voice(dying, Audio::VoiceEvent::kDeath);
				Audio::Flesh(dying, true);
				if (a_event->actorKiller.get() != player) {
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
					if (auto* b = e->AsButtonEvent()) {
						if (b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->GetIDCode() == 0 && b->IsDown()) {
							Hud::OnClick();
						}
						if (!Intro::OnButton(b)) {
							if (Shops::OnButton(b)) {
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
				// Skyrim weapons and spells keep up with the rounds: each hit takes a share of a zombie's round health.
				if (a_event && a_event->cause.get() == player && a_event->target) {
					auto* z = a_event->target->As<RE::Actor>();
					if (z && !z->IsDead() && Director::IsRoundUndead(z)) {
						auto* src = RE::TESForm::LookupByID(a_event->source);
						bool  ourSpell = false;
						for (int i = 0; i < static_cast<int>(gen::kSpells.size()); ++i) {
							if (auto* m = Forms::Spell(i); m && src == m) {
								ourSpell = true;
							}
						}
						if (src && !ourSpell && Forms::GunIndexOf(src) < 0) {
							static std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> last;
							const auto now = std::chrono::steady_clock::now();
							auto&      t = last[z->GetFormID()];
							if (now - t > 250ms) {
								t = now;
								float share = gen::kShareSpell;
								if (auto* w = src->As<RE::TESObjectWEAP>()) {
									share = (w->IsBow() || w->IsCrossbow()) ? gen::kShareBow : gen::kShareMelee;
								}
								auto*       av = z->AsActorValueOwner();
								const float dmg = Director::RoundHealth() * share;
								av->DamageActorValue(RE::ActorValue::kHealth, dmg);
								if (av->GetActorValue(RE::ActorValue::kHealth) <= 0) {
									z->KillImpl(player, dmg, true, false);
								}
							}
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
					if (const int sh = Gun::ShoutOfSpell(a_event->spell, level); sh >= 0) {
						Gun::Shout(sh, level);
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
				++n;
			}
			logger::info("world: nights darkened in {} weathers", n);
		}

		void OnDataLoaded()
		{
			DarkenNights();
			logger::info("data loaded");
			if (!Forms::Resolve()) {
				logger::error("a form the mashup needs is missing; rounds stay off");
			}
			Audio::InstallSteps();
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
			logger::info("hooked PlayerCharacter::Update");

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
