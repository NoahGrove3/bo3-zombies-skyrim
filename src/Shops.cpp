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
				return;
			}
			auto ref = anchor->PlaceObjectAtMe(base, false);
			if (!ref) {
				return;
			}
			ref->SetPosition(want);
			ref->MoveToNearestNavmesh();
			const auto at = ref->GetPosition();
			const float face = std::atan2(anchor->GetPosition().x - at.x, anchor->GetPosition().y - at.y);  // face the town marker
			ref->SetAngle({ 0, 0, face });
			g_placed[a_index] = ref->CreateRefHandle();
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
			const int town = TownOf(a_ref);
			if (town != ActiveBoxTown()) {
				Hud::Banner("The Mystery Box is in " + std::string(gen::kTowns[ActiveBoxTown()]), 3.0f);
				return;
			}
			const int crew = Save::Crew();
			const int cost = crew >= 0 && gen::kCrew[crew].passive == 3 ? static_cast<int>(gen::kCrew[crew].value) : gen::kObjects[0].cost;
			if (!Points::Spend(cost)) {
				return;
			}
			const int spins = BoxSpins() + 1;
			// Teddy bear: never in the first 4 spins at a spot, then 1 in 6.
			if (spins > 4 && std::uniform_int_distribution<int>(1, 6)(g_rng) == 1) {
				Points::Add(cost);
				auto towns = BoxTowns();
				std::erase(towns, town);
				const int next = towns.empty() ? town : towns[std::uniform_int_distribution<std::size_t>(0, towns.size() - 1)(g_rng)];
				SetBox(next, 0);
				Hud::Banner("Bye bye! The box moved to " + std::string(gen::kTowns[next]), 5.0f);
				logger::info("box: teddy bear, moved to {}", gen::kTowns[next]);
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
					Gun::Give(i);
					Hud::Banner("Mystery Box: " + std::string(gen::kGuns[i].label), 3.0f);
					logger::info("box: {}", gen::kGuns[i].id);
					return;
				}
			}
		}

		void BuyPerk(const gen::ObjectDef& a_obj)
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
			Hud::Banner(std::string(gen::kPerks[perk].label) + " added to your potions. Drink it!", 3.0f);
		}

		void BuyWall(const gen::ObjectDef& a_obj)
		{
			const int   gun = a_obj.ref;
			const auto& g = gen::kGuns[gun];
			if (Gun::Owns(gun)) {
				if (Points::Spend(g.ammoCost)) {
					Gun::RefillAmmo(gun);
					Hud::Banner(std::string(g.label) + " ammo", 2.0f);
				}
				return;
			}
			if (Points::Spend(g.wallBuyCost)) {
				Gun::Give(gun);
				Hud::Banner(std::string(g.label), 2.0f);
			}
		}
	}

	namespace Shops
	{
		void Reset() { g_placed.fill({}); }

		std::string BoxTown() { return std::string(gen::kTowns[ActiveBoxTown()]); }

		void Tick(float a_delta)
		{
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
			case 1: BuyPerk(o); break;
			default: BuyWall(o); break;
			}
		}
	}
}
