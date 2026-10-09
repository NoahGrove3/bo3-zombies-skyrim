#include "BO3Z.h"

// The Undying Dirge (sheet: curse). At Helgen, Alduin's word raised every grave in Skyrim; the curse is rooted in the
// cities of the holds. Each city has four night quests, in order:
//   0 The Ashen Tithe      slay the risen within the city's bounds
//   1 The Ward-Fires       kindle three braziers around the city's station (stand by each until it catches)
//   2 The Vigil            hold the station while the night pushes harder
//   3 The Herald in Fog    fog rolls in, Grimmvar rises; slay it and the city is free
// then the city is freed: a sanctuary where no undead rise. Progress: one global per city (step*1000 + progress).
namespace bo3z
{
	namespace
	{
		constexpr int kFreed = 4;

		RE::TESGlobal* Global(std::uint32_t a_id)
		{
			return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(a_id, gen::kPlugin);
		}
		int Value(int a_town)
		{
			auto* g = Global(gen::kCurseCities[a_town].global);
			return g ? static_cast<int>(std::lround(g->value)) : 0;
		}
		int  Step(int a_town) { return std::clamp(Value(a_town) / 1000, 0, kFreed); }
		int  Prog(int a_town) { return Value(a_town) % 1000; }
		void Set(int a_town, int a_step, int a_prog)
		{
			if (auto* g = Global(gen::kCurseCities[a_town].global)) {
				g->value = static_cast<float>(a_step * 1000 + std::clamp(a_prog, 0, 999));
			}
		}

		std::string Upper(std::string s)
		{
			for (auto& c : s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return s;
		}
		void Replace(std::string& s, std::string_view what, const std::string& with)
		{
			for (std::size_t p = 0; (p = s.find(what, p)) != std::string::npos; p += with.size()) {
				s.replace(p, what.size(), with);
			}
		}
		std::string Fill(std::string_view a_text, int a_town, int a_n = 0)
		{
			std::string s(a_text);
			const std::string city(gen::kTowns[a_town]);
			Replace(s, "{city}", city);
			Replace(s, "{CITY}", Upper(city));
			Replace(s, "{landmark}", std::string(gen::kCurseCities[a_town].landmark));
			Replace(s, "{n}", std::to_string(a_n));
			return s;
		}

		// Quest news lives in the tracker (top right): a line under the goal for a few seconds. No pop-ups.
		void Note(std::string a_text, float a_seconds = 5.0f)
		{
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().questNote = std::move(a_text);
			Hud::Edit().questNoteTime = a_seconds;
		}
		// The lore lives in a book, "The Undying Dirge", in the player's pack.
		bool GiveBook(RE::PlayerCharacter* a_player)
		{
			auto* book = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectBOOK>(gen::kDirgeBook, gen::kPlugin);
			if (!book || !a_player) {
				return false;
			}
			auto inv = a_player->GetInventory([&](RE::TESBoundObject& o) { return &o == book; });
			if (!inv.empty() && inv.begin()->second.first > 0) {
				return false;
			}
			a_player->AddObjectToContainer(book, nullptr, 1, nullptr);
			return true;
		}

		int   g_zone = -1;
		float g_zoneTimer = 0;
		std::unordered_set<int> g_loreShown;  // town*10+step, this session
		int   g_heraldTown = -1;
		bool  g_holding = false;
		float g_holdAcc = 0;

		struct Ward
		{
			RE::ObjectRefHandle brazier, fire, light;
			RE::NiPoint3        pos;
		};
		std::array<Ward, 3> g_wards{};
		int                 g_wardTown = -1;
		float               g_channel = 0;

		// The city the player is in: its own location (streets and interiors), or within reach of its station.
		int ComputeZone(RE::PlayerCharacter* a_player)
		{
			if (auto* loc = a_player->GetCurrentLocation()) {
				for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
					auto* city = RE::TESForm::LookupByID<RE::BGSLocation>(gen::kCurseCities[t].location);
					for (auto* l = loc; l && city; l = l->parentLoc) {
						if (l == city) {
							return t;
						}
					}
				}
			}
			auto*       ws = a_player->GetWorldspace();
			const auto  p = a_player->GetPosition();
			int         best = -1;
			float       bestD = gen::kCurseZone;
			for (int t = 0; ws && t < static_cast<int>(gen::kTowns.size()); ++t) {
				RE::NiPoint3        c;
				float               yaw = 0;
				RE::TESWorldSpace*  w = nullptr;
				Shops::StandFrame(t, c, yaw, w);
				if (w != ws) {
					continue;
				}
				const float d = std::hypot(c.x - p.x, c.y - p.y);
				if (d < bestD) {
					bestD = d, best = t;
				}
			}
			return best;
		}

		void Remove(RE::ObjectRefHandle& h)
		{
			if (auto r = h.get()) {
				r->Disable();
				r->SetDelete(true);
			}
			h = {};
		}
		void RemoveWards()
		{
			for (auto& w : g_wards) {
				Remove(w.brazier), Remove(w.fire), Remove(w.light);
			}
			g_wardTown = -1;
			g_channel = 0;
		}

		RE::ObjectRefHandle PlaceAt(RE::PlayerCharacter* a_player, std::uint32_t a_id, const RE::NiPoint3& a_at, float a_scale)
		{
			auto* base = Forms::Ours(a_id);
			auto  ref = base ? a_player->PlaceObjectAtMe(base, false) : nullptr;
			if (!ref) {
				return {};
			}
			if (a_scale != 1.0f) {
				ref->SetScale(a_scale);
			}
			ref->SetPosition(a_at);
			return ref->CreateRefHandle();
		}

		void Kindle(RE::PlayerCharacter* a_player, Ward& w)
		{
			if (!w.fire.get()) {
				w.fire = PlaceAt(a_player, gen::kWardFire, w.pos + RE::NiPoint3{ 0, 0, 81.0f }, 0.42f);
			}
			if (!w.light.get()) {
				w.light = PlaceAt(a_player, gen::kWardLight, w.pos + RE::NiPoint3{ 0, 0, 125.0f }, 1.0f);
			}
		}

		// Three cold braziers round the station (left, right, out front), lit ones burning.
		void EnsureWards(RE::PlayerCharacter* a_player, int a_town)
		{
			if (g_wardTown == a_town && g_wards[0].brazier.get()) {
				return;
			}
			RemoveWards();
			RE::NiPoint3       c;
			float              yaw = 0;
			RE::TESWorldSpace* ws = nullptr;
			if (!Shops::StandFrame(a_town, c, yaw, ws) || ws != a_player->GetWorldspace()) {
				return;
			}
			// leftovers from a save: cold braziers (and their fires) near the station that this session didn't place
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, 9000.0f, [&](RE::TESObjectREFR* r) {
				auto* b = r ? r->GetBaseObject() : nullptr;
				if (b && (b == Forms::Ours(gen::kWardBrazier) || b == Forms::Ours(gen::kWardFire) || b == Forms::Ours(gen::kWardLight)) && !r->IsDeleted()) {
					r->Disable();
					r->SetDelete(true);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			constexpr float dirs[3][2] = { { -1.0f, 0.1f }, { 1.0f, 0.1f }, { 0.0f, -1.0f } };  // plan frame: the station faces -y
			const int bits = Prog(a_town);
			for (int i = 0; i < 3; ++i) {
				const float x = dirs[i][0] * gen::kCurseWardDistance, y = dirs[i][1] * gen::kCurseWardDistance;
				RE::NiPoint3 at{ c.x + x * std::cos(yaw) + y * std::sin(yaw), c.y - x * std::sin(yaw) + y * std::cos(yaw), c.z };
				float        land = 0;
				if (RE::TES::GetSingleton()->GetLandHeight(at, land)) {
					at.z = land;
				}
				at.z += 25.0f;  // the brazier's pedestal sits on the ground
				g_wards[i].pos = at;
				g_wards[i].brazier = PlaceAt(a_player, gen::kWardBrazier, at, 0.75f);
				if ((bits >> i) & 1) {
					Kindle(a_player, g_wards[i]);
				}
			}
			g_wardTown = a_town;
			logger::info("curse: {} ward-fires placed ({} lit)", gen::kTowns[a_town], std::popcount(static_cast<unsigned>(bits & 7)));
		}

		void Advance(int a_town)
		{
			const int s = Step(a_town);
			const auto& st = gen::kCurseSteps[std::clamp(s, 0, 3)];
			Set(a_town, s + 1, 0);
			Forms::Play(gen::Sound::quest_step);
			Note(Fill(st.title, a_town) + " - complete", 6.0f);
			Points::Add(500 * (s + 1));
			logger::info("curse: {} step {} ({}) done", gen::kTowns[a_town], s, st.id);
		}

		// The city's shout (sheet: shouts): what freeing it earns, claimed at the Shrine of the Dirge.
		int ShoutOf(int a_town)
		{
			for (int i = 0; i < static_cast<int>(gen::kShouts.size()); ++i) {
				if (gen::kShouts[i].town == a_town) {
					return i;
				}
			}
			return -1;
		}

		int Relic(int a_town)
		{
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (gen::kGuns[i].relicTown == a_town && !gen::kGuns[i].retired) {
					return i;
				}
			}
			return -1;
		}

		// The relic hangs turning over the Shrine of the Dirge once the city is freed.
		std::unordered_map<int, RE::ObjectRefHandle> g_relicShow;
		float                                         g_relicTimer = 0;
		void TickRelicShow(RE::PlayerCharacter* a_player, float a_delta)
		{
			for (auto& [t, h] : g_relicShow) {
				if (auto r = h.get(); r && r->Is3DLoaded()) {
					auto a = r->GetAngle();
					r->SetAngle({ a.x, a.y, a.z + 0.9f * a_delta });
				}
			}
			if ((g_relicTimer -= a_delta) > 0) {
				return;
			}
			g_relicTimer = 2.0f;
			const int z = g_zone;
			const int relic = z >= 0 ? Relic(z) : -1;
			if (relic < 0 || Step(z) < kFreed) {
				return;
			}
			auto* altar = Shops::Placed(z, 6);
			if (!altar || (g_relicShow.contains(z) && g_relicShow[z].get())) {
				return;
			}
			auto* base = Forms::Ours(gen::kShowStat[relic]);
			if (!base) {
				return;
			}
			const auto at = altar->GetPosition() + RE::NiPoint3{ 0, 0, 105.0f };
			RE::TES::GetSingleton()->ForEachReferenceInRange(altar, 400.0f, [&](RE::TESObjectREFR* r) {  // one from an earlier session
				if (r && r->GetBaseObject() == base && !r->IsDeleted()) {
					r->Disable();
					r->SetDelete(true);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (auto ref = a_player->PlaceObjectAtMe(base, false)) {
				ref->SetPosition(at);
				ref->SetAngle({ 0, 0, altar->GetAngleZ() });
				g_relicShow[z] = ref->CreateRefHandle();
			}
		}

		void Lift(RE::PlayerCharacter* a_player, int a_town)
		{
			Set(a_town, kFreed, 0);
			g_holding = false;
			Forms::Play(gen::Sound::curse_lifted);
			std::string text = Fill(gen::kCurseLiftedBanner, a_town);
			if (const int sh = ShoutOf(a_town); sh >= 0) {
				// its shout waits on the Shrine of the Dirge at the station (nothing is forced into your hands)
				text += " - its Thu'um, " + std::string(gen::kShouts[sh].label) + ", waits at the Shrine of the Dirge";
			}
			Note(text, 10.0f);
			Director::EndNight();  // the city's last rite ends the night: dawn breaks, the next night brings the next round
			Director::CrumbleNear(a_player->GetPosition(), 1.0e9f);
			Points::Add(5000);
			logger::info("curse: {} is freed", gen::kTowns[a_town]);
			bool all = true;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				all &= Step(t) >= kFreed;
			}
			if (all) {
				Note("Every city is free. The Dirge echoes only in the wilds.", 12.0f);
			}
		}

		// Where the quest wants you now (in your current world): the next ward-fire, the shrine to hold, the Herald,
		// a freed city's waiting shout, or from the wilds the nearest city still under the Dirge.
		bool ObjectiveTarget(RE::PlayerCharacter* a_player, RE::NiPoint3& a_at, std::string& a_label)
		{
			const int z = g_zone;
			if (z >= 0) {
				const int s = Step(z);
				if (s >= kFreed) {
					const int sh = ShoutOf(z);
					if (sh >= 0 && !Gun::KnowsShout(sh)) {
						if (auto* altar = Shops::Placed(z, 6)) {
							a_at = altar->GetPosition() + RE::NiPoint3{ 0, 0, 110.0f };
							a_label = "Claim " + std::string(gen::kShouts[sh].label);
							return true;
						}
					}
					return false;
				}
				switch (s) {
				case 1: {  // the nearest ward-fire still cold
					const int bits = Prog(z);
					float     best = FLT_MAX;
					for (int i = 0; i < 3; ++i) {
						if (!((bits >> i) & 1) && g_wards[i].brazier.get()) {
							const float d = g_wards[i].pos.GetDistance(a_player->GetPosition());
							if (d < best) {
								best = d, a_at = g_wards[i].pos + RE::NiPoint3{ 0, 0, 120.0f };
							}
						}
					}
					if (best < FLT_MAX) {
						a_label = "Ward-fire";
						return true;
					}
					break;
				}
				case 3:
					if (Director::BossPos(a_at)) {
						a_label = "The Herald";
						return true;
					}
					break;
				default:
					break;
				}
				if (Shops::StationTarget(z, a_at)) {
					a_label = s == 2 ? "Hold the shrine" : s == 0 ? "The Dirge's heart" : "The shrine";
					return true;
				}
				return false;
			}
			// out in the wilds: the nearest discovered city whose rites are unfinished, or whose shout waits
			float best = FLT_MAX;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				const int  sh = ShoutOf(t);
				const bool wants = Step(t) < kFreed || (sh >= 0 && !Gun::KnowsShout(sh));
				RE::NiPoint3 at;
				if (!wants || !Shops::Discovered(t) || !Shops::StationTarget(t, at)) {
					continue;
				}
				if (const float d = at.GetDistance(a_player->GetPosition()); d < best) {
					best = d, a_at = at;
					a_label = std::string(gen::kTowns[t]) + (Step(t) < kFreed ? ": " + Fill(gen::kCurseSteps[std::clamp(Step(t), 0, 3)].title, t) : ": its shout waits");
				}
			}
			return best < FLT_MAX;
		}

		void PublishObjective(RE::PlayerCharacter* a_player)
		{
			RE::NiPoint3 at;
			std::string  label;
			const bool   on = ObjectiveTarget(a_player, at, label);
			float        x = 0, y = 0, zc = 0;
			bool         front = false;
			if (on) {
				if (auto* cam = RE::Main::WorldRootCamera()) {
					cam->WorldPtToScreenPt3(at, x, y, zc, 1e-5f);
					front = zc > 0.0f;
					y = 1.0f - y;  // (Skyrim's screen y runs up)
				}
			}
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.objOn = on;
			h.objFront = front;
			h.objX = x, h.objY = y;
			h.objDist = on ? at.GetDistance(a_player->GetPosition()) : 0.0f;
			h.objLabel = label;
		}

		void Publish(int a_zone, bool a_night)
		{
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.questCity.clear(), h.questTitle.clear(), h.questGoal.clear(), h.sanctuary.clear();
			h.questFrac = 0;
			if (a_zone < 0) {
				return;
			}
			const int s = Step(a_zone);
			if (s >= kFreed) {
				if (a_night) {
					h.sanctuary = Upper(std::string(gen::kTowns[a_zone]));
				}
				return;
			}
			const auto& st = gen::kCurseSteps[s];
			const int   p = Prog(a_zone);
			h.questCity = Upper(std::string(gen::kTowns[a_zone]));
			h.questStep = s;
			h.questTitle = Fill(st.title, a_zone, st.target);
			std::string goal = Fill(st.goal, a_zone, st.target);
			switch (s) {
			case 0: goal += "  " + std::to_string(p) + "/" + std::to_string(st.target); h.questFrac = static_cast<float>(p) / st.target; break;
			case 1: { const int lit = std::popcount(static_cast<unsigned>(p & 7)); goal += "  " + std::to_string(lit) + "/3"; h.questFrac = lit / 3.0f; break; }
			case 3: goal += a_night ? "" : "  (at night)"; h.questFrac = 0; break;
			default: goal += "  " + std::to_string(p) + "/" + std::to_string(st.target) + "s"; h.questFrac = static_cast<float>(p) / st.target; break;
			}
			h.questGoal = goal;
		}
	}

	namespace Curse
	{
		int  ZoneTown() { return g_zone; }
		int  RelicOf(int a_town) { return a_town >= 0 ? Relic(a_town) : -1; }

		void ClaimRelic(int a_town)
		{
			if (const int sh = a_town >= 0 ? ShoutOf(a_town) : -1; sh >= 0) {
				const std::string name(gen::kShouts[sh].label);
				if (Step(a_town) < kFreed) {
					Hud::Banner(name + " sleeps here until the Dirge over " + std::string(gen::kTowns[a_town]) + " is broken", 3.5f);
					return;
				}
				if (Gun::KnowsShout(sh)) {
					Hud::Banner("You already know " + name, 2.5f);
					return;
				}
				Gun::LearnShout(sh);
				Forms::Play(gen::Sound::curse_lifted);
				Note("THU'UM LEARNED: " + name + "  (" + std::string(gen::kShouts[sh].w1) + " " + std::string(gen::kShouts[sh].w2) + " " + std::string(gen::kShouts[sh].w3) + ") - equip it in the Magic menu", 8.0f);
				logger::info("curse: {} learned at {}", gen::kShouts[sh].id, gen::kTowns[a_town]);
				return;
			}
			const int relic = a_town >= 0 ? Relic(a_town) : -1;
			if (relic < 0) {
				return;
			}
			const std::string name(gen::kGuns[relic].label);
			if (Step(a_town) < kFreed) {
				Hud::Banner(name + " waits until the Dirge over " + std::string(gen::kTowns[a_town]) + " is broken", 3.5f);
				if (GiveBook(RE::PlayerCharacter::GetSingleton())) {
					Note("The Undying Dirge is back in your pack", 5.0f);
				}
				return;
			}
			if (Gun::Owns(relic)) {
				Hud::Banner("You already carry " + name, 2.5f);
				Gun::Give(relic);  // (puts it in your hands)
				return;
			}
			Gun::Give(relic);
			Forms::Play(gen::Sound::buy);
			Hud::Banner(name + " RECLAIMED", 3.0f);
			logger::info("curse: {} reclaimed at {}", gen::kGuns[relic].id, gen::kTowns[a_town]);
		}
		bool SafeHere(RE::PlayerCharacter*) { return g_zone >= 0 && Step(g_zone) >= kFreed; }
		float SpawnBoost() { return g_holding ? 2.0f : 1.0f; }

		void Reset()
		{
			RemoveWards();
			g_zone = -1, g_zoneTimer = 0, g_heraldTown = -1, g_holding = false, g_holdAcc = 0;
			g_loreShown.clear();
			g_relicShow.clear();
		}

		void OnUndeadDeath(RE::Actor*)
		{
			if (!Director::Fighting() || g_zone < 0 || Step(g_zone) != 0) {
				return;
			}
			const int p = Prog(g_zone) + 1;
			if (p >= gen::kCurseSteps[0].target) {
				Advance(g_zone);
			} else {
				Set(g_zone, 0, p);
			}
		}

		void OnBossSlain()
		{
			if (g_heraldTown >= 0 && Step(g_heraldTown) == 3) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					Advance(g_heraldTown);
					Lift(player, g_heraldTown);
				}
			}
			g_heraldTown = -1;
		}

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Is3DLoaded()) {
				return;
			}
			if ((g_zoneTimer -= a_delta) <= 0) {
				g_zoneTimer = 0.5f;
				const int z = ComputeZone(player);
				if (z != g_zone) {
					logger::info("curse: entered {}", z >= 0 ? std::string(gen::kTowns[z]) : std::string("the wilds"));
				}
				g_zone = z;
			}
			const bool fighting = Director::Fighting();
			Publish(g_zone, fighting);
			PublishObjective(player);
			TickRelicShow(player, a_delta);
			// The prologue: as soon as the new character is made (out of Helgen, at Riverwood), the book is in the pack
			if (Save::Flag(Save::kFlagStarted)) {
				if (auto* intro = Global(gen::kCurseIntroGlobal); intro && intro->value < 1.5f) {  // (1 = an older save saw the pop-up: it gets the book too)
					intro->value = 2.0f;
					GiveBook(player);
					Forms::Play(gen::Sound::quest_start);
					Note("A book is in your pack: The Undying Dirge", 8.0f);
					g_loreShown.insert(-1);
					return;
				}
			}
			if (!fighting) {
				g_holding = false;
				g_holdAcc = 0;
				if (g_wardTown >= 0) {
					RemoveWards();
				}
				return;
			}
			const int z = g_zone;
			if (z < 0) {
				g_holding = false;
				if (g_wardTown >= 0) {
					RemoveWards();
				}
				return;
			}
			const int s = Step(z);
			if (s >= kFreed) {
				g_holding = false;
				return;
			}
			if (g_wardTown >= 0 && (g_wardTown != z || s != 1)) {
				RemoveWards();
			}
			// each quest's scroll, once per session, the first time you're in the city at night with it open
			if (const int key = z * 10 + s; !g_loreShown.contains(key)) {  // a new rite: a toll in the dark, and the tracker updates
				g_loreShown.insert(key);
				Forms::Play(gen::Sound::quest_start);
			}
			float wardFrac = 0;
			switch (s) {
			case 1:  // the ward-fires
				{
					EnsureWards(player, z);
					const int   bits = Prog(z);
					const auto  pp = player->GetPosition();
					int         nearIdx = -1;
					for (int i = 0; i < 3; ++i) {
						if (!((bits >> i) & 1) && g_wards[i].brazier.get() && std::hypot(g_wards[i].pos.x - pp.x, g_wards[i].pos.y - pp.y) < gen::kCurseWardRadius) {
							nearIdx = i;
						}
					}
					if (nearIdx < 0) {
						g_channel = 0;
						break;
					}
					g_channel += a_delta;
					wardFrac = std::clamp(g_channel / gen::kCurseWardSeconds, 0.0f, 1.0f);
					if (g_channel >= gen::kCurseWardSeconds) {
						g_channel = 0;
						const int nb = bits | (1 << nearIdx);
						Kindle(player, g_wards[nearIdx]);
						Forms::Play(gen::Sound::ward_lit);
						Points::Add(250);
						const int lit = std::popcount(static_cast<unsigned>(nb & 7));
						if (lit >= 3) {
							Advance(z);
						} else {
							Set(z, 1, nb);
							Note("Ward-fire kindled", 4.0f);
						}
					}
				}
				break;
			case 3:  // the Herald in the fog
				g_heraldTown = z;
				if (!Director::BossAlive()) {
					Director::CallHerald();
				}
				break;
			case 2:  // hold the shrine
				{
					RE::NiPoint3       c;
					float              yaw = 0;
					RE::TESWorldSpace* ws = nullptr;
					g_holding = Shops::StandFrame(z, c, yaw, ws) && ws == player->GetWorldspace() && player->GetPosition().GetDistance(c) < gen::kCurseHoldRadius;
					if (g_holding && (g_holdAcc += a_delta) >= 1.0f) {
						g_holdAcc -= 1.0f;
						const int p = Prog(z) + 1;
						if (p >= gen::kCurseSteps[2].target) {
							g_holding = false;
							Advance(z);
						} else {
							Set(z, 2, p);
						}
					}
				}
				break;
			default:
				break;
			}
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().wardFrac = wardFrac;
		}

		int  Raw(int a_town) { return a_town >= 0 && a_town < static_cast<int>(gen::kCurseCities.size()) ? Value(a_town) : 0; }
		void SetRaw(int a_town, int a_value)
		{
			if (a_town < 0 || a_town >= static_cast<int>(gen::kCurseCities.size())) {
				return;
			}
			if (auto* g = Global(gen::kCurseCities[a_town].global)) {
				g->value = static_cast<float>(a_value);
			}
		}
	}
}
