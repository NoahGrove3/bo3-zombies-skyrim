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

		// Sheogorath's Coffer show: weapons flicker above the chest, then one is offered for a few seconds.
		enum class Show { Idle, Spinning, Offering, Cheese };
		struct ShowState
		{
			Show                phase{ Show::Idle };
			RE::ObjectRefHandle chest, display;
			int                 gun{ -1 };
			bool                cheese{ false };
			float               t{ 0 }, step{ 0 };
			int                 cheeseTown{ 0 };
		} g_show;

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

		void Dress(std::size_t a_index, RE::TESObjectREFR* a_main)
		{
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
				const auto want = Local(a_main, d.x, d.y, d.z);
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
						ref->SetScale(d.scale);
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
			ref->SetPosition(Local(chest.get(), 0, 0, 40.0f + a_rise));
			ref->SetAngle({ 0, 0, chest->GetAngleZ() + RE::NI_PI / 2 });
			g_show.display = ref->CreateRefHandle();
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
			case Show::Spinning:
				{
					const float k = std::min(1.0f, g_show.t / gen::kShowSpin);
					if ((g_show.step -= a_delta) <= 0) {
						g_show.step = 0.07f + 0.30f * k * k;  // flicker slows down like a wheel of fortune
						std::vector<int> pool;
						for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
							if (gen::kGuns[i].boxWeight > 0) {
								pool.push_back(i);
							}
						}
						const int pick = pool.empty() ? 0 : pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(g_rng)];
						ShowModel(Forms::Ours(gen::kShowStat[pick]), gen::kShowRise * k);
					} else if (auto d = g_show.display.get()) {
						d->SetPosition(Local(chest.get(), 0, 0, 40.0f + gen::kShowRise * k));
					}
					if (g_show.t >= gen::kShowSpin) {
						g_show.t = 0;
						if (g_show.cheese) {
							ShowModel(Forms::Ours(gen::kShowCheese), gen::kShowRise);
							if (auto d = g_show.display.get()) {
								d->SetScale(gen::kSkullScale);
							}
							Forms::PlayAt(gen::Sound::coffer_skull, chest.get());
							Hud::Banner(std::string(gen::kText_box_joke) + " (" + std::string(gen::kTowns[g_show.cheeseTown]) + ")", 6.0f);
							g_show.phase = Show::Cheese;
						} else {
							ShowModel(Forms::Ours(gen::kShowStat[g_show.gun]), gen::kShowRise);
							Hud::Banner(std::string(gen::kGuns[g_show.gun].label) + "  -  press Activate on the Coffer to take it", 4.0f);
							g_show.phase = Show::Offering;
						}
					}
					break;
				}
			case Show::Offering:
				if (auto d = g_show.display.get()) {
					auto a = d->GetAngle();
					d->SetAngle({ 0, 0, a.z + a_delta * 1.2f });
					d->SetPosition(Local(chest.get(), 0, 0, 40.0f + gen::kShowRise + 4.0f * std::sin(g_show.t * 3.0f)));
				}
				if (g_show.t >= gen::kShowOffer) {
					Remove(g_show.display);
					Hud::Banner(std::string(gen::kText_box_short) + " took it back.", 2.5f);
					g_show.phase = Show::Idle;
				}
				break;
			case Show::Cheese:
				if (g_show.t >= 3.5f) {
					Remove(g_show.display);
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
		int  BoxSpins() { return Save::Get(gen::kAV_box) & 0xFF; }
		void SetBox(int a_town, int a_spins) { Save::Set(gen::kAV_box, ((a_town + 1) << 8) | std::clamp(a_spins, 0, 255)); }

		std::vector<int> BoxTowns()
		{
			std::vector<int> t;
			for (const auto& l : gen::kLocations) {
				if (gen::kObjects[l.object].kind == 0 && std::find(t.begin(), t.end(), l.town) == t.end()) {
					t.push_back(l.town);
				}
			}
			return t;
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
		};
		std::array<TownPlan, gen::kTowns.size()> g_towns{};
		RE::ObjectRefHandle                       g_probe;

		RE::TESObjectREFR* TownAnchor(int a_town)
		{
			for (const auto& l : gen::kLocations) {
				if (l.town == a_town) {
					return RE::TESForm::LookupByID<RE::TESObjectREFR>(l.anchor);
				}
			}
			return nullptr;
		}

		RE::NiPoint3 SlotPos(const TownPlan& a_plan, int a_slot)
		{
			const auto& s = gen::kSlots[a_slot];
			const float a = a_plan.yaw;
			return { a_plan.center.x + s.x * std::cos(a) + s.y * std::sin(a), a_plan.center.y - s.x * std::sin(a) + s.y * std::cos(a), a_plan.center.z };
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
			const auto got = probe->GetPosition();
			const float dxy = std::hypot(got.x - at.x, got.y - at.y);
			if (dxy > gen::kPlanSnap) {
				return std::nullopt;
			}
			return got;
		}

		struct Best
		{
			float    score{ FLT_MAX };
			TownPlan plan;
		};
		std::array<Best, gen::kTowns.size()> g_best{};

		bool NearDoor(RE::TESObjectREFR* a_anchor, const RE::NiPoint3& a_at)
		{
			bool door = false;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep + gen::kPlanDoor * 2, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() && r->GetBaseObject()->Is(RE::FormType::Door) && std::hypot(r->GetPosition().x - a_at.x, r->GetPosition().y - a_at.y) < gen::kPlanDoor) {
					door = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return door;
		}

		// Score a candidate alley (lower is better); 0 failures + flat + no doors near = accept immediately.
		bool TryCandidate(int a_town, RE::TESObjectREFR* a_anchor, int a_index)
		{
			const int perDist = gen::kPlanAngles * static_cast<int>(gen::kPlanYaw.size());
			const int di = a_index / perDist, rest = a_index % perDist;
			const int ai = rest / static_cast<int>(gen::kPlanYaw.size()), yi = rest % static_cast<int>(gen::kPlanYaw.size());
			if (gen::kPlanDist[di] == 0 && ai > 0) {
				return false;
			}
			const float ang = ai * 2.0f * RE::NI_PI / gen::kPlanAngles;
			const auto  a = a_anchor->GetPosition();
			TownPlan    plan;
			plan.center = { a.x + std::sin(ang) * gen::kPlanDist[di], a.y + std::cos(ang) * gen::kPlanDist[di], a.z };
			plan.yaw = gen::kPlanYaw[yi] * RE::NI_PI / 180.0f;
			float lo = FLT_MAX, hi = -FLT_MAX;
			int   fails = 0, doors = 0;
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
				doors += NearDoor(a_anchor, want) ? 1 : 0;
			}
			const float spread = hi > lo ? hi - lo : 0;
			auto        c = Snap(a_anchor, plan.center);
			plan.center.z = c ? c->z : (lo < hi ? (lo + hi) * 0.5f : a.z);
			plan.state = TownPlan::State::Ready;
			const float score = fails * 1000.0f + doors * 400.0f + std::max(0.0f, spread - gen::kPlanHeight) * 5.0f + gen::kPlanDist[di] * 0.05f;
			if (score < g_best[a_town].score) {
				g_best[a_town] = { score, plan };
			}
			if (fails == 0 && doors == 0 && spread <= gen::kPlanHeight) {
				g_towns[a_town] = plan;
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

		// Existing alley from an earlier session? The ammo cache only exists in this layout, so it marks it.
		bool AdoptExisting(int a_town, RE::TESObjectREFR* a_anchor)
		{
			// Alleys built by the v0.4.0 fallback sit right at the marker (Whiterun's gate): rebuild those.
			int ammoIdx = -1;
			for (int i = 0; i < static_cast<int>(gen::kObjects.size()); ++i) {
				if (gen::kObjects[i].kind == 3 && !gen::kObjects[i].retired) {
					ammoIdx = i;
				}
			}
			if (auto* ammoBase = ammoIdx >= 0 ? Forms::Object(ammoIdx) : nullptr) {
				bool atMarker = false;
				RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, 900.0f, [&](RE::TESObjectREFR* r) {
					if (r && !r->IsDeleted() && r->GetBaseObject() == ammoBase && NearDoor(a_anchor, r->GetPosition())) {
						atMarker = true;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				if (atMarker) {
					logger::info("shops: {} alley blocks a door, rebuilding", gen::kTowns[a_town]);
					return false;
				}
			}
			RE::TESForm* ammo = nullptr;
			int          ammoSlot = -1;
			for (const auto& l : gen::kLocations) {
				if (l.town == a_town && gen::kObjects[l.object].kind == 3 && !gen::kObjects[l.object].retired) {
					ammo = Forms::Object(l.object), ammoSlot = l.slot;
				}
			}
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				if (r && !r->IsDeleted() && r->GetBaseObject() == ammo) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!found || ammoSlot < 0) {
				return false;
			}
			TownPlan plan;
			plan.yaw = found->GetAngleZ() - gen::kSlots[ammoSlot].faceDeg * RE::NI_PI / 180.0f;
			const auto& s = gen::kSlots[ammoSlot];
			const auto  p = found->GetPosition();
			plan.center = { p.x - (s.x * std::cos(plan.yaw) + s.y * std::sin(plan.yaw)), p.y - (-s.x * std::sin(plan.yaw) + s.y * std::cos(plan.yaw)), p.z };
			plan.state = TownPlan::State::Ready;
			g_towns[a_town] = plan;
			return true;
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
			if (plan.state != TownPlan::State::Ready || !anchor || !base) {
				return;
			}
			auto want = SlotPos(plan, loc.slot);
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && std::hypot(r->GetPosition().x - want.x, r->GetPosition().y - want.y) < 80.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!found) {
				if (auto p = Snap(anchor, want)) {
					want.z = p->z;
				}
				auto ref = anchor->PlaceObjectAtMe(base, false);
				if (!ref) {
					return;
				}
				ref->SetScale(gen::kObjects[loc.object].scale);
				ref->SetPosition(want);
				ref->SetAngle({ 0, 0, SlotFacing(plan, loc.slot) });
				found = ref.get();
				logger::info("shops: placed {} in {} at ({:.0f},{:.0f},{:.0f})", gen::kObjects[loc.object].id, gen::kTowns[loc.town], want.x, want.y, want.z);
			}
			g_placed[a_index] = found->CreateRefHandle();
			Dress(a_index, found);
		}

		void TickTowns()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				auto* anchor = TownAnchor(t);
				auto* cell = anchor ? anchor->GetParentCell() : nullptr;
				if (!cell || !cell->IsAttached() || anchor->GetPosition().GetDistance(player->GetPosition()) > 6000.0f) {
					continue;
				}
				auto& plan = g_towns[t];
				if (plan.state == TownPlan::State::Unknown) {
					if (AdoptExisting(t, anchor)) {
						logger::info("shops: {} alley already built", gen::kTowns[t]);
					} else {
						Sweep(t, anchor);
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

		void SpinBox(RE::TESObjectREFR* a_ref)
		{
			if (g_show.phase == Show::Offering && g_show.chest.get().get() == a_ref) {
				Gun::Give(g_show.gun);
				Forms::Play(gen::Sound::buy);
				Hud::Banner(std::string(gen::kGuns[g_show.gun].label), 2.5f);
				logger::info("box: took {}", gen::kGuns[g_show.gun].id);
				Remove(g_show.display);
				g_show.phase = Show::Idle;
				return;
			}
			if (g_show.phase != Show::Idle) {
				return;  // the Coffer is busy
			}
			const int town = TownOf(a_ref);
			logger::info("box: activated in {} (Coffer is in {}), phase {}", gen::kTowns[town], gen::kTowns[ActiveBoxTown()], static_cast<int>(g_show.phase));
			if (town != ActiveBoxTown()) {
				Hud::Banner(std::string(gen::kText_box_short) + " is in " + std::string(gen::kTowns[ActiveBoxTown()]), 3.0f);
				return;
			}
			const int crew = Save::Crew();
			const int cost = crew >= 0 && gen::kCrew[crew].passive == 3 ? static_cast<int>(gen::kCrew[crew].value) : gen::kObjects[0].cost;
			if (!Points::Spend(cost)) {
				return;
			}
			Forms::PlayAt(gen::Sound::coffer_open, a_ref);
			Forms::PlayAt(gen::Sound::coffer_spin, a_ref);
			g_show = {};
			g_show.chest = a_ref->CreateRefHandle();
			g_show.phase = Show::Spinning;
			const int spins = BoxSpins() + 1;
			// Cheese wheel (the Coffer gets bored): never in the first 4 spins at a spot, then 1 in 6. Points back.
			if (spins > 4 && std::uniform_int_distribution<int>(1, 6)(g_rng) == 1) {
				Points::Add(cost);
				auto towns = BoxTowns();
				std::erase(towns, town);
				g_show.cheeseTown = towns.empty() ? town : towns[std::uniform_int_distribution<std::size_t>(0, towns.size() - 1)(g_rng)];
				g_show.cheese = true;
				SetBox(g_show.cheeseTown, 0);
				logger::info("box: cheese, moving to {}", gen::kTowns[g_show.cheeseTown]);
				return;
			}
			SetBox(town, spins);
			int total = 0;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				total += Gun::Owns(i) ? 0 : gen::kGuns[i].boxWeight;
			}
			int roll = total > 0 ? std::uniform_int_distribution<int>(1, total)(g_rng) : 0;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				const int w = Gun::Owns(i) ? 0 : gen::kGuns[i].boxWeight;
				if (w > 0 && (roll -= w) <= 0) {
					g_show.gun = i;
					logger::info("box: rolling {}", gen::kGuns[i].id);
					return;
				}
			}
			// Nothing left to give: refund.
			Points::Add(cost);
			g_show.phase = Show::Idle;
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

		std::string BoxTown() { return std::string(gen::kTowns[ActiveBoxTown()]); }

		bool OnButton(const RE::ButtonEvent* a_event)
		{
			if (!g_menu || !a_event || !a_event->IsDown()) {
				return false;
			}
			const auto code = a_event->GetIDCode();
			const auto dev = a_event->GetDevice();
			const int  n = static_cast<int>(gen::kPerks.size());
			if (dev == RE::INPUT_DEVICE::kKeyboard) {
				if (code >= 0x02 && code < 0x02 + static_cast<std::uint32_t>(n)) {  // 1..9
					g_sel = static_cast<int>(code - 0x02);
					BuyPerk(g_sel, g_menuRef.get().get());
				} else if (code == 0xC8) {
					g_sel = (g_sel + n - 1) % n;
				} else if (code == 0xD0) {
					g_sel = (g_sel + 1) % n;
				} else if (code == 0x1C) {
					BuyPerk(g_sel, g_menuRef.get().get());
				} else if (code == 0x0F || code == 0x0E) {
					g_menu = false;
				} else {
					return false;
				}
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kMouse && (code == 8 || code == 9)) {
				g_sel = (g_sel + (code == 8 ? n - 1 : 1)) % n;
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kGamepad) {
				if (code == 0x0001) g_sel = (g_sel + n - 1) % n;       // dpad up
				else if (code == 0x0002) g_sel = (g_sel + 1) % n;      // dpad down
				else if (code == 0x2000) g_menu = false;               // B
				else return false;
				return true;
			}
			return false;
		}

		void Tick(float a_delta)
		{
			if (g_menu) {
				auto r = g_menuRef.get();
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!r || !player || r->GetPosition().GetDistance(player->GetPosition()) > 300.0f) {
					g_menu = false;
				}
			}
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().perkMenu = g_menu;
				Hud::Edit().perkSel = g_sel;
			}
			for (auto& s : g_spinners) {
				if (auto r = s.ref.get(); r && r->Is3DLoaded()) {
					auto a = r->GetAngle();
					r->SetAngle({ a.x, a.y, a.z + s.degPerSec * RE::NI_PI / 180.0f * a_delta });
				}
			}
			TickShow(a_delta);
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
			case 0: SpinBox(a_ref); break;
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
