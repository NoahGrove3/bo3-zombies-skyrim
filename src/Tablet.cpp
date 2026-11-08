#include "BO3Z.h"

// (Noah: "something like the mystery box from call of duty zombies" - the Tablet of Mysteries; docs/TABLET_OF_MYSTERIES.md)
// The tablet stands at one freed city at a time (its spot: the city's tablet location, src/Shops.cpp places it there and
// nowhere else) and is animated here, on its own model (tools/tablet.py), by its nodes' names:
//   idle  - the slab bobs and sways a little; its glyph rings drift; (in the model) the glyphs breathe, the pool swirls
//   roll  - the rings grind round unevenly, stalling and lurching; the slab shudders; the eight gems light one by one;
//           sparks spit from the cracks; the weapons' ghosts flicker in the pool; a battered win: gold light and a bell
// Saved with the altars' rolls (Shops::RollState): the city, the rolls since it last moved, tonight's price steps.
namespace bo3z::Tablet
{
	namespace
	{
		std::mt19937 g_rng{ std::random_device{}() };
		int   g_town = -1;          // where it stands (-1: no city freed yet)
		int   g_rolls = 0;          // rolls since it last moved
		int   g_moveAfter = 6;      // (4..8) after this many the skull can come
		int   g_steps = 0, g_night = -1;  // tonight's spins and repairs (the price's steps)
		enum class Phase { Idle, Rolling, Offering } g_phase = Phase::Idle;
		float g_t = 0.0f;           // into the phase
		float g_clock = 0.0f;       // its own time (the idle's sway)
		float g_ringO = 0.0f, g_ringI = 0.0f;      // the rings' turn (radians)
		float g_stallO = 0.0f, g_stallI = 0.0f;    // (grinding: a ring's moment of sticking)
		bool  g_gold = false;       // this roll is a battered (stronger) win
		bool  g_bell = false;
		RE::ObjectRefHandle g_ref;  // the one being animated

		// the model's measures (tools/tablet.py): the pool's centre on the face (x 0, z Z0 + 42)
		constexpr float kPoolZ = 116.0f;
		constexpr float kRollTime = 3.6f, kOfferTime = 12.0f;

		int Freed()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		// a freed city for it (not the one it leaves, when there is another)
		int PickTown(int a_not)
		{
			std::vector<int> ok;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				if (Curse::Freed(t) && t != a_not) ok.push_back(t);
			}
			if (ok.empty()) return Curse::Freed(a_not) ? a_not : -1;
			return ok[std::uniform_int_distribution<int>(0, static_cast<int>(ok.size()) - 1)(g_rng)];
		}
		void Night()
		{
			const int night = Save::Get(gen::kAV_round);
			if (night != g_night) g_night = night, g_steps = 0;
		}

		RE::NiAVObject* Find(RE::NiAVObject* a_root, const char* a_name) { return a_root ? a_root->GetObjectByName(a_name) : nullptr; }
		void Show(RE::NiAVObject* a_root, const char* a_name, bool a_on)
		{
			if (auto* o = Find(a_root, a_name); o && o->GetAppCulled() == a_on) o->SetAppCulled(!a_on);
		}
		// a ring's node turned a_ang about the pool's centre, in the face's plane (about the Y axis through x 0, z kPoolZ)
		void TurnRing(RE::NiAVObject* a_node, float a_ang)
		{
			if (!a_node) return;
			const float c = std::cos(a_ang), s = std::sin(a_ang);
			RE::NiMatrix3 r;
			r.entry[0][0] = c, r.entry[0][1] = 0, r.entry[0][2] = s;
			r.entry[1][0] = 0, r.entry[1][1] = 1, r.entry[1][2] = 0;
			r.entry[2][0] = -s, r.entry[2][1] = 0, r.entry[2][2] = c;
			a_node->local.rotate = r;
			// (turned about the pool's centre, not the model's origin: the centre stays where it was)
			a_node->local.translate = RE::NiPoint3{ -s * kPoolZ, 0.0f, kPoolZ - c * kPoolZ };
		}

		void Animate(RE::TESObjectREFR* a_ref, float a_delta)
		{
			auto* root = a_ref ? a_ref->Get3D() : nullptr;
			auto* body = Find(root, "TabletBody");
			if (!body) return;
			g_clock += a_delta;
			const bool rolling = g_phase == Phase::Rolling;
			const float k = rolling ? std::clamp(g_t / kRollTime, 0.0f, 1.0f) : 0.0f;
			// the slab: a slow bob and sway; in a roll it shudders, harder as the reel slows to its end
			float sx = 0.0f, sz = 0.0f;
			if (rolling) {
				const float amp = 0.25f + 0.55f * k;
				sx = std::uniform_real_distribution<float>(-amp, amp)(g_rng), sz = std::uniform_real_distribution<float>(-amp, amp)(g_rng);
			}
			body->local.translate = RE::NiPoint3{ sx, 0.0f, 2.6f * std::sin(g_clock * 0.9f) + sz };
			const float yaw = 0.055f * std::sin(g_clock * 0.33f);
			RE::NiMatrix3 by;
			by.entry[0][0] = std::cos(yaw), by.entry[0][1] = -std::sin(yaw), by.entry[0][2] = 0;
			by.entry[1][0] = std::sin(yaw), by.entry[1][1] = std::cos(yaw), by.entry[1][2] = 0;
			by.entry[2][0] = 0, by.entry[2][1] = 0, by.entry[2][2] = 1;
			body->local.rotate = by;
			// the rings: idle, a slow drift; rolling, they grind - fast and uneven, sticking a moment and lurching on, the
			// two the opposite ways, slowing to a stop with the reel
			float vo = 0.06f, vi = -0.09f;
			if (rolling) {
				const float sp = 7.0f * (1.0f - k * k) + 0.4f;
				if ((g_stallO -= a_delta) < -0.6f && std::uniform_real_distribution<float>(0, 1)(g_rng) < a_delta * 2.5f) g_stallO = 0.12f;
				if ((g_stallI -= a_delta) < -0.5f && std::uniform_real_distribution<float>(0, 1)(g_rng) < a_delta * 3.0f) g_stallI = 0.10f;
				vo = g_stallO > 0 ? 0.0f : sp * (0.8f + 0.4f * std::sin(g_t * 9.0f));
				vi = g_stallI > 0 ? 0.0f : -sp * 1.3f * (0.8f + 0.4f * std::sin(g_t * 11.0f + 1.0f));
			}
			g_ringO += vo * a_delta, g_ringI += vi * a_delta;
			TurnRing(Find(root, "RingOuter"), g_ringO);
			TurnRing(Find(root, "RingInner"), g_ringI);
			// the gems: lit one by one through the roll, all lit while the win waits, dark at rest
			const int lit = rolling ? static_cast<int>(k * 8.0f + 0.5f) : g_phase == Phase::Offering ? 8 : 0;
			for (int i = 0; i < 8; ++i) Show(root, fmt::format("Gem{}_bb", i).c_str(), i < lit);
			// sparks spitting from the cracks: in bursts through the roll
			for (int i = 0; i < 6; ++i) {
				const bool on = rolling && std::fmod(g_t * (1.3f + 0.21f * i) + i * 0.37f, 1.0f) < 0.35f;
				Show(root, fmt::format("Spark{}_bb", i).c_str(), on);
			}
			Show(root, "Ghost", rolling && k < 0.92f);           // (the reel's ghosts, until it lands)
			Show(root, "GoldFlare_bb", g_gold && (g_phase == Phase::Offering || (rolling && k > 0.85f)));
			RE::NiUpdateData ud;
			body->Update(ud);
		}
	}

	int Town() { return g_town; }
	int Price()
	{
		Night();
		return 1500 + 500 * g_steps;  // (Noah: 1,500, +500 for every spin or repair, back to 1,500 at dawn)
	}

	void OnActivate(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) return;
		if (g_phase != Phase::Idle) {
			Hud::Banner(g_phase == Phase::Rolling ? "The tablet is still turning" : "The tablet's offering waits", 1.5f);
			return;
		}
		// (step 1 of the build: the tablet in the world and its roll - its weapons, price and repairs come next)
		g_ref = a_ref->CreateRefHandle();
		g_phase = Phase::Rolling, g_t = 0.0f, g_bell = false;
		g_gold = std::uniform_int_distribution<int>(0, 99)(g_rng) < 15;   // (the stronger weapons: 15%)
		Forms::PlayAt(gen::Sound::coffer_charge, a_ref);
		Forms::PlayAt(gen::Sound::coffer_spin, a_ref);
		logger::info("tablet: rolled in {} ({})", g_town >= 0 ? gen::kTowns[g_town] : "?", g_gold ? "battered" : "worn");
	}

	void Tick(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		Night();
		// where it stands: a freed city (chosen when the first is freed, and again should its city fall)
		if ((g_town < 0 || !Curse::Freed(g_town)) && Freed() > 0) {
			g_town = PickTown(-1), g_rolls = 0, g_moveAfter = std::uniform_int_distribution<int>(4, 8)(g_rng);
			logger::info("tablet: stands in {}", g_town >= 0 ? gen::kTowns[g_town] : "nowhere");
		}
		auto* ref = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr;
		if (g_phase != Phase::Idle) {
			g_t += a_delta;
			if (g_phase == Phase::Rolling && g_t >= kRollTime) {
				g_phase = Phase::Offering, g_t = 0.0f;
				if (auto r = g_ref.get()) {
					Forms::PlayAt(gen::Sound::coffer_land, r.get());
					if (g_gold) Forms::PlayAt(gen::Sound::shout_toll, r.get());   // (a battered win: the tablet rings like a bell)
				}
				Hud::Banner(g_gold ? "THE TABLET RINGS GOLD - its weapons are still being forged" : "The tablet settles - its weapons are still being forged", 4.0f);
			} else if (g_phase == Phase::Offering && g_t >= kOfferTime) {
				g_phase = Phase::Idle, g_t = 0.0f, g_gold = false;
			}
		}
		if (ref && ref->Is3DLoaded() && ref->GetPosition().GetDistance(p->GetPosition()) < 6000.0f) Animate(ref, a_delta);
	}

	std::vector<std::uint8_t> State()
	{
		return { static_cast<std::uint8_t>(g_town + 1), static_cast<std::uint8_t>(std::clamp(g_rolls, 0, 255)), static_cast<std::uint8_t>(g_moveAfter),
			static_cast<std::uint8_t>(std::clamp(g_steps, 0, 255)) };
	}
	void SetState(const std::uint8_t* a_v, std::size_t a_n)
	{
		g_town = -1, g_rolls = 0, g_moveAfter = 6, g_steps = 0, g_night = -1;
		g_phase = Phase::Idle, g_t = 0.0f, g_gold = false;
		if (a_n < 4) return;
		g_town = static_cast<int>(a_v[0]) - 1;
		if (g_town >= static_cast<int>(gen::kTowns.size())) g_town = -1;
		g_rolls = a_v[1], g_moveAfter = std::clamp<int>(a_v[2], 4, 8), g_steps = a_v[3];
		g_night = Save::Get(gen::kAV_round);
	}

	// dev: tablet [town|roll|gold]: move it to a city, or roll it now (gold: a battered roll)
	void Dev(std::string_view a_what, int a_town)
	{
		if (a_what == "town" && a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size())) {
			g_town = a_town, g_rolls = 0;
			logger::info("dev: the tablet moved to {}", gen::kTowns[a_town]);
		} else if (a_what == "roll" || a_what == "gold") {
			if (auto* r = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr) {
				g_phase = Phase::Idle;
				OnActivate(r);
				g_gold = a_what == "gold";
			}
		}
	}
}
