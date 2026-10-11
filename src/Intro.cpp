#include "BO3Z.h"

// New games: Bethesda's own quick start (MQQuickstart = 3 makes MQ101 stage 0 run its skip fragment:
// character creation, choose Hadvar or Ralof, start MQ102, complete MQ101). Then we land outside
// Riverwood with the starting staff (no crew picker: you play your own character).
namespace bo3z
{
	namespace
	{
		int  g_settle = 0;

		bool FreshQuickstartGame(RE::PlayerCharacter* a_player)
		{
			auto* cal = RE::Calendar::GetSingleton();
			const float days = cal && cal->gameDaysPassed ? cal->gameDaysPassed->value : 99.0f;
			return a_player->GetLevel() <= 1 && days < 2.0f;
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

		bool CrewScreenOpen() { return false; }  // the BO3 crew picker is gone: you play your own character

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
			// No crew pick any more: you play your own Skyrim character. First time in, you get the starting wand.
			if (Save::Flag(Save::kFlagMoved) && Forms::IntroDone() && !Save::Flag(Save::kFlagStarted)) {
				if (Save::Crew() >= 0) {
					Save::SetCrew(-1);  // older saves: drop the crew character
				} else {
					Gun::Give(gen::kGunIdx_sidearm);
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
