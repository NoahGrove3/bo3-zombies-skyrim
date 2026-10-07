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
					DevTools::RunQueuedInGame();
					Intro::Tick();
					Director::Tick(a_delta);
					Gun::Tick(a_delta);
					Shops::Tick(a_delta);
					Perks::Tick();
					Movement::Tick(a_delta);
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
				// "Data loaded" without the Script Extender: the first menu event once the player form exists.
				if (!dataLoaded && RE::TESForm::LookupByID(0x7)) {
					dataLoaded = true;
					OnDataLoaded();
				}
				if (dataLoaded) {
					Intro::Install();  // keep MQQuickstart at 3 for the next new game
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
						if (!Intro::OnButton(b)) {
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
					Shops::OnActivate(a_event->objectActivated.get());
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

		void OnDataLoaded()
		{
			logger::info("data loaded");
			if (!Forms::Resolve()) {
				logger::error("a form the mashup needs is missing; rounds stay off");
			}
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
			logger::info("hooked PlayerCharacter::Update");

			auto* events = RE::ScriptEventSourceHolder::GetSingleton();
			events->AddEventSink<RE::TESDeathEvent>(DeathWatch::Get());
			events->AddEventSink<RE::TESLoadGameEvent>(LoadWatch::Get());
			events->AddEventSink<RE::TESActivateEvent>(ActivateWatch::Get());
			events->AddEventSink<RE::TESMagicEffectApplyEvent>(EffectWatch::Get());
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
