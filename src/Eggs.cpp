#include "BO3Z.h"

// (Noah: "one longer Easter egg for each class ... make the wonders harder to obtain") the hidden quests, Black Ops style
// (sheet: eggs): one per class, waking once six cities are freed, never explained in game - and finishing it is the
// only way to win the class's wonder. Each step is played at a freed city's Echo Altar, and never in the same city as the
// step before. The altar's menu keeps its whispers (each step's hint, a line more for each step found). A step:
//   touch    glowing things round the altar (an element in hand if asked; at night if night; unhurt if nodamage;
//            all inside a time if seconds)            order  stones touched in the order they light up
//   altar    back to an altar                          kills  the night's dead slain on the marked ground
//   classkills  the night's dead killed your class's way in that city   survive  the dead risen round the altar
//   boss     a named foe (Director's champion, town -10 - city)
// Progress lives in one global per quest (step x 1000 + progress; 99000 done) and one for its cities (the city of the
// step under way + 1, and x 100 the city of the step before + 1).
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
		// your class's quest, once it is open (Classes: six cities freed) and not yet done
		int Active()
		{
			const int c = Classes::Get();
			if (c < 0 || !Classes::WonderOpen()) return -1;
			for (int e = 0; e < kCount; ++e) {
				if (gen::kEggs[e].cls == c) return Done(e) ? -1 : e;
			}
			return -1;
		}
		int  Towns(int e) { auto* g = Glob(gen::kEggs[e].townGlob); return g ? static_cast<int>(g->value) : 0; }
		void SetTowns(int e, int v) { if (auto* g = Glob(gen::kEggs[e].townGlob)) g->value = static_cast<float>(v); }
		int  CurTown(int e) { return Towns(e) % 100 - 1; }   // the city of the step under way (-1 none yet)
		int  PrevTown(int e) { return Towns(e) / 100 - 1; }  // the city of the step before (-1 none)
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
		float                        g_beamCd = 0;
		int                          g_hintKey = -1, g_lastProg = 0;  // (the hints: the step they're for, its progress last seen)
		float                        g_stuck = 0;                     // night seconds spent on it without getting anywhere
		bool                         g_plain = false;                 // the altar has said plainly what it wants

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
			if (a_loud) {
				Hud::Lore(std::string(d.title), std::string(d.rewardText), 9.0f);
				Forms::Play(gen::Sound::curse_lifted);
			}
			Classes::EarnWonder(d.cls);  // (the quest's end is the wonder)
			logger::info("eggs: {} rewarded", d.id);
		}
		void Complete(int e)
		{
			SetRaw(e, kDone);
			Clear();
			Stats::Note(Stats::kQuest);
			Points::Add(2500);
			Award(e, true);
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
			SetTowns(e, (CurTown(e) + 1) * 100);  // (this city did its step: the next is found in another)
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
						if (!g_live.started && s.row >= 0 && !Director::ChampionActive(-10 - CurTown(e))) {
							const auto at = Walkable(a_p, c + RE::NiPoint3{ 700.0f, 0, 0 }, 500.0f).value_or(c);
							g_live.started = Director::RaiseChampion(-10 - CurTown(e), gen::kEnemies[s.row].id, s.name, 14.0f, at);
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
		const int e = Active();
		const int town = Curse::ZoneTown();
		if (e < 0 || town < 0 || !Curse::Freed(town)) {
			if (g_live.egg >= 0) Clear();
			return;
		}
		// which city this step is played in: the first freed city you reach that did not give the step before - and once
		// the step has begun, only that one
		const int cur = CurTown(e);
		if (cur != town) {
			static float told = 0;
			told = std::max(0.0f, told - a_delta);
			if (cur >= 0 && Prog(e) > 0) {
				if (told <= 0) told = 90.0f, Hud::Banner(fmt::format("Your path's secret waits in {}", gen::kTowns[cur]), 4.0f);
				if (g_live.egg >= 0) Clear();
				return;
			}
			if (town == PrevTown(e)) {
				if (told <= 0 && Step(e) > 0) told = 90.0f, Hud::Banner("The altar here has given its sign. The next waits in another freed city.", 4.0f);
				if (g_live.egg >= 0) Clear();
				return;
			}
			SetTowns(e, (PrevTown(e) + 1) * 100 + town + 1);
		}
		auto* altar = Shops::Placed(town, 0);
		if (!altar) return;
		const int st = Step(e);
		// (Noah: "how would players figure out the Easter eggs with no prior context?") the hints come to you: reach a city
		// where the step can be played and its whisper is spoken; spend long enough on a step without getting anywhere
		// (about two nights' worth, here at night) and the altar says plainly what it wants - and keeps saying it in its
		// whispers
		{
			static int   lastTown = -1, lastKey = -1;
			const int    key = e * 100 + st;
			if (key != g_hintKey) g_hintKey = key, g_stuck = 0, g_plain = false, g_lastProg = Prog(e);
			if (town != lastTown || key != lastKey) {
				lastTown = town, lastKey = key;
				Hud::Banner("The altar whispers: " + std::string(gen::kEggs[e].steps[st].hint), 6.0f);
			}
			if (Prog(e) != g_lastProg) g_lastProg = Prog(e), g_stuck = std::max(0.0f, g_stuck - 120.0f);
			if (Director::NightNow() && !g_plain && (g_stuck += a_delta) > 480.0f) {
				g_plain = true;
				Forms::Play(gen::Sound::quest_step);
				Hud::Lore("THE ALTAR SPEAKS PLAINLY", std::string(gen::kEggs[e].steps[st].text), 9.0f);
			}
		}
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

	void OnClassKill()  // (Classes) a kill your class's own way
	{
		const int e = g_live.egg;
		if (e < 0 || Done(e) || !Director::NightNow() || Curse::ZoneTown() != CurTown(e)) return;
		const auto& s = gen::kEggs[e].steps[Step(e)];
		if (s.type != gen::kEggClasskills) return;
		const int prog = Prog(e) + 1;
		SetRaw(e, Step(e) * 1000 + prog);
		if (prog >= s.n) {
			Advance(e);
		} else if (prog % 10 == 0) {
			Forms::Play(gen::Sound::quest_step);
			Hud::Banner(fmt::format("The secret drinks: {} of {}", prog, s.n), 2.5f);
		}
	}

	void OnBossSlain(int a_town)
	{
		const int e = Active();
		if (e >= 0 && a_town == CurTown(e) && gen::kEggs[e].steps[Step(e)].type == gen::kEggBoss) {
			Advance(e);
		}
	}

	bool OnSpellCast(RE::FormID) { return false; }  // (the city quests' summon powers are retired)

	void Heard(int) {}  // (each class keeps its own quest: a friend's isn't yours)

	std::vector<std::string> Whispers(int a_town)
	{
		std::vector<std::string> out;
		if (!Curse::Freed(a_town)) return out;
		const int c = Classes::Get();
		for (int e = 0; e < kCount; ++e) {
			const auto& d = gen::kEggs[e];
			if (d.cls != c) continue;
			if (Done(e) || Gifted(e)) {
				out.push_back(std::string(d.title) + " - done. " + std::string(d.rewardText));
			} else if (Classes::WonderOpen()) {
				const int shown = std::clamp(Step(e) + 1, 1, d.nSteps);
				for (int i = 0; i < shown; ++i) out.push_back(std::string(d.steps[i].hint));
				if (g_plain && g_hintKey == e * 100 + Step(e)) out.push_back("Plainly: " + std::string(d.steps[Step(e)].text));
				if (a_town == PrevTown(e)) out.push_back("(This altar has given its sign. The next waits in another freed city.)");
			}
		}
		return out;
	}

	std::string Line()  // the class guide's line: how far along your path's secret you are
	{
		const int c = Classes::Get();
		for (int e = 0; e < kCount; ++e) {
			const auto& d = gen::kEggs[e];
			if (d.cls != c) continue;
			if (Done(e)) return std::string(d.title) + " - done";
			const int cur = CurTown(e);
			return fmt::format("{} - step {} of {}{}", d.title, Step(e) + 1, d.nSteps, cur >= 0 && Prog(e) > 0 ? fmt::format(" (under way in {})", gen::kTowns[cur]) : std::string(" (listen to the altars' whispers)"));
		}
		return {};
	}

	float DamageMult() { return 1.0f; }  // (Vaermina's Gift retired with the city quests)
	bool  ShadowStep() { return false; }  // (the Nightingale's Debt retired with them)
	void DevStep(int)  // dev: your class's quest one step on
	{
		const int e = Active();
		if (e >= 0 && !Done(e)) Advance(e);
	}
}
