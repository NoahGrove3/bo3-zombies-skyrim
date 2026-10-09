#include "BO3Z.h"

// The Undying Dirge (sheet: curse). At Helgen, Alduin's word raised every grave in Skyrim; the curse is rooted in the
// cities of the holds. Each city has its own four rites (curse.rites), in order, of these kinds:
//   slay      put n of the risen down within the city
//   wards     light n braziers round the shrine (stand by each until it catches)
//   order     light the braziers at the compass points in the order the rite's lore gives (wrong: they go out, the dead come)
//   relics    find n tokens hidden round the city (day or night: walk up to each)
//   circle    keep the dead out of the shrine's ritual circle for n seconds (they swarm it: it breaks, it starts again)
//   hold      stand at the shrine n seconds while the night pushes harder
//   champion  hunt a named champion through the night
//   herald    the fog rolls in and Grimmvar rises; slay it
// After the fourth the city is freed (and the night ends): a sanctuary where no undead rise, its shout waiting at the
// shrine. Progress: one global per city (step*1000 + progress).
namespace bo3z
{
	namespace
	{
		constexpr int kFreed = 4;
		float         Rand01()
		{
			static std::mt19937 r{ std::random_device{}() };
			return std::uniform_real_distribution<float>(0.0f, 1.0f)(r);
		}

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

		// The rite's pieces in the world round the shrine: braziers (wards, order), the circle's fires, relic tokens.
		struct Ward
		{
			RE::ObjectRefHandle brazier, fire, light;
			RE::NiPoint3        pos;
			char                dir{ 0 };  // order rites: the compass letter this brazier stands at
		};
		std::array<Ward, 8> g_wards{};
		int                 g_wardCount = 0;
		int                 g_wardTown = -1, g_wardKey = -1;  // which town and rite (town*10+step) they belong to
		float               g_channel = 0;
		float               g_circleHp = 100.0f, g_circleAcc = 0;
		RE::ObjectRefHandle g_probe;

		const gen::RiteDef& Rite(int a_town, int a_step) { return gen::kRites[a_town][std::clamp(a_step, 0, 3)]; }
		int                 Kind(int a_town) { return Step(a_town) >= kFreed ? -1 : Rite(a_town, Step(a_town)).type; }

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
				w.dir = 0;
			}
			g_wardTown = -1, g_wardKey = -1, g_wardCount = 0;
			g_channel = 0;
		}

		// Nearest walkable ground (navmesh) to a point: city streets have paving the land height knows nothing of.
		std::optional<RE::NiPoint3> Walkable(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float a_maxShift = 400.0f)
		{
			auto probe = g_probe.get();
			if (!probe || probe->GetWorldspace() != a_player->GetWorldspace()) {
				auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);
				auto  ref = xm ? a_player->PlaceObjectAtMe(xm, false) : nullptr;
				if (!ref) {
					return std::nullopt;
				}
				g_probe = ref->CreateRefHandle();
				probe = ref;
			}
			RE::NiPoint3 at = a_at;
			float        land = 0;
			if (RE::TES::GetSingleton()->GetLandHeight(at, land)) {
				at.z = land;
			}
			probe->SetPosition(at);
			probe->MoveToNearestNavmesh();
			const auto got = probe->GetPosition();
			if (std::hypot(got.x - at.x, got.y - at.y) > a_maxShift) {
				return std::nullopt;
			}
			return got;
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

		// The pieces of the rite at hand, placed round the shrine (once per town and rite; lit ones burning):
		//   wards: n braziers in an arc in front of the shrine; order: one at each compass point the rite names;
		//   circle: four fires round the shrine, burning; relics: n tokens scattered over the city (each town's own places)
		void EnsureWards(RE::PlayerCharacter* a_player, int a_town)
		{
			const int step = Step(a_town), key = a_town * 10 + step;
			if (g_wardKey == key && g_wardCount > 0 && g_wards[0].brazier.get()) {
				return;
			}
			RemoveWards();
			RE::NiPoint3       c;
			float              yaw = 0;
			RE::TESWorldSpace* ws = nullptr;
			if (!Shops::StandFrame(a_town, c, yaw, ws) || ws != a_player->GetWorldspace()) {
				return;
			}
			// leftovers from a save: braziers, fires and tokens near the station that this session didn't place
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, 9000.0f, [&](RE::TESObjectREFR* r) {
				auto* b = r ? r->GetBaseObject() : nullptr;
				if (b && (b == Forms::Ours(gen::kWardBrazier) || b == Forms::Ours(gen::kWardFire) || b == Forms::Ours(gen::kWardLight)) && !r->IsDeleted()) {
					r->Disable();
					r->SetDelete(true);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			const auto& rite = Rite(a_town, step);
			const int   bits = Prog(a_town);
			std::vector<std::pair<RE::NiPoint3, char>> spots;
			auto plan = [&](float x, float y) { return RE::NiPoint3{ c.x + x * std::cos(yaw) + y * std::sin(yaw), c.y - x * std::sin(yaw) + y * std::cos(yaw), c.z }; };
			switch (rite.type) {
			case gen::kRiteWards:
				for (int i = 0; i < std::min(rite.target, 8); ++i) {  // an arc round the front of the shrine
					const float a = -1.9f + 3.8f * (rite.target > 1 ? static_cast<float>(i) / (rite.target - 1) : 0.5f);
					spots.push_back({ plan(std::sin(a) * gen::kCurseWardDistance, -std::cos(a) * gen::kCurseWardDistance), 0 });
				}
				break;
			case gen::kRiteOrder:
				for (char d : std::string_view(rite.order)) {  // north is +y in the world, east +x
					const float r = gen::kCurseWardDistance + 80.0f;
					const RE::NiPoint3 off = d == 'N' ? RE::NiPoint3{ 0, r, 0 } : d == 'S' ? RE::NiPoint3{ 0, -r, 0 } : d == 'E' ? RE::NiPoint3{ r, 0, 0 } : RE::NiPoint3{ -r, 0, 0 };
					spots.push_back({ c + off, d });
				}
				break;
			case gen::kRiteCircle:
				for (int i = 0; i < 4; ++i) {
					const float a = i * 1.5708f + 0.785f;
					spots.push_back({ plan(std::sin(a) * 330.0f, -std::cos(a) * 330.0f - 120.0f), 0 });
				}
				break;
			case gen::kRiteRelics: {
				std::mt19937 rng(static_cast<unsigned>(a_town * 7919 + step * 31 + 5));  // the same places every time
				for (int i = 0; i < std::min(rite.target, 8); ++i) {
					const float a = std::uniform_real_distribution<float>(0, 6.2832f)(rng), r = std::uniform_real_distribution<float>(700.0f, 2300.0f)(rng);
					spots.push_back({ c + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 0 });
				}
				break;
			}
			default:
				return;
			}
			for (std::size_t i = 0; i < spots.size(); ++i) {
				auto at = Walkable(a_player, spots[i].first).value_or(spots[i].first);
				at.z += 25.0f;  // the pedestal sits on the ground
				auto& w = g_wards[i];
				w.pos = at, w.dir = spots[i].second;
				const bool relic = rite.type == gen::kRiteRelics;
				if (relic && ((bits >> i) & 1)) {
					continue;  // found already
				}
				w.brazier = PlaceAt(a_player, gen::kWardBrazier, at, relic ? 0.4f : 0.75f);
				const bool lit = rite.type == gen::kRiteCircle || relic || (rite.type == gen::kRiteWards && ((bits >> i) & 1)) || (rite.type == gen::kRiteOrder && static_cast<int>(i) < bits);
				if (lit) {
					Kindle(a_player, w);
				}
			}
			g_wardCount = static_cast<int>(spots.size());
			g_wardTown = a_town, g_wardKey = key;
			g_circleHp = 100.0f;
			logger::info("curse: {} rite {} ({}): {} pieces placed", gen::kTowns[a_town], step, rite.title, g_wardCount);
		}

		void Lift(RE::PlayerCharacter* a_player, int a_town);
		void Advance(int a_town)
		{
			const int s = Step(a_town);
			const auto& rite = Rite(a_town, s);
			Set(a_town, s + 1, 0);
			Forms::Play(gen::Sound::quest_step);
			Note(Fill(rite.title, a_town) + " - complete", 6.0f);
			Points::Add(400 * (s + 1));
			logger::info("curse: {} rite {} ({}) done", gen::kTowns[a_town], s, rite.title);
			if (s + 1 >= kFreed) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					Lift(player, a_town);
				}
			}
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
				const auto& rite = Rite(z, s);
				const int   bits = Prog(z);
				auto nearestPiece = [&](auto&& a_open) {
					float best = FLT_MAX;
					for (int i = 0; i < g_wardCount && g_wardKey == z * 10 + s; ++i) {
						if (a_open(i)) {
							const float d = g_wards[i].pos.GetDistance(a_player->GetPosition());
							if (d < best) {
								best = d, a_at = g_wards[i].pos + RE::NiPoint3{ 0, 0, 120.0f };
							}
						}
					}
					return best < FLT_MAX;
				};
				switch (rite.type) {
				case gen::kRiteWards:
					if (nearestPiece([&](int i) { return !((bits >> i) & 1); })) {
						a_label = "Light the brazier";
						return true;
					}
					break;
				case gen::kRiteOrder:
					if (bits < g_wardCount && g_wardKey == z * 10 + s) {  // the next one in the order
						a_at = g_wards[bits].pos + RE::NiPoint3{ 0, 0, 120.0f };
						const char d = g_wards[bits].dir;
						a_label = std::string(d == 'N' ? "North" : d == 'S' ? "South" : d == 'E' ? "East" : "West") + " brazier";
						return true;
					}
					break;
				case gen::kRiteRelics:
					if (nearestPiece([&](int i) { return !((bits >> i) & 1); })) {
						a_label = Fill(rite.title, z);
						return true;
					}
					break;
				case gen::kRiteChampion:
					if (Director::ChampionPos(a_at)) {
						a_label = std::string(rite.championName);
						return true;
					}
					break;
				case gen::kRiteHerald:
					if (Director::BossPos(a_at)) {
						a_label = "The Herald";
						return true;
					}
					break;
				default:
					break;
				}
				if (Shops::StationTarget(z, a_at)) {
					a_label = rite.type == gen::kRiteHold ? "Hold the shrine" : rite.type == gen::kRiteCircle ? "Guard the circle" : rite.type == gen::kRiteSlay ? "The Dirge's heart" : "The shrine";
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
					a_label = std::string(gen::kTowns[t]) + (Step(t) < kFreed ? ": " + Fill(Rite(t, Step(t)).title, t) : ": its shout waits");
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
			const auto& st = Rite(a_zone, s);
			const int   p = Prog(a_zone);
			h.questCity = Upper(std::string(gen::kTowns[a_zone]));
			h.questStep = s;
			h.questTitle = Fill(st.title, a_zone, st.target);
			std::string goal = Fill(st.goal, a_zone, st.target);
			const std::string night = a_night ? "" : "  (at night)";
			switch (st.type) {
			case gen::kRiteSlay: goal += "  " + std::to_string(p) + "/" + std::to_string(st.target) + night; h.questFrac = static_cast<float>(p) / st.target; break;
			case gen::kRiteWards:
			case gen::kRiteRelics: { const int got = std::popcount(static_cast<unsigned>(p & 0xFF)); goal += "  " + std::to_string(got) + "/" + std::to_string(st.target) + (st.type == gen::kRiteWards ? night : ""); h.questFrac = static_cast<float>(got) / st.target; break; }
			case gen::kRiteOrder: {
				std::string seq;
				for (char d : std::string_view(st.order)) {
					seq += seq.empty() ? "" : " > ";
					seq += d == 'N' ? "North" : d == 'S' ? "South" : d == 'E' ? "East" : "West";
				}
				goal += "  " + std::to_string(p) + "/" + std::to_string(st.target) + "   (" + seq + ")" + night;
				h.questFrac = static_cast<float>(p) / st.target;
				break;
			}
			case gen::kRiteCircle: goal += "  " + std::to_string(p) + "/" + std::to_string(st.target) + "s   circle " + std::to_string(static_cast<int>(g_circleHp)) + "%" + night; h.questFrac = static_cast<float>(p) / st.target; break;
			case gen::kRiteHold: goal += "  " + std::to_string(p) + "/" + std::to_string(st.target) + "s" + night; h.questFrac = static_cast<float>(p) / st.target; break;
			default: goal += night; h.questFrac = 0; break;
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
			if (!Director::Fighting() || g_zone < 0 || Kind(g_zone) != gen::kRiteSlay) {
				return;
			}
			const int p = Prog(g_zone) + 1;
			if (p >= Rite(g_zone, Step(g_zone)).target) {
				Advance(g_zone);
			} else {
				Set(g_zone, Step(g_zone), p);
			}
		}

		void OnBossSlain()
		{
			if (g_heraldTown >= 0 && Kind(g_heraldTown) == gen::kRiteHerald) {
				Advance(g_heraldTown);
			}
			g_heraldTown = -1;
		}

		void OnChampionSlain(int a_town)
		{
			if (a_town >= 0 && Kind(a_town) == gen::kRiteChampion) {
				Advance(a_town);
			}
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
			const int z = g_zone;
			const int s = z >= 0 ? Step(z) : kFreed;
			const int kind = z >= 0 ? Kind(z) : -1;
			float     wardFrac = 0;
			// relics are found day or night: walk up to each token
			if (kind == gen::kRiteRelics) {
				EnsureWards(player, z);
				const int  bits = Prog(z);
				const auto pp = player->GetPosition();
				for (int i = 0; i < g_wardCount; ++i) {
					if (!((bits >> i) & 1) && g_wards[i].brazier.get() && pp.GetDistance(g_wards[i].pos) < 170.0f) {
						const int nb = bits | (1 << i);
						Remove(g_wards[i].brazier), Remove(g_wards[i].fire), Remove(g_wards[i].light);
						Forms::Play(gen::Sound::ward_lit);
						Points::Add(150);
						if (std::popcount(static_cast<unsigned>(nb & 0xFF)) >= Rite(z, s).target) {
							Advance(z);
						} else {
							Set(z, s, nb);
							Note("Found one (" + std::to_string(std::popcount(static_cast<unsigned>(nb & 0xFF))) + "/" + std::to_string(Rite(z, s).target) + ")", 4.0f);
						}
						break;
					}
				}
			} else if (!fighting || z < 0 || s >= kFreed || (g_wardKey >= 0 && g_wardKey != z * 10 + s)) {
				if (g_wardTown >= 0) {
					RemoveWards();
				}
			}
			if (!fighting || z < 0 || s >= kFreed) {
				g_holding = false;
				g_holdAcc = 0;
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().wardFrac = 0;
				return;
			}
			// each rite's toll, once per session, the first time you're in the city at night with it open
			if (const int key = z * 10 + s; !g_loreShown.contains(key)) {
				g_loreShown.insert(key);
				Forms::Play(gen::Sound::quest_start);
			}
			const auto& rite = Rite(z, s);
			RE::NiPoint3       c;
			float              yaw = 0;
			RE::TESWorldSpace* ws = nullptr;
			const bool         here = Shops::StandFrame(z, c, yaw, ws) && ws == player->GetWorldspace();
			switch (rite.type) {
			case gen::kRiteWards:
			case gen::kRiteOrder:
				{
					EnsureWards(player, z);
					const int  bits = Prog(z);
					const auto pp = player->GetPosition();
					int        nearIdx = -1;
					for (int i = 0; i < g_wardCount; ++i) {
						const bool lit = rite.type == gen::kRiteWards ? ((bits >> i) & 1) : i < bits;
						if (!lit && g_wards[i].brazier.get() && std::hypot(g_wards[i].pos.x - pp.x, g_wards[i].pos.y - pp.y) < gen::kCurseWardRadius) {
							nearIdx = i;
						}
					}
					if (nearIdx < 0) {
						g_channel = 0;
						break;
					}
					g_channel += a_delta;
					wardFrac = std::clamp(g_channel / gen::kCurseWardSeconds, 0.0f, 1.0f);
					if (g_channel < gen::kCurseWardSeconds) {
						break;
					}
					g_channel = 0;
					if (rite.type == gen::kRiteOrder && nearIdx != bits) {  // out of order: every fire dies, and the dead answer
						for (int i = 0; i < g_wardCount; ++i) {
							Remove(g_wards[i].fire), Remove(g_wards[i].light);
						}
						Set(z, s, 0);
						for (int k = 0; k < 4; ++k) {
							const float a = k * 1.5708f;
							Director::RaiseAt(pp + RE::NiPoint3{ std::cos(a) * 350.0f, std::sin(a) * 350.0f, 40.0f });
						}
						Forms::Play(gen::Sound::hound_night);
						Note("Wrong order - the fires die and the dead answer", 5.0f);
						break;
					}
					Kindle(player, g_wards[nearIdx]);
					Forms::Play(gen::Sound::ward_lit);
					Points::Add(200);
					const int nb = rite.type == gen::kRiteWards ? bits | (1 << nearIdx) : bits + 1;
					const int done = rite.type == gen::kRiteWards ? std::popcount(static_cast<unsigned>(nb & 0xFF)) : nb;
					if (done >= rite.target) {
						Advance(z);
					} else {
						Set(z, s, nb);
						Note("Kindled (" + std::to_string(done) + "/" + std::to_string(rite.target) + ")", 4.0f);
					}
				}
				break;
			case gen::kRiteCircle:  // keep them out: the circle's strength falls while the dead are inside
				EnsureWards(player, z);
				if (here && (g_circleAcc += a_delta) >= 1.0f) {
					g_circleAcc -= 1.0f;
					const auto center = c + RE::NiPoint3{ -std::sin(yaw) * 120.0f, -std::cos(yaw) * 120.0f, 0 };
					int        inside = 0;
					for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
						if (auto a = h.get(); a && !a->IsDead() && Director::IsRoundUndead(a.get()) && a->GetPosition().GetDistance(center) < 430.0f) {
							++inside;
						}
					}
					g_circleHp = inside ? g_circleHp - 7.0f * inside : std::min(100.0f, g_circleHp + 2.0f);
					if (g_circleHp <= 0) {
						g_circleHp = 100.0f;
						Set(z, s, 0);
						Forms::Play(gen::Sound::hound_night);
						Note("The circle breaks! The rite begins again", 5.0f);
					} else if (const int p = Prog(z) + 1; p >= rite.target) {
						Advance(z);
					} else {
						Set(z, s, p);
					}
				}
				break;
			case gen::kRiteHold:
				g_holding = here && player->GetPosition().GetDistance(c) < gen::kCurseHoldRadius;
				if (g_holding && (g_holdAcc += a_delta) >= 1.0f) {
					g_holdAcc -= 1.0f;
					if (const int p = Prog(z) + 1; p >= rite.target) {
						g_holding = false;
						Advance(z);
					} else {
						Set(z, s, p);
					}
				}
				break;
			case gen::kRiteChampion:  // it rises somewhere out in the city, once a night until it falls
				if (here && !Director::ChampionActive(z)) {
					static float retry = 0;
					if ((retry -= a_delta) <= 0) {
						retry = 4.0f;
						const float a = Rand01() * 6.2832f, r = 1500.0f + 700.0f * Rand01();
						const auto  at = Walkable(player, c + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 800.0f);
						if (at) {
							Director::RaiseChampion(z, rite.champion, rite.championName, rite.championHealth, *at + RE::NiPoint3{ 0, 0, 40.0f });
						}
					}
				}
				break;
			case gen::kRiteHerald:
				g_heraldTown = z;
				if (!Director::BossAlive()) {
					Director::CallHerald();
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
