#include "BO3Z.h"

// The compass of The Curse of Alduin (drawn by the HUD, src/Hud.cpp DrawCompass, over Skyrim's own, which is hidden):
// which way you face, and what lies around you - the rite you are on, the cities' shrines, the places of the
// map (discovered ones from afar, the rest once you are near), your friends, and at night the dead closing in.
namespace bo3z::Compass
{
	namespace
	{
		struct Place { RE::NiPoint3 at; int kind; std::string name; bool known; };
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
				g_places.push_back({ ref->GetPosition(), kind, name ? name : "", mm->mapData->flags.all(RE::MapMarkerData::Flag::kVisible) });
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
		for (const auto& p : g_places) {
			const float d = p.at.GetDistance(me);
			const float reach = p.kind == 3 ? 60000.0f : p.known ? 20000.0f : 6000.0f;
			if (d < reach && d > 300.0f) {
				marks.push_back({ bearing(p.at), d, p.kind, p.name });
			}
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
		h.compass = std::move(marks);
		h.qmarks = std::move(qmarks);
	}
}
