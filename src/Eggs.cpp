#include "BO3Z.h"

// (Noah) the hidden quests, Black Ops style (sheet: eggs): one for each city's Echo Altar, waiting once the city is
// freed, never explained in game. The altar's menu keeps its whispers (a line more for each step found) and the Codex
// the first. A quest's steps:
//   touch    glowing things round the altar (an element in hand if asked; at night if night; unhurt if nodamage;
//            all inside a time if seconds)            order  stones touched in the order they light up
//   altar    back to the altar                         kills  the night's dead slain on the marked ground
//   survive  the dead risen round the altar            boss   a named foe (Director's champion, town -10 - city)
// Its reward: a permanent buff, a summon (a lesser power: one summon out at a time, for the night) or Winterhold's
// beam. In a party, whoever finishes a quest gives its reward to everyone near that altar (Net kEgg).
// Progress lives in one global per quest (step x 1000 + progress; 99000 done), the gifts in another (bits).
namespace bo3z::Eggs
{
	namespace
	{
		constexpr int        kDone = 99000;
		constexpr RE::FormID kXMarker = 0x0000003B;
		constexpr int        kCount = static_cast<int>(gen::kEggs.size());

		RE::TESGlobal* Glob(std::uint32_t a_id) { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(a_id, gen::kPlugin); }
		int  Raw(int e) { auto* g = Glob(gen::kEggs[e].glob); return g ? static_cast<int>(g->value) : 0; }
		void SetRaw(int e, int v) { if (auto* g = Glob(gen::kEggs[e].glob)) g->value = static_cast<float>(v); }
		int  Gifts() { auto* g = Glob(gen::kEggGifts); return g ? static_cast<int>(g->value) : 0; }
		void SetGift(int e) { if (auto* g = Glob(gen::kEggGifts)) g->value = static_cast<float>(Gifts() | (1 << e)); }
		bool Gifted(int e) { return (Gifts() >> e) & 1; }
		bool Done(int e) { return Raw(e) >= kDone; }
		int  Step(int e) { return Done(e) ? gen::kEggs[e].nSteps : Raw(e) / 1000; }
		int  Prog(int e) { return Done(e) ? 0 : Raw(e) % 1000; }
		int  EggOf(int a_town)
		{
			for (int e = 0; e < kCount; ++e) {
				if (gen::kEggs[e].town == a_town) return e;
			}
			return -1;
		}
		int Index(std::string_view a_id)
		{
			for (int e = 0; e < kCount; ++e) {
				if (gen::kEggs[e].id == a_id) return e;
			}
			return -1;
		}
		bool Has(std::string_view a_id) { const int e = Index(a_id); return e >= 0 && Gifted(e); }

		void Remove(RE::ObjectRefHandle& h)
		{
			if (auto r = h.get()) {
				r->Disable();
				r->SetDelete(true);
			}
			h = {};
		}
		RE::ObjectRefHandle g_probe;
		std::optional<RE::NiPoint3> Walkable(RE::PlayerCharacter* a_p, const RE::NiPoint3& a_at, float a_maxShift)
		{
			auto probe = g_probe.get();
			if (!probe || probe->IsDeleted() || probe->GetWorldspace() != a_p->GetWorldspace()) {
				auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
				auto  ref = xm ? a_p->PlaceObjectAtMe(xm, false) : nullptr;
				if (!ref) return std::nullopt;
				g_probe = ref->CreateRefHandle();
				probe = ref;
			}
			RE::NiPoint3 at = a_at;
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(at, land)) at.z = std::max(land, at.z - 300.0f);
			probe->SetPosition(at);
			probe->MoveToNearestNavmesh();
			const auto got = probe->GetPosition();
			if (std::hypot(got.x - at.x, got.y - at.y) > a_maxShift) return std::nullopt;
			return got;
		}
		RE::ObjectRefHandle Place(RE::PlayerCharacter* a_p, std::uint32_t a_base, const RE::NiPoint3& a_at, float a_scale)
		{
			auto* base = Forms::Ours(a_base);
			auto  ref = base ? a_p->PlaceObjectAtMe(base, false) : nullptr;
			if (!ref) return {};
			ref->SetPosition(a_at);
			if (a_scale != 1.0f) ref->SetScale(a_scale);
			return ref->CreateRefHandle();
		}
		// the element in your hands: the bow's, else a Coffer spell's (right hand first): 1 fire, 2 frost, 3 shock (0 other)
		int HandElement()
		{
			auto el = [](std::string_view n) { return n == "fire" ? 1 : n == "frost" ? 2 : n == "shock" ? 3 : 0; };
			if (const int b = Bows::Held(); b >= 0) return el(Bows::Element(b));
			const auto h = Arcana::Hands();
			for (int i : { h[1], h[0] }) {
				if (i >= 0) {
					if (const int x = el(Arcana::ElementName(Arcana::ElementOf(i)))) return x;
				}
			}
			return 0;
		}
		constexpr const char* kElemName[4] = { "", "fire", "frost", "lightning" };

		struct Mark
		{
			RE::ObjectRefHandle obj, light;
			RE::NiPoint3        at;
			bool                taken = false;
		};
		struct Live
		{
			int                          egg = -1, step = -1;
			std::vector<Mark>            marks;
			std::vector<int>             order;
			RE::ObjectRefHandle          anchorObj, anchorLight;
			RE::NiPoint3                 anchor;
			std::vector<RE::ActorHandle> foes;
			bool                         started = false, hurt = false, toldDay = false;
			float                        timer = -1, showT = 0, holdT = 0;
			int                          last = -1;  // the order stone last touched (step off it before the next)
		} g_live;
		std::vector<RE::ActorHandle> g_summons;
		int                          g_summonEgg = -1;
		float                        g_beamCd = 0;

		void Clear()
		{
			for (auto& m : g_live.marks) Remove(m.obj), Remove(m.light);
			Remove(g_live.anchorObj), Remove(g_live.anchorLight);
			g_live = {};
		}
		std::uint32_t Hash(std::uint32_t x)
		{
			x ^= x >> 16, x *= 0x7feb352d, x ^= x >> 15, x *= 0x846ca68b, x ^= x >> 16;
			return x;
		}
		void Setup(RE::PlayerCharacter* a_p, int e, int st, RE::TESObjectREFR* a_altar)
		{
			Clear();
			g_live.egg = e, g_live.step = st;
			const auto& s = gen::kEggs[e].steps[st];
			const auto  c = a_altar->GetPosition();
			if (s.type == gen::kEggTouch || s.type == gen::kEggOrder) {
				const int prog = Prog(e);
				for (int i = 0; i < s.n; ++i) {
					const std::uint32_t h = Hash(static_cast<std::uint32_t>(e * 131 + st * 17 + i * 7 + 1));
					std::optional<RE::NiPoint3> at;
					for (int tries = 0; tries < 6 && !at; ++tries) {
						const float ang = (h % 3600) * 0.0017453f + tries * 1.1f + (s.type == gen::kEggOrder ? 6.2832f * i / s.n : 0.0f);
						const float r = s.r0 + ((h >> 12) % 1000) / 1000.0f * (s.r1 - s.r0);
						at = Walkable(a_p, c + RE::NiPoint3{ std::cos(ang) * r, std::sin(ang) * r, 0 }, 350.0f);
						if (at && std::abs(at->z - c.z) > 600.0f) at.reset();  // (never up on a roof)
					}
					Mark m;
					m.at = at.value_or(c + RE::NiPoint3{ 200.0f * (i + 1), 0, 0 });
					m.taken = s.type == gen::kEggTouch && i < prog;
					if (!m.taken) {
						m.obj = Place(a_p, gen::kRelRelic, m.at, s.type == gen::kEggOrder ? 0.55f : 0.32f);
						m.light = Place(a_p, gen::kRelRelicLight, m.at + RE::NiPoint3{ 0, 0, 90.0f }, 1.0f);
					}
					g_live.marks.push_back(m);
				}
				if (s.type == gen::kEggOrder) {
					for (int i = 0; i < s.n; ++i) g_live.order.push_back(i);
					std::shuffle(g_live.order.begin(), g_live.order.end(), std::mt19937(Hash(static_cast<std::uint32_t>(e * 977 + st))));
				}
			} else if (s.type == gen::kEggKills) {
				const std::uint32_t h = Hash(static_cast<std::uint32_t>(e * 313 + st));
				const float ang = (h % 3600) * 0.0017453f, r = s.r0 + ((h >> 12) % 1000) / 1000.0f * (s.r1 - s.r0);
				g_live.anchor = Walkable(a_p, c + RE::NiPoint3{ std::cos(ang) * r, std::sin(ang) * r, 0 }, 400.0f).value_or(c);
				g_live.anchorObj = Place(a_p, gen::kRelBeam, g_live.anchor, 0.5f);  // (the marked ground: a pale beam)
				g_live.anchorLight = Place(a_p, gen::kRelRelicLight, g_live.anchor + RE::NiPoint3{ 0, 0, 120.0f }, 1.0f);
			}
		}

		void Award(int e, bool a_loud)
		{
			if (Gifted(e)) return;
			SetGift(e);
			const auto& d = gen::kEggs[e];
			auto*       p = RE::PlayerCharacter::GetSingleton();
			if (d.reward == gen::kEggSummon || d.reward == gen::kEggSpell) {
				if (auto* sp = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(d.power, gen::kPlugin); sp && p && !p->HasSpell(sp)) {
					p->AddSpell(sp);
				}
			} else if (d.id == "toast" && p) {  // Hearth-Mead: health and stamina come back 15% faster, for good
				p->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealRateMult, 15.0f);
				p->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kStaminaRateMult, 15.0f);
			}
			if (a_loud) {
				Hud::Lore(std::string(d.title), std::string(d.rewardText), 9.0f);
				Forms::Play(gen::Sound::curse_lifted);
			}
			logger::info("eggs: {} rewarded", d.id);
		}
		void Complete(int e)
		{
			SetRaw(e, kDone);
			Clear();
			Stats::Note(Stats::kQuest);
			Points::Add(2500);
			Award(e, true);
			Net::EggDone(gen::kEggs[e].town);
			logger::info("eggs: {} done", gen::kEggs[e].id);
		}
		void Advance(int e)
		{
			const int st = Step(e) + 1;
			Forms::Play(gen::Sound::quest_step);
			if (st >= gen::kEggs[e].nSteps) {
				Complete(e);
				return;
			}
			SetRaw(e, st * 1000);
			Hud::Banner(std::string(gen::kEggs[e].steps[st].text), 6.0f);  // (found the way on: the next step is told)
			logger::info("eggs: {} step {}", gen::kEggs[e].id, st);
		}
		void Bump(int e, int a_n, std::string_view a_what)
		{
			const int prog = Prog(e) + 1;
			SetRaw(e, Step(e) * 1000 + prog);
			if (prog < a_n) {
				Forms::Play(gen::Sound::quest_step);
				Hud::Banner(fmt::format("{} {} of {}", a_what, prog, a_n), 2.5f);
			} else {
				Advance(e);
			}
		}

		void TickStep(RE::PlayerCharacter* a_p, int e, RE::TESObjectREFR* a_altar, float a_dt)
		{
			const int   st = Step(e);
			const auto& s = gen::kEggs[e].steps[st];
			const bool  night = Director::NightNow();
			const auto  pp = a_p->GetPosition();
			const auto  c = a_altar->GetPosition();
			switch (s.type) {
			case gen::kEggTouch:
				{
					if (s.nodamage && g_live.hurt && Prog(e) > 0) {  // (the Nightingale's coins slip away when you bleed)
						g_live.hurt = false;
						SetRaw(e, st * 1000);
						Hud::Banner("You were hurt - the " + std::string(s.name) + "s slip back into the shadows", 4.0f);
						Setup(a_p, e, st, a_altar);
						return;
					}
					if (s.seconds > 0 && g_live.timer >= 0 && (g_live.timer += a_dt) > s.seconds) {
						g_live.timer = -1;
						SetRaw(e, st * 1000);
						Hud::Banner("Too slow - the " + std::string(s.name) + "s gutter out", 4.0f);
						Setup(a_p, e, st, a_altar);
						return;
					}
					for (auto& m : g_live.marks) {
						if (m.taken || pp.GetDistance(m.at) > 170.0f) continue;
						if (s.night && !night) {
							if (!g_live.toldDay) g_live.toldDay = true, Hud::Banner("It sleeps. Come back at night.", 3.0f);
							continue;
						}
						if (s.element != 0 && HandElement() != s.element) {
							if (!g_live.toldDay) g_live.toldDay = true, Hud::Banner(fmt::format("It doesn't stir. It wants {} in your hands.", kElemName[s.element]), 3.0f);
							continue;
						}
						m.taken = true;
						Remove(m.obj), Remove(m.light);
						Gun::FxBlast("light", m.at + RE::NiPoint3{ 0, 0, 40.0f });
						if (s.seconds > 0 && g_live.timer < 0) g_live.timer = 0;
						if (Prog(e) == 0) g_live.hurt = false;
						Bump(e, s.n, std::string(s.name) + (s.n > 1 ? "s" : ""));
						return;
					}
					if (std::none_of(g_live.marks.begin(), g_live.marks.end(), [&](const Mark& m) { return !m.taken && pp.GetDistance(m.at) < 400.0f; })) g_live.toldDay = false;
					break;
				}
			case gen::kEggOrder:
				{
					// the order shown: the stones' lights wake one after another, then a pause, again
					const int n = static_cast<int>(g_live.order.size());
					g_live.showT += a_dt;
					const float cyc = 0.9f * n + 1.6f;
					const int   lit = static_cast<int>(std::fmod(g_live.showT, cyc) / 0.9f);
					const int   prog = Prog(e);
					for (int i = 0; i < n; ++i) {
						const bool done = std::find(g_live.order.begin(), g_live.order.begin() + std::min(prog, n), i) != g_live.order.begin() + std::min(prog, n);
						const bool on = done || (lit < n && g_live.order[lit] == i);
						if (auto l = g_live.marks[i].light.get()) {
							if (on && l->IsDisabled()) l->Enable(false);
							if (!on && !l->IsDisabled()) l->Disable();
						}
					}
					if (s.night && !night) break;
					for (int i = 0; i < n; ++i) {
						const float d = pp.GetDistance(g_live.marks[i].at);
						if (d > 170.0f) {
							if (g_live.last == i && d > 260.0f) g_live.last = -1;
							continue;
						}
						if (g_live.last == i) continue;
						g_live.last = i;
						if (g_live.order[prog] == i) {
							Gun::FxBlast("light", g_live.marks[i].at + RE::NiPoint3{ 0, 0, 60.0f });
							Bump(e, n, std::string(s.name) + (n > 1 ? "s" : ""));
						} else {
							SetRaw(e, st * 1000);
							Forms::Play(gen::Sound::rite_fail);
							Hud::Banner("Wrong - watch the " + std::string(s.name) + "s again", 3.0f);
							g_live.showT = 0;
						}
						return;
					}
					break;
				}
			case gen::kEggAltar:
				if (std::hypot(pp.x - c.x, pp.y - c.y) < 230.0f && std::abs(pp.z - c.z) < 200.0f) {
					Advance(e);
				}
				break;
			case gen::kEggKills:
				break;  // (OnUndeadDeath counts them)
			case gen::kEggSurvive:
			case gen::kEggBoss:
				{
					if (!night || !Director::Fighting()) {
						g_live.started = false, g_live.foes.clear(), g_live.holdT = 0;
						break;
					}
					if (pp.GetDistance(c) > 2200.0f) break;
					if (Coop::Client()) {  // (a friend's game raises nothing: it holds the altar a minute instead)
						if ((g_live.holdT += a_dt) > 60.0f) Advance(e);
						break;
					}
					if (s.type == gen::kEggBoss) {
						if (!g_live.started && s.row >= 0 && !Director::ChampionActive(-10 - gen::kEggs[e].town)) {
							const auto at = Walkable(a_p, c + RE::NiPoint3{ 700.0f, 0, 0 }, 500.0f).value_or(c);
							g_live.started = Director::RaiseChampion(-10 - gen::kEggs[e].town, gen::kEnemies[s.row].id, s.name, 12.0f, at);
						}
						break;  // (OnBossSlain moves it on)
					}
					if (!g_live.started) {
						g_live.started = true;
						Hud::Banner(std::string(s.text), 4.0f);
						for (int i = 0; i < s.n; ++i) {
							const float a = 6.2832f * i / s.n, r = 650.0f + 250.0f * (i % 3);
							const auto  spot = Walkable(a_p, c + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 500.0f);
							if (auto h = Director::RaiseTrial(spot.value_or(c), i % 6 == 5); h) g_live.foes.push_back(h);
						}
					}
					std::erase_if(g_live.foes, [](const RE::ActorHandle& h) { auto a = h.get(); return !a || a->IsDead() || a->IsDisabled(); });
					if (g_live.foes.empty()) Advance(e);
					break;
				}
			default:
				break;
			}
		}
	}

	void Tick(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !p->Is3DLoaded()) return;
		g_beamCd = std::max(0.0f, g_beamCd - a_delta);
		// the Gildergreen's Guardian mends you while it stands near
		if (g_summonEgg >= 0 && gen::kEggs[g_summonEgg].id == "sap") {
			for (auto& h : g_summons) {
				if (auto a = h.get(); a && !a->IsDead() && a->GetPosition().GetDistance(p->GetPosition()) < 900.0f) {
					auto* av = p->AsActorValueOwner();
					av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.012f * a_delta);
					break;
				}
			}
		}
		if (!Director::NightNow() && !g_summons.empty()) {  // dawn: the summons go back
			for (auto& h : g_summons) {
				if (auto a = h.get()) a->Disable(), a->SetDelete(true);
			}
			g_summons.clear();
		}
		const int town = Curse::ZoneTown();
		const int e = town >= 0 && Curse::Freed(town) ? EggOf(town) : -1;
		if (e < 0 || Done(e)) {
			if (g_live.egg >= 0) Clear();
			return;
		}
		auto* altar = Shops::Placed(town, 0);
		if (!altar) return;
		const int st = Step(e);
		if (g_live.egg != e || g_live.step != st) Setup(p, e, st, altar);
		TickStep(p, e, altar, a_delta);
		{
			std::scoped_lock l(Hud::Lock());
			const auto& s = gen::kEggs[e].steps[std::min(Step(e), gen::kEggs[e].nSteps - 1)];
			Hud::Edit().eggDark = s.dark && g_live.started && !Done(e);
		}
	}

	void OnHurt() { g_live.hurt = true; }

	void OnUndeadDeath(RE::Actor* a_actor)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!a_actor || !p) return;
		// Arkay's Mercy: the dead you kill near you sometimes give back a little health
		if (Has("rites") && a_actor->GetPosition().GetDistance(p->GetPosition()) < 700.0f && std::rand() % 4 == 0) {
			auto* av = p->AsActorValueOwner();
			av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.06f);
		}
		const int e = g_live.egg;
		if (e < 0 || Done(e)) return;
		const auto& s = gen::kEggs[e].steps[Step(e)];
		if (s.type == gen::kEggKills && Director::NightNow() && a_actor->GetPosition().GetDistance(g_live.anchor) < 550.0f) {
			const int prog = Prog(e) + 1;
			SetRaw(e, Step(e) * 1000 + prog);
			if (prog >= s.n) {
				Advance(e);
			} else if (prog % 5 == 0) {
				Forms::Play(gen::Sound::quest_step);
				Hud::Banner(fmt::format("The ground drinks: {} of {}", prog, s.n), 2.5f);
			}
		}
	}

	void OnBossSlain(int a_town)
	{
		const int e = EggOf(a_town);
		if (e >= 0 && !Done(e) && gen::kEggs[e].steps[Step(e)].type == gen::kEggBoss) {
			Advance(e);
		}
	}

	bool OnSpellCast(RE::FormID a_spell)
	{
		auto* sp = RE::TESForm::LookupByID(a_spell);
		auto* file = sp ? sp->GetFile(0) : nullptr;
		if (!file || file->GetFilename() != gen::kPlugin) return false;
		const auto local = sp->GetLocalFormID();
		for (int e = 0; e < kCount; ++e) {
			const auto& d = gen::kEggs[e];
			if (d.power == 0 || local != d.power) continue;
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (d.reward == gen::kEggSpell) {
				if (g_beamCd > 0) {
					Hud::Banner(fmt::format("The Eye's light gathers again ({:.0f}s)", std::ceil(g_beamCd)), 2.0f);
				} else {
					g_beamCd = 45.0f;
					Gun::EyeBeam();
				}
				return true;
			}
			// a summon: one out at a time, for the night
			for (auto& h : g_summons) {
				if (auto a = h.get()) a->Disable(), a->SetDelete(true);
			}
			g_summons = Echo::Summon(d.summonPlugin, d.summonId, std::max(1, d.summonCount), p->GetPosition());
			g_summonEgg = e;
			return true;
		}
		return false;
	}

	void Heard(int a_town)  // a friend finished this city's quest: yours too if you were there
	{
		const int e = EggOf(a_town);
		auto*     p = RE::PlayerCharacter::GetSingleton();
		auto*     altar = e >= 0 ? Shops::Placed(a_town, 0) : nullptr;
		if (e < 0 || !p || !altar || p->GetPosition().GetDistance(altar->GetPosition()) > 4000.0f || Gifted(e)) return;
		SetRaw(e, kDone);
		Clear();
		Stats::Note(Stats::kQuest);
		Award(e, true);
	}

	std::vector<std::string> Whispers(int a_town)
	{
		std::vector<std::string> out;
		const int e = EggOf(a_town);
		if (e < 0 || !Curse::Freed(a_town)) return out;
		const auto& d = gen::kEggs[e];
		if (Done(e)) {
			out.push_back(std::string(d.title) + " - done. " + std::string(d.rewardText));
			return out;
		}
		const int shown = std::clamp(Step(e) + 1, 1, 3);
		for (int i = 0; i < shown; ++i) out.push_back(std::string(d.hints[i]));
		return out;
	}

	float DamageMult()  // Vaermina's Gift: +10% at night (+5% once SLEN is silenced: the night's bonuses are capped)
	{
		if (!Has("dream") || !Director::NightNow()) return 1.0f;
		return Echo::WordSilenced(0) ? 1.05f : 1.10f;
	}
	bool ShadowStep() { return Has("debt"); }
	void DevStep(int a_town)  // dev: the city's quest one step on
	{
		const int e = EggOf(a_town);
		if (e >= 0 && !Done(e)) Advance(e);
	}
}
