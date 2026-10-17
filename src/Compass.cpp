#include "BO3Z.h"

// The compass of The Curse of Alduin (drawn by the HUD, src/Hud.cpp DrawCompass, over Skyrim's own, which is hidden):
// which way you face, and what lies around you - the rite you are on, the cities' shrines, the places of the
// map (discovered ones from afar, the rest once you are near), your friends, and at night the dead closing in.
namespace bo3z::Compass
{
	namespace
	{
		struct Place { RE::NiPoint3 at; int kind; std::string name; bool known; int type; };
		std::vector<Place> g_places;
		RE::TESWorldSpace* g_world = nullptr;

		int KindOf(RE::MARKER_TYPE a_t)
		{
			using T = RE::MARKER_TYPE;
			const auto v = static_cast<int>(a_t);
			if (a_t == T::kCity || (v >= static_cast<int>(T::kRiftenCastle) && v <= static_cast<int>(T::kDawnstarCapitol))) {
				return 3;
			}
			switch (a_t) {
			case T::kTown:
			case T::kSettlement:
			case T::kFarm:
			case T::kOrcStronghold:
				return 4;
			case T::kCave:
			case T::kFort:
			case T::kNordicRuin:
			case T::kDwemerRuin:
			case T::kShipwreck:
			case T::kDragonLair:
			case T::kMine:
			case T::kImperialTower:
			case T::kNordicTower:
			case T::kGiantCamp:
			case T::kCamp:
				return 5;
			case T::kDoor:
			case T::kQuestTarget:
			case T::kMultipleQuestTarget:
			case T::kPlayerSet:
			case T::kYouAreHere:
			case T::kNone:
				return 0;
			default:
				return 6;
			}
		}

		// a goal's text with its <Alias=...> names filled in (as Skyrim's journal does)
		std::string Fill(RE::TESQuest* a_q, std::string a_t)
		{
			for (std::size_t at; (at = a_t.find('<')) != std::string::npos;) {
				const auto end = a_t.find('>', at);
				if (end == std::string::npos) {
					break;
				}
				std::string tag = a_t.substr(at + 1, end - at - 1), put;
				if (const auto eq = tag.find('='); eq != std::string::npos && tag.rfind("Alias", 0) == 0) {
					const std::string name = tag.substr(eq + 1);
					RE::BSReadLockGuard lk(a_q->aliasAccessLock);
					for (auto* al : a_q->aliases) {
						if (al && _stricmp(al->aliasName.c_str(), name.c_str()) == 0) {
							if (auto it = a_q->refAliasMap.find(al->aliasID); it != a_q->refAliasMap.end()) {
								if (auto r = it->second.get()) {
									const char* n = r->GetDisplayFullName();
									put = n ? n : "";
								}
							}
							break;
						}
					}
				}
				a_t.replace(at, end - at + 1, put);
			}
			return a_t;
		}

		std::vector<Hud::State::Tracked> Tracked()
		{
			std::vector<Hud::State::Tracked> out;
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				return out;
			}
			for (auto* q : dh->GetFormArray<RE::TESQuest>()) {
				if (!q || !q->IsActive() || !q->IsRunning() || q->IsCompleted()) {
					continue;
				}
				Hud::State::Tracked t;
				const char* n = q->GetName();
				t.title = n ? n : "";
				for (auto* o : q->objectives) {
					if (o && o->state.get() == RE::QUEST_OBJECTIVE_STATE::kDisplayed && !o->displayText.empty() && t.goals.size() < 3) {
						t.goals.push_back(Fill(q, o->displayText.c_str()));
					}
				}
				if (!t.title.empty() && !t.goals.empty()) {
					out.push_back(std::move(t));
					if (out.size() >= 3) {
						break;
					}
				}
			}
			return out;
		}

		// the map's places in this worldspace (read once each time you enter one)
		void ReadPlaces(RE::TESWorldSpace* a_world)
		{
			g_places.clear();
			g_world = a_world;
			auto* cell = a_world ? a_world->persistentCell : nullptr;
			if (!cell) {
				return;
			}
			for (auto& ref : cell->GetRuntimeData().references) {
				if (!ref) {
					continue;
				}
				auto* mm = ref->extraList.GetByType<RE::ExtraMapMarker>();
				if (!mm || !mm->mapData) {
					continue;
				}
				const int kind = KindOf(mm->mapData->type.get());
				if (kind == 0) {
					continue;
				}
				const char* name = mm->mapData->locationName.GetFullName();
				g_places.push_back({ ref->GetPosition(), kind, name ? name : "", mm->mapData->flags.all(RE::MapMarkerData::Flag::kVisible), static_cast<int>(mm->mapData->type.get()) });
			}
			logger::info("compass: {} places in {}", g_places.size(), a_world->GetFormEditorID());
		}
	}

	void Tick(float a_delta)
	{
		static float scan = 0, settle = 0;
		auto*        pl = RE::PlayerCharacter::GetSingleton();
		auto*        cam = RE::PlayerCamera::GetSingleton();
		auto*        ui = RE::UI::GetSingleton();
		if (!pl || !cam || !cam->cameraRoot || !ui || !pl->Is3DLoaded()) {
			return;
		}
		// (never while a load is under way: the world's places are still being read in on another thread)
		if (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
			settle = 0;
			return;
		}
		if ((settle += a_delta) < 3.0f) {
			return;
		}
		const auto&        m = cam->cameraRoot->world.rotate;
		const float        heading = std::atan2(m.entry[0][1], m.entry[1][1]);
		const RE::NiPoint3 me = pl->GetPosition();
		std::vector<Hud::State::CompassMark> marks;
		std::vector<Hud::State::QuestMark>   qmarks;
		auto bearing = [&](const RE::NiPoint3& a) { return std::atan2(a.x - me.x, a.y - me.y); };
		auto* world = pl->GetWorldspace();
		if (world != g_world || (scan -= a_delta) <= 0) {
			scan = 30.0f;  // (and again now and then: places get discovered)
			ReadPlaces(world);
		}
		int shownPlaces = 0;
		const Place* nearest = nullptr;
		float        nearD = 1e9f;
		for (const auto& p : g_places) {
			const float d = p.at.GetDistance(me);
			if (d < nearD) nearD = d, nearest = &p;
			const float reach = p.kind == 3 ? 60000.0f : p.known ? 20000.0f : 6000.0f;
			if (d < reach && d > 300.0f) {
				marks.push_back({ bearing(p.at), d, p.kind, p.name, p.type, p.known });
				++shownPlaces;
			}
		}
		static float logT = 0;
		if ((logT -= a_delta) <= 0) {
			logT = 20.0f;
			logger::info("compass: {} places on it (of {}); nearest {} ({:.0f}, type {}, known {}) at ({:.0f},{:.0f}) - you at ({:.0f},{:.0f})", shownPlaces, g_places.size(),
				nearest ? nearest->name : "-", nearD, nearest ? nearest->type : -1, nearest ? nearest->known : false, nearest ? nearest->at.x : 0.0f, nearest ? nearest->at.y : 0.0f, me.x, me.y);
		}
		if (world) {  // the shrines of the cities
			for (std::size_t t = 0; t < gen::kTowns.size(); ++t) {
				if (auto* s = Shops::Placed(static_cast<int>(t), 0); s && s->GetWorldspace() == world) {
					const float d = s->GetPosition().GetDistance(me);
					if (d < 40000.0f && d > 150.0f) {
						marks.push_back({ bearing(s->GetPosition()), d, 2, "Echo Altar" });
					}
				}
			}
		}
		std::vector<std::array<float, 4>> dead;
		Director::UndeadNear(me, 4500.0f, dead);
		for (const auto& u : dead) {
			marks.push_back({ std::atan2(u[0], u[1]), u[2], u[3] > 0.5f ? 9 : 8, std::string() });
		}
		// Skyrim's own quests: their targets (what its compass would have shown) among our marks, as the rite's are
		{
			RE::BSSpinLockGuard guard(pl->GetQuestTargetsLock());
			int shown = 0;
			for (auto& [quest, targets] : pl->GetQuestTargets()) {
				if (!quest || !targets) {
					continue;
				}
				for (auto* t : *targets) {
					if (!t || shown >= 6) {
						continue;
					}
					RE::ObjectRefHandle h;
					t->GetTrackingRef(h, quest);
					if (!h) {
						t->GetTargetRef(h, true, quest);
					}
					auto ref = h.get();
					if (!ref || ref.get() == pl || ref->GetWorldspace() != world) {
						continue;
					}
					if (const float d = ref->GetPosition().GetDistance(me); d > 150.0f) {
						const char* qn = quest->GetName();
						marks.push_back({ bearing(ref->GetPosition()), d, 1, qn ? qn : "" });
						++shown;
						// and over the target itself, on the screen (Skyrim's floating marker, in our style)
						if (auto* cam = RE::Main::WorldRootCamera(); cam && d < 12000.0f) {
							auto at = ref->GetPosition();
							at.z += ref->As<RE::Actor>() ? 150.0f : 70.0f;
							float x = 0, y = 0, z = 0;
							cam->WorldPtToScreenPt3(at, x, y, z, 1e-5f);
							qmarks.push_back({ x, 1.0f - y, d, z > 0, qn ? qn : "" });
						}
					}
				}
			}
		}
		// who is looking for you (while you sneak): Skyrim's own detection level for each one near
		static float detT = 0;
		static std::vector<Hud::State::Watcher> watchers;
		static float aware = 0;
		static bool  seen = false;
		const bool   sneaking = pl->IsSneaking() && !pl->IsDead();
		if (!sneaking) {
			watchers.clear(), aware = 0, seen = false;
		} else if ((detT -= a_delta) <= 0) {
			detT = 0.2f;
			watchers.clear(), aware = 0, seen = false;
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (auto& hnd : lists->highActorHandles) {
					auto a = hnd.get();
					if (!a || a.get() == pl || a->IsDead() || a->IsPlayerTeammate() || Party::IsFriend(a.get()) || Director::EnemyRow(a.get()) >= 0) {
						continue;
					}
					const float d = a->GetPosition().GetDistance(me);
					if (d > 4000.0f) {
						continue;
					}
					const std::int32_t lv = a->RequestDetectionLevel(pl, RE::DETECTION_PRIORITY::kNormal);
					// (below -100 they have no idea; from -100 they grow suspicious; at 0 and above they see you)
					const float f = std::clamp((static_cast<float>(lv) + 100.0f) / 100.0f, 0.0f, 1.0f);
					if (f <= 0.02f) {
						continue;
					}
					float rel = std::atan2(a->GetPosition().x - me.x, a->GetPosition().y - me.y) - heading;
					rel = std::remainder(rel, 6.2831853f);
					watchers.push_back({ rel, f, lv >= 0 });
					aware = std::max(aware, f);
					seen = seen || lv >= 0;
				}
			}
		}
		static float trackT = 0;
		static std::vector<Hud::State::Tracked> tracked;
		if ((trackT -= a_delta) <= 0) {
			trackT = 0.5f;
			tracked = Tracked();
		}
		for (const auto& f : Party::Now().marks) {
			marks.push_back({ f.ang, f.dist, 7, f.name });
		}
		std::scoped_lock l(Hud::Lock());
		auto& h = Hud::Edit();
		if (h.objOn) {
			marks.push_back({ h.objAng, h.objDist, 1, h.objLabel });
		}
		h.compassOn = true;
		h.heading = heading;
		{  // the shout's cooldown (Noah: on the border of our compass, not Skyrim's shout meter)
			static float most = 0, ready = 0, last = 0;
			const float rec = pl ? std::max(0.0f, pl->GetVoiceRecoveryTime()) : 0.0f;
			if (rec > last + 0.25f || rec > most) most = rec;  // (a new shout: its full cooldown)
			if (last > 0.05f && rec <= 0.0f) ready = 1.0f;
			last = rec;
			ready = std::max(0.0f, ready - a_delta * 1.6f);
			h.shoutCd = most > 0.05f ? std::clamp(rec / most, 0.0f, 1.0f) : 0.0f;
			h.shoutReady = ready;
		}
		h.compass = std::move(marks);
		h.qmarks = std::move(qmarks);
		h.tracked = tracked;
		h.sneaking = sneaking;
		h.aware = aware;
		h.seen = seen;
		h.watchers = watchers;
	}
}
