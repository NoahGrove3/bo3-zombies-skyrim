#include "CurseOfAlduin.h"
#include "CofferSpells.h"

// (Noah, 2026-11-26: "next I want to play in the actual game and test everything. So come up with a kit that I could use to test
// everything in all of the different tiers of weapons" - then: a power you cast, there only when turned on) the Tester's Kit: a lesser
// power that is yours while Data/SKSE/Plugins/CurseOfAlduin_Test.ini sits there (take the file away and the power goes with it - a
// player from the Nexus never has it). Cast, it opens Skyrim's own message boxes:
//   Weapons     a class, then its weapon, flasks and armour at Base, I, II, III or an Ascend; the relics, the Tablet's weapons, the wonders
//   The night   start it now, or as night 5, 10, 15 or 20
//   Stay alive  god mode, endless magicka and stamina, +50,000 points
//   The dead    10 or 30 of them before you, or any of the four trials at the city's Echo Altar
namespace coa::Tester
{
	namespace
	{
		constexpr const char* kFile = "Data/SKSE/Plugins/CurseOfAlduin_Test.ini";
		bool  g_on = false;        // the file was there at the last look
		bool  g_endless = false;   // endless magicka and stamina
		float g_look = 0;

		RE::SpellItem* Power()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm<RE::SpellItem>(gen::kTesterKitSpell, gen::kPlugin) : nullptr;
		}

		// one of Skyrim's message boxes; a_pick gets the button's index (on the main thread)
		class Pick : public RE::IMessageBoxCallback
		{
		public:
			explicit Pick(std::function<void(int)> a_fn) : fn(std::move(a_fn)) {}
			void Run(std::uint8_t a_button) override
			{
				auto f = fn;
				const int i = a_button;
				logger::info("tester: button {}", i);
				Tasks::Add([f, i]() { f(i); });
			}
			std::function<void(int)> fn;
		};
		void Ask(const std::string& a_text, const std::vector<std::string>& a_buttons, std::function<void(int)> a_pick)
		{
			auto* mgr = RE::MessageDataFactoryManager::GetSingleton();
			auto* str = RE::InterfaceStrings::GetSingleton();
			auto* make = mgr && str ? mgr->GetCreator<RE::MessageBoxData>(str->messageBoxData) : nullptr;
			auto* box = make ? make->Create() : nullptr;
			if (!box) {
				logger::warn("tester: no message box could be made");
				return;
			}
			box->bodyText = RE::BSString(a_text.c_str());
			for (const auto& b : a_buttons) box->buttonText.push_back(RE::BSString(b.c_str()));
			box->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(new Pick(std::move(a_pick)));
			box->buttonPressOffset = 0;
			box->cancelButtonIndex = static_cast<std::int32_t>(a_buttons.size()) - 1;   // (Escape: the last, Back or Close)
			box->isCancellable = true;
			box->verticalButtons = a_buttons.size() > 4;
			box->menuDepth = 10;
			using Queue = void(RE::MessageBoxData*);
			static REL::Relocation<Queue> queue{ RELOCATION_ID(51422, 52271) };
			queue(box);
		}

		void Main();
		void Give(RE::TESBoundObject* a_item)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (p && a_item && p->GetItemCount(a_item) <= 0) p->AddObjectToContainer(a_item, nullptr, 1, nullptr);
		}

		// ---- Weapons
		void Tier(int a_cls)
		{
			static const std::vector<std::string> kTiers{ "Base", "Tier I", "Tier II", "Tier III", "Ascend 1", "Ascend 10 (the last)", "Back" };
			Ask(std::string(coffer::kClassName[a_cls]) + ": its weapon, flasks and armour at which tier?", kTiers, [a_cls](int t) {
				if (t < 0 || t >= 6) return Main();
				const int tier = t <= 4 ? t : 13;
				if (Classes::Get() != a_cls) Classes::Choose(a_cls);
				Kit::DevGiveClass(a_cls, tier);
				Hud::Banner(fmt::format("TESTER'S KIT: {} at {}", coffer::kClassName[a_cls], kTiers[t]), 4.0f);
			});
		}
		void Relics()
		{
			Ask("Which?", { "The ten Dragon Priest relics", "The Tablet's weapons", "The wonders, the Dragon Slayer and Alduin's Bane", "Back" }, [](int i) {
				auto* dh = RE::TESDataHandler::GetSingleton();
				auto* p = RE::PlayerCharacter::GetSingleton();
				if (!dh || !p) return;
				switch (i) {
				case 0:
					for (const auto& b : gen::kCityBosses) {
						auto* w = dh->LookupForm<RE::TESObjectWEAP>(b.weapon, gen::kPlugin);
						if (w && !w->IsBow() && !w->IsCrossbow()) Give(w);
					}
					Hud::Banner("TESTER'S KIT: the ten Dragon Priest relics are in your pack", 4.0f);
					break;
				case 1:
					Tablet::DevKit();
					Hud::Banner("TESTER'S KIT: the Tablet's weapons are in your pack", 4.0f);
					break;
				case 2:
					for (int c = 0; c < 4; ++c) Classes::DevGiveWonder(c);
					Echo::DevGiveBane();
					Give(dh->LookupForm<RE::TESObjectWEAP>(gen::kSlayerWeap, gen::kPlugin));
					Hud::Banner("TESTER'S KIT: the four wonders, the Dragon Slayer and Alduin's Bane", 4.0f);
					break;
				default: Main(); break;
				}
			});
		}
		void Weapons()
		{
			Ask("Weapons: a class at a tier, or the relics and wonders", { coffer::kClassName[0], coffer::kClassName[1], coffer::kClassName[2], coffer::kClassName[3], "Relics, Tablet weapons, wonders", "Back" },
				[](int i) {
					if (i >= 0 && i < 4) return Tier(i);
					if (i == 4) return Relics();
					Main();
				});
		}

		// ---- The night
		void Night()
		{
			Ask("The night", { "Start the night now", "Night 5", "Night 10", "Night 15", "Night 20", "Back" }, [](int i) {
				static constexpr int kRound[5] = { 0, 5, 10, 15, 20 };
				if (i < 0 || i >= 5) return Main();
				Director::DevNight(kRound[i]);
				Hud::Banner(i == 0 ? std::string("TESTER'S KIT: the night falls") : fmt::format("TESTER'S KIT: night {} falls", kRound[i]), 4.0f);
			});
		}

		// ---- Stay alive
		void God(bool a_on)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || a_on == RE::PlayerCharacter::IsGodMode()) return;
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>()) {
				if (auto* script = factory->Create()) {
					script->SetCommand("tgm");
					script->CompileAndRun(p);
					delete script;
				}
			}
		}
		void Alive()
		{
			const bool god = RE::PlayerCharacter::IsGodMode();
			Ask("Stay alive", { god ? "God mode: turn it off" : "God mode: turn it on", g_endless ? "Endless magicka and stamina: off" : "Endless magicka and stamina: on", "+50,000 points", "Back" },
				[god](int i) {
					switch (i) {
					case 0:
						God(!god);
						Hud::Banner(RE::PlayerCharacter::IsGodMode() ? "TESTER'S KIT: god mode on" : "TESTER'S KIT: god mode off", 3.0f);
						break;
					case 1:
						g_endless = !g_endless;
						Hud::Banner(g_endless ? "TESTER'S KIT: endless magicka and stamina" : "TESTER'S KIT: magicka and stamina as they are", 3.0f);
						break;
					case 2:
						Points::Refund(50000);
						Hud::Banner("TESTER'S KIT: +50,000", 3.0f);
						break;
					default: Main(); break;
					}
				});
		}

		// ---- The dead
		void Dead()
		{
			std::vector<std::string> b{ "10 of the dead", "30 of the dead" };
			for (int t = 0; t < 4; ++t) b.push_back("Trial: " + Curse::TrialTitle(t));
			b.push_back("Back");
			Ask("The dead: before you, or a trial at this city's Echo Altar", b, [](int i) {
				if (i == 0 || i == 1) {
					Director::DevSpawn("zombie", i == 0 ? 10 : 30);
				} else if (i >= 2 && i < 6) {
					Curse::DevTrial(i - 2);
				} else {
					Main();
				}
			});
		}

		void Main()
		{
			Ask("Tester's Kit", { "Weapons", "The night", "Stay alive", "The dead", "Close" }, [](int i) {
				switch (i) {
				case 0: Weapons(); break;
				case 1: Night(); break;
				case 2: Alive(); break;
				case 3: Dead(); break;
				default: break;
				}
			});
		}
	}

	bool OnCast(RE::FormID a_spell)
	{
		auto* sp = Power();
		if (!sp || a_spell != sp->GetFormID()) return false;
		if (!g_on) return true;
		Main();
		return true;
	}

	void Tick(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !p->Is3DLoaded()) return;
		if ((g_look -= a_delta) <= 0) {
			g_look = 2.0f;
			std::error_code ec;
			const bool on = std::filesystem::exists(kFile, ec);
			auto* sp = Power();
			if (sp) {
				const bool has = p->HasSpell(sp);
				if (on && !has) p->AddSpell(sp), logger::info("tester: the Tester's Kit is yours ({} is there)", kFile);
				else if (!on && has) p->RemoveSpell(sp), logger::info("tester: the Tester's Kit taken back (no {})", kFile);
			}
			if (on != g_on && !on) g_endless = false;
			g_on = on;
		}
		if (g_endless && g_on) {
			auto* av = p->AsActorValueOwner();
			for (auto v : { RE::ActorValue::kMagicka, RE::ActorValue::kStamina }) {
				const float want = av->GetPermanentActorValue(v);
				if (const float now = av->GetActorValue(v); now < want) av->RestoreActorValue(v, want - now);
			}
		}
	}
}
