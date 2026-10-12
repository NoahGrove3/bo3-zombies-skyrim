#include "BO3Z.h"

// Mystery Box, perk machines and wall-buys (sheets: objects, locations). Each is placed at runtime next to
// its town's map marker, snapped to the nearest walkable navmesh point, and re-used if it is already there.
// Activating one (E) spends points.
namespace bo3z
{
	namespace
	{
		std::array<RE::ObjectRefHandle, gen::kLocations.size()> g_placed{};
		std::mt19937 g_rng{ std::random_device{}() };
		float        g_scan = 0;

		// Dressing (giant bottles, glows, beams, lights) and the pieces that turn.
		struct Spinner
		{
			RE::ObjectRefHandle ref;
			float               degPerSec;
		};
		std::array<bool, gen::kLocations.size()> g_dressed{};
		std::vector<Spinner>                     g_spinners;

		// The shrine's roll: it gathers power (Charging), every spell in the pool flickers over the crown, slowing
		// (Spinning), then one is held there as a ball of light until you take it (Offering).
		enum class Show { Idle, Charging, Spinning, Offering };
		struct ShowState
		{
			Show                phase{ Show::Idle };
			RE::ObjectRefHandle chest, display;
			int                 gun{ -1 }, shown{ -1 };
			float               t{ 0 }, step{ 0 };
		} g_show;
		constexpr float kChargeTime = 1.1f;
		constexpr float kOrbZ = 236.0f;   // the shrine's crown (tools/shrine.py: the cradle of prongs at z 257)
		constexpr float kOrbScale = 1.6f;

		RE::NiPoint3 Local(RE::TESObjectREFR* a_ref, float a_x, float a_y, float a_z)
		{
			const float a = a_ref->GetAngleZ();
			const auto  p = a_ref->GetPosition();
			// Skyrim: angle z is clockwise from +y (north); local y = forward.
			return { p.x + a_x * std::cos(a) + a_y * std::sin(a), p.y - a_x * std::sin(a) + a_y * std::cos(a), p.z + a_z };
		}

		void Remove(RE::ObjectRefHandle& a_h)
		{
			if (auto r = a_h.get()) {
				r->Disable();
				r->SetDelete(true);
			}
			a_h = {};
		}

		float CompactOf(int a_town);  // a fixed station's layout scale (1 = full size)
		void  Dress(std::size_t a_index, RE::TESObjectREFR* a_main)
		{
			const float c = CompactOf(gen::kLocations[a_index].town);
			if (g_dressed[a_index]) {
				return;
			}
			g_dressed[a_index] = true;
			const int obj = gen::kLocations[a_index].object;
			for (const auto& d : gen::kDress) {
				if (d.object != obj) {
					continue;
				}
				auto* base = Forms::Ours(d.formId);
				if (!base) {
					continue;
				}
				const auto want = Local(a_main, d.x * c, d.y * c, d.z);
				RE::TESObjectREFR* found = nullptr;
				RE::TES::GetSingleton()->ForEachReferenceInRange(a_main, 400.0f, [&](RE::TESObjectREFR* r) {
					if (r && r->GetBaseObject() == base && !r->IsDeleted() && r->GetPosition().GetDistance(want) < 80.0f) {
						found = r;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				if (!found) {
					auto ref = a_main->PlaceObjectAtMe(base, false);
					if (!ref) {
						continue;
					}
					ref->SetPosition(want);
					ref->SetAngle({ 0, 0, a_main->GetAngleZ() });
					if (!d.light) {
						ref->SetScale(d.scale * c);
					}
					found = ref.get();
				}
				if (d.spin != 0) {
					g_spinners.push_back({ found->CreateRefHandle(), d.spin });
				}
			}
		}

		void ShowModel(RE::TESBoundObject* a_base, float a_rise)
		{
			auto chest = g_show.chest.get();
			Remove(g_show.display);
			if (!chest || !a_base) {
				return;
			}
			auto ref = chest->PlaceObjectAtMe(a_base, false);
			if (!ref) {
				return;
			}
			ref->SetPosition(Local(chest.get(), 0, 0, kOrbZ * chest->GetScale() + a_rise));
			ref->SetAngle({ 0, 0, chest->GetAngleZ() + RE::NI_PI / 2 });
			ref->SetScale(kOrbScale);
			g_show.display = ref->CreateRefHandle();
		}

		std::vector<int> RollPool()
		{
			std::vector<int> pool;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (gen::kGuns[i].boxWeight > 0 && !gen::kGuns[i].retired) {
					pool.push_back(i);
				}
			}
			return pool;
		}

		void TickShow(float a_delta)
		{
			if (g_show.phase == Show::Idle) {
				return;
			}
			g_show.t += a_delta;
			auto chest = g_show.chest.get();
			if (!chest) {
				Remove(g_show.display);
				g_show.phase = Show::Idle;
				return;
			}
			switch (g_show.phase) {
			case Show::Charging:
				if (g_show.t >= kChargeTime) {
					g_show.t = 0;
					g_show.step = 0;
					g_show.phase = Show::Spinning;
					Forms::PlayAt(gen::Sound::coffer_spin, chest.get());
				}
				break;
			case Show::Spinning:
				{
					const float k = std::min(1.0f, g_show.t / gen::kShowSpin);
					if ((g_show.step -= a_delta) <= 0) {
						g_show.step = 0.07f + 0.32f * k * k;  // flickers through the whole pool, slowing like a wheel of fortune
						const auto pool = RollPool();
						if (!pool.empty()) {
							auto it = std::find(pool.begin(), pool.end(), g_show.shown);
							g_show.shown = it == pool.end() || ++it == pool.end() ? pool.front() : *it;  // in order: every spell passes by
						}
						if (g_show.shown >= 0) {
							ShowModel(Forms::Ours(gen::kShowStat[g_show.shown]), gen::kShowRise * k);
						}
					} else if (auto d = g_show.display.get()) {
						d->SetPosition(Local(chest.get(), 0, 0, kOrbZ * chest->GetScale() + gen::kShowRise * k));
					}
					if (g_show.t >= gen::kShowSpin) {
						g_show.t = 0;
						g_show.shown = g_show.gun;
						ShowModel(Forms::Ours(gen::kShowStat[g_show.gun]), gen::kShowRise);
						Forms::PlayAt(gen::Sound::coffer_land, chest.get());
						Hud::Banner(std::string(gen::kGuns[g_show.gun].label) + "  -  Activate the shrine to take it", 4.0f);
						g_show.phase = Show::Offering;
					}
					break;
				}
			case Show::Offering:
				if (auto d = g_show.display.get()) {
					auto a = d->GetAngle();
					d->SetAngle({ 0, 0, a.z + a_delta * 1.2f });
					d->SetPosition(Local(chest.get(), 0, 0, kOrbZ * chest->GetScale() + gen::kShowRise + 4.0f * std::sin(g_show.t * 3.0f)));
				}
				if (g_show.t >= gen::kShowOffer) {
					Remove(g_show.display);
					Hud::Banner("The shrine took it back.", 2.5f);
					g_show.phase = Show::Idle;
				}
				break;
			default: break;
			}
		}

		int ActiveBoxTown()
		{
			const int v = Save::Get(gen::kAV_box);
			const int town = ((v >> 8) & 0x1F) - 1;
			return town >= 0 ? town : 0;  // Riverwood first
		}
		// ---- Brew alley layout (sheets: layout, locations) ----
		// Per town: find open, flat, walkable ground near the map marker for the whole alley, clear out
		// anything we placed there under an older layout, then place every piece on its slot.
		struct TownPlan
		{
			enum class State { Unknown, Searching, Ready } state{ State::Unknown };
			RE::NiPoint3 center{};
			float        yaw{ 0 };
			int          candidate{ 0 };
			float        top{ 0 };        // the stand's floor: every machine stands at this height
			bool         topSet{ false };
			float        compact{ 1.0f };  // fixed stations in city streets: the layout drawn in
		};
		std::array<TownPlan, gen::kTowns.size()> g_towns{};
		std::optional<float> GroundTop(RE::TESObjectREFR* a_anchor, const TownPlan& a_plan, float* a_low = nullptr);
		RE::ObjectRefHandle                       g_probe;

		// Stands placed under the outskirts layout (v2): one bit per town in UN_StandLayout (saved with the game).
		void MarkLayout(int a_town)
		{
			if (auto* g = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(gen::kStandLayoutGlobal, gen::kPlugin)) {
				g->value = static_cast<float>(static_cast<int>(g->value) | (1 << a_town));
			}
		}

		float CompactOf(int a_town) { return a_town >= 0 && a_town < static_cast<int>(g_towns.size()) ? g_towns[a_town].compact : 1.0f; }

		// The town's own reference in Skyrim (a walled city's gate outside, a town's map marker): where its world-map
		// icon goes and how the town is recognised.
		RE::TESObjectREFR* MapAnchor(int a_town)
		{
			for (const auto& l : gen::kLocations) {
				if (l.town == a_town) {
					return RE::TESForm::LookupByID<RE::TESObjectREFR>(l.anchor);
				}
			}
			return nullptr;
		}

		// Where the station is built from. A fixed station (sheet: stations) is anchored on a marker of ours set at
		// its spot the first time you come near it (inside a walled city: in the city's own world); the marker stays
		// in the save and is found again next time.
		std::array<RE::ObjectRefHandle, gen::kTowns.size()> g_anchors{};
		RE::TESObjectREFR* TownAnchor(int a_town)
		{
			const auto& st = gen::kStations[a_town];
			if (!st.fixed) {
				return MapAnchor(a_town);
			}
			if (auto a = g_anchors[a_town].get(); a && !a->IsDeleted()) {
				return a.get();
			}
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* ws = p ? p->GetWorldspace() : nullptr;
			const RE::NiPoint3 at{ st.x, st.y, st.z };
			if (!ws || ws->GetFormID() != st.world || p->GetPosition().GetDistance(at) > 6000.0f) {
				return nullptr;
			}
			auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);  // XMarker
			if (!xm) {
				return nullptr;
			}
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(p, 7000.0f, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == xm && (r->GetFormID() >> 24) == 0xFF && !r->IsDeleted() && r->GetPosition().GetDistance(at) < 5.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!found) {
				auto ref = p->PlaceObjectAtMe(xm, true);
				if (!ref) {
					return nullptr;
				}
				ref->SetPosition(at);
				found = ref.get();
				logger::info("shops: {} station anchor set at ({:.0f},{:.0f},{:.0f})", gen::kTowns[a_town], at.x, at.y, at.z);
			}
			g_anchors[a_town] = found->CreateRefHandle();
			return found;
		}

		RE::NiPoint3 SlotPos(const TownPlan& a_plan, int a_slot)
		{
			const auto& s = gen::kSlots[a_slot];
			const float a = a_plan.yaw, x = s.x * a_plan.compact, y = s.y * a_plan.compact;
			return { a_plan.center.x + x * std::cos(a) + y * std::sin(a), a_plan.center.y - x * std::sin(a) + y * std::cos(a), a_plan.center.z };
		}
		float SlotFacing(const TownPlan& a_plan, int a_slot) { return a_plan.yaw + gen::kSlots[a_slot].faceDeg * RE::NI_PI / 180.0f; }

		bool IsOurBase(RE::TESForm* a_base)
		{
			static std::unordered_set<RE::TESForm*> ours;
			if (ours.empty()) {
				for (int i = 0; i < static_cast<int>(gen::kObjects.size()); ++i) {
					ours.insert(Forms::Object(i));
				}
				for (const auto& d : gen::kDress) {
					ours.insert(Forms::Ours(d.formId));
				}
				ours.erase(nullptr);
			}
			return a_base && ours.contains(a_base);
		}

		// Highest solid surface (statics, terrain) under (x,y), casting down from well above a_top.
		std::optional<float> FloorZ(RE::TESObjectREFR* a_anchor, const RE::NiPoint3& a_xy, float a_top)
		{
			auto* cell = a_anchor ? a_anchor->GetParentCell() : nullptr;
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return std::nullopt;
			}
			const float s = RE::bhkWorld::GetWorldScale();
			const RE::NiPoint3 from{ a_xy.x, a_xy.y, a_top + 300.0f }, to{ a_xy.x, a_xy.y, a_top - 2000.0f };
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(from * s);
			pick.rayInput.to = RE::hkVector4(to * s);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
			{
				RE::BSReadLockGuard lock(world->worldLock);
				world->PickObject(pick);
			}
			if (!pick.rayOutput.HasHit()) {
				return std::nullopt;
			}
			return from.z + (to.z - from.z) * pick.rayOutput.hitFraction;
		}

		// Nearest walkable point (navmesh) to a_at, via an invisible marker; nullopt if there is none close.
		std::optional<RE::NiPoint3> Snap(RE::TESObjectREFR* a_anchor, const RE::NiPoint3& a_at)
		{
			auto probe = g_probe.get();
			if (!probe) {
				auto* marker = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);  // XMarker
				auto  ref = marker ? a_anchor->PlaceObjectAtMe(marker, false) : nullptr;
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
			auto got = probe->GetPosition();
			const float dxy = std::hypot(got.x - at.x, got.y - at.y);
			if (dxy > gen::kPlanSnap) {
				return std::nullopt;
			}
			// Navmesh/land heights sit under town paving and bridges: stand on what the physics world says is there.
			// only just above the navmesh: a roof or awning overhead must not count as the floor
			if (auto z = FloorZ(a_anchor, got, got.z - 220.0f); z && *z > got.z - 150.0f && *z < got.z + 80.0f) {
				got.z = *z;
			}
			return got;
		}

		struct Best
		{
			float    score{ FLT_MAX };
			TownPlan plan;
		};
		std::array<Best, gen::kTowns.size()> g_best{};

		// What stands around a town, gathered once per search: doors, buildings/walls/cliffs (big) and fences, rocks,
		// carts, trees (clutter). The stand goes out where none of them are close: open ground just outside town.
		struct Obstacle
		{
			float x, y, r;
			int   kind;  // 0 door, 1 big, 2 clutter
		};
		std::array<std::vector<Obstacle>, gen::kTowns.size()> g_obstacles{};
		constexpr float kStandRadius = 370.0f;  // half-diagonal of the platform
		constexpr float kStandLift = 34.0f;     // the dais floor above the highest ground under it (its skirt reaches 100 down)

		void GatherObstacles(int a_town, RE::TESObjectREFR* a_anchor)
		{
			auto& list = g_obstacles[a_town];
			list.clear();
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				auto* base = r ? r->GetBaseObject() : nullptr;
				if (!base || r->IsDisabled() || r->IsDeleted() || IsOurBase(base) || r->As<RE::Actor>()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto p = r->GetPosition();
				const auto t = base->GetFormType();
				if (t == RE::FormType::Door) {
					list.push_back({ p.x, p.y, 0.0f, 0 });
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (t != RE::FormType::Static && t != RE::FormType::MovableStatic && t != RE::FormType::Tree && t != RE::FormType::Furniture &&
					t != RE::FormType::Container && t != RE::FormType::Activator && t != RE::FormType::Light) {
					return RE::BSContainer::ForEachResult::kContinue;  // grass, flora, items: tidied off the footprint anyway
				}
				auto* bo = base->As<RE::TESBoundObject>();
				if (!bo) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto& b = bo->boundData;
				const float sc = std::max(0.1f, r->GetScale());
				const float wx = (b.boundMax.x - b.boundMin.x) * sc, wy = (b.boundMax.y - b.boundMin.y) * sc, wz = (b.boundMax.z - b.boundMin.z) * sc;
				const float horiz = std::max(wx, wy), size = std::max(horiz, wz);
				if (size < 70.0f) {
					return RE::BSContainer::ForEachResult::kContinue;  // pebbles, cups, small clutter
				}
				if (t == RE::FormType::Tree) {
					list.push_back({ p.x, p.y, std::min(horiz * 0.15f, 40.0f) - 120.0f, 2 });  // just the trunk (the canopy is the sky test's job); forests are fine
				} else {
					list.push_back({ p.x, p.y, horiz * 0.5f, size > 450.0f ? 1 : 2 });
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			logger::info("shops: {} has {} doors/buildings/clutter around it", gen::kTowns[a_town], list.size());
		}

		// How much the surroundings crowd a stand centred at a_at: 0 = open ground, nothing near.
		float Crowding(int a_town, const RE::NiPoint3& a_at, int* a_doors = nullptr)
		{
			float pen = 0;
			int   doors = 0;
			for (const auto& o : g_obstacles[a_town]) {
				const float d = std::hypot(o.x - a_at.x, o.y - a_at.y);
				const float need = o.kind == 0 ? gen::kPlanDoor : kStandRadius + o.r + (o.kind == 1 ? gen::kPlanBuilding : gen::kPlanClutter);
				if (d < need) {
					pen += (need - d) * (o.kind == 0 ? 2.0f : o.kind == 1 ? 1.5f : 1.0f);
					doors += o.kind == 0;
				}
			}
			if (a_doors) {
				*a_doors = doors;
			}
			return pen;
		}

		// Score a candidate alley (lower is better); 0 failures + flat + no doors near = accept immediately.
		// Clear sky over the whole stand: cast down from high above on a grid; trees, porches and roofs stand
		// out as hits far above the ground and rule the spot out.
		// a_overhead > 0 (an existing stand, whose own wall and machines are in the way, up to ~350): only count things that far above the floor.
		bool Obstructed(RE::TESObjectREFR* a_anchor, const TownPlan& a_plan, float a_overhead = 0.0f)
		{
			float glo = FLT_MAX, ghi = -FLT_MAX;
			constexpr int NX = 12, NY = 7;  // ~60 units apart: fences and posts can't slip between the rays
			for (int gx = 0; gx < NX; ++gx) {
				for (int gy = 0; gy < NY; ++gy) {
					const float x = -gen::kStandHalfX + gx * (2 * gen::kStandHalfX / (NX - 1)), y = gen::kStandFrontY + gy * ((gen::kStandBackY - gen::kStandFrontY) / (NY - 1));
					const float a = a_plan.yaw;
					const RE::NiPoint3 at{ a_plan.center.x + x * std::cos(a) + y * std::sin(a), a_plan.center.y - x * std::sin(a) + y * std::cos(a), a_plan.center.z };
					if (auto z = FloorZ(a_anchor, at, a_plan.center.z + 700.0f)) {
						glo = std::min(glo, *z), ghi = std::max(ghi, *z);
					}
				}
			}
			if (a_overhead > 0) {
				return ghi > a_plan.top + a_overhead;
			}
			return ghi > glo && ghi - glo > 35.0f;
		}

		bool TryCandidate(int a_town, RE::TESObjectREFR* a_anchor, int a_index)
		{
			const int perDist = gen::kPlanAngles * static_cast<int>(gen::kPlanYaw.size());
			const int di = a_index / perDist, rest = a_index % perDist;
			const int ai = rest / static_cast<int>(gen::kPlanYaw.size()), yi = rest % static_cast<int>(gen::kPlanYaw.size());
			const float ang = ai * 2.0f * RE::NI_PI / gen::kPlanAngles;
			const auto  a = a_anchor->GetPosition();
			TownPlan    plan;
			plan.center = { a.x + std::sin(ang) * gen::kPlanDist[di], a.y + std::cos(ang) * gen::kPlanDist[di], a.z };
			plan.yaw = gen::kPlanYaw[yi] * RE::NI_PI / 180.0f;
			float lo = FLT_MAX, hi = -FLT_MAX;
			int   fails = 0, doors = 0;
			const float crowd = Crowding(a_town, plan.center, &doors);
			for (const auto& l : gen::kLocations) {
				if (l.town != a_town) {
					continue;
				}
				const auto want = SlotPos(plan, l.slot);
				if (auto p = Snap(a_anchor, want)) {
					lo = std::min(lo, p->z), hi = std::max(hi, p->z);
				} else {
					++fails;
				}
			}
			int blocked = Obstructed(a_anchor, plan) ? 1 : 0;
			float slope = 0;
			{
				float low = 0;
				if (auto g = GroundTop(a_anchor, plan, &low); !g || (*g - kStandLift) - low > gen::kStandMaxSlope) {
					fails += 3;  // the ground drops away under part of the stand (or isn't there): never acceptable
				} else {
					slope = std::max(0.0f, *g - kStandLift - low);
				}
			}
			const float spread = hi > lo ? hi - lo : 0;
			auto        c = Snap(a_anchor, plan.center);
			plan.center.z = c ? c->z : (lo < hi ? (lo + hi) * 0.5f : a.z);
			plan.state = TownPlan::State::Ready;
			const float score = fails * 1.0e7f + blocked * 1.0e5f + std::min(crowd * 2.0f, 5.0e4f) + std::max(0.0f, slope - gen::kPlanFlat) * 20.0f + std::max(0.0f, spread - gen::kPlanHeight) * 5.0f + gen::kPlanDist[di] * 0.05f;
			if (score < g_best[a_town].score) {
				g_best[a_town] = { score, plan };
			}
			if (fails == 0 && !blocked && crowd <= 0.0f && slope <= gen::kPlanFlat && spread <= gen::kPlanHeight) {
				g_towns[a_town] = plan;
				MarkLayout(a_town);
				logger::info("shops: {} alley at ({:.0f},{:.0f}) yaw {:.0f} (candidate {})", gen::kTowns[a_town], plan.center.x, plan.center.y, gen::kPlanYaw[yi], a_index);
				return true;
			}
			return false;
		}

		void Sweep(int a_town, RE::TESObjectREFR* a_anchor)
		{
			int n = 0;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				if (r && !r->IsDeleted() && IsOurBase(r->GetBaseObject())) {
					r->Disable();
					r->SetDelete(true);
					++n;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
				if (gen::kLocations[i].town == a_town) {
					g_placed[i] = {};
					g_dressed[i] = false;
				}
			}
			std::erase_if(g_spinners, [](const Spinner& s) { return !s.ref.get(); });
			logger::info("shops: cleared {} old pieces in {}", n, gen::kTowns[a_town]);
		}

		// Existing stand from an earlier session? The Coffer with the stand under it marks it; older layouts
		// (separate machines on the ground) are swept and rebuilt as a stand.
		void ClearSite(int a_town, RE::TESObjectREFR* a_anchor);
		bool AdoptExisting(int a_town, RE::TESObjectREFR* a_anchor)
		{
			RE::TESForm* coffer = nullptr;
			int          slot = -1;
			for (const auto& l : gen::kLocations) {
				if (l.town == a_town && gen::kObjects[l.object].kind == 0 && !gen::kObjects[l.object].retired) {
					coffer = Forms::Object(l.object), slot = l.slot;
				}
			}
			auto* stand = Forms::Ours(gen::kStandStat);
			if (!coffer || !stand || slot < 0) {
				return false;
			}
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				if (r && !r->IsDeleted() && r->GetBaseObject() == coffer) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!found) {
				return false;
			}
			bool hasStand = false;
			RE::TES::GetSingleton()->ForEachReferenceInRange(found, 300.0f, [&](RE::TESObjectREFR* r) {
				if (r && !r->IsDeleted() && r->GetBaseObject() == stand) {
					hasStand = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!hasStand) {
				logger::info("shops: {} has the old ground layout, rebuilding as a stand", gen::kTowns[a_town]);
				return false;
			}
			TownPlan plan;
			plan.yaw = found->GetAngleZ() - gen::kSlots[slot].faceDeg * RE::NI_PI / 180.0f;
			const auto& s = gen::kSlots[slot];
			const auto  p = found->GetPosition();
			plan.center = { p.x - (s.x * std::cos(plan.yaw) + s.y * std::sin(plan.yaw)), p.y - (-s.x * std::sin(plan.yaw) + s.y * std::cos(plan.yaw)), p.z };
			plan.top = p.z, plan.topSet = true;
			plan.state = TownPlan::State::Ready;
			plan.center.z = p.z;
			float low = 0;
			auto g = GroundTop(a_anchor, plan, &low);
			if (g && *g - p.z > 8.0f && *g - p.z <= kStandLift + 12.0f) {
				// a stand from before the raised dais: lift it and everything on it in place
				const float dz = *g - p.z;
				int         moved = 0;
				RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
					if (r && !r->IsDeleted() && IsOurBase(r->GetBaseObject()) && std::hypot(r->GetPosition().x - plan.center.x, r->GetPosition().y - plan.center.y) < 600.0f) {
						auto q = r->GetPosition();
						q.z += dz;
						r->SetPosition(q);
						++moved;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				plan.top = plan.center.z = p.z + dz;
				logger::info("shops: {} stand raised {:.0f} above the grass ({} pieces)", gen::kTowns[a_town], dz, moved);
				g = plan.top;
			}
			if (!g || std::abs(*g - plan.top) > 30.0f || (*g - kStandLift) - low > gen::kStandMaxSlope) {
				logger::info("shops: {} stand floats {:.0f} off the ground, rebuilding", gen::kTowns[a_town], g ? p.z - *g : 999.0f);
				return false;
			}
			if (auto* v2 = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(gen::kStandLayoutGlobal, gen::kPlugin); v2 && !((static_cast<int>(v2->value) >> a_town) & 1)) {
				logger::info("shops: {} stand predates the outskirts layout, moving it", gen::kTowns[a_town]);
				return false;
			}
			if (g && *g - plan.top > 8.0f) {
				logger::info("shops: {} stand sits {:.0f} into the ground, rebuilding", gen::kTowns[a_town], *g - plan.top);
				return false;
			}
			if (Obstructed(a_anchor, plan, 440.0f)) {
				logger::info("shops: {} stand is boxed in by trees or buildings, moving it", gen::kTowns[a_town]);
				return false;
			}
			GatherObstacles(a_town, a_anchor);
			if (const float c = Crowding(a_town, plan.center); c > 0.0f || (g && (*g - kStandLift) - low > gen::kPlanFlat + 10.0f)) {
				logger::info("shops: {} stand is in town (crowding {:.0f}), moving it out to open ground", gen::kTowns[a_town], c);
				return false;
			}
			g_towns[a_town] = plan;
			ClearSite(a_town, a_anchor);
			// machines that were retired (wall-buys, the old ammo cache) leave the stand
			int gone = 0;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				for (std::size_t i = 0; r && i < gen::kObjects.size(); ++i) {
					if (gen::kObjects[i].retired && r->GetBaseObject() == Forms::Object(static_cast<int>(i)) && !r->IsDisabled()) {
						r->Disable();
						r->SetDelete(true);
						++gone;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (gone) {
				logger::info("shops: removed {} retired machines from the {} stand", gone, gen::kTowns[a_town]);
			}
			return true;
		}

		// Tidy the site: small statics, plants and furniture standing on the stand's footprint are hidden (never
		// buildings, doors, containers, actors, lights, quest-persistent objects or our own pieces).
		void ClearSite(int a_town, RE::TESObjectREFR* a_anchor)
		{
			const auto& plan = g_towns[a_town];
			int         n = 0;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				auto* base = r ? r->GetBaseObject() : nullptr;
				if (!base || r->IsDisabled() || r->IsDeleted() || IsOurBase(base) || (r->GetFormFlags() & RE::TESForm::RecordFlags::kPersistent)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto t = base->GetFormType();
				if (t != RE::FormType::Static && t != RE::FormType::MovableStatic && t != RE::FormType::Tree && t != RE::FormType::Flora &&
					t != RE::FormType::Furniture && t != RE::FormType::Misc && t != RE::FormType::Grass) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* bo = base->As<RE::TESBoundObject>();
				if (bo) {
					const auto& b = bo->boundData;
					const int   size = std::max({ b.boundMax.x - b.boundMin.x, b.boundMax.y - b.boundMin.y, b.boundMax.z - b.boundMin.z }) * static_cast<int>(std::max(1.0f, r->GetScale()));
					if (size > 450) {
						return RE::BSContainer::ForEachResult::kContinue;  // something big (a building, a wall): leave it
					}
				}
				// in stand space?
				const auto  p = r->GetPosition();
				const float dx = p.x - plan.center.x, dy = p.y - plan.center.y, a = plan.yaw;
				const float lx = dx * std::cos(a) - dy * std::sin(a), ly = dx * std::sin(a) + dy * std::cos(a);
				// the footprint, plus the approach in front of it (bushes there hide the machines from the path)
				if (std::abs(lx) <= gen::kStandHalfX + 160.0f && ly >= gen::kStandFrontY - 420.0f && ly <= gen::kStandBackY + 80.0f && std::abs(p.z - plan.top) < 300.0f) {
					r->Disable();
					++n;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (n) {
				logger::info("shops: cleared {} bits of clutter off the {} stand", n, gen::kTowns[a_town]);
			}
		}

		// The stand's floor: the ground under its footprint, measured from the walkable (navmesh) height so roofs,
		// branches and awnings above are never mistaken for ground. Highest ground point wins (no corner digs in).
		std::optional<float> GroundTop(RE::TESObjectREFR* a_anchor, const TownPlan& a_plan, float* a_low)
		{
			float ref = a_plan.center.z;
			if (auto c = Snap(a_anchor, a_plan.center)) {
				ref = c->z;
			}
			const float hx = gen::kStandHalfX * a_plan.compact, fy = gen::kStandFrontY * a_plan.compact, by = gen::kStandBackY * a_plan.compact;
			float hi = -FLT_MAX, lo = FLT_MAX;
			int   got = 0;
			for (int gx = 0; gx < 5; ++gx) {
				for (int gy = 0; gy < 4; ++gy) {
					const float x = -hx + gx * (2 * hx / 4), y = fy + gy * ((by - fy) / 3), a = a_plan.yaw;
					const RE::NiPoint3 at{ a_plan.center.x + x * std::cos(a) + y * std::sin(a), a_plan.center.y - x * std::sin(a) + y * std::cos(a), ref };
					if (auto z = FloorZ(a_anchor, at, ref - 220.0f); z && *z > ref - 300.0f && *z < ref + 80.0f) {
						hi = std::max(hi, *z), lo = std::min(lo, *z), ++got;
					}
				}
			}
			if (got < 16) {
				return std::nullopt;  // the ground drops away (ledge, cliff, water) under part of the footprint
			}
			if (hi - lo > gen::kStandMaxSlope + 40.0f) {
				return std::nullopt;  // too steep: one side buried deep or the other hanging
			}
			if (a_low) {
				*a_low = lo;
			}
			// The dais sits ON the ground: its floor at the highest point under it (its skirt reaches 100 down, so the low
			// side is covered). Only on a real slope does it cut into the hill rather than float over the low side.
			// Raised a step above the ground (kStandLift) so the grass round it never hides the machines.
			return (hi - lo <= 60.0f ? hi : std::min(hi, lo + 60.0f)) + kStandLift;
		}

		void SettleStand(int a_town, RE::TESObjectREFR* a_anchor)
		{
			auto& plan = g_towns[a_town];
			if (plan.topSet) {
				return;
			}
			auto top = GroundTop(a_anchor, plan);
			if (!top) {  // no clean footprint: stand on whatever is under its centre, never at the anchor's height
				if (auto c = Snap(a_anchor, plan.center)) {
					top = c->z + kStandLift;
				} else if (auto z = FloorZ(a_anchor, plan.center, plan.center.z + 3000.0f)) {
					top = *z + kStandLift;
				} else {
					float land = 0;
					RE::NiPoint3 at = plan.center;
					if (RE::TES::GetSingleton()->GetLandHeight(at, land)) {
						top = land + kStandLift;
					}
				}
			}
			plan.top = top ? *top : plan.center.z;
			plan.topSet = true;
			logger::info("shops: {} stand floor at z {:.0f}", gen::kTowns[a_town], plan.top);
			ClearSite(a_town, a_anchor);
		}


		void PlaceIfMissing(std::size_t a_index)
		{
			const auto& loc = gen::kLocations[a_index];
			if (auto ref = g_placed[a_index].get(); ref && !ref->IsDeleted()) {
				return;
			}
			const auto& plan = g_towns[loc.town];
			auto*       anchor = TownAnchor(loc.town);
			auto*       base = Forms::Object(loc.object);
			if (plan.state != TownPlan::State::Ready || !anchor || !base || gen::kObjects[loc.object].retired) {
				return;
			}
			SettleStand(loc.town, anchor);
			auto want = SlotPos(plan, loc.slot);
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && std::hypot(r->GetPosition().x - want.x, r->GetPosition().y - want.y) < 80.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			const float floorZ = gen::kObjects[loc.object].kind == 0 ? plan.top - kStandLift - 6.0f : plan.top;  // the shrine brings its own dais
			want.z = floorZ;
			if (!found) {
				auto ref = anchor->PlaceObjectAtMe(base, false);
				if (!ref) {
					return;
				}
				ref->SetScale(gen::kObjects[loc.object].scale);
				ref->SetPosition(want);
				ref->SetAngle({ 0, 0, SlotFacing(plan, loc.slot) });
				found = ref.get();
				logger::info("shops: placed {} in {} at ({:.0f},{:.0f},{:.0f})", gen::kObjects[loc.object].id, gen::kTowns[loc.town], want.x, want.y, want.z);
			} else if (auto pos = found->GetPosition(); std::abs(pos.z - floorZ) > 4.0f) {
				pos.z = floorZ;  // keep every machine on the stand's floor
				found->SetPosition(pos);
			}
			if (std::abs(found->GetScale() - gen::kObjects[loc.object].scale) > 0.01f) {
				found->SetScale(gen::kObjects[loc.object].scale);  // (the Coffer grew)
			}
			g_placed[a_index] = found->CreateRefHandle();
			Dress(a_index, found);
		}

		// ---- world-map markers: one "Undying Nights Station" per town, at the plaza once it is built ----
		std::array<RE::ObjectRefHandle, gen::kTowns.size()> g_markers{};
		float g_markerTimer = 0;

		RE::MapMarkerData* TemplateMarkerData(RE::PlayerCharacter* a_player)
		{
			for (auto& h : a_player->GetPlayerRuntimeData().currentMapMarkers) {
				if (auto r = h.get()) {
					if (auto* x = r->extraList.GetByType<RE::ExtraMapMarker>(); x && x->mapData) {
						return x->mapData;
					}
				}
			}
			return nullptr;
		}

		// Our station icons from earlier sessions. Map markers are persistent, so they live in the worldspace's
		// persistent cell wherever they stand: found there even when the town is far away and not loaded (a range
		// search only sees loaded cells, and would make a new icon every session).
		std::array<bool, gen::kTowns.size()> g_markerScanned{};
		RE::TESObjectREFR* FindOurMarker(int a_town, RE::TESObjectREFR* a_anchor, RE::TESBoundObject* a_base)
		{
			auto* ws = a_anchor->GetWorldspace();
			auto* cell = ws ? ws->persistentCell : nullptr;
			if (!cell) {
				return nullptr;
			}
			std::vector<RE::TESObjectREFR*> mine;
			cell->ForEachReference([&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == a_base && (r->GetFormID() >> 24) == 0xFF && !r->IsDeleted() && !r->IsDisabled() &&
					r->GetPosition().GetDistance(a_anchor->GetPosition()) < gen::kPlanSweep * 1.5f) {
					mine.push_back(r);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (mine.empty()) {
				return nullptr;
			}
			// the one nearest the station (or the town), any doubles from older builds go
			const RE::NiPoint3 want = g_towns[a_town].state == TownPlan::State::Ready ? g_towns[a_town].center : a_anchor->GetPosition();
			std::sort(mine.begin(), mine.end(), [&](auto* x, auto* y) { return x->GetPosition().GetDistance(want) < y->GetPosition().GetDistance(want); });
			for (std::size_t i = 1; i < mine.size(); ++i) {
				mine[i]->Disable();
				mine[i]->SetDelete(true);
			}
			if (mine.size() > 1) {
				logger::info("shops: removed {} doubled map icons for {}", mine.size() - 1, gen::kTowns[a_town]);
			}
			return mine[0];
		}

		// Before a town's station is built (it is placed when you first come near), its icon stands out in the
		// open on that town's side of the outskirts, clear of the city's own icon; it moves onto the station once
		// the station is up.
		RE::NiPoint3 PreviewSpot(int a_town, RE::TESObjectREFR* a_anchor)
		{
			const auto  p = a_anchor->GetPosition();
			const float a = 2.4f + 0.63f * static_cast<float>(a_town);  // a different side for each town
			return { p.x + std::cos(a) * 2200.0f, p.y + std::sin(a) * 2200.0f, p.z };
		}

		std::string MarkerName(int a_town)
		{
			std::string name = std::string(gen::kText_station_marker) + " - " + std::string(gen::kTowns[a_town]);
			return name;
		}

		// Has the player found this town yet? Its own map marker (the city/town icon by that name, nearest the
		// town's anchor) is discovered. Station icons and the Coffer's travels wait for it.
		std::array<RE::ObjectRefHandle, gen::kTowns.size()> g_cityMarkers{};
		std::array<bool, gen::kTowns.size()>                g_cityLooked{};
		bool TownDiscovered(int a_town)
		{
			if (a_town < 0 || a_town >= static_cast<int>(gen::kTowns.size())) {
				return false;
			}
			auto ref = g_cityMarkers[a_town].get();
			if (!ref && !g_cityLooked[a_town]) {
				// the walled cities' own icons stand in their city worldspaces (WhiterunWorld...), not out in Tamriel
				static const std::pair<std::string_view, RE::FormID> kWalled[] = { { "Whiterun", 0x0162CE }, { "Solitude", 0x04D0F4 },
					{ "Windhelm", 0x038436 }, { "Markarth", 0x01C38A }, { "Riften", 0x01C390 } };
				for (const auto& [name, id] : kWalled) {
					if (name == gen::kTowns[a_town]) {
						if (auto* r = RE::TESForm::LookupByID<RE::TESObjectREFR>(id); r && r->extraList.GetByType<RE::ExtraMapMarker>()) {
							g_cityMarkers[a_town] = r->CreateRefHandle();
							ref = RE::NiPointer<RE::TESObjectREFR>(r);
							g_cityLooked[a_town] = true;
							logger::info("shops: {} is found by its map marker {:08X}", gen::kTowns[a_town], id);
						}
					}
				}
			}
			if (!ref && !g_cityLooked[a_town]) {
				g_cityLooked[a_town] = true;
				auto* anchor = MapAnchor(a_town);
				auto* ws = anchor ? anchor->GetWorldspace() : nullptr;
				if (auto* cell = ws ? ws->persistentCell : nullptr) {
					RE::TESObjectREFR* best = nullptr;
					float              bestD = 9000.0f;
					bool               named = false;
					int                seen = 0;
					cell->ForEachReference([&](RE::TESObjectREFR* r) {
						++seen;
						auto* x = r ? r->extraList.GetByType<RE::ExtraMapMarker>() : nullptr;
						if (!x || !x->mapData || (r->GetFormID() >> 24) == 0xFF) {
							return RE::BSContainer::ForEachResult::kContinue;
						}
						const bool  isTown = x->mapData->type == RE::MARKER_TYPE::kCity || x->mapData->type == RE::MARKER_TYPE::kTown;
						const bool  sameName = std::string_view(x->mapData->locationName.fullName.c_str()) == gen::kTowns[a_town];
						const float d = r->GetPosition().GetDistance(anchor->GetPosition());
						if ((sameName && (!named || d < bestD)) || (!named && isTown && d < bestD)) {
							best = r, bestD = d, named = sameName;
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
					if (!best && seen < 200) {
						g_cityLooked[a_town] = false;  // the world isn't loaded yet: look again later
						return false;
					}
					if (best) {
						g_cityMarkers[a_town] = best->CreateRefHandle();
						ref = RE::NiPointer<RE::TESObjectREFR>(best);
						logger::info("shops: {} is found by its map marker {:08X}{}", gen::kTowns[a_town], best->GetFormID(), named ? "" : " (nearest town icon)");
					} else {
						logger::warn("shops: no map marker for {}; its station shows from the start", gen::kTowns[a_town]);
					}
				}
			}
			if (!ref) {
				return g_cityLooked[a_town];  // no marker to go by: treat as found
			}
			auto* x = ref->extraList.GetByType<RE::ExtraMapMarker>();
			return x && x->mapData && x->mapData->flags.all(RE::MapMarkerData::Flag::kCanTravelTo);
		}

		void EnsureMarker(RE::PlayerCharacter* a_player, int a_town)
		{
			auto* anchor = MapAnchor(a_town);
			auto* markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(0x10);  // MapMarker
			if (!anchor || !markerBase) {
				return;
			}
			const auto& plan = g_towns[a_town];
			const auto& st = gen::kStations[a_town];
			auto*        aws = anchor->GetWorldspace();
			// a fixed station: its icon on the spot (a walled city's world has no map of its own: there the icon
			// stands at the city gate outside)
			const bool   fixedHere = st.fixed && aws && aws->GetFormID() == st.world;
			const bool   ready = st.fixed || plan.state == TownPlan::State::Ready;
			const RE::NiPoint3 at = st.fixed ? (fixedHere ? RE::NiPoint3{ st.x, st.y, st.z } : anchor->GetPosition()) : ready ? plan.center : PreviewSpot(a_town, anchor);
			if (!TownDiscovered(a_town)) {
				// not found yet: no station icon (one from an earlier build is hidden until then)
				if (!g_markers[a_town].get() && !g_markerScanned[a_town]) {
					g_markerScanned[a_town] = true;
					if (auto* r = FindOurMarker(a_town, anchor, markerBase)) {
						g_markers[a_town] = r->CreateRefHandle();
					}
				}
				if (auto r = g_markers[a_town].get()) {
					if (auto* x = r->extraList.GetByType<RE::ExtraMapMarker>(); x && x->mapData) {
						x->mapData->flags.reset(RE::MapMarkerData::Flag::kVisible, RE::MapMarkerData::Flag::kCanTravelTo);
					}
				}
				return;
			}
			auto ref = g_markers[a_town].get();
			if (!ref && !g_markerScanned[a_town]) {
				g_markerScanned[a_town] = true;
				if (auto* r = FindOurMarker(a_town, anchor, markerBase)) {
					ref = RE::NiPointer<RE::TESObjectREFR>(r);
				}
			}
			if (!ref) {
				ref = anchor->PlaceObjectAtMe(markerBase, true);
				if (!ref) {
					return;
				}
				ref->SetPosition(at);
			}
			g_markers[a_town] = ref->CreateRefHandle();
			if (ready && ref->GetPosition().GetDistance(at) > 50.0f) {
				ref->SetPosition(at);  // onto the station once it is built
			}
			const std::string name = MarkerName(a_town);
			auto* x = ref->extraList.GetByType<RE::ExtraMapMarker>();
			if (!x) {
				auto* tmpl = TemplateMarkerData(a_player);
				if (!tmpl) {
					return;
				}
				// Clone a game-made marker record (keeps the engine's vtable), then give it our own name.
				auto* data = static_cast<RE::MapMarkerData*>(RE::malloc(sizeof(RE::MapMarkerData)));
				std::memcpy(static_cast<void*>(data), tmpl, sizeof(RE::MapMarkerData));
				new (&data->locationName.fullName) RE::BSFixedString(name.c_str());
				data->flags.reset(RE::MapMarkerData::Flag::kShowAllHidden);
				data->flags.set(RE::MapMarkerData::Flag::kVisible);
				data->type = RE::MARKER_TYPE::kShrine;
				x = RE::BSExtraData::Create<RE::ExtraMapMarker>();
				x->mapData = data;
				ref->extraList.Add(x);
				logger::info("shops: map marker '{}' at ({:.0f},{:.0f}){}", name, at.x, at.y, ready ? "" : " (until the station is built)");
			} else if (x->mapData) {
				if (std::string_view(x->mapData->locationName.fullName.c_str()) != name) {
					x->mapData->locationName.fullName = name.c_str();  // the Coffer moved: the icon says where it is
				}
			}
			if (auto* md = x->mapData) {
				// seen on the map from the start; fast travel once the station is built (the preview spot has no
				// known ground under it)
				md->flags.set(RE::MapMarkerData::Flag::kVisible);
				if (ready) {
					md->flags.set(RE::MapMarkerData::Flag::kCanTravelTo);
				} else {
					md->flags.reset(RE::MapMarkerData::Flag::kCanTravelTo);
				}
			}
			auto& list = a_player->GetPlayerRuntimeData().currentMapMarkers;
			const auto handle = ref->CreateRefHandle();
			if (std::none_of(list.begin(), list.end(), [&](const RE::ObjectRefHandle& h) { return h.native_handle() == handle.native_handle(); })) {
				list.push_back(handle);
			}
		}

		void TickMarkers(RE::PlayerCharacter* a_player, float a_delta)
		{
			if ((g_markerTimer -= a_delta) > 0) {
				return;
			}
			g_markerTimer = 5.0f;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				EnsureMarker(a_player, t);
			}
		}

		// Pieces from older layouts that no town owns any more (stands that used to be inside a city, before stands
		// moved outside): gone once the player is near them.
		void SweepOldStands(RE::PlayerCharacter* a_player)
		{
			auto* ws = a_player->GetWorldspace();
			if (!ws) {
				return;
			}
			int n = 0;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, 6000.0f, [&](RE::TESObjectREFR* r) {
				if (!r || r->IsDeleted() || !IsOurBase(r->GetBaseObject())) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (auto* b = r->GetBaseObject(); b == Forms::Ours(gen::kWardBrazier) || b == Forms::Ours(gen::kWardFire) || b == Forms::Ours(gen::kWardLight)) {
					return RE::BSContainer::ForEachResult::kContinue;  // the Dirge's ward-fires (Curse.cpp keeps them)
				}
				for (const auto& h : g_placed) {
					if (h.get().get() == r) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
				}
				bool owned = false;
				for (int t = 0; t < static_cast<int>(gen::kTowns.size()) && !owned; ++t) {
					auto* a = TownAnchor(t);
					if (!a || a->GetWorldspace() != ws || a->GetPosition().GetDistance(r->GetPosition()) > gen::kPlanSweep) {
						continue;
					}
					// a stand that is still being placed, or one that will be adopted: not ours to judge yet
					owned = g_towns[t].state != TownPlan::State::Ready || std::hypot(r->GetPosition().x - g_towns[t].center.x, r->GetPosition().y - g_towns[t].center.y) < 700.0f;
				}
				if (!owned) {
					r->Disable();
					r->SetDelete(true);
					++n;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (n) {
				std::erase_if(g_spinners, [](const Spinner& s) { return !s.ref.get() || s.ref.get()->IsDeleted(); });
				logger::info("shops: removed {} pieces of an old in-town stand", n);
			}
		}

		void TickTowns()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			SweepOldStands(player);
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				auto* anchor = TownAnchor(t);
				auto* cell = anchor ? anchor->GetParentCell() : nullptr;
				if (!cell || !cell->IsAttached() || anchor->GetPosition().GetDistance(player->GetPosition()) > 3200.0f) {
					continue;
				}
				auto& plan = g_towns[t];
				if (plan.state == TownPlan::State::Unknown && gen::kStations[t].fixed) {
					// its spot is known (sheet: stations): build there; machines already standing on it are kept
					const auto& st = gen::kStations[t];
					plan = TownPlan{};
					plan.state = TownPlan::State::Ready;
					plan.center = { st.x, st.y, st.z };
					plan.yaw = st.yaw * RE::NI_PI / 180.0f;
					plan.compact = st.compact;
					// the one-piece shrine (layout v3): a station built to an older layout here comes down first
					if (auto* g = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(gen::kStandLayoutGlobal, gen::kPlugin); g && !((static_cast<int>(g->value) >> (16 + t)) & 1)) {
						int gone = 0;
						RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, 1200.0f, [&](RE::TESObjectREFR* r) {
							auto* b = r ? r->GetBaseObject() : nullptr;
							if (b && IsOurBase(b) && !r->IsDeleted() && b != Forms::Ours(gen::kWardBrazier) && b != Forms::Ours(gen::kWardFire) && b != Forms::Ours(gen::kWardLight)) {
								r->Disable();
								r->SetDelete(true);
								++gone;
							}
							return RE::BSContainer::ForEachResult::kContinue;
						});
						g->value = static_cast<float>(static_cast<int>(g->value) | (1 << (16 + t)));
						for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
							if (gen::kLocations[i].town == t) {
								g_placed[i] = {};
								g_dressed[i] = false;
							}
						}
						logger::info("shops: {} old station taken down for the round shrine ({} pieces)", gen::kTowns[t], gone);
					}
					// the spot moved (sheet: stations, re-picked where paths meet with room all round): a shrine still standing
					// at the old spot comes down and is raised again at the new one
					{
						std::vector<RE::TESObjectREFR*> ours;
						RE::NiPoint3 sum{};
						std::unordered_set<RE::TESForm*> station;  // only the station's own machines (never a rite's bones)
						for (const auto& l : gen::kLocations) {
							if (l.town == t) {
								if (auto* f = Forms::Object(l.object)) {
									station.insert(f);
								}
							}
						}
						RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, 1800.0f, [&](RE::TESObjectREFR* r) {
							auto* b = r ? r->GetBaseObject() : nullptr;
							if (b && station.contains(b) && !r->IsDeleted() && !r->IsDisabled()) {
								ours.push_back(r);
								sum += r->GetPosition();
							}
							return RE::BSContainer::ForEachResult::kContinue;
						});
						if (!ours.empty()) {
							const RE::NiPoint3 c = sum / static_cast<float>(ours.size());
							if (std::hypot(c.x - plan.center.x, c.y - plan.center.y) > 140.0f) {
								for (auto* r : ours) {
									r->Disable();
									r->SetDelete(true);
								}
								// and the shrine's own body and dressing standing with them (ours, close round the old spot)
								RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, 1800.0f, [&](RE::TESObjectREFR* r) {
									auto* b = r ? r->GetBaseObject() : nullptr;
									if (b && IsOurBase(b) && !r->IsDeleted() && !r->IsDisabled() && !r->As<RE::Actor>() && std::hypot(r->GetPosition().x - c.x, r->GetPosition().y - c.y) < 420.0f &&
										b != Forms::Ours(gen::kWardBrazier) && b != Forms::Ours(gen::kWardFire) && b != Forms::Ours(gen::kWardLight)) {
										r->Disable();
										r->SetDelete(true);
									}
									return RE::BSContainer::ForEachResult::kContinue;
								});
								for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
									if (gen::kLocations[i].town == t) {
										g_placed[i] = {};
										g_dressed[i] = false;
									}
								}
								logger::info("shops: {} station moved to its new spot ({} pieces taken down at the old one)", gen::kTowns[t], ours.size());
							}
						}
					}
					MarkLayout(t);
					ClearSite(t, anchor);
					logger::info("shops: {} station at its fixed spot ({:.0f},{:.0f}) yaw {:.0f}", gen::kTowns[t], st.x, st.y, st.yaw);
				}
				if (plan.state == TownPlan::State::Unknown) {
					if (AdoptExisting(t, anchor)) {
						logger::info("shops: {} alley already built", gen::kTowns[t]);
						MarkLayout(t);
					} else {
						Sweep(t, anchor);
						GatherObstacles(t, anchor);
						plan.state = TownPlan::State::Searching;
						plan.candidate = 0;
					}
				}
				if (plan.state == TownPlan::State::Searching) {
					const int total = static_cast<int>(gen::kPlanDist.size()) * gen::kPlanAngles * static_cast<int>(gen::kPlanYaw.size());
					for (int k = 0; k < 3 && plan.state == TownPlan::State::Searching; ++k) {
						if (plan.candidate >= total) {
							// Nowhere perfect: take the best-scoring spot found.
							plan = g_best[t].score < FLT_MAX ? g_best[t].plan : TownPlan{ TownPlan::State::Ready, anchor->GetPosition(), 0, 0 };
							plan.state = TownPlan::State::Ready;
							MarkLayout(t);
							logger::warn("shops: no perfect spot in {}, best score {:.0f} at ({:.0f},{:.0f})", gen::kTowns[t], g_best[t].score, plan.center.x, plan.center.y);
							break;
						}
						TryCandidate(t, anchor, plan.candidate++);
					}
					if (plan.state == TownPlan::State::Searching) {
						return;  // keep searching next frame
					}
					if (auto probe = g_probe.get()) {
						probe->Disable();
						probe->SetDelete(true);
						g_probe = {};
					}
				}
				for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
					if (gen::kLocations[i].town == t) {
						PlaceIfMissing(i);
					}
				}
			}
		}

		int TownOf(RE::TESObjectREFR* a_ref)
		{
			for (std::size_t i = 0; i < g_placed.size(); ++i) {
				if (g_placed[i].get().get() == a_ref) {
					return gen::kLocations[i].town;
				}
			}
			// Not tracked this session: nearest town anchor.
			int   best = 0;
			float bestD = FLT_MAX;
			for (const auto& l : gen::kLocations) {
				if (auto* a = RE::TESForm::LookupByID<RE::TESObjectREFR>(l.anchor)) {
					const float d = a->GetPosition().GetDistance(a_ref->GetPosition());
					if (d < bestD && a->GetWorldspace() == a_ref->GetWorldspace()) {
						bestD = d, best = l.town;
					}
				}
			}
			return best;
		}

		// Take the offered spell (Activate on the shrine while it is held over the crown).
		bool TakeOffer(RE::TESObjectREFR* a_ref)
		{
			if (g_show.phase != Show::Offering || g_show.chest.get().get() != a_ref) {
				return false;
			}
			Gun::Give(g_show.gun);
			Forms::Play(gen::Sound::coffer_take);
			Hud::Banner(std::string(gen::kGuns[g_show.gun].label), 2.5f);
			logger::info("shrine: took {}", gen::kGuns[g_show.gun].id);
			Remove(g_show.display);
			g_show.phase = Show::Idle;
			return true;
		}

		int RollCost()
		{
			const int crew = Save::Crew();
			return PowerUps::FireSale() ? 10 : crew >= 0 && gen::kCrew[crew].passive == 3 ? static_cast<int>(gen::kCrew[crew].value) : gen::kObjects[0].cost;
		}

		// Roll a spell: every spell you do not carry has the same chance.
		void SpinBox(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref || g_show.phase != Show::Idle) {
				if (g_show.phase != Show::Idle) {
					Hud::Banner("The shrine is still turning", 1.5f);
				}
				return;
			}
			std::vector<int> pool;
			for (int i : RollPool()) {
				if (!Gun::Owns(i)) {
					pool.push_back(i);
				}
			}
			if (pool.empty()) {
				Hud::Banner("You already carry every spell", 2.0f);
				return;
			}
			const int cost = RollCost();
			if (!Points::Spend(cost)) {
				return;
			}
			g_show = {};
			g_show.chest = a_ref->CreateRefHandle();
			g_show.phase = Show::Charging;
			g_show.gun = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(g_rng)];
			Forms::PlayAt(gen::Sound::coffer_charge, a_ref);
			logger::info("shrine: rolling {} (1 in {})", gen::kGuns[g_show.gun].id, pool.size());
		}

		bool                g_menu = false;
		int                 g_sel = 0;
		RE::ObjectRefHandle g_menuRef;

		void BuyPerk(int perk, RE::TESObjectREFR* a_ref)
		{
			auto*     potion = Forms::PerkPotion(perk);
			if (!potion) {
				return;
			}
			if (Save::HasPerk(perk)) {
				Hud::Banner("You already have " + std::string(gen::kPerks[perk].label), 2.0f);
				return;
			}
			if (Save::PerkCount() >= gen::kMaxPerks) {
				Hud::Banner("Perk limit reached (" + std::to_string(gen::kMaxPerks) + ")", 2.0f);
				return;
			}
			if (!Points::Spend(gen::kPerks[perk].cost)) {
				return;
			}
			RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(potion, nullptr, 1, nullptr);
			Forms::PlayAt(Forms::Brew(perk), a_ref);
			Hud::Banner(std::string(gen::kPerks[perk].label) + " added to your potions. Drink it!", 3.0f);
		}

		void BuyAmmo(const gen::ObjectDef& a_obj)
		{
			std::vector<int> owned;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (Gun::Owns(i)) {
					owned.push_back(i);
				}
			}
			if (owned.empty()) {
				Hud::Banner("No weapons to restock", 2.0f);
				return;
			}
			if (!Points::Spend(a_obj.cost)) {
				return;
			}
			for (int g : owned) {
				Gun::RefillAmmo(g);
			}
			Forms::Play(gen::Sound::reload_mag);
			Hud::Banner("All ammo restocked", 2.0f);
		}

		void BuyWall(const gen::ObjectDef& a_obj)
		{
			const int   gun = a_obj.ref;
			const auto& g = gen::kGuns[gun];
			if (Gun::Owns(gun)) {
				if (Points::Spend(g.ammoCost)) {
					Gun::RefillAmmo(gun);
					Forms::Play(gen::Sound::buy);
					Hud::Banner(std::string(g.label) + " ammo", 2.0f);
				}
				return;
			}
			if (Points::Spend(g.wallBuyCost)) {
				Gun::Give(gun);
				Forms::Play(gen::Sound::buy);
				Hud::Banner(std::string(g.label), 2.0f);
			}
		}
	}

	namespace Shops
	{
		void Reset()
		{
			g_placed.fill({});
			g_dressed.fill(false);
			g_towns.fill({});
			g_best.fill({});
			g_probe = {};
			g_spinners.clear();
			g_show = {};
		}

		RE::TESObjectREFR* Placed(int a_town, int a_kind)
		{
			for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
				if (gen::kLocations[i].town == a_town && gen::kObjects[gen::kLocations[i].object].kind == a_kind) {
					if (auto r = g_placed[i].get(); r && !r->IsDeleted()) {
						return r.get();
					}
				}
			}
			return nullptr;
		}
		int TownOfRef(RE::TESObjectREFR* a_ref) { return a_ref ? TownOf(a_ref) : -1; }

		bool Discovered(int a_town) { return a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size()) && TownDiscovered(a_town); }
		bool StationTarget(int a_town, RE::NiPoint3& a_at)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* ws = p ? p->GetWorldspace() : nullptr;
			if (!ws || a_town < 0 || a_town >= static_cast<int>(gen::kTowns.size())) {
				return false;
			}
			if (const auto& st = gen::kStations[a_town]; st.fixed && ws->GetFormID() == st.world) {
				a_at = { st.x, st.y, st.z + 60.0f };
				return true;
			}
			if (g_towns[a_town].state == TownPlan::State::Ready && TownAnchor(a_town) && TownAnchor(a_town)->GetWorldspace() == ws) {
				a_at = g_towns[a_town].center + RE::NiPoint3{ 0, 0, 60.0f };
				return true;
			}
			if (auto* m = MapAnchor(a_town); m && m->GetWorldspace() == ws) {
				a_at = m->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
				return true;
			}
			return false;
		}
		bool StandFrame(int a_town, RE::NiPoint3& a_center, float& a_yaw, RE::TESWorldSpace*& a_world)
		{
			auto* anchor = a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size()) ? TownAnchor(a_town) : nullptr;
			a_world = anchor ? anchor->GetWorldspace() : nullptr;
			if (!anchor) {
				return false;
			}
			const auto& plan = g_towns[a_town];
			if (plan.state == TownPlan::State::Ready) {
				a_center = plan.center;
				a_center.z = plan.topSet ? plan.top : plan.center.z;
				a_yaw = plan.yaw;
				return true;
			}
			a_center = anchor->GetPosition();
			a_yaw = 0;
			return false;
		}

		std::string BoxTown() { return std::string(gen::kTowns[ActiveBoxTown()]); }

		static bool IsMenuKey(const RE::ButtonEvent* b)
		{
			const auto code = b->GetIDCode();
			switch (b->GetDevice()) {
			case RE::INPUT_DEVICE::kKeyboard: return (code >= 0x02 && code <= 0x05) || code == 0x12 || code == 0xCB || code == 0xCD || code == 0x0E;
			case RE::INPUT_DEVICE::kMouse: return code == 8 || code == 9;
			case RE::INPUT_DEVICE::kGamepad: return code == 0x4000 || code == 0x8000 || code == 0x1000 || code == 0x2000 || code == 0x0001 || code == 0x0004 || code == 0x0008;
			default: return false;
			}
		}
		// (a key that closed the menu is still eaten a moment after, so Activate does not open it again behind you)
		std::chrono::steady_clock::time_point g_closedAt{};
		bool MenuEats(RE::InputEvent* a_event)
		{
			auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
			if (!b || !IsMenuKey(b)) {
				return false;
			}
			return g_menu || std::chrono::steady_clock::now() - g_closedAt < std::chrono::milliseconds(350);
		}

		// The shrine's keys (the menu never pauses: the dead keep coming, so everything is one press away).
		// Keyboard: 1 roll a spell, 2 reforge, 3 drink the chosen brew, 4 learn the city's Thu'um, mouse wheel or
		// Left/Right picks a brew, Activate (E) or Backspace leaves; walking away closes it too.
		// Gamepad: X roll, Y reforge, A drink, D-pad left/right pick, D-pad up Thu'um, B leave.
		bool OnButton(const RE::ButtonEvent* a_event)
		{
			if (!g_menu || !a_event || !a_event->IsDown()) {
				return false;
			}
			const auto code = a_event->GetIDCode();
			const auto dev = a_event->GetDevice();
			const int  n = static_cast<int>(gen::kPerks.size());
			auto*      ref = g_menuRef.get().get();
			auto act = [&](int a) {
				switch (a) {
				case 0:
					if (!TakeOffer(ref)) {
						SpinBox(ref);
					}
					break;
				case 1:
					if (Gun::Reforge() && ref) {
						Forms::PlayAt(gen::Sound::forge_upgrade, ref);
					}
					break;
				case 2: BuyPerk(g_sel, ref); break;
				case 3: Curse::ClaimRelic(ref ? TownOf(ref) : -1); break;
				case 4:
					Forms::Play(gen::Sound::ui_close);
					g_menu = false;
					g_closedAt = std::chrono::steady_clock::now();
					break;
				}
			};
			if (dev == RE::INPUT_DEVICE::kKeyboard) {
				switch (code) {
				case 0x02: act(0); return true;                 // 1
				case 0x03: act(1); return true;                 // 2
				case 0x04: act(2); return true;                 // 3
				case 0x05: act(3); return true;                 // 4
				case 0xCB: g_sel = (g_sel + n - 1) % n; Forms::Play(gen::Sound::ui_move); return true;  // left arrow
				case 0xCD: g_sel = (g_sel + 1) % n; Forms::Play(gen::Sound::ui_move); return true;      // right arrow
				case 0x0E: act(4); return true;                 // backspace
				case 0x12:                                      // E (Activate): takes a waiting spell, else leaves
					if (!TakeOffer(ref)) {
						act(4);
					}
					return true;
				default: return false;
				}
			}
			if (dev == RE::INPUT_DEVICE::kMouse && (code == 8 || code == 9)) {
				g_sel = (g_sel + (code == 8 ? n - 1 : 1)) % n;
				Forms::Play(gen::Sound::ui_move);
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kGamepad) {
				switch (code) {
				case 0x4000: act(0); return true;  // X (square)
				case 0x8000: act(1); return true;  // Y (triangle)
				case 0x1000: act(2); return true;  // A (cross)
				case 0x0001: act(3); return true;  // dpad up
				case 0x0004: g_sel = (g_sel + n - 1) % n; Forms::Play(gen::Sound::ui_move); return true;  // dpad left
				case 0x0008: g_sel = (g_sel + 1) % n; Forms::Play(gen::Sound::ui_move); return true;      // dpad right
				case 0x2000: act(4); return true;  // B (circle)
				default: return false;
				}
			}
			return false;
		}

		void Tick(float a_delta)
		{
			if (g_menu) {
				auto r = g_menuRef.get();
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!r || !player || r->GetPosition().GetDistance(player->GetPosition()) > 420.0f) {
					g_menu = false;
				}
			}
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.perkMenu = g_menu;
				h.perkSel = g_sel;
				h.rollPhase = static_cast<int>(g_show.phase);
				h.rollT = g_show.phase == Show::Charging ? std::min(1.0f, g_show.t / kChargeTime) : g_show.phase == Show::Spinning ? std::min(1.0f, g_show.t / gen::kShowSpin) : g_show.phase == Show::Offering ? std::min(1.0f, g_show.t / gen::kShowOffer) : 0.0f;
				h.rollGun = g_show.phase == Show::Idle || g_show.phase == Show::Charging ? -1 : g_show.shown;
				h.rollCost = RollCost();
				h.shrineShout = -1;
				if (g_menu) {
					if (auto r = g_menuRef.get()) {
						h.shrineShout = Curse::ClaimableShout(TownOf(r.get()));
					}
				}
			}
			for (auto& s : g_spinners) {
				if (auto r = s.ref.get(); r && r->Is3DLoaded()) {
					auto a = r->GetAngle();
					r->SetAngle({ a.x, a.y, a.z + s.degPerSec * RE::NI_PI / 180.0f * a_delta });
				}
			}
			TickShow(a_delta);
			if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
				TickMarkers(pl, a_delta);
			}
			const bool searching = std::any_of(g_towns.begin(), g_towns.end(), [](const TownPlan& p) { return p.state == TownPlan::State::Searching; });
			if (!searching && (g_scan -= a_delta) > 0) {
				return;
			}
			g_scan = 2.0f;
			TickTowns();
		}

		void OnActivate(RE::TESObjectREFR* a_ref)
		{
			const int obj = a_ref ? Forms::ObjectIndexOf(a_ref->GetBaseObject()) : -1;
			if (obj < 0) {
				return;
			}
			const auto& o = gen::kObjects[obj];
			logger::info("shops: activated {} ({} points)", o.id, Points::Get());
			switch (o.kind) {
			case 0:  // the shrine: take a waiting spell, else open (or close) its menu
				if (TakeOffer(a_ref)) {
					break;
				}
				if (g_menu && g_menuRef.get().get() == a_ref) {
					Forms::Play(gen::Sound::ui_close);
					g_menu = false;
					g_closedAt = std::chrono::steady_clock::now();
				} else {
					g_menu = true;
					g_menuRef = a_ref->CreateRefHandle();
					Forms::PlayAt(gen::Sound::coffer_open, a_ref);
				}
				break;
			case 1: BuyPerk(o.ref, a_ref); break;
			case 4:
				if (!g_menu) {
					g_menu = true, g_sel = 0;
					g_menuRef = a_ref->CreateRefHandle();
					Forms::Play(gen::Sound::coffer_open);
				} else {
					BuyPerk(g_sel, a_ref);
				}
				break;
			case 3: BuyAmmo(o); break;
			case 6:
				Curse::ClaimRelic(TownOf(a_ref));
				break;
			case 5:
				if (Gun::Reforge()) {
					Forms::PlayAt(gen::Sound::forge_upgrade, a_ref);
				}
				break;
			default: BuyWall(o); break;
			}
		}
	}
}
