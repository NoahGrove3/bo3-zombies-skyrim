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
		int  Step(int e) { return Done(e) ? gen::kEggs[e].nSteps : std::clamp(Raw(e) / 1000, 0, std::max(0, static_cast<int>(gen::kEggs[e].nSteps) - 1)); }  // (the audit: a bad save or console edit never indexes past its steps)
		int  Prog(int e) { return Done(e) ? 0 : Raw(e) % 1000; }
		// (Noah, 2026-11-11: "Any class, all 4 quests") the quest under way: one at a time - your own class's first while it is
		// open and not done, then the others in turn (each open once six cities are free, Classes)
		bool g_dev = false;      // (dev: egg <quest>) the wonders' quests open whatever the freed cities, and in any city
		int  g_devEgg = -1;      // (dev) the quest woken by hand comes first
		int Active()
		{
			if (g_devEgg >= 0 && g_devEgg < static_cast<int>(gen::kEggs.size()) && !Done(g_devEgg)) return g_devEgg;
			const int c = Classes::Get();
			if (c < 0) return -1;
			int pick = -1;
			for (int e = 0; e < kCount; ++e) {
				const int ec = gen::kEggs[e].cls;
				const int st = Classes::WonderState(ec);
				if (Done(e) || st < 1 || st > 3) continue;
				if (ec == c) return e;
				if (pick < 0) pick = e;
			}
			return pick;
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
		// the element in your hands: a Coffer spell's (right hand first): 1 fire, 2 frost, 3 shock (0 other)
		int HandElement()
		{
			auto el = [](std::string_view n) { return n == "fire" ? 1 : n == "frost" ? 2 : n == "shock" ? 3 : 0; };
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
			int                 el = -1;       // (stage 2) its element: seals and light-stones 1 fire 2 frost 3 storm 4 sun; braziers and the wrapped dead a flask's 0-5
			RE::ActorHandle     guard;         // a shard's guardian, a wrapped one of the dead
			float               wait = 0;      // (a wrapped one rises again after this)
			bool                told = false;
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
			// (stage 2: the design's twists)
			RE::ActorHandle              boss, ally;
			bool                         allyUp = false, struck = false, ghosted = false;
			float                        mechT = 0, saidT = 0;
			int                          worn = 1, lastEl = -1, shownLit = -1;
			std::array<int, 3>           elKills{};
		} g_live;
		// ---- (Noah, 2026-11-19: the Wonder Easter Eggs design - stage 2) what the twists need to know ----
		std::string_view Mech(int e, int st) { return gen::kEggs[e].steps[st].mech; }
		int  g_castEl = 0;                                   // your last cast: 1 fire 2 frost 3 storm 4 sun
		std::chrono::steady_clock::time_point g_castAt{};
		RE::NiPoint3 g_castFrom{};
		struct FlaskEvent { int el; RE::NiPoint3 at; };
		std::vector<FlaskEvent> g_flaskEvents;               // flasks burst since the last frame
		std::unordered_map<RE::FormID, int> g_flaskHit;      // the last flask's element on each of the wrapped dead
		bool g_dawnArmed = false;                            // (the crucible) the essences were carried by night
		constexpr const char* kEl[5] = { "", "fire", "frost", "storm", "sun" };
		constexpr const char* kFlaskEl[6] = { "fire", "frost", "shock", "acid", "holy", "blood" };
		constexpr int kElFx[5] = { 0, 0, 1, 2, 4 };          // an element's burst look (Gun::ElemBurst)
		constexpr int kFlaskFx[6] = { 0, 1, 2, 7, 10, 5 };
		int SpellEl(RE::TESForm* a_f)
		{
			auto* mi = a_f ? a_f->As<RE::MagicItem>() : nullptr;
			if (!mi) return 0;
			if (const int i = Arcana::IndexOf(a_f); i >= 0) {
				const auto n = Arcana::ElementName(Arcana::ElementOf(i));
				if (n == "fire") return 1;
				if (n == "frost") return 2;
				if (n == "shock") return 3;
				if (n == "sun") return 4;
			}
			for (auto* ef : mi->effects) {
				if (!ef || !ef->baseEffect) continue;
				switch (ef->baseEffect->data.resistVariable) {
				case RE::ActorValue::kResistFire: return 1;
				case RE::ActorValue::kResistFrost: return 2;
				case RE::ActorValue::kResistShock: return 3;
				default: break;
				}
			}
			return 0;
		}
		void Con(RE::TESObjectREFR* a_r, const char* a_cmd)
		{
			if (!a_r) return;
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>()) {
				if (auto* script = factory->Create()) {
					script->SetCommand(a_cmd);
					script->CompileAndRun(a_r);
					delete script;
				}
			}
		}
		RE::Actor* FindNamed(std::string_view a_name, const RE::NiPoint3& a_at, float a_r)
		{
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (a && !a->IsDead() && !a->IsDisabled() && std::string_view(a->GetName()) == a_name && a->GetPosition().GetDistance(a_at) < a_r) return a.get();
			}
			return nullptr;
		}
		// a cast of yours close by, a moment ago: its element (consumed)
		int CastNear(const RE::NiPoint3& a_at, float a_r)
		{
			if (!g_castEl || std::chrono::steady_clock::now() - g_castAt > std::chrono::milliseconds(1200) || g_castFrom.GetDistance(a_at) > a_r) return 0;
			return std::exchange(g_castEl, 0);
		}
		bool Swinging(RE::PlayerCharacter* a_p)
		{
			auto* w = a_p->GetEquippedObject(false);
			auto* wp = w ? w->As<RE::TESObjectWEAP>() : nullptr;
			return wp && !wp->IsBow() && !wp->IsCrossbow() && !wp->IsStaff() && a_p->AsActorState()->IsWeaponDrawn() &&
			       a_p->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
		}
		void Gone(RE::ActorHandle& h)
		{
			if (auto a = h.get()) {
				a->Disable();
				a->SetDelete(true);
			}
			h = {};
		}
		void Heal(RE::Actor* a, float a_frac)
		{
			auto* av = a->AsActorValueOwner();
			av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * a_frac);
		}
		bool Throttle(float a_secs)
		{
			if (g_live.saidT > 0) return false;
			g_live.saidT = a_secs;
			return true;
		}
		float                        g_beamCd = 0;
		int                          g_hintKey = -1, g_lastProg = 0;  // (the hints: the step they're for, its progress last seen)
		float                        g_stuck = 0;                     // night seconds spent on it without getting anywhere
		bool                         g_plain = false;                 // the altar has said plainly what it wants

		void Clear()
		{
			for (auto& m : g_live.marks) Remove(m.obj), Remove(m.light), Gone(m.guard);
			Remove(g_live.anchorObj), Remove(g_live.anchorLight);
			Gone(g_live.ally);
			if (auto b = g_live.boss.get(); b && g_live.ghosted) Con(b.get(), "SetGhost 0");
			g_live = {};
			g_flaskHit.clear(), g_flaskEvents.clear();
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
					const auto mech = Mech(e, st);
					if (mech == "seals" || mech == "prism") m.el = i + 1;          // ember, rime, storm (and the sun's stone)
					if (mech == "wrapped") m.el = i;                                // one of the dead for each flask's element
					if (!m.taken && mech == "flee") {                               // a shy spirit cat, not a relic
						if (auto* cat = Classes::SpiritCat(m.at, false, 3600.0f)) m.obj = cat->CreateRefHandle();
					} else if (!m.taken && mech != "wrapped") {
						m.obj = Place(a_p, gen::kRelRelic, m.at, s.type == gen::kEggOrder ? 0.55f : 0.32f);
						m.light = Place(a_p, gen::kRelRelicLight, m.at + RE::NiPoint3{ 0, 0, 90.0f }, 1.0f);
					}
					g_live.marks.push_back(m);
				}
				if (s.type == gen::kEggOrder) {
					for (int i = 0; i < s.n; ++i) g_live.order.push_back(i);
					if (Mech(e, st) != "prism") std::shuffle(g_live.order.begin(), g_live.order.end(), std::mt19937(Hash(static_cast<std::uint32_t>(e * 977 + st))));   // (the prism's order is the riddle's)
					if (Mech(e, st) == "flaskorder") {   // each brazier its own flask's element
						std::array<int, 6> els{ 0, 1, 2, 3, 4, 5 };
						std::shuffle(els.begin(), els.end(), std::mt19937(Hash(static_cast<std::uint32_t>(e * 541 + st))));
						for (int i = 0; i < static_cast<int>(g_live.marks.size()); ++i) g_live.marks[i].el = els[i % 6];
					}
				}
			} else if (s.type == gen::kEggKills) {
				const std::uint32_t h = Hash(static_cast<std::uint32_t>(e * 313 + st));
				const float ang = (h % 3600) * 0.0017453f, r = s.r0 + ((h >> 12) % 1000) / 1000.0f * (s.r1 - s.r0);
				g_live.anchor = Walkable(a_p, c + RE::NiPoint3{ std::cos(ang) * r, std::sin(ang) * r, 0 }, 400.0f).value_or(c);
				g_live.anchorObj = Place(a_p, gen::kRelBeam, g_live.anchor, 0.5f);  // (the marked ground: a pale beam)
				g_live.anchorLight = Place(a_p, gen::kRelRelicLight, g_live.anchor + RE::NiPoint3{ 0, 0, 120.0f }, 1.0f);
			} else if (s.type == gen::kEggBoss && Mech(e, st) == "chains") {
				// the Bone-Caller's three soul-chains round the altar: he can't be hurt while one stands
				for (int i = 0; i < 3; ++i) {
					const float ang = 6.2832f * i / 3 + 0.4f;
					Mark m;
					m.at = Walkable(a_p, c + RE::NiPoint3{ std::cos(ang) * 650.0f, std::sin(ang) * 650.0f, 0 }, 350.0f).value_or(c + RE::NiPoint3{ 300.0f * (i + 1), 0, 0 });
					m.obj = Place(a_p, gen::kRelRelic, m.at, 0.6f);
					m.light = Place(a_p, gen::kRelRelicLight, m.at + RE::NiPoint3{ 0, 0, 90.0f }, 1.0f);
					g_live.marks.push_back(m);
				}
			}
		}

		// ---- (Noah, 2026-11-19: the Wonder Easter Eggs design) Felldir the Old whispers through the altars: lines queued and
		// spoken one after another, each with its words under the compass while it plays ----
		struct Said { gen::Sound snd; std::string text; float secs; };
		std::deque<Said> g_say;
		float            g_sayT = 0, g_whCd = 0;
		int              g_visited = -1;   // the city whose altar has whispered this visit (walk away and it may again)
		bool Say(std::string_view a_key, std::string_view a_text)
		{
			for (const auto& w : gen::kWhispers) {
				if (w.key == a_key) {
					g_say.push_back({ w.sound, std::string(a_text), w.secs });
					return true;
				}
			}
			logger::warn("eggs: no whisper {}", a_key);
			return false;
		}
		std::string StepKey(int e, int st, bool a_done) { return fmt::format("echo_{}_s{}{}", gen::kEggs[e].voice, st + 1, a_done ? "_done" : ""); }
		void SayCity(int a_town)
		{
			if (a_town < 0 || a_town >= 10) return;
			std::string t(gen::kTowns[a_town]);
			std::transform(t.begin(), t.end(), t.begin(), ::tolower);
			Say("echo_next", gen::kWhisperNext);
			Say("echo_city_" + t, gen::kWhisperCity[a_town]);
		}
		int FreedTowns()
		{
			int n = 0;
			for (int t = 0; t < 10; ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		bool OpenSaid(int e) { return (Gifts() >> (8 + e)) & 1; }
		void SetOpenSaid(int e, bool a_on)
		{
			if (auto* g = Glob(gen::kEggGifts)) g->value = static_cast<float>(a_on ? (Gifts() | (1 << (8 + e))) : (Gifts() & ~(1 << (8 + e))));
		}
		// what an altar says: before six cities, that the wonders sleep; with a quest under way, its opening once, then the
		// step's own line at the step's city, or a riddle naming that city anywhere else
		void SayHere(int e, int a_town)
		{
			if (e < 0) {
				if (FreedTowns() < 6 && !g_dev) Say("echo_asleep", gen::kWhisperAsleep);
				return;
			}
			if (!OpenSaid(e)) {
				Say(fmt::format("echo_{}_open", gen::kEggs[e].voice), gen::kEggs[e].open);
				SetOpenSaid(e, true);
			}
			if (a_town >= 0 && a_town == CurTown(e)) Say(StepKey(e, Step(e), false), gen::kEggs[e].steps[Step(e)].hint);
			else SayCity(CurTown(e));
		}
		void TickWhispers(RE::PlayerCharacter* a_p, int e, float a_dt)
		{
			g_whCd = std::max(0.0f, g_whCd - a_dt);
			if (g_sayT > 0) {
				g_sayT -= a_dt;
			} else if (!g_say.empty()) {
				auto l = g_say.front();
				g_say.pop_front();
				Audio::Ui(l.snd);
				Hud::QuestBanner("Felldir: " + l.text, l.secs + 0.8f);
				g_sayT = l.secs + 0.5f;
			}
			// (the timing) walking within about 12 m of an altar, out of a fight, once a visit, and one whisper every 3 minutes
			const int town = Curse::ZoneTown();
			auto*     altar = town >= 0 ? Shops::Placed(town, 0) : nullptr;
			const float d = altar ? a_p->GetPosition().GetDistance(altar->GetPosition()) : FLT_MAX;
			if (d > 2000.0f && g_visited == town) g_visited = -1;
			if (town < 0) g_visited = -1;
			if (!altar || d > 850.0f || g_visited == town || a_p->IsInCombat() || g_whCd > 0 || g_sayT > 0 || !g_say.empty()) return;
			if (e < 0 && (FreedTowns() >= 6 || g_dev)) return;   // (nothing under way: nothing to say)
			g_visited = town;
			g_whCd = 180.0f;
			SayHere(e, town);
		}
		// the step's city, chosen as the step begins (never the city of the step before) - so every altar can point to it
		bool Eligible(int t) { return g_dev || Curse::Freed(t); }
		int PickTown(int e)
		{
			std::vector<int> c;
			for (int t = 0; t < 10; ++t) if (Eligible(t) && t != PrevTown(e)) c.push_back(t);
			if (c.empty()) return -1;
			static std::mt19937 rr{ std::random_device{}() };
			return c[std::uniform_int_distribution<std::size_t>(0, c.size() - 1)(rr)];
		}

		void Award(int e, bool a_loud)
		{
			if (Gifted(e)) return;
			SetGift(e);
			const auto& d = gen::kEggs[e];
			if (a_loud) {
				std::string t(d.title);
				for (auto& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
				Hud::QuestBanner(t + " - COMPLETE", 5.0f);
			}
			Classes::EarnWonder(d.cls);  // (the quest's end: the Greybeards call, and the wonder waits in the Reliquary)
			logger::info("eggs: {} rewarded", d.id);
		}
		void Complete(int e)
		{
			Say(fmt::format("echo_{}_final", gen::kEggs[e].voice), gen::kEggs[e].finale);
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
			Say(StepKey(e, st - 1, true), gen::kEggs[e].steps[st - 1].done);   // (it counted: Felldir says so, wherever you are)
			if (st >= gen::kEggs[e].nSteps) {
				Complete(e);
				return;
			}
			SetRaw(e, st * 1000);
			SetTowns(e, (CurTown(e) + 1) * 100);  // (this city did its step: the next is found in another)
			g_visited = -1, g_whCd = 0;  // (the next altar you reach tells you where to go)
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
			g_live.saidT = std::max(0.0f, g_live.saidT - a_dt);
			switch (s.type) {
			case gen::kEggTouch:
				{
					const auto mech = Mech(e, st);
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
						Hud::Banner(mech == "seals" ? std::string("Too slow - the seals close again") : "Too slow - the " + std::string(s.name) + "s gutter out", 4.0f);
						Setup(a_p, e, st, a_altar);
						return;
					}
					const auto take = [&](Mark& m, const char* a_fx) {
						m.taken = true;
						Remove(m.obj), Remove(m.light);
						Gun::FxBlast(a_fx, m.at + RE::NiPoint3{ 0, 0, 40.0f });
						if (s.seconds > 0 && g_live.timer < 0) g_live.timer = 0;
						if (Prog(e) == 0) g_live.hurt = false;
						Bump(e, s.n, std::string(s.name) + (s.n > 1 ? "s" : ""));
					};
					// (The Ghost Pride) the spirit cats shy from a hunter: run at one and it flickers away; crouch and close in to calm it
					if (mech == "flee") {
						for (auto& m : g_live.marks) {
							if (m.taken) continue;
							auto r = m.obj.get();
							if (!r || r->IsDisabled()) {
								if (auto* cat = Classes::SpiritCat(m.at, false, 3600.0f)) m.obj = cat->CreateRefHandle();
								continue;
							}
							const auto at = r->GetPosition();
							const float d = pp.GetDistance(at);
							const bool  sneak = a_p->IsSneaking();
							if (d < 650.0f && !sneak) {
								RE::NiPoint3 away = at - pp;
								away.z = 0;
								const float l = std::max(1.0f, away.Length());
								if (const auto to = Walkable(a_p, at + away * (650.0f / l), 400.0f)) {
									Gun::Motes(1, at, 0.6f, 1.0f);
									r->SetPosition(*to);
									m.at = *to;
								}
								if (Throttle(6.0f)) Hud::Banner("It shies from you. Come to it quietly.", 3.0f);
							} else if (d < 200.0f && sneak) {
								if (s.night && !night) {
									if (Throttle(6.0f)) Hud::Banner("It won't settle by day. Come back at night.", 3.0f);
									continue;
								}
								if (auto* act = r->As<RE::Actor>()) act->Disable();
								take(m, "light");
								return;
							}
						}
						break;
					}
					// (The Eye of Magnus) each seal opens to a spell of its own element cast close by
					if (mech == "seals") {
						for (auto& m : g_live.marks) {
							if (m.taken) continue;
							const float d = pp.GetDistance(m.at);
							if (d < 400.0f && !m.told) m.told = true, Hud::Banner(fmt::format("A {} seal. It wants {}.", m.el == 1 ? "ember" : m.el == 2 ? "rime" : "storm", kEl[m.el]), 3.0f);
							if (d > 1400.0f) continue;
							if (const int el = CastNear(m.at, 1400.0f); el) {
								if (el == m.el) {
									Gun::ElemBurst(kElFx[m.el], m.at + RE::NiPoint3{ 0, 0, 40.0f }, 1.0f, 0.0f, 0.9f);
									take(m, "light");
								} else {
									Hud::Banner(fmt::format("The {} seal doesn't answer {}.", kEl[m.el], kEl[el]), 2.5f);
								}
								return;
							}
						}
						break;
					}
					// (The Philosopher's Stone) six of the dead, each wrapped in an element: only a flask of its own element frees its essence
					if (mech == "wrapped") {
						if (!night) {
							for (auto& m : g_live.marks) Gone(m.guard);
							break;
						}
						g_live.mechT -= a_dt;
						const bool aura = g_live.mechT <= 0;
						if (aura) g_live.mechT = 1.6f;
						for (auto& m : g_live.marks) {
							if (m.taken) continue;
							auto a = m.guard.get();
							if (!a) {
								if ((m.wait -= a_dt) > 0) continue;
								if (const auto spot = Walkable(a_p, m.at, 500.0f); spot) {
									if (auto h = Director::RaiseTrial(*spot, true)) m.guard = h, m.told = false;
								}
								m.wait = 3.0f;
								continue;
							}
							if (aura && !a->IsDead()) Gun::ElemBurst(kFlaskFx[m.el], a->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, 0.6f, 0.0f, 0.8f);
							if (!m.told && pp.GetDistance(a->GetPosition()) < 900.0f) m.told = true, Hud::Banner(fmt::format("One of the dead, wrapped in {}.", kFlaskEl[m.el]), 3.0f);
							if (!a->IsDead()) continue;
							const auto it = g_flaskHit.find(a->GetFormID());
							const int  by = it != g_flaskHit.end() ? it->second : -1;
							const auto at = a->GetPosition();
							m.guard = {};
							if (by == m.el) {
								m.at = at;
								Gun::ElemBurst(kFlaskFx[m.el], at + RE::NiPoint3{ 0, 0, 40.0f }, 1.2f, 0.0f, 1.0f);
								take(m, "light");
								return;
							}
							Gun::ElemBurst(kFlaskFx[m.el], at + RE::NiPoint3{ 0, 0, 40.0f }, 1.6f, 0.0f, 1.0f);
							Gun::BlastSound("lightmassive", at, 0.7f);
							if (pp.GetDistance(at) < 300.0f) a_p->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -15.0f);
							Hud::Banner(fmt::format("Wrong - only {} frees it. It bursts, and rises again.", kFlaskEl[m.el]), 3.0f);
							m.wait = 4.0f;
						}
						break;
					}
					for (auto& m : g_live.marks) {
						if (m.taken) continue;
						const float d = pp.GetDistance(m.at);
						// (The Slayer's Oath) a guardian rises from each shard as you come near; the shard is yours once it falls
						if (mech == "guardians") {
							auto g = m.guard.get();
							if (night && d < 750.0f && !g && !m.told) {
								if (const auto spot = Walkable(a_p, m.at + RE::NiPoint3{ 120.0f, 0, 0 }, 300.0f); spot) {
									if (auto h = Director::RaiseTrial(*spot, true)) {
										m.guard = h, m.told = true;
										Hud::Banner("A guardian rises from the shard.", 3.0f);
									}
								}
							}
							if (g && !g->IsDead() && d < 170.0f) {
								if (Throttle(4.0f)) Hud::Banner("Its guardian still stands.", 2.5f);
								continue;
							}
						}
						if (d > 170.0f) continue;
						if (s.night && !night) {
							if (!g_live.toldDay) g_live.toldDay = true, Hud::Banner("It sleeps. Come back at night.", 3.0f);
							continue;
						}
						if (s.element != 0 && HandElement() != s.element) {
							if (!g_live.toldDay) g_live.toldDay = true, Hud::Banner(fmt::format("It doesn't stir. It wants {} in your hands.", kElemName[s.element]), 3.0f);
							continue;
						}
						take(m, "light");
						return;
					}
					if (std::none_of(g_live.marks.begin(), g_live.marks.end(), [&](const Mark& m) { return !m.taken && pp.GetDistance(m.at) < 400.0f; })) g_live.toldDay = false;
					break;
				}
			case gen::kEggOrder:
				{
					const auto mech = Mech(e, st);
					const int  n = static_cast<int>(g_live.order.size());
					const int  prog = Prog(e);
					g_live.showT += a_dt;
					const float cyc = 0.9f * n + 1.6f;
					const int   lit = static_cast<int>(std::fmod(g_live.showT, cyc) / 0.9f);
					// the order shown: the stones' lights wake one after another (the pride's roaring stones: heard, never seen;
					// the braziers: each flickers its own element's colour; the prism: the riddle tells it, the stones all lit)
					for (int i = 0; i < n; ++i) {
						const bool done = std::find(g_live.order.begin(), g_live.order.begin() + std::min(prog, n), i) != g_live.order.begin() + std::min(prog, n);
						const bool on = done || mech == "prism" || (mech != "sound" && lit < n && g_live.order[lit] == i);
						if (auto l = g_live.marks[i].light.get()) {
							if (on && l->IsDisabled()) l->Enable(false);
							if (!on && !l->IsDisabled()) l->Disable();
						}
					}
					if (lit != g_live.shownLit) {
						g_live.shownLit = lit;
						if (lit < n && (!s.night || night)) {
							const auto& m = g_live.marks[g_live.order[lit]];
							if (mech == "sound") Audio::AtPoint(gen::Sound::boss_growl, m.at + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, 0.9f);
							if (mech == "flaskorder") Gun::ElemBurst(kFlaskFx[std::clamp(m.el, 0, 5)], m.at + RE::NiPoint3{ 0, 0, 70.0f }, 0.7f, 0.0f, 0.7f);
						}
					}
					if (s.night && !night) break;
					int hit = -1, el = -1;
					if (mech == "prism") {   // struck by a spell cast close by
						for (int i = 0; i < n && hit < 0; ++i) {
							const float d = pp.GetDistance(g_live.marks[i].at);
							if (d < 350.0f && !g_live.marks[i].told) g_live.marks[i].told = true, Hud::Banner(fmt::format("The {}-stone.", kEl[g_live.marks[i].el]), 2.5f);
						}
						int best = -1;
						float bd = 900.0f;
						for (int i = 0; i < n; ++i) if (const float d = pp.GetDistance(g_live.marks[i].at); d < bd) bd = d, best = i;
						if (best >= 0) if (const int ce = CastNear(g_live.marks[best].at, 900.0f); ce) hit = best, el = ce;
					} else if (mech == "flaskorder") {   // a flask burst on it
						for (const auto& f : g_flaskEvents) {
							for (int i = 0; i < n && hit < 0; ++i) if (f.at.GetDistance(g_live.marks[i].at) < 240.0f) hit = i, el = f.el;
						}
					} else {
						for (int i = 0; i < n; ++i) {
							const float d = pp.GetDistance(g_live.marks[i].at);
							if (d > (mech == "steel" ? 240.0f : 170.0f)) {
								if (g_live.last == i && d > 260.0f) g_live.last = -1;
								continue;
							}
							if (g_live.last == i) continue;
							if (mech == "steel" && !Swinging(a_p)) {   // (The Slayer's Oath) struck with steel, not walked to
								if (Throttle(5.0f)) Hud::Banner("Strike it with your blade.", 2.5f);
								continue;
							}
							g_live.last = i, hit = i;
							break;
						}
					}
					if (hit < 0) break;
					const bool elOk = (mech != "prism" && mech != "flaskorder") || el == g_live.marks[hit].el;
					if (prog < n && g_live.order[prog] == hit && elOk) {
						if (mech == "prism") Gun::ElemBurst(kElFx[std::clamp(el, 0, 4)], g_live.marks[hit].at + RE::NiPoint3{ 0, 0, 60.0f }, 1.0f, 0.0f, 0.9f);
						Gun::FxBlast("light", g_live.marks[hit].at + RE::NiPoint3{ 0, 0, 60.0f });
						Bump(e, n, std::string(s.name) + (n > 1 ? "s" : ""));
					} else {
						SetRaw(e, st * 1000);
						Forms::Play(gen::Sound::rite_fail);
						Hud::Banner(!elOk ? (mech == "prism" ? std::string("Wrong element - start again") : std::string("Wrong flask - watch the braziers again")) :
						                    mech == "sound" ? std::string("Wrong - listen to the stones again") : "Wrong - watch the " + std::string(s.name) + "s again", 3.0f);
						g_live.showT = 0;
					}
					return;
				}
			case gen::kEggAltar:
				if (std::hypot(pp.x - c.x, pp.y - c.y) < 230.0f && std::abs(pp.z - c.z) < 200.0f) {
					Advance(e);
				}
				break;
			case gen::kEggKills:
			case gen::kEggClasskills:
				{
					// (the class review: these steps are played in a freed city, where no waves run and the dead crumble - the
					// count could never move) while the step is live at night, the dead keep coming for you here, a few at a time
					if (night && Director::Fighting() && Coop::Client()) {
						// (the story audit: a friend's game raises nothing and the host's crumbles the dead in a freed city - the
						// count could never move) as the survive steps: hold the altar's ground ninety seconds at night instead
						g_live.foes.clear();
						if (pp.GetDistance(c) < 2200.0f && (g_live.holdT += a_dt) > 90.0f) {
							g_live.holdT = 0;
							Advance(e);
						}
						break;
					}
					if (!night || !Director::Fighting()) {
						g_live.foes.clear();
						break;
					}
					// (The Ghost Pride) a spirit cat hunts at your side: should it fall, the hunt starts over
					if (Mech(e, st) == "escort") {
						auto al = g_live.ally.get();
						if (al && (al->IsDead() || al->IsDisabled())) {
							g_live.ally = {}, al = {};
							if (g_live.allyUp && Prog(e) > 0) {
								SetRaw(e, st * 1000);
								Hud::Banner("The spirit cat fell - the hunt starts over", 3.5f);
							}
							g_live.allyUp = false, g_live.mechT = 6.0f;
						}
						if (!al && (g_live.mechT -= a_dt) <= 0) {
							if (auto* cat = Classes::SpiritCat(pp + RE::NiPoint3{ 150.0f, 0, 0 }, true, 3600.0f)) {
								g_live.ally = cat->GetHandle(), g_live.allyUp = true;
								Hud::Banner("A spirit cat hunts beside you. Keep it alive.", 3.0f);
							}
							g_live.mechT = 6.0f;
						}
					}
					std::erase_if(g_live.foes, [](const RE::ActorHandle& h) { auto a = h.get(); return !a || a->IsDead() || a->IsDisabled(); });
					if ((g_live.holdT -= a_dt) <= 0 && g_live.foes.size() < 6) {
						g_live.holdT = 2.5f;
						static std::mt19937 rr{ std::random_device{}() };
						std::uniform_real_distribution<float> u(0.0f, 1.0f);
						const float a = u(rr) * 6.2832f, r = 900.0f + 500.0f * u(rr);
						const auto  spot = Walkable(a_p, pp + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 500.0f);
						if (spot) {
							if (auto h = Director::RaiseTrial(*spot, false); h) g_live.foes.push_back(h);
						}
					}
					break;  // (OnUndeadDeath and OnClassKill count them)
				}
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
					const auto mech = Mech(e, st);
					if (s.type == gen::kEggBoss) {
						if (!g_live.started && s.row >= 0 && !Director::ChampionActive(-10 - CurTown(e))) {
							const auto at = Walkable(a_p, c + RE::NiPoint3{ 700.0f, 0, 0 }, 500.0f).value_or(c);
							g_live.started = Director::RaiseChampion(-10 - CurTown(e), gen::kEnemies[s.row].id, s.name, 14.0f, at);
						}
						auto b = g_live.boss.get();
						if (!b && g_live.started && (g_live.holdT -= a_dt) <= 0) {
							g_live.holdT = 1.0f;
							if (auto* f = FindNamed(s.name, c, 4000.0f)) g_live.boss = f->GetHandle(), b = f->GetHandle().get();
						}
						if (!b || b->IsDead()) break;
						if (mech == "chains") {   // (The Ghost Pride) untouchable while a soul-chain stands
							const int left = static_cast<int>(std::count_if(g_live.marks.begin(), g_live.marks.end(), [](const Mark& m) { return !m.taken; }));
							if (left > 0 && !g_live.ghosted) Con(b.get(), "SetGhost 1"), g_live.ghosted = true, Hud::Banner("Three soul-chains hold him. Break them.", 3.5f);
							for (auto& m : g_live.marks) {
								if (m.taken || pp.GetDistance(m.at) > 170.0f) continue;
								m.taken = true;
								Remove(m.obj), Remove(m.light);
								Gun::FxBlast("light", m.at + RE::NiPoint3{ 0, 0, 40.0f });
								Forms::Play(gen::Sound::quest_step);
								Hud::Banner(left - 1 > 0 ? fmt::format("A chain breaks - {} left", left - 1) : std::string("The last chain breaks - he can be hurt"), 3.0f);
								if (left - 1 == 0) Con(b.get(), "SetGhost 0"), g_live.ghosted = false;
								break;
							}
						} else if (mech == "anomaly") {   // (The Eye of Magnus) it wears each element in turn
							if ((g_live.mechT -= a_dt) <= 0) {
								g_live.mechT = 6.0f;
								g_live.worn = g_live.worn % 3 + 1;
								Gun::ElemBurst(kElFx[g_live.worn], b->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f }, 1.4f, 0.0f, 1.0f);
								Hud::Banner(fmt::format("The Anomaly wears {}", kEl[g_live.worn]), 2.0f);
							}
						}
						break;  // (OnBossSlain moves it on)
					}
					// a held altar: a hit of your own spoils the den (summons only); a wound spoils the brew (unhurt)
					const bool spoilt = (mech == "summonsonly" && g_live.struck) || (s.nodamage && g_live.hurt);
					if (g_live.started && spoilt) {
						for (auto& h : g_live.foes) Gone(h);
						g_live.foes.clear();
						g_live.started = false, g_live.struck = false, g_live.hurt = false;
						Forms::Play(gen::Sound::rite_fail);
						Hud::Banner(mech == "summonsonly" ? "You struck a blow - only your summons may kill. The den starts again." : "You were hurt - the brew is shaken. Hold the altar again.", 4.0f);
						break;
					}
					if (!g_live.started) {
						g_live.started = true, g_live.struck = false, g_live.hurt = false;
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
		struct Flush { ~Flush() { g_flaskEvents.clear(); } } flush;   // (a flask's burst is heard once, the frame after it)
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !p->Is3DLoaded()) return;
		g_beamCd = std::max(0.0f, g_beamCd - a_delta);
		const int e = Active();
		TickWhispers(p, e, a_delta);
		if (e < 0) {
			if (g_live.egg >= 0) Clear();
			return;
		}
		// (Noah, 2026-11-19) the step's city is chosen as the step begins, so every altar's riddle can point to it
		if (CurTown(e) < 0) {
			if (const int t = PickTown(e); t >= 0) {
				SetTowns(e, (PrevTown(e) + 1) * 100 + t + 1);
				logger::info("eggs: {} step {} waits in {}", gen::kEggs[e].id, Step(e) + 1, gen::kTowns[t]);
			}
		}
		// (The Philosopher's Stone) the essences were taken by night: should dawn come before they reach the altar, they're spoilt
		if (Step(e) < gen::kEggs[e].nSteps && Mech(e, Step(e)) == "dawn") {
			if (Director::NightNow()) {
				g_dawnArmed = true;
			} else if (g_dawnArmed) {
				g_dawnArmed = false;
				SetRaw(e, 0), SetTowns(e, 0);
				Clear();
				Forms::Play(gen::Sound::rite_fail);
				Hud::Banner("Dawn broke - the essences spoilt. Six more must be taken by night.", 5.0f);
				return;
			}
		} else {
			g_dawnArmed = false;
		}
		const int town = Curse::ZoneTown();
		if (town < 0 || town != CurTown(e) || !Eligible(town)) {
			if (g_live.egg >= 0) Clear();
			return;
		}
		auto* altar = Shops::Placed(town, 0);
		if (!altar) return;
		const int st = Step(e);
		// (Noah: "how would players figure out the Easter eggs with no prior context?") spend long enough on a step without
		// getting anywhere (about two nights' worth, here at night) and the altar says plainly what it wants
		{
			const int key = e * 100 + st;
			if (key != g_hintKey) g_hintKey = key, g_stuck = 0, g_plain = false, g_lastProg = Prog(e);
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

	// (Director) a step here wants the dead about: the freed city doesn't crumble them while it runs
	bool WantsDead()
	{
		const int e = g_live.egg;
		if (e < 0 || Done(e) || Curse::ZoneTown() != CurTown(e)) return false;
		const auto t = gen::kEggs[e].steps[Step(e)].type;
		const auto m = Mech(e, Step(e));
		return t == gen::kEggKills || t == gen::kEggClasskills || t == gen::kEggSurvive || t == gen::kEggBoss || m == "wrapped" || m == "guardians";
	}

	// (Classes) a kill: 0 by your summons, 1 by Destruction (its coffer element), 2 by a flask, 3 by a blade - a quest counts its
	// own class's way only
	void OnClassKill(int a_how, int a_element)
	{
		const int e = g_live.egg;
		if (e < 0 || Done(e) || !Director::NightNow() || Curse::ZoneTown() != CurTown(e)) return;
		const auto& s = gen::kEggs[e].steps[Step(e)];
		if (s.type != gen::kEggClasskills || a_how != gen::kEggs[e].cls) return;
		const auto mech = Mech(e, Step(e));
		auto* p = RE::PlayerCharacter::GetSingleton();
		// (The Slayer's Oath) the blade, with a spell ready in the other hand
		if (mech == "bladespell") {
			auto* l = p ? p->GetEquippedObject(true) : nullptr;
			if (!l || !l->As<RE::SpellItem>()) {
				if (Throttle(8.0f)) Hud::Banner("The oath wants a spell ready in your other hand.", 3.0f);
				return;
			}
		}
		// (The Eye of Magnus) fire, frost and storm: ten each at the least
		if (mech == "elemsplit") {
			if (a_element < 0 || a_element > 2) {
				if (Throttle(8.0f)) Hud::Banner("The Eye wants fire, frost or storm.", 3.0f);
				return;
			}
			auto& k = g_live.elKills;
			++k[a_element];
			const int sum = k[0] + k[1] + k[2];
			SetRaw(e, Step(e) * 1000 + std::min(sum, 999));
			if (sum >= s.n && k[0] >= 10 && k[1] >= 10 && k[2] >= 10) {
				Advance(e);
			} else if (sum % 5 == 0 || (k[a_element] == 10)) {
				Forms::Play(gen::Sound::quest_step);
				Hud::Banner(fmt::format("The Eye drinks: fire {}  frost {}  storm {}  ({} of {})", k[0], k[1], k[2], sum, s.n), 3.0f);
			}
			return;
		}
		const int prog = Prog(e) + 1;
		SetRaw(e, Step(e) * 1000 + prog);
		if (prog >= s.n) {
			Advance(e);
		} else if (prog % 10 == 0) {
			Forms::Play(gen::Sound::quest_step);
			Hud::Banner(fmt::format("The secret drinks: {} of {}", prog, s.n), 2.5f);
		}
	}

	// (Events) your blow or spell landed on someone
	void OnPlayerHit(RE::Actor* a_target, RE::TESForm* a_source)
	{
		const int e = g_live.egg;
		if (e < 0 || Done(e) || !a_target || Curse::ZoneTown() != CurTown(e)) return;
		const auto mech = Mech(e, Step(e));
		// (The Ghost Pride) the den: only what you call may kill
		if (mech == "summonsonly" && g_live.started && !a_target->IsPlayerTeammate()) {
			g_live.struck = true;
			return;
		}
		auto b = g_live.boss.get();
		if (!b || b.get() != a_target || b->IsDead()) return;
		if (mech == "anomaly") {   // (The Eye of Magnus) struck with what it wears: it drinks it
			if (const int el = SpellEl(a_source); el && el == g_live.worn) {
				Heal(a_target, 0.04f);
				Gun::ElemBurst(kElFx[el], a_target->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f }, 0.8f, 0.0f, 0.6f);
				if (Throttle(3.0f)) Hud::Banner(fmt::format("It drinks your {} - strike with what it isn't wearing", kEl[el]), 2.5f);
			}
		} else if (mech == "thorgar") {   // (The Slayer's Oath) magic won't touch him
			if (a_source && a_source->As<RE::MagicItem>()) {
				Heal(a_target, 0.03f);
				if (Throttle(3.0f)) Hud::Banner("Magic will not touch him. The blade alone.", 2.5f);
			}
		}
	}

	// (Classes) one of your flasks burst
	void OnFlask(int a_element, const RE::NiPoint3& a_at)
	{
		const int e = g_live.egg;
		if (e < 0 || Done(e) || Curse::ZoneTown() != CurTown(e)) return;
		g_flaskEvents.push_back({ a_element, a_at });
		const auto mech = Mech(e, Step(e));
		if (mech == "wrapped") {
			for (auto& m : g_live.marks) {
				if (auto a = m.guard.get(); a && a->GetPosition().GetDistance(a_at) < 320.0f) g_flaskHit[a->GetFormID()] = a_element;
			}
		} else if (mech == "matriarch") {   // (The Philosopher's Stone) she drinks the same element twice
			auto b = g_live.boss.get();
			if (!b || b->IsDead() || b->GetPosition().GetDistance(a_at) > 350.0f) return;
			if (a_element == g_live.lastEl) {
				Heal(b.get(), 0.05f);
				if (Throttle(3.0f)) Hud::Banner(fmt::format("She drinks your {} - never the same twice", kFlaskEl[std::clamp(a_element, 0, 5)]), 2.5f);
			}
			g_live.lastEl = a_element;
		}
	}

	void OnBossSlain(int a_town)
	{
		const int e = Active();
		if (e >= 0 && a_town == CurTown(e) && gen::kEggs[e].steps[Step(e)].type == gen::kEggBoss) {
			Advance(e);
		}
	}

	// (Events) a spell of yours: its element and where, for the seals and the light-stones (never consumes the cast)
	bool OnSpellCast(RE::FormID a_spell)
	{
		if (const int el = SpellEl(RE::TESForm::LookupByID(a_spell)); el) {
			if (auto* p = RE::PlayerCharacter::GetSingleton()) {
				g_castEl = el, g_castAt = std::chrono::steady_clock::now(), g_castFrom = p->GetPosition();
			}
		}
		return false;
	}

	// (the story audit: another save loaded on the same step kept this one's marks and its boss half-started)
	void Reset()
	{
		Clear();
		g_hintKey = -1, g_lastProg = 0, g_stuck = 0, g_plain = false, g_beamCd = 0, g_dawnArmed = false;
	}

	void Heard(int) {}  // (each class keeps its own quest: a friend's isn't yours)

	std::vector<std::string> Whispers(int a_town)
	{
		// (Noah, 2026-11-19) the altar menu keeps what Felldir has said: each quest's lines found so far, and where the next waits
		std::vector<std::string> out;
		const int act = Active();
		if (act < 0) {
			if (FreedTowns() < 6) out.push_back(std::string(gen::kWhisperAsleep));
		}
		for (int e = 0; e < kCount; ++e) {
			const auto& d = gen::kEggs[e];
			if (Done(e) || Gifted(e)) {
				out.push_back(std::string(d.title) + " - done. " + (Classes::WonderState(d.cls) == 4 ? std::string(Classes::WonderName(d.cls)) + " is yours." : std::string(d.rewardText)));
			} else if (e == act) {
				out.push_back(std::string(d.open));
				for (int i = 0; i < Step(e); ++i) out.push_back(std::string(d.steps[i].hint) + " (" + std::string(d.steps[i].done) + ")");
				const int cur = CurTown(e);
				if (cur >= 0 && a_town == cur) out.push_back(std::string(d.steps[Step(e)].hint));
				else if (cur >= 0) out.push_back(std::string(gen::kWhisperNext) + " " + std::string(gen::kWhisperCity[cur]));
				if (g_plain && g_hintKey == e * 100 + Step(e)) out.push_back("Plainly: " + std::string(d.steps[Step(e)].text));
			}
		}
		return out;
	}

	std::string Line()  // the class guide's line: how far along your path's secret you are
	{
		const int act = Active();
		int done = 0;
		for (int e = 0; e < kCount; ++e) done += Done(e) ? 1 : 0;
		if (act < 0) return done >= kCount ? "Every secret won" : std::string{};
		const auto& d = gen::kEggs[act];
		const int   cur = CurTown(act);
		return fmt::format("{} - step {} of {}{}", d.title, Step(act) + 1, d.nSteps, cur >= 0 && Prog(act) > 0 ? fmt::format(" (under way in {})", gen::kTowns[cur]) : std::string(" (listen to the altars' whispers)"));
	}

	float DamageMult() { return 1.0f; }  // (Vaermina's Gift retired with the city quests)
	bool  ShadowStep() { return false; }  // (the Nightingale's Debt retired with them)
	void DevStep(int)  // dev: the quest under way one step on (eggnext)
	{
		const int e = Active();
		if (e >= 0 && !Done(e)) Advance(e);
	}
	bool DevOpen() { return g_dev; }
	// (Noah, 2026-11-19: "a way to test the Easter eggs") egg <pride|eye|stone|oath>, eggjump <step>, eggnext, eggto, eggsay, eggreset
	void Dev(std::string_view a_cmd, std::string_view a_arg)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (a_cmd == "egg") {
			int e = -1;
			for (int i = 0; i < kCount; ++i) if (gen::kEggs[i].voice == a_arg || gen::kEggs[i].id == a_arg) e = i;
			if (e < 0) { logger::info("dev: egg - no quest {} (pride, eye, stone, oath)", a_arg); return; }
			g_dev = true, g_devEgg = e;
			Classes::DevWonder(gen::kEggs[e].cls, 1);
			SetRaw(e, 0), SetTowns(e, 0), SetOpenSaid(e, false);
			Clear();
			g_say.clear(), g_sayT = 0, g_whCd = 0, g_visited = -1;
			if (const int t = PickTown(e); t >= 0) SetTowns(e, t + 1);
			SayHere(e, Curse::ZoneTown());
			logger::info("dev: egg {} woken - step 1 waits in {}", gen::kEggs[e].id, CurTown(e) >= 0 ? gen::kTowns[CurTown(e)] : "nowhere");
		} else if (a_cmd == "eggjump") {
			const int e = Active();
			int n = 1;
			try { n = std::stoi(std::string(a_arg)); } catch (...) {}
			if (e < 0) return;
			n = std::clamp(n, 1, static_cast<int>(gen::kEggs[e].nSteps));
			SetRaw(e, (n - 1) * 1000), SetTowns(e, 0);
			Clear();
			g_visited = -1, g_whCd = 0;
			logger::info("dev: eggjump {} step {}", gen::kEggs[e].id, n);
		} else if (a_cmd == "eggnext") {
			DevStep(0);
		} else if (a_cmd == "eggto") {
			const int e = Active();
			const int t = e >= 0 ? CurTown(e) : -1;
			if (!p || t < 0) return;
			for (const auto& l : gen::kLocations) {
				if (l.town != t) continue;
				if (auto* a = RE::TESForm::LookupByID<RE::TESObjectREFR>(l.anchor)) {
					p->MoveTo(a);
					logger::info("dev: eggto {}", gen::kTowns[t]);
				}
				break;
			}
		} else if (a_cmd == "eggsay") {
			g_say.clear(), g_sayT = 0;
			SayHere(Active(), Curse::ZoneTown());
		} else if (a_cmd == "eggreset") {
			for (int e = 0; e < kCount; ++e) SetRaw(e, 0), SetTowns(e, 0), SetOpenSaid(e, false);
			Clear();
			g_say.clear(), g_sayT = 0, g_whCd = 0, g_visited = -1, g_devEgg = -1;
			logger::info("dev: eggreset - every wonder quest back to its start");
		}
	}
}
