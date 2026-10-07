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
						const int pick = std::uniform_int_distribution<int>(0, static_cast<int>(gen::kGuns.size()) - 1)(g_rng);
						ShowModel(Forms::Ours(gen::kShowStat[pick]), gen::kShowRise * k);
					} else if (auto d = g_show.display.get()) {
						d->SetPosition(Local(chest.get(), 0, 0, 40.0f + gen::kShowRise * k));
					}
					if (g_show.t >= gen::kShowSpin) {
						g_show.t = 0;
						if (g_show.cheese) {
							ShowModel(Forms::Ours(gen::kShowCheese), gen::kShowRise);
							Forms::PlayAt(gen::Sound::coffer_cheese, chest.get());
							Hud::Banner(std::string(gen::kText_box_joke) + " (" + std::string(gen::kTowns[g_show.cheeseTown]) + ")", 6.0f);
							g_show.phase = Show::Cheese;
						} else {
							ShowModel(Forms::Ours(gen::kShowStat[g_show.gun]), gen::kShowRise);
							Hud::Banner("Take the " + std::string(gen::kGuns[g_show.gun].label) + " from " + std::string(gen::kText_box_short) + "!", 3.0f);
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

		RE::NiPoint3 Target(const gen::LocationDef& a_loc, RE::TESObjectREFR* a_anchor)
		{
			const float a = a_loc.angleDeg * RE::NI_PI / 180.0f;
			const auto  p = a_anchor->GetPosition();
			return { p.x + std::sin(a) * a_loc.distance, p.y + std::cos(a) * a_loc.distance, p.z };
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

		void PlaceIfMissing(std::size_t a_index)
		{
			const auto& loc = gen::kLocations[a_index];
			if (auto ref = g_placed[a_index].get(); ref && !ref->IsDeleted()) {
				return;
			}
			auto* anchor = RE::TESForm::LookupByID<RE::TESObjectREFR>(loc.anchor);
			auto* base = Forms::Object(loc.object);
			if (!anchor || !base) {
				return;
			}
			auto* cell = anchor->GetParentCell();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!cell || !cell->IsAttached() || !player || anchor->GetPosition().GetDistance(player->GetPosition()) > 6000.0f) {
				return;
			}
			const auto want = Target(loc, anchor);
			// Already in the world (placed in an earlier session and saved)?
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, loc.distance + 600.0f, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && r->GetPosition().GetDistance(want) < 500.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (found) {
				g_placed[a_index] = found->CreateRefHandle();
				if (std::abs(found->GetScale() - gen::kObjects[loc.object].scale) > 0.01f) {
					found->SetScale(gen::kObjects[loc.object].scale);
				}
				Dress(a_index, found);
				return;
			}
			auto ref = anchor->PlaceObjectAtMe(base, false);
			if (!ref) {
				return;
			}
			ref->SetScale(gen::kObjects[loc.object].scale);
			ref->SetPosition(want);
			ref->MoveToNearestNavmesh();
			const auto at = ref->GetPosition();
			const float face = std::atan2(anchor->GetPosition().x - at.x, anchor->GetPosition().y - at.y);  // face the town marker
			ref->SetAngle({ 0, 0, face });
			g_placed[a_index] = ref->CreateRefHandle();
			Dress(a_index, ref.get());
			logger::info("shops: placed {} in {} at ({:.0f},{:.0f},{:.0f})", gen::kObjects[loc.object].id, gen::kTowns[loc.town], at.x, at.y, at.z);
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

		void BuyPerk(const gen::ObjectDef& a_obj, RE::TESObjectREFR* a_ref)
		{
			const int perk = a_obj.ref;
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
			if (!Points::Spend(a_obj.cost)) {
				return;
			}
			RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(potion, nullptr, 1, nullptr);
			Forms::PlayAt(Forms::Brew(perk), a_ref);
			Hud::Banner(std::string(gen::kPerks[perk].label) + " added to your potions. Drink it!", 3.0f);
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
			g_spinners.clear();
			g_show = {};
		}

		std::string BoxTown() { return std::string(gen::kTowns[ActiveBoxTown()]); }

		void Tick(float a_delta)
		{
			for (auto& s : g_spinners) {
				if (auto r = s.ref.get(); r && r->Is3DLoaded()) {
					auto a = r->GetAngle();
					r->SetAngle({ a.x, a.y, a.z + s.degPerSec * RE::NI_PI / 180.0f * a_delta });
				}
			}
			TickShow(a_delta);
			if ((g_scan -= a_delta) > 0) {
				return;
			}
			g_scan = 2.0f;
			for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
				PlaceIfMissing(i);
			}
		}

		void OnActivate(RE::TESObjectREFR* a_ref)
		{
			const int obj = a_ref ? Forms::ObjectIndexOf(a_ref->GetBaseObject()) : -1;
			if (obj < 0) {
				return;
			}
			const auto& o = gen::kObjects[obj];
			switch (o.kind) {
			case 0: SpinBox(a_ref); break;
			case 1: BuyPerk(o, a_ref); break;
			default: BuyWall(o); break;
			}
		}
	}
}
