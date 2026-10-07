#include "BO3Z.h"

// New games: Bethesda's own quick start (MQQuickstart = 3 makes MQ101 stage 0 run its skip fragment:
// character creation, choose Hadvar or Ralof, start MQ102, complete MQ101). Then we land outside
// Riverwood and show the BO3 crew screen.
namespace bo3z
{
	namespace
	{
		bool g_crewOpen = false;
		int  g_highlight = 0;
		int  g_settle = 0;

		bool FreshQuickstartGame(RE::PlayerCharacter* a_player)
		{
			auto* cal = RE::Calendar::GetSingleton();
			const float days = cal && cal->gameDaysPassed ? cal->gameDaysPassed->value : 99.0f;
			return a_player->GetLevel() <= 1 && days < 2.0f;
		}

		void Choose(int a_crew)
		{
			const auto& c = gen::kCrew[a_crew];
			Save::SetCrew(a_crew);
			if (c.passive == 1) {  // bonus health
				RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, c.value);
			}
			g_crewOpen = false;
			Gun::Give(gen::kGunIdx_sidearm);
			Hud::Banner(std::string(c.label) + ": \"" + std::string(c.quote) + "\"", 6.0f);
			logger::info("crew chosen: {}", c.id);
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

		bool CrewScreenOpen() { return g_crewOpen; }

		void Tick()
		{
			static int dbg = 0;
			if (dbg++ % 1800 == 0) {
				logger::info("intro: {} moved={} crew={}", Forms::IntroDebug(), Save::Flag(Save::kFlagMoved), Save::Crew());
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* intro = Forms::IntroQuest();
			auto* ui = RE::UI::GetSingleton();
			if (!player || !intro || !ui || ui->GameIsPaused()) {
				return;
			}
			if (!Save::Flag(Save::kFlagMoved) && Forms::IntroDone()) {
				if (FreshQuickstartGame(player)) {
					if (++g_settle < 60) {  // let the skip fragment finish (race menu, choose sides) before moving
						return;
					}
					if (auto* marker = Forms::RiverwoodMarker()) {
						player->MoveTo(marker);
						logger::info("intro: moved the new character to Riverwood");
					}
				}
				Save::SetFlag(Save::kFlagMoved, true);
			}
			if (Save::Flag(Save::kFlagMoved) && Forms::IntroDone() && Save::Crew() < 0 && !g_crewOpen) {
				g_crewOpen = true;
				g_highlight = 0;
				logger::info("intro: crew screen open");
			}
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.crewScreen = g_crewOpen;
			h.crewHighlight = g_highlight;
			h.crew = Save::Crew();
		}

		bool OnButton(const RE::ButtonEvent* a_event)
		{
			if (!g_crewOpen || !a_event->IsDown() || a_event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
				return g_crewOpen;  // swallow everything else for gun/knife while the screen is up
			}
			const auto code = a_event->GetIDCode();
			const std::array<std::uint32_t, 4> keys{ gen::kKey2_crew1, gen::kKey2_crew2, gen::kKey2_crew3, gen::kKey2_crew4 };
			for (int i = 0; i < 4; ++i) {
				if (code == keys[i]) {
					g_highlight = i;
				}
			}
			if (code == gen::kKey2_confirm) {
				Choose(g_highlight);
			}
			return true;
		}
	}
}
