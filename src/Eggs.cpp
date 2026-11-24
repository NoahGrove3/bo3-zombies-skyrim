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
		// (Noah, 2026-11-19: "a unique way for each class to start their Easter egg ... instead of it just being fully open") a
		// quest under way only once its sign has been found (bits 12-15); the voice's three reveals said (bits 20-22)
		void SetBit(int a_bit, bool a_on)
		{
			if (auto* g = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(gen::kEggGifts, gen::kPlugin)) {
				const int v = static_cast<int>(g->value);
				g->value = static_cast<float>(a_on ? (v | (1 << a_bit)) : (v & ~(1 << a_bit)));
			}
		}
		bool Begun(int e) { return ((Gifts() >> (12 + e)) & 1) || Raw(e) > 0; }   // (a quest already under way before the signs: begun)
		bool RevealSaid(int i) { return (Gifts() >> (20 + i)) & 1; }
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
				if (Done(e) || st < 1 || st > 3 || !Begun(e)) continue;   // (its sign not yet found: not under way)
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
		std::optional<RE::NiPoint3> Walkable(RE::PlayerCharacter* a_p, const RE::NiPoint3& a_at, float a_maxShift, bool a_dry = false)
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
			// (Noah, 2026-11-21: the spirit cats "going in water") dry ground only: no navmesh under a stream, a pond, the sea
			if (a_dry) {
				auto* cell = RE::TES::GetSingleton()->GetCell(got);
				if (float wh = 0; cell && cell->GetWaterHeight(got, wh) && wh > got.z - 12.0f) return std::nullopt;
			}
			return got;
		}
		// (Noah, 2026-11-21: the Ghost Pride's cats "going in water or running away even when I'm sneaking up to them", the lone spirit
		// "teleporting all over the place" - "following them to the fang should actually look like it's leading me there") a spirit
		// cat goes on its own feet: a marker where it's headed, and the cat told to find its way there (PathToReference - the game's
		// own pathing, round rocks and walls); there, it's held where it stands (SetDontMove), so its own wandering never carries it
		// into water or off from you
		void CatScript(RE::Actor* a_cat, const char* a_fn, RE::BSScript::IFunctionArguments* a_args)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || !a_cat) {
				delete a_args;
				return;
			}
			const auto hd = vm->GetObjectHandlePolicy()->GetHandleForObject(a_cat->GetFormType(), a_cat);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			vm->DispatchMethodCall(hd, "Actor", a_fn, a_args, none);
		}
		void CatHold(RE::Actor* a_cat, bool a_on) { CatScript(a_cat, "SetDontMove", RE::MakeFunctionArguments(static_cast<bool>(a_on))); }
		void CatGo(RE::PlayerCharacter* a_p, RE::Actor* a_cat, RE::ObjectRefHandle& a_goal, const RE::NiPoint3& a_to, bool a_run)
		{
			auto goal = a_goal.get();
			if (!goal) {
				auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
				auto  ref = xm ? a_p->PlaceObjectAtMe(xm, false) : nullptr;
				if (!ref) return;
				a_goal = ref->CreateRefHandle();
				goal = ref;
			}
			goal->SetPosition(a_to);
			CatHold(a_cat, false);
			CatScript(a_cat, "PathToReference", RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(goal.get()), a_run ? 1.0f : 0.0f));
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
			float               glow = 0;      // (a roaming spirit cat) its next drift of motes
			RE::NiPoint3        dry;           // (Noah: "they get stuck and swim around in the water") where it last stood on land
			float               wet = 0;       // how long it's been swimming
			RE::ObjectRefHandle goal;          // (a spirit cat on its feet) the marker it paths to
			RE::NiPoint3        dest;
			bool                moving = false, held = false;
			float               moveT = 0;
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
		constexpr const char* kFlaskEl[6] = { "fire", "frost", "shock", "acid", "holy water", "void" };   // (Classes' flasks, in order)
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
		// (the desk check: a spell "near" two seals went to whichever came first in the list) the mark you're looking at, within a_r of
		// you and about 20 degrees of your aim; else the nearest within 400 (-1 none)
		bool CamAim(RE::NiPoint3& a_from, RE::NiPoint3& a_dir);
		template <class M>
		int AimedMark(const std::vector<M>& a_marks, const RE::NiPoint3& a_you, float a_r, bool a_skipTaken)
		{
			RE::NiPoint3 from, dir;
			const bool   aim = CamAim(from, dir);
			int          best = -1, nearest = -1;
			float        bestCos = 0.94f, nearD = 400.0f;
			for (int i = 0; i < static_cast<int>(a_marks.size()); ++i) {
				const auto& m = a_marks[i];
				if (a_skipTaken && m.taken) continue;
				const float d = a_you.GetDistance(m.at);
				if (d > a_r) continue;
				if (d < nearD) nearD = d, nearest = i;
				if (!aim) continue;
				RE::NiPoint3 to = (m.at + RE::NiPoint3{ 0, 0, 50.0f }) - from;
				const float  l = to.Length();
				if (l < 1.0f) continue;
				if (const float c = (to.x * dir.x + to.y * dir.y + to.z * dir.z) / l; c > bestCos) bestCos = c, best = i;
			}
			return best >= 0 ? best : nearest;
		}
		// a cast of yours close by, a moment ago: its element (consumed)
		int CastNear(const RE::NiPoint3& a_at, float a_r)
		{
			if (!g_castEl || std::chrono::steady_clock::now() - g_castAt > std::chrono::milliseconds(1200) || g_castFrom.GetDistance(a_at) > a_r) return 0;
			return std::exchange(g_castEl, 0);
		}
		// (the desk check: the player's own pitch isn't where a third-person camera looks) where you're looking from, and which way
		bool CamAim(RE::NiPoint3& a_from, RE::NiPoint3& a_dir)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->cameraRoot) return false;
			const auto& w = cam->cameraRoot->world;
			a_from = w.translate;
			a_dir = { w.rotate.entry[0][1], w.rotate.entry[1][1], w.rotate.entry[2][1] };
			const float l = a_dir.Length();
			if (l < 0.01f) return false;
			a_dir = a_dir / l;
			return true;
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
			for (auto& m : g_live.marks) Remove(m.obj), Remove(m.light), Gone(m.guard), Remove(m.goal);
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
					// (Noah: "not so close to the altar - randomly spawned in and wandering around on a path within 200m of the altar so it
					// makes more sense to search for them") the spirit cats: anywhere 35 to 200 m out, a new spread each time
					if (Mech(e, st) == "flee") {
						static std::mt19937 rr{ std::random_device{}() };
						std::uniform_real_distribution<float> u(0.0f, 1.0f);
						for (int tries = 0; tries < 10 && !at; ++tries) {
							const float ang = u(rr) * 6.2832f, r = s.r0 + u(rr) * (s.r1 - s.r0) * (tries < 6 ? 1.0f : 0.4f);
							at = Walkable(a_p, c + RE::NiPoint3{ std::cos(ang) * r, std::sin(ang) * r, 0 }, 600.0f, true);
							if (at && std::abs(at->z - c.z) > 1800.0f) at.reset();
						}
					}
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
					if (!m.taken && mech == "flee") {                               // a shy spirit cat, not a relic (made as you come near)
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
		struct Said { gen::Sound snd; std::string text; float secs; int card = -1; std::string file{}; };   // (file: a Talk line, played by its file)
		// (Noah, 2026-11-24: the altar's Talk) Felldir's answers are files of their own (Sound\fx\CurseOfAlduin\altarvo\<key>.wav),
		// played as the whispers are: one voice at a time, through Skyrim's UI output (the whispers' own, 0xB75FB)
		RE::BSSoundHandle g_talkH;
		void StopTalk()
		{
			if (g_talkH.IsValid()) g_talkH.FadeOutAndRelease(200);
			g_talkH = RE::BSSoundHandle{};
		}
		void NoteFreed();   // (the altar's Talk, below) a city seen to wake now
		// (Noah, 2026-11-24: "the echo altar voice should be coming from the echo altar's center stone, heard within 30 m - normal volume
		// within 5 m, gradually quieter the farther away") every line of the altar's - its answers and its whispers alike - played from
		// its file at the centre stone, through Skyrim's own distant mono voice output (SOMMono10000_verb: the place's own echo on it),
		// and its loudness set by the distance each moment: whole within 5 m (350 units), fading smoothly to nothing at 30 m (2100)
		RE::NiPoint3 g_talkAt{};
		bool         g_talkPlaced = false;
		float        g_talkVol = 1.0f;
		float Falloff(float a_d)
		{
			if (a_d <= 350.0f) return 1.0f;
			if (a_d >= 2100.0f) return 0.0f;
			const float x = (a_d - 350.0f) / 1750.0f;
			return (1.0f - x) * (1.0f - x) * (1.0f + 2.0f * x * 0.35f);   // (a soft shoulder, then a long tail)
		}
		void TalkVolume()
		{
			if (!g_talkH.IsValid()) return;
			auto* p = RE::PlayerCharacter::GetSingleton();
			const float f = g_talkPlaced && p ? Falloff(p->GetPosition().GetDistance(g_talkAt)) : 1.0f;
			g_talkH.SetVolume(std::clamp(g_talkVol * f, 0.0f, 1.0f));
		}
		void PlayTalk(const std::string& a_rel, float a_vol)   // (a_rel: under Data\Sound, e.g. fx\CurseOfAlduin\altarvo\av_bye_1.wav)
		{
			StopTalk();
			auto* am = RE::BSAudioManager::GetSingleton();
			if (!am || a_vol <= 0.001f) return;
			RE::BSResource::ID id;
			id.GenerateFromPath(("Sound\\" + a_rel).c_str());
			am->GetSoundHandleByFile(g_talkH, id, 0x1A, 128);
			if (!g_talkH.IsValid()) {
				logger::warn("talk: {} could not be played", a_rel);
				return;
			}
			g_talkPlaced = false;
			const int town = Curse::ZoneTown();
			if (auto* altar = town >= 0 ? Shops::Placed(town, 0) : nullptr) {
				g_talkAt = altar->GetPosition() + RE::NiPoint3{ 0, 0, 150.0f };   // (the centre stone, about its middle)
				g_talkPlaced = true;
				if (auto* som = RE::TESForm::LookupByID<RE::BGSSoundOutput>(0x00104D56)) g_talkH.SetOutputModel(som);   // SOMMono10000_verb
				g_talkH.SetPosition(g_talkAt);
			} else if (auto* som = RE::TESForm::LookupByID<RE::BGSSoundOutput>(0x000B75FB)) {
				g_talkH.SetOutputModel(som);   // (no altar near: as the interface's own sound)
			}
			g_talkVol = a_vol;
			TalkVolume();
			g_talkH.Play();
		}
		std::deque<Said> g_say;
		float            g_sayT = 0, g_whCd = 0;
		int              g_sayCard = -1;   // (the altar's Whispers) the card whose line is being said
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
		// the quest whose sign waits for you: your class's, once six altars are free, not yet begun
		int SignQuest()
		{
			const int c = Classes::Get();
			if (c < 0 || (FreedTowns() < 6 && !g_dev)) return -1;
			for (int e = 0; e < kCount; ++e) {
				if (gen::kEggs[e].cls != c || Done(e) || Gifted(e) || Begun(e)) continue;
				const int st = Classes::WonderState(c);
				if (g_dev || (st >= 1 && st <= 3)) return e;
			}
			return -1;
		}
		constexpr const char* kSignLine[4] = { "A lone spirit walks the night. Follow it softly... and see where it leads.",
			                                   "When a star crosses the sky above you... strike it down.",
			                                   "One of the dead burns brighter than the rest. Break it with glass, and catch what falls.",
			                                   "Where the World-Eater first returned, a broken blade waits for a hand." };
		constexpr int         kRevealAt[3] = { 2, 4, 6 };
		constexpr const char* kRevealTitle[3] = { "Older Than the Empire", "The Three", "Felldir the Old" };
		constexpr const char* kRevealLine[3] = { "Two stones wake... so you can hear me. These altars were old before your Empire was born. They were raised while dragons still ruled the sky.",
			                                     "We raised them, the three of us... the first who learned the Voice and turned it on its masters. One fell at the summit. One I have not seen since. And I... I am still here.",
			                                     "Six stones are free... you may hear my name. I am Felldir the Old. I read the Scroll that cast Alduin out of time, and I wait for you behind his mist. The wonders stir... watch the night." };
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
				if (const int sq = SignQuest(); sq >= 0) Say(fmt::format("echo_{}_sign", gen::kEggs[sq].voice), kSignLine[sq]);
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
			TalkVolume();   // (the voice from the stone: louder as you come near, gone past 30 m)
			if (g_sayT > 0) {
				if ((g_sayT -= a_dt) <= 0 && g_say.empty()) g_sayCard = -1;
			} else if (!g_say.empty()) {
				auto l = g_say.front();
				g_say.pop_front();
				StopTalk();
				Audio::Voice(std::nullopt);
				const std::string rel = !l.file.empty() ? "fx\\CurseOfAlduin\\altarvo\\" + l.file + ".wav" : std::string(gen::kSoundFile[static_cast<int>(l.snd)]);
				if (!rel.empty()) PlayTalk(rel, Settings::AltarVoiceVolume());   // (MCM: its volume)  (one voice at a time: a card clicked cuts off the line before it)
				else Audio::Voice(l.snd, Settings::AltarVoiceVolume());
				g_sayCard = l.card;
				logger::info("eggs: the altar says \"{}\"", l.text);   // (Noah: "remove the subtitle cards when the altar is speaking so it's more cryptic")
				Hud::Subtitle(l.text, l.secs + 0.4f);   // (Noah, 2026-11-23: its words in Edge UI's subtitle line - never a card)
				g_sayT = l.secs + 0.5f;
			}
			// (the timing) walking within about 12 m of an altar, out of a fight, once a visit, and one whisper every 3 minutes
			const int town = Curse::ZoneTown();
			auto*     altar = town >= 0 ? Shops::Placed(town, 0) : nullptr;
			const float d = altar ? a_p->GetPosition().GetDistance(altar->GetPosition()) : FLT_MAX;
			if (d > 2000.0f && g_visited == town) g_visited = -1;
			if (town < 0) g_visited = -1;
			if (!altar || d > 850.0f || g_visited == town || a_p->IsInCombat() || g_whCd > 0 || g_sayT > 0 || !g_say.empty()) return;
			if (e < 0 && SignQuest() < 0) return;   // (nothing under way, no sign to hint: nothing to say - the wonders' sleep is said once, as the first altar wakes)
			g_visited = town;
			g_whCd = 180.0f;
			SayHere(e, town);
		}
		// ---- (Noah, 2026-11-19: "a unique way for each class to start their Easter egg/wonder quest instead of it just being fully
		// open after 6 echo altars are free") each secret's sign, seen only by its own path once six altars are free:
		//   the Ghost Pride      a lone spirit cat at night near a freed altar - follow it softly and it leads to a sabre cat's fang
		//   the Eye of Magnus    once a night a star streaks over the freed city you're in - strike it with a spell; a shard falls
		//   the Elixir of Aetherius   once a night one of the dead burns gold - kill it with a flask and catch its essence
		//   the Slayer's Oath    a rusted blade in Helgen's ruins - lift it
		// the thing found on the ground (fang, shard, essence) touched: the secret begins ----
		struct Sign
		{
			int                       egg = -1;
			RE::ObjectRefHandle       drop, dropLight;      // the fang, the shard, the essence
			RE::NiPoint3              dropAt;
			bool                      dropOut = false;
			RE::ActorHandle           cat;                  // (the pride) the lone spirit and its trail
			std::vector<RE::NiPoint3> path;
			int                       at = 0;
			float                     wait = 0, growl = 0, glowT = 0, saidT = 0, wet = 0;
			RE::NiPoint3              dry;
			bool                      ghostDone = false, atEnd = false;
			RE::ObjectRefHandle       goal;                 // (the lone spirit on its feet) the marker it paths to
			RE::NiPoint3              dest;
			bool                      moving = false, held = false;
			float                     moveT = 0;
			float                     starIn = -1, starT = -1, emit = 0;   // (the Eye) the star's wait, its flight
			RE::NiPoint3              starFrom, starVel;
			bool                      starDone = false;
			float                     glowIn = -1;          // (the Stone) the gold one of the dead
			RE::ActorHandle           glow;
			bool                      glowDone = false;
			RE::ObjectRefHandle       blade, bladeLight;    // (the Oath) the rusted blade in Helgen
			int                       bladeHave = -1;
		} g_sign;
		bool g_signNow = false;   // (dev: eggsign) the sign at once, day or night
		bool g_resetReveal = false;   // (a load) the freed count read afresh
		std::chrono::steady_clock::time_point g_lastFlaskKill{};
		constexpr RE::FormID kSabreFang = 0x06BC04, kRustedBlade = 0x02C66F;   // Skyrim.esm: SabrecatTooth, DraugrSword (Ancient Nord Sword)
		const RE::NiPoint3   kHelgenSpot{ 15460.0f, -81716.0f, 8205.0f };       // Helgen's square, where the headsman's block stood
		void ClearSign(bool a_blade)
		{
			Remove(g_sign.drop), Remove(g_sign.dropLight);
			g_sign.dropOut = false;
			Gone(g_sign.cat), Remove(g_sign.goal);
			g_sign.path.clear(), g_sign.at = 0, g_sign.wait = 0, g_sign.moving = false, g_sign.held = false, g_sign.atEnd = false;
			g_sign.starT = -1, g_sign.starIn = -1;
			g_sign.glow = {}, g_sign.glowIn = -1;
			if (a_blade) Remove(g_sign.bladeLight), g_sign.blade = {}, g_sign.bladeHave = -1;
		}
		void SignSay(std::string_view a_text, float a_secs = 3.0f)
		{
			if (g_sign.saidT > 0) return;
			g_sign.saidT = 6.0f;
			Hud::QuestBanner(std::string(a_text), a_secs);
		}
		void DropAt(RE::PlayerCharacter* a_p, const RE::NiPoint3& a_at, RE::FormID a_vanilla, float a_scale = 1.0f)
		{
			const auto spot = Walkable(a_p, a_at, 600.0f).value_or(a_at);
			Remove(g_sign.drop), Remove(g_sign.dropLight);
			if (a_vanilla) {
				// (Noah: "an actual model instead of a blue glow on the ground with nothing there") the thing itself, large enough to
				// see and held where it lies (never dropped through the ground by its physics)
				if (auto* b = RE::TESForm::LookupByID<RE::TESBoundObject>(a_vanilla)) g_sign.drop = Gun::PlaceAt(b, spot + RE::NiPoint3{ 0, 0, 10.0f }, 0.0f);
				if (auto r = g_sign.drop.get()) {
					if (a_scale != 1.0f) r->SetScale(a_scale);
					r->SetMotionType(RE::hkpMotion::MotionType::kKeyframed);
				}
			} else {
				g_sign.drop = Place(a_p, gen::kRelRelic, spot, 0.32f);
			}
			g_sign.dropLight = Place(a_p, gen::kRelRelicLight, spot + RE::NiPoint3{ 0, 0, 70.0f }, 1.0f);
			g_sign.dropAt = spot, g_sign.dropOut = true;
			Gun::FxBlast("light", spot + RE::NiPoint3{ 0, 0, 30.0f });
		}
		void Begin(int e)
		{
			SetBit(12 + e, true);
			ClearSign(true);
			SetRaw(e, 0), SetTowns(e, 0);
			Forms::Play(gen::Sound::quest_step);
			Hud::QuestBanner(std::string(gen::kEggs[e].title) + " has begun", 4.0f);
			g_say.clear(), g_sayT = 0;
			Say(fmt::format("echo_{}_open", gen::kEggs[e].voice), gen::kEggs[e].open);
			SetOpenSaid(e, true);
			logger::info("eggs: {} begun - its sign found", gen::kEggs[e].id);
		}
		// (Events) a spell of yours as the star crosses: aimed close enough, it's struck
		void SignCast(RE::PlayerCharacter* a_p, int a_el)
		{
			if (g_sign.starT < 0 || !a_el) return;
			const auto   star = g_sign.starFrom + g_sign.starVel * g_sign.starT;
			RE::NiPoint3 from, aim;
			if (!CamAim(from, aim)) {
				const float pitch = a_p->GetAngleX(), yaw = a_p->GetAngleZ();
				aim = { std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
				from = a_p->GetPosition() + RE::NiPoint3{ 0, 0, 120.0f };
			}
			RE::NiPoint3 to = star - from;
			const float d = to.Length();
			if (d < 1.0f || d > 14000.0f) return;
			to = to / d;
			if (aim.x * to.x + aim.y * to.y + aim.z * to.z < 0.98f) return;   // (within ~11 degrees)
			g_sign.starT = -1, g_sign.starDone = true;
			Gun::ElemBurst(4, star, 3.0f, 0.0f, 1.2f);
			Gun::LightningArc(a_p->GetPosition() + RE::NiPoint3{ 0, 0, 150.0f }, star, 0.0f);
			Audio::AtPoint(gen::Sound::boss_quake, a_p->GetPosition(), 0.3f, 0.6f);
			logger::info("eggs: the falling star struck at ({:.0f},{:.0f},{:.0f})", star.x, star.y, star.z);
			Gun::Lob("CurseOfAlduin\\fx\\lightorb.nif", star, RE::NiPoint3{ 0, 0, -600.0f }, 1.4f, [](const RE::NiPoint3& land) {
				if (auto* p = RE::PlayerCharacter::GetSingleton()) DropAt(p, land, 0);
				Gun::ElemBurst(4, land + RE::NiPoint3{ 0, 0, 20.0f }, 1.6f, 0.0f, 1.0f);
			});
		}
		void TickSign(RE::PlayerCharacter* a_p, float a_dt)
		{
			const int e = SignQuest();
			if (e != g_sign.egg) {
				ClearSign(true);
				g_sign = {};
				g_sign.egg = e;
			}
			if (e < 0) return;
			g_sign.saidT = std::max(0.0f, g_sign.saidT - a_dt);
			const bool night = Director::NightNow() || g_signNow;
			static bool wasNight = false;
			if (wasNight && !Director::NightNow()) g_sign.starDone = g_sign.glowDone = g_sign.ghostDone = false;   // (a new night: a new sign)
			wasNight = Director::NightNow();
			const auto pp = a_p->GetPosition();
			// the found thing: touched (or picked up), the secret begins
			if (g_sign.dropOut) {
				auto d = g_sign.drop.get();
				if (pp.GetDistance(g_sign.dropAt) < 170.0f || (g_sign.drop && (!d || d->IsDeleted() || !d->GetParentCell()))) {
					Begin(e);
					return;
				}
			}
			const int  town = Curse::ZoneTown();
			const bool freedHere = town >= 0 && (Curse::Freed(town) || g_dev);
			auto*      altar = freedHere ? Shops::Placed(town, 0) : nullptr;
			static std::mt19937 rr{ std::random_device{}() };
			std::uniform_real_distribution<float> u(0.0f, 1.0f);
			const auto v = gen::kEggs[e].voice;
			if (v == "pride") {
				auto cat = g_sign.cat.get();
				if (!night || !altar || pp.GetDistance(altar->GetPosition()) > 9000.0f) {
					if (cat || !g_sign.path.empty()) Gone(g_sign.cat), Remove(g_sign.goal), g_sign.path.clear(), g_sign.moving = false;
					return;
				}
				if (g_sign.dropOut || g_sign.ghostDone) return;
				if (!cat && g_sign.path.empty()) {   // the lone spirit and the way it will lead
					const auto  c = altar->GetPosition();
					float       a0 = 0;
					std::optional<RE::NiPoint3> at;
					for (int t = 0; t < 12 && !at; ++t) {   // (a walled city's streets: a few tries, nearer in each time)
						a0 = u(rr) * 6.2832f;
						const float r = 1500.0f - t * 90.0f;
						at = Walkable(a_p, c + RE::NiPoint3{ std::cos(a0) * r, std::sin(a0) * r, 0 }, 900.0f, true);
					}
					if (!at) {
						static int miss = 0;
						if (miss++ % 120 == 0) logger::info("eggs: no walkable ground near {}'s altar for the lone spirit yet", gen::kTowns[town]);
						return;
					}
					g_sign.path.push_back(*at);
					// (Noah, 2026-11-21) its trail on dry ground, a stretch at a time it walks while you follow - eight stops of ~13 m
					float a = a0;
					for (int i = 0; i < 16 && g_sign.path.size() < 8; ++i) {
						a += (u(rr) - 0.5f) * 1.0f;
						if (auto nx = Walkable(a_p, g_sign.path.back() + RE::NiPoint3{ std::cos(a) * 900.0f, std::sin(a) * 900.0f, 0 }, 450.0f, true); nx && std::abs(nx->z - g_sign.path.back().z) < 500.0f) g_sign.path.push_back(*nx);
						else a += 0.9f;   // (blocked: try round it)
					}
					if (auto* k = Classes::SpiritCat(g_sign.path[0], false, 1800.0f)) g_sign.cat = k->GetHandle();
					g_sign.at = 0, g_sign.wait = 0, g_sign.moving = false, g_sign.held = false, g_sign.atEnd = false;
					logger::info("eggs: a lone spirit cat walks near {}'s altar ({} steps to its fang)", gen::kTowns[town], g_sign.path.size());
					return;
				}
				if (!cat || cat->IsDead()) { Gone(g_sign.cat), Remove(g_sign.goal), g_sign.path.clear(), g_sign.moving = false; return; }
				if (cat->IsInCombat()) cat->StopCombat();
				if (!g_sign.held && cat->Get3D()) g_sign.held = true, CatHold(cat.get(), true);   // (it stands where it was set down, waiting for you)
				const auto  at = cat->GetPosition();
				const float d = pp.GetDistance(at);
				if (cat->AsActorState()->IsSwimming()) {   // (in the water all the same: back to the dry ground it last stood on - on foot while you watch)
					if ((g_sign.wet += a_dt) > 0.8f) {
						g_sign.wet = 0;
						const auto back = g_sign.dry.Length() > 1.0f ? g_sign.dry : g_sign.path[0];
						if (d > 1600.0f) cat->SetPosition(back, true), CatHold(cat.get(), true), g_sign.moving = false;
						else CatGo(a_p, cat.get(), g_sign.goal, back, true), g_sign.moving = true, g_sign.moveT = 0, g_sign.dest = back;
					}
					return;
				}
				g_sign.wet = 0;
				g_sign.dry = at;
				if (d < 2100.0f && (g_sign.growl -= a_dt) <= 0) g_sign.growl = 6.0f + u(rr) * 6.0f, Audio::At(gen::Sound::boss_growl, cat.get(), 0.45f);
				if (d < 9000.0f && (g_sign.glowT -= a_dt) <= 0) g_sign.glowT = 2.0f + u(rr), Gun::Motes(1, at + RE::NiPoint3{ 0, 0, 40.0f }, 0.35f, 1.8f);
				if (g_sign.moving) {   // on its way: there, it stops and waits for you; stuck where you can't see it, set down where it was going
					g_sign.moveT += a_dt;
					if (at.GetDistance(g_sign.dest) < 170.0f || g_sign.moveT > 16.0f) {
						if (g_sign.moveT > 16.0f && d > 1600.0f) cat->SetPosition(g_sign.dest, true);
						g_sign.moving = false;
						CatHold(cat.get(), true);
					}
				} else if (d > 2500.0f && at.GetDistance(g_sign.path[g_sign.at]) > 700.0f) {
					cat->SetPosition(g_sign.path[g_sign.at], true);   // (far out of sight: back on its trail)
				}
				const bool sneak = a_p->IsSneaking();
				if (g_sign.atEnd || g_sign.moving) {
					g_sign.wait = 0;   // (on its way, or waiting at its trail's end for you)
				} else if (d < 450.0f && !sneak) {   // rushed: it bolts back along its trail on foot - one stop lost
					Gun::Motes(1, at, 0.6f, 1.0f);
					g_sign.at = std::max(0, g_sign.at - 1), g_sign.wait = 0;
					auto to = g_sign.path[g_sign.at];
					if (to.GetDistance(at) < 300.0f) {   // (already at its first stop: off to the side of it instead)
						RE::NiPoint3 away = at - pp;
						away.z = 0;
						if (const float l = away.Length(); l > 1.0f) away = away * (1.0f / l);
						if (auto side = Walkable(a_p, at + away * 900.0f, 450.0f, true)) to = *side;
					}
					CatGo(a_p, cat.get(), g_sign.goal, to, true), g_sign.moving = true, g_sign.moveT = 0, g_sign.dest = to;
					SignSay("It fled from you. Come softly.");
				} else if (d < 900.0f && sneak) {
					if ((g_sign.wait += a_dt) > 1.0f) {   // followed softly: it pads on to its next stop, at a walk
						g_sign.wait = 0;
						if (++g_sign.at >= static_cast<int>(g_sign.path.size()) - 1) g_sign.at = static_cast<int>(g_sign.path.size()) - 1, g_sign.atEnd = true;
						CatGo(a_p, cat.get(), g_sign.goal, g_sign.path[g_sign.at], false), g_sign.moving = true, g_sign.moveT = 0, g_sign.dest = g_sign.path[g_sign.at];
					}
				} else {
					g_sign.wait = 0;
				}
				// (Noah: "revealed once the cat and the player gets within 10 meters of it. Invisible unless following the cat to it") at its
				// trail's end it waits; come within 10 m of it there and it sinks away, and its fang lies where it stood
				if (g_sign.atEnd && !g_sign.moving && pp.GetDistance(g_sign.path.back()) < 700.0f) {
					const auto end = g_sign.path.back();
					Gone(g_sign.cat), Remove(g_sign.goal);
					g_sign.path.clear(), g_sign.ghostDone = true, g_sign.atEnd = false;
					Gun::ElemBurst(1, end + RE::NiPoint3{ 0, 0, 40.0f }, 1.4f, 0.0f, 1.0f);
					Audio::AtPoint(gen::Sound::boss_growl, end, 0.0f, 0.5f);
					DropAt(a_p, end, kSabreFang, 4.0f);
					logger::info("eggs: the lone spirit led you to its fang");
				}
			} else if (v == "eye") {
				if (g_sign.starT >= 0) {   // a star streaking over the city: a burning trail
					g_sign.starT += a_dt;
					const auto at = g_sign.starFrom + g_sign.starVel * g_sign.starT;
					if ((g_sign.emit -= a_dt) <= 0) {
						g_sign.emit = 0.05f;
						Gun::QueueFx("CurseOfAlduin\\fx\\lightorb.nif", at, 4.0f, 0.0f, 0.6f, false);
					}
					if (g_sign.starT > 9.0f) g_sign.starT = -1, g_sign.starDone = true;   // (missed: another night)
					return;
				}
				if (!night || !freedHere || g_sign.starDone || g_sign.dropOut) { g_sign.starIn = -1; return; }
				if (g_sign.starIn < 0) g_sign.starIn = g_signNow ? 2.0f : 20.0f + u(rr) * 100.0f;
				if ((g_sign.starIn -= a_dt) > 0) return;
				const float a = u(rr) * 6.2832f;
				const RE::NiPoint3 dir{ std::cos(a), std::sin(a), 0 };
				g_sign.starFrom = pp + dir * 5500.0f + RE::NiPoint3{ 0, 0, 4200.0f };
				const auto to = pp - dir * 5500.0f + RE::NiPoint3{ 0, 0, 2600.0f };
				g_sign.starVel = (to - g_sign.starFrom) / 9.0f;
				g_sign.starT = 0, g_sign.starIn = -1, g_signNow = false;
				Audio::AtPoint(gen::Sound::boss_quake_wind, pp, 0.0f, 0.5f);
				logger::info("eggs: a star crosses the sky over {}", town >= 0 ? gen::kTowns[town] : "you");
			} else if (v == "stone") {
				auto g = g_sign.glow.get();
				if (g) {
					if ((g_sign.glowT -= a_dt) <= 0 && !g->IsDead()) g_sign.glowT = 1.2f, Gun::Motes(4, g->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, 0.4f, 1.4f);
					if (g->IsDead()) {
						const bool byFlask = std::chrono::steady_clock::now() - g_lastFlaskKill < std::chrono::milliseconds(1500);
						if (byFlask) {
							Gun::ElemBurst(4, g->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f }, 1.4f, 0.0f, 1.0f);
							DropAt(a_p, g->GetPosition(), 0);
							logger::info("eggs: the burning one broken with glass - its essence falls");
						} else {
							SignSay("Its light goes out.");
							logger::info("eggs: the burning one died, not by a flask");
						}
						g_sign.glow = {}, g_sign.glowDone = true;
					} else if (g->IsDisabled() || g->IsDeleted()) {
						g_sign.glow = {};
					}
					return;
				}
				if (!night || g_sign.glowDone || g_sign.dropOut || (!Director::Fighting() && !g_signNow)) { g_sign.glowIn = -1; return; }
				if (g_sign.glowIn < 0) g_sign.glowIn = g_signNow ? 1.0f : 25.0f + u(rr) * 60.0f;
				if ((g_sign.glowIn -= a_dt) > 0) return;
				RE::Actor* pick = nullptr;
				float      best = 3000.0f;
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
					auto a = h.get();
					if (!a || a->IsDead() || a->IsDisabled() || !Director::IsFoe(a.get())) continue;
					if (const float d = a->GetPosition().GetDistance(pp); d < best && d > 500.0f) best = d, pick = a.get();
				}
				if (!pick) { g_sign.glowIn = 4.0f; return; }
				static auto* gold = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kTabletGold, gen::kPlugin);
				if (gold) pick->ApplyEffectShader(gold, 600.0f);
				g_sign.glow = pick->GetHandle(), g_sign.glowIn = -1, g_signNow = false;
				logger::info("eggs: one of the dead burns gold ({:08X})", pick->GetFormID());
			} else if (v == "oath") {
				auto* ws = a_p->GetWorldspace();
				if (!ws || ws->GetFormID() != 0x3C || pp.GetDistance(kHelgenSpot) > 7000.0f) return;
				auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kRustedBlade);
				if (!base) return;
				const auto count = [&] {
					for (const auto& [obj, data] : a_p->GetInventory()) if (obj == base) return data.first;
					return 0;
				};
				auto b = g_sign.blade.get();
				if (!b) {   // the blade: the one left there before (a save), or laid there now
					RE::TES::GetSingleton()->ForEachReferenceInRange(a_p, 7500.0f, [&](RE::TESObjectREFR* r) {
						if (r && r->GetBaseObject() == base && !r->IsDeleted() && r->GetPosition().GetDistance(kHelgenSpot) < 600.0f) {
							g_sign.blade = r->CreateRefHandle();
							return RE::BSContainer::ForEachResult::kStop;
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
					if (!g_sign.blade) {
						const auto spot = Walkable(a_p, kHelgenSpot, 500.0f).value_or(kHelgenSpot);
						g_sign.blade = Gun::PlaceAt(base, spot + RE::NiPoint3{ 0, 0, 15.0f }, u(rr) * 6.28f);
						logger::info("eggs: a rusted blade lies in Helgen ({:.0f},{:.0f},{:.0f})", spot.x, spot.y, spot.z);
					}
					if (auto nb = g_sign.blade.get()) g_sign.bladeLight = Place(a_p, gen::kRelRelicLight, nb->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, 1.0f);
					g_sign.bladeHave = count();
					return;
				}
				static float look = 0;   // (your pack counted twice a second, not every frame)
				if ((look -= a_dt) > 0 && !b->IsDeleted() && b->GetParentCell()) return;
				look = 0.5f;
				if (count() > g_sign.bladeHave || b->IsDeleted() || !b->GetParentCell()) Begin(e);   // lifted
			}
		}

		// the step's city, chosen as the step begins (never the city of the step before) - so every altar can point to it
		bool Eligible(int t) { return g_dev || Curse::Freed(t); }
		// (Noah, 2026-11-21: "the summoners quest should only be done at cities that aren't walled in (require door entrance to render)
		// so it's outdoors and feels more natural") the walled cities - Whiterun, Solitude, Windhelm, Markarth, Riften
		bool Walled(int t) { return t >= 1 && t <= 5; }
		bool OpenOnly(int e) { return gen::kEggs[e].cls == 0; }   // (the Ghost Pride: out under the sky, its cats roaming free)
		int PickTown(int e)
		{
			std::vector<int> c;
			for (int t = 0; t < 10; ++t) if (Eligible(t) && t != PrevTown(e) && !(OpenOnly(e) && Walled(t))) c.push_back(t);
			if (c.empty() && OpenOnly(e)) for (int t = 0; t < 10; ++t) if (Eligible(t) && !Walled(t)) c.push_back(t);   // (only one open town freed: it again)
			if (c.empty()) for (int t = 0; t < 10; ++t) if (Eligible(t) && t != PrevTown(e)) c.push_back(t);
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
			Classes::VoiceSay(gen::VoTrigger::kEgg, 7.0f);   // (and after Felldir, your own word on it)
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
				Hud::QuestBanner(fmt::format("{} {} of {}", a_what, prog, a_n), 2.5f);
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
						Hud::QuestBanner("You were hurt - the " + std::string(s.name) + "s slip back into the shadows", 4.0f);
						Setup(a_p, e, st, a_altar);
						return;
					}
					if (s.seconds > 0 && g_live.timer >= 0 && (g_live.timer += a_dt) > s.seconds) {
						g_live.timer = -1;
						SetRaw(e, st * 1000);
						Hud::QuestBanner(mech == "seals" ? std::string("Too slow - the seals close again") : "Too slow - the " + std::string(s.name) + "s gutter out", 4.0f);
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
						static std::mt19937 rr{ std::random_device{}() };
						std::uniform_real_distribution<float> u(0.0f, 1.0f);
						for (auto& m : g_live.marks) {
							if (m.taken) continue;
							// (Noah, 2026-11-21: "Make them only spawn at night when the step of the quest is reached so they aren't running around
							// during the day") by day there are no cats: any out are put away where they stood, and come again at nightfall
							if (s.night && !night) {
								if (auto r0 = m.obj.get()) m.at = r0->GetPosition();
								Remove(m.obj), Remove(m.goal), m.moving = false, m.held = false;
								continue;
							}
							auto r = m.obj.get();
							// (the crash at Dawnstar's altar: a cat set down 200 m out stood in ground not yet loaded, and its feet's footing
							// was worked out against a world that wasn't there) each cat is made only as you come within the loaded ground
							// round you, and put away again as you leave it - where it wandered to, kept
							const float dCat = pp.GetDistance(m.at);
							if (!r || r->IsDisabled() || r->IsDeleted()) {
								m.obj = {};
								if (dCat < 6000.0f) {
									if (auto* cat = Classes::SpiritCat(m.at, false, 3600.0f)) m.obj = cat->CreateRefHandle(), m.held = false, m.moving = false;
								}
								continue;
							}
							if (dCat > 9000.0f && r->GetPosition().GetDistance(pp) > 9000.0f) {
								m.at = r->GetPosition();
								Remove(m.obj), Remove(m.goal), m.moving = false, m.held = false;
								continue;
							}
							auto* act = r->As<RE::Actor>();
							if (!act) continue;
							if (act->IsInCombat()) act->StopCombat();   // (a shy one never fights)
							// (Noah, 2026-11-21: "running away even when I'm sneaking up to them") it lies where it was set down - held there,
							// never wandering off on its own; it only moves when it sees you coming
							if (!m.held && act->Get3D()) m.held = true, CatHold(act, true);
							const auto  at = r->GetPosition();
							const float d = pp.GetDistance(at);
							// (Noah: "they get stuck and swim around in the water and there's no way to sneak up on them while swimming") a cat
							// in water goes back to the land it last stood on - on its own feet while you can see it
							if (act->AsActorState()->IsSwimming()) {
								if ((m.wet += a_dt) > 0.8f) {
									m.wet = 0;
									auto back = m.dry.Length() > 1.0f ? std::optional<RE::NiPoint3>(m.dry) : std::nullopt;
									for (int tries = 0; tries < 8 && !back; ++tries) {
										const float ang = u(rr) * 6.2832f, rad = s.r0 + u(rr) * (s.r1 - s.r0) * 0.6f;
										back = Walkable(a_p, c + RE::NiPoint3{ std::cos(ang) * rad, std::sin(ang) * rad, 0 }, 500.0f, true);
									}
									if (back) {
										if (d > 1600.0f) act->SetPosition(*back, true), CatHold(act, true), m.moving = false;
										else CatGo(a_p, act, m.goal, *back, true), m.moving = true, m.moveT = 0, m.dest = *back;
										m.dry = {};
									}
								}
								continue;
							}
							m.wet = 0;
							m.at = at;   // (wherever it has gone to)
							m.dry = at;
							const bool sneak = a_p->IsSneaking();
							// (Noah: "growl + faint glow") heard close by, a ghostly growl now and then; seen far off at night, its blue motes
							if (d < 2100.0f && (m.wait -= a_dt) <= 0) {
								m.wait = 7.0f + u(rr) * 7.0f;
								Audio::At(gen::Sound::boss_growl, r.get(), 0.45f);
							}
							if (night && d < 9000.0f && (m.glow -= a_dt) <= 0) {
								m.glow = 2.2f + u(rr) * 1.2f;
								Gun::Motes(1, at + RE::NiPoint3{ 0, 0, 40.0f }, 0.35f, 1.8f);
							}
							if (m.moving) {   // bolting: there, it settles again; stuck where you can't see it, set down where it was going
								m.moveT += a_dt;
								if (at.GetDistance(m.dest) < 170.0f || m.moveT > 14.0f) {
									if (m.moveT > 14.0f && d > 1600.0f) act->SetPosition(m.dest, true);
									m.moving = false;
									CatHold(act, true);
								}
								continue;
							}
							if (d < 500.0f && !sneak) {   // (Noah: "Run off on foot") seen coming: it bolts away over the ground to a new spot
								for (int tries = 0; tries < 8; ++tries) {
									RE::NiPoint3 away = at - pp;
									away.z = 0;
									const float l = std::max(1.0f, away.Length());
									const float ang = (u(rr) - 0.5f) * 1.6f, cs = std::cos(ang), sn = std::sin(ang);
									const RE::NiPoint3 dir{ (away.x * cs - away.y * sn) / l, (away.x * sn + away.y * cs) / l, 0 };
									if (const auto to = Walkable(a_p, at + dir * (1300.0f + u(rr) * 700.0f), 500.0f, true); to && to->GetDistance(c) < s.r1) {
										CatGo(a_p, act, m.goal, *to, true), m.moving = true, m.moveT = 0, m.dest = *to;
										break;
									}
								}
								if (Throttle(6.0f)) Hud::QuestBanner("It shies from you. Come to it quietly.", 3.0f);
							} else if (d < 220.0f && sneak) {
								if (s.night && !night) {
									if (Throttle(6.0f)) Hud::QuestBanner("It won't settle by day. Come back at night.", 3.0f);
									continue;
								}
								act->Disable();
								Remove(m.goal);
								take(m, "light");
								return;
							}
						}
						break;
					}
					// (The Eye of Magnus) each seal opens to a spell of its own element cast close by
					if (mech == "seals") {
						for (auto& m : g_live.marks) {
							if (!m.taken && !m.told && pp.GetDistance(m.at) < 400.0f) m.told = true, Hud::QuestBanner(fmt::format("A {} seal. It wants {}.", m.el == 1 ? "ember" : m.el == 2 ? "rime" : "storm", kEl[m.el]), 3.0f);
						}
						if (const int i = AimedMark(g_live.marks, pp, 1400.0f, true); i >= 0) {
							auto& m = g_live.marks[i];
							if (const int el = CastNear(m.at, 1400.0f); el) {
								if (el == m.el) {
									Gun::ElemBurst(kElFx[m.el], m.at + RE::NiPoint3{ 0, 0, 40.0f }, 1.0f, 0.0f, 0.9f);
									take(m, "light");
								} else {
									Hud::QuestBanner(fmt::format("The {} seal doesn't answer {}.", kEl[m.el], kEl[el]), 2.5f);
								}
								return;
							}
						}
						break;
					}
					// (The Elixir of Aetherius) six of the dead, each wrapped in an element: only a flask of its own element frees its essence
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
							if (!m.told && pp.GetDistance(a->GetPosition()) < 900.0f) m.told = true, Hud::QuestBanner(fmt::format("One of the dead, wrapped in {}.", kFlaskEl[m.el]), 3.0f);
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
							if (pp.GetDistance(at) < 300.0f) a_p->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, 15.0f);
							Hud::QuestBanner(fmt::format("Wrong - only {} frees it. It bursts, and rises again.", kFlaskEl[m.el]), 3.0f);
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
										Hud::QuestBanner("A guardian rises from the shard.", 3.0f);
									}
								}
							}
							if (g && !g->IsDead() && d < 170.0f) {
								if (Throttle(4.0f)) Hud::QuestBanner("Its guardian still stands.", 2.5f);
								continue;
							}
						}
						if (d > 170.0f) continue;
						if (s.night && !night) {
							if (!g_live.toldDay) g_live.toldDay = true, Hud::QuestBanner("It sleeps. Come back at night.", 3.0f);
							continue;
						}
						if (s.element != 0 && HandElement() != s.element) {
							if (!g_live.toldDay) g_live.toldDay = true, Hud::QuestBanner(fmt::format("It doesn't stir. It wants {} in your hands.", kElemName[s.element]), 3.0f);
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
							if (d < 350.0f && !g_live.marks[i].told) g_live.marks[i].told = true, Hud::QuestBanner(fmt::format("The {}-stone.", kEl[g_live.marks[i].el]), 2.5f);
						}
						const int best = AimedMark(g_live.marks, pp, 1200.0f, false);
						if (best >= 0) if (const int ce = CastNear(g_live.marks[best].at, 1200.0f); ce) hit = best, el = ce;
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
								if (Throttle(5.0f)) Hud::QuestBanner("Strike it with your blade.", 2.5f);
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
						Hud::QuestBanner(!elOk ? (mech == "prism" ? std::string("Wrong element - start again") : std::string("Wrong flask - watch the braziers again")) :
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
								Hud::QuestBanner("The spirit cat fell - the hunt starts over", 3.5f);
							}
							g_live.allyUp = false, g_live.mechT = 6.0f;
						}
						if (!al && (g_live.mechT -= a_dt) <= 0) {
							if (auto* cat = Classes::SpiritCat(pp + RE::NiPoint3{ 150.0f, 0, 0 }, true, 3600.0f)) {
								g_live.ally = cat->GetHandle(), g_live.allyUp = true;
								Hud::QuestBanner("A spirit cat hunts beside you. Keep it alive.", 3.0f);
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
							// (the desk check: a champion wears its own kind's name - the quest's name is only its banner) the champion
							// raised for this city's step, by its handle
							auto* f = Director::ChampionActive(-10 - CurTown(e)) ? Director::Champion() : nullptr;
							if (!f) f = FindNamed(s.name, c, 4000.0f);
							if (f) g_live.boss = f->GetHandle(), b = f->GetHandle().get();
						}
						if (!b || b->IsDead()) break;
						if (mech == "chains") {   // (The Ghost Pride) untouchable while a soul-chain stands
							const int left = static_cast<int>(std::count_if(g_live.marks.begin(), g_live.marks.end(), [](const Mark& m) { return !m.taken; }));
							if (left > 0 && !g_live.ghosted) Con(b.get(), "SetGhost 1"), g_live.ghosted = true, Hud::QuestBanner("Three soul-chains hold him. Break them.", 3.5f);
							for (auto& m : g_live.marks) {
								if (m.taken || pp.GetDistance(m.at) > 170.0f) continue;
								m.taken = true;
								Remove(m.obj), Remove(m.light);
								Gun::FxBlast("light", m.at + RE::NiPoint3{ 0, 0, 40.0f });
								Forms::Play(gen::Sound::quest_step);
								Hud::QuestBanner(left - 1 > 0 ? fmt::format("A chain breaks - {} left", left - 1) : std::string("The last chain breaks - he can be hurt"), 3.0f);
								if (left - 1 == 0) Con(b.get(), "SetGhost 0"), g_live.ghosted = false;
								break;
							}
						} else if (mech == "anomaly") {   // (The Eye of Magnus) it wears each element in turn
							if ((g_live.mechT -= a_dt) <= 0) {
								g_live.mechT = 6.0f;
								g_live.worn = g_live.worn % 3 + 1;
								Gun::ElemBurst(kElFx[g_live.worn], b->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f }, 1.4f, 0.0f, 1.0f);
								Hud::QuestBanner(fmt::format("The Anomaly wears {}", kEl[g_live.worn]), 2.0f);
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
						Hud::QuestBanner(mech == "summonsonly" ? "You struck a blow - only your summons may kill. The den starts again." : "You were hurt - the brew is shaken. Hold the altar again.", 4.0f);
						break;
					}
					if (!g_live.started) {
						g_live.started = true, g_live.struck = false, g_live.hurt = false;
						Hud::QuestBanner(std::string(s.text), 4.0f);
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
		// (Noah: "make the reveal to who rose the alters and who the voice is slowly as the echo altars are freed") as the second,
		// fourth and sixth altars wake, the voice says a little more of who it is (said once; a save already past them stays quiet)
		{
			static int last = -1;
			const int  f = FreedTowns();
			if (last < 0 || f < last) {
				for (int i = 0; i < 3; ++i) if (f >= kRevealAt[i]) SetBit(20 + i, true);
				if (f >= 1) SetBit(23, true);
			} else if (f > last) {
				// (Noah, 2026-11-20: "Make the wonders still sleep voice play once the first echo alter is freed in riverwood") the
				// voice's first words, once, as the first altar wakes
				if (f >= 1 && !((Gifts() >> 23) & 1)) {
					SetBit(23, true);
					if (Say("echo_asleep", gen::kWhisperAsleep)) g_say.back().card = 1;
				}
				for (int i = 0; i < 3; ++i) {
					if (f < kRevealAt[i] || RevealSaid(i)) continue;
					SetBit(20 + i, true);
					if (Say(fmt::format("echo_voice_{}", kRevealAt[i]), kRevealLine[i])) g_say.back().card = 3 + i;
				}
			}
			last = f;
			if (g_resetReveal) g_resetReveal = false, last = -1;
		}
		NoteFreed();
		TickSign(p, a_delta);
		TickWhispers(p, e, a_delta);
		if (e < 0) {
			if (g_live.egg >= 0) Clear();
			return;
		}
		// (Noah, 2026-11-21) a Ghost Pride step waiting in a walled city, not yet begun there: moved out to an open town
		if (OpenOnly(e) && CurTown(e) >= 0 && Walled(CurTown(e)) && Prog(e) == 0 && g_live.egg < 0) {
			if (const int t = PickTown(e); t >= 0 && !Walled(t)) {
				logger::info("eggs: {} step {} moved from walled {} to {}", gen::kEggs[e].id, Step(e) + 1, gen::kTowns[CurTown(e)], gen::kTowns[t]);
				SetTowns(e, (PrevTown(e) + 1) * 100 + t + 1);
			}
		}
		// (Noah, 2026-11-19) the step's city is chosen as the step begins, so every altar's riddle can point to it
		if (CurTown(e) < 0) {
			// (the desk check: carried by night to another city's altar, the essences could never beat dawn across Skyrim) the
			// crucible's step is this same city's altar
			if (Step(e) < gen::kEggs[e].nSteps && Mech(e, Step(e)) == "dawn" && PrevTown(e) >= 0) {
				SetTowns(e, (PrevTown(e) + 1) * 100 + PrevTown(e) + 1);
				logger::info("eggs: {} step {} waits in {} (the same altar)", gen::kEggs[e].id, Step(e) + 1, gen::kTowns[PrevTown(e)]);
			} else if (const int t = PickTown(e); t >= 0) {
				SetTowns(e, (PrevTown(e) + 1) * 100 + t + 1);
				logger::info("eggs: {} step {} waits in {}", gen::kEggs[e].id, Step(e) + 1, gen::kTowns[t]);
			}
		}
		// (The Elixir of Aetherius) the essences were taken by night: should dawn come before they reach the altar, they're spoilt
		if (Step(e) < gen::kEggs[e].nSteps && Mech(e, Step(e)) == "dawn") {
			if (Director::NightNow()) {
				g_dawnArmed = true;
			} else if (g_dawnArmed) {
				g_dawnArmed = false;
				SetRaw(e, 0), SetTowns(e, 0);
				Clear();
				Forms::Play(gen::Sound::rite_fail);
				Hud::QuestBanner("Dawn broke - the essences spoilt. Six more must be taken by night.", 5.0f);
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
				Hud::QuestBanner(fmt::format("The ground drinks: {} of {}", prog, s.n), 2.5f);
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
		if (a_how == 2) g_lastFlaskKill = std::chrono::steady_clock::now();   // (the Stone's sign: the burning one broken with glass)
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
				if (Throttle(8.0f)) Hud::QuestBanner("The oath wants a spell ready in your other hand.", 3.0f);
				return;
			}
		}
		// (The Eye of Magnus) fire, frost and storm: ten each at the least
		if (mech == "elemsplit") {
			if (a_element < 0 || a_element > 2) {
				if (Throttle(8.0f)) Hud::QuestBanner("The Eye wants fire, frost or storm.", 3.0f);
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
				Hud::QuestBanner(fmt::format("The Eye drinks: fire {}  frost {}  storm {}  ({} of {})", k[0], k[1], k[2], sum, s.n), 3.0f);
			}
			return;
		}
		const int prog = Prog(e) + 1;
		SetRaw(e, Step(e) * 1000 + prog);
		if (prog >= s.n) {
			Advance(e);
		} else if (prog % 10 == 0) {
			Forms::Play(gen::Sound::quest_step);
			Hud::QuestBanner(fmt::format("The secret drinks: {} of {}", prog, s.n), 2.5f);
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
				if (Throttle(3.0f)) Hud::QuestBanner(fmt::format("It drinks your {} - strike with what it isn't wearing", kEl[el]), 2.5f);
			}
		} else if (mech == "thorgar") {   // (The Slayer's Oath) magic won't touch him
			if (a_source && a_source->As<RE::MagicItem>()) {
				Heal(a_target, 0.03f);
				if (Throttle(3.0f)) Hud::QuestBanner("Magic will not touch him. The blade alone.", 2.5f);
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
		} else if (mech == "matriarch") {   // (The Elixir of Aetherius) she drinks the same element twice
			auto b = g_live.boss.get();
			if (!b || b->IsDead() || b->GetPosition().GetDistance(a_at) > 350.0f) return;
			if (a_element == g_live.lastEl) {
				Heal(b.get(), 0.05f);
				if (Throttle(3.0f)) Hud::QuestBanner(fmt::format("She drinks your {} - never the same twice", kFlaskEl[std::clamp(a_element, 0, 5)]), 2.5f);
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
				SignCast(p, el);   // (the Eye's sign: the star struck)
			}
		}
		return false;
	}

	// (the story audit: another save loaded on the same step kept this one's marks and its boss half-started)
	void Reset()
	{
		Clear();
		g_hintKey = -1, g_lastProg = 0, g_stuck = 0, g_plain = false, g_beamCd = 0, g_dawnArmed = false;
		ClearSign(true);
		g_sign = {};
		g_resetReveal = true;
	}

	void Heard(int) {}  // (each class keeps its own quest: a friend's isn't yours)

	// (Noah, 2026-11-19: "the whispers tab should be cards like the armory with the title of each whisper ... when a title card is
	// clicked it plays the whisper") each of Felldir's lines this altar has for you, as a card: a title, a short note, and the id a
	// click replays - 1 the wonders asleep; 1000 + quest*20: + 0 its calling, + 1.. a step, + 15 where the next step waits,
	// + 16 the altar's plain words; 2000 + quest its finale
	// (Noah: "Whispers tab -> sub tabs with the wonders names -> whispers for that wonder when discovered") group 0 the voice
	// itself (the wonders asleep, what it has said of who it is); 1-4 a wonder's secret (its sign, its calling, its steps). Ids:
	// 1 asleep, 3-5 the voice's reveals, 1000 + quest*20: + 0 its calling, + 1.. a step, + 15 where the next step waits, + 16 the
	// altar's plain words, + 17 its sign; 2000 + quest its finale
	std::vector<Whisper> Whispers(int a_town)
	{
		static constexpr const char* kNum[9] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX" };
		std::vector<Whisper> out;
		if ((Gifts() >> 23) & 1) out.push_back({ "The Wonders Sleep", "the first altar awake", 1, 0 });
		for (int i = 0; i < 3; ++i) if (RevealSaid(i)) out.push_back({ kRevealTitle[i], fmt::format("{} altars awake", kRevealAt[i]), 3 + i, 0 });
		const int act = Active();
		const int sq = SignQuest();
		for (int e = 0; e < kCount; ++e) {
			const auto& d = gen::kEggs[e];
			const int   base = 1000 + e * 20, g = 1 + e;
			if (e == sq) out.push_back({ "A Sign", "how it begins", base + 17, g });
			if (!Begun(e) && !Done(e) && !Gifted(e)) continue;
			out.push_back({ std::string(d.title), "the calling", base, g });
			if (Done(e) || Gifted(e)) {
				for (int i = 0; i < d.nSteps; ++i) out.push_back({ std::string(kNum[i]) + ". " + std::string(d.steps[i].title), "done", base + 1 + i, g });
				out.push_back({ "The End", Classes::WonderState(d.cls) == 4 ? std::string(Classes::WonderName(d.cls)) + " is yours" : std::string("High Hrothgar calls"), 2000 + e, g });
				continue;
			}
			for (int i = 0; i < Step(e); ++i) out.push_back({ std::string(kNum[i]) + ". " + std::string(d.steps[i].title), "done", base + 1 + i, g });
			if (e != act) continue;
			const int st = Step(e), cur = CurTown(e);
			if (cur >= 0 && a_town == cur) out.push_back({ std::string(kNum[st]) + ". " + std::string(d.steps[st].title), "here, now", base + 1 + st, g });
			else if (cur >= 0) out.push_back({ "The Next Whisper", fmt::format("where step {} waits", kNum[st]), base + 15, g });
			if (g_plain && g_hintKey == e * 100 + st) out.push_back({ "The Altar Speaks Plainly", "what it wants, spelled out", base + 16, g });
		}
		return out;
	}
	bool Named() { return RevealSaid(2) || FreedTowns() >= 6; }
	std::string GroupName(int a_g)
	{
		if (a_g <= 0) return Named() ? "Felldir the Old" : "The Voice in the Stone";
		return std::string(Classes::WonderName(gen::kEggs[a_g - 1].cls));
	}

	void Replay(int a_id)
	{
		// (Noah, 2026-11-21: Riverwood's altar said its riddle four times, 14 s apart) a card's replay is written down, so a line said
		// again can be told from one the altar chose to say
		logger::info("eggs: whisper card {} chosen at the altar - said again", a_id);
		g_say.clear(), g_sayT = 0, g_sayCard = -1;
		const auto at = g_say.size();
		if (a_id == 1) {
			Say("echo_asleep", gen::kWhisperAsleep);
		} else if (a_id >= 3 && a_id <= 5) {
			Say(fmt::format("echo_voice_{}", kRevealAt[a_id - 3]), kRevealLine[a_id - 3]);
		} else if (a_id >= 2000 && a_id < 2000 + kCount) {
			const auto& d = gen::kEggs[a_id - 2000];
			Say(fmt::format("echo_{}_final", d.voice), d.finale);
		} else if (a_id >= 1000 && a_id < 1000 + kCount * 20) {
			const int   e = (a_id - 1000) / 20, k = (a_id - 1000) % 20;
			const auto& d = gen::kEggs[e];
			if (k == 0) {
				Say(fmt::format("echo_{}_open", d.voice), d.open);
			} else if (k >= 1 && k <= d.nSteps) {
				Say(StepKey(e, k - 1, false), d.steps[k - 1].hint);
				if (k - 1 < Step(e)) Say(StepKey(e, k - 1, true), d.steps[k - 1].done);   // (a step found: how it ended, too)
			} else if (k == 15) {
				SayCity(CurTown(e));
			} else if (k == 17) {
				Say(fmt::format("echo_{}_sign", d.voice), kSignLine[e]);
			} else if (k == 16 && Step(e) < d.nSteps) {
				Forms::Play(gen::Sound::quest_step);
				Hud::Lore("THE ALTAR SPEAKS PLAINLY", std::string(d.steps[Step(e)].text), 9.0f);
			}
		}
		for (auto i = at; i < g_say.size(); ++i) g_say[i].card = a_id;
		if (g_say.empty()) return;
		StopTalk();
		Audio::Voice(std::nullopt);   // (the line being said stops for the one asked for)
	}

	int Speaking() { return g_sayCard >= 0 || !g_say.empty() ? (g_say.empty() ? g_sayCard : g_say.front().card) : -1; }

	// ---- (Noah, 2026-11-24: "give the echo alter a lot more voice lines that players can interact with like talking with npcs,
	// having a list of things to say that changes as the mod's story progresses") the altar's Talk: four topics, each a list of
	// what you can ask - the list changing with the story (each prompt's gate, tools/altarvo_script.py) - and Felldir's answers,
	// said one after another with their words in the subtitle line. What you have asked is kept in the save (UN_TalkHeard0-5).
	namespace
	{
		constexpr int kTalkCard = 5000;   // (a Talk answer's card: 5000 + its prompt - the row lit while it's said)
		float g_newFreedAt[10]{};          // when each city was seen to wake (this session): its priest just fell
		bool  g_freedSeen[10]{}, g_freedInit = false;
		int   g_talkVisit = -1;            // the altar talked at this visit (-1 none): its goodbye when you leave
		int   g_greeted = -1;              // (Noah: "the Speak to the voice tab talks every time I click the drop down") greeted once a visit
		int   g_nothingSaid = 0;           // the topics whose "nothing more" was said this visit (bits)
		int   TalkPrompt(std::string_view a_id)
		{
			for (int i = 0; i < static_cast<int>(gen::kTalkPrompts.size()); ++i) if (gen::kTalkPrompts[i].id == a_id) return i;
			return -1;
		}
		bool TalkHeard(int a_p)
		{
			auto* g = a_p >= 0 && a_p < 144 ? Glob(gen::kTalkHeard[a_p / 24]) : nullptr;
			return g && ((static_cast<int>(g->value) >> (a_p % 24)) & 1);
		}
		void SetTalkHeard(int a_p)
		{
			if (auto* g = a_p >= 0 && a_p < 144 ? Glob(gen::kTalkHeard[a_p / 24]) : nullptr) g->value = static_cast<float>(static_cast<int>(g->value) | (1 << (a_p % 24)));
		}
		void NoteFreed()   // (every tick) a city seen to wake now: its priest just fell
		{
			const float now = static_cast<float>(RE::GetDurationOfApplicationRunTime()) / 1000.0f;
			for (int t = 0; t < 10; ++t) {
				const bool f = Curse::Freed(t);
				if (g_freedInit && f && !g_freedSeen[t]) g_newFreedAt[t] = now;
				g_freedSeen[t] = f;
			}
			g_freedInit = true;
		}
		bool Gate1(std::string_view c, int a_town)
		{
			const auto num = [&](std::size_t at) { return std::atoi(std::string(c.substr(at)).c_str()); };
			const int  freed = FreedTowns(), cls = Classes::Get();
			const bool here = a_town >= 0 && a_town < 10;
			if (c == "always") return true;
			if (c.starts_with("freed>=")) return freed >= num(7);
			if (c.starts_with("freed<")) return freed < num(6);
			if (c.starts_with("night>=")) return Save::Get(gen::kAV_round) >= num(7);   // (Noah: "it should follow the mod's progression")
			if (c == "here_dark") return here && !Curse::Freed(a_town);
			if (c == "here_free") return here && Curse::Freed(a_town);
			if (c.starts_with("here=")) {
				if (!here) return false;
				std::string t(gen::kTowns[a_town]);
				std::transform(t.begin(), t.end(), t.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); });
				return c.substr(5) == t;
			}
			if (c == "noclass") return cls < 0;
			if (c == "class=any") return cls >= 0;
			if (c.starts_with("class=")) {
				constexpr std::string_view kCls[4] = { "sum", "des", "alc", "ssw" };
				return cls >= 0 && cls < 4 && c.substr(6) == kCls[cls];
			}
			if (c == "night") return Director::NightNow();
			if (c == "day") return !Director::NightNow();
			if (c == "party") return Party::FriendCount() > 0;
			if (c.starts_with("trial=")) {
				if (!here || Curse::Freed(a_town)) return false;
				constexpr std::string_view kTr[4] = { "restless", "whisper", "echo", "seals" };
				const int n = Curse::Altar(a_town).next;
				return n >= 0 && n < 4 && c.substr(6) == kTr[n];
			}
			if (c == "wonder_sleeps" || c == "wonder_open" || c == "wonder_won") {
				if (cls < 0) return false;
				const int st = Classes::WonderState(cls);
				if (c == "wonder_won") return st >= 4;
				if (c == "wonder_open") return st >= 1 && st <= 3;
				return st == 0 && freed < 6;
			}
			if (c == "wonders_all") return Echo::WondersAll();
			if (c == "slain_slen") return Echo::WordSilenced(0);
			if (c == "slain_tiid") return Echo::WordSilenced(1);
			if (c == "slain_vo") return Echo::WordSilenced(2);
			if (c == "echo_awake") return Echo::DragonsSlain() && !Echo::CurseBroken();
			if (c == "echo_slain") return Echo::CurseBroken();
			if (c == "priest_new") {
				const float now = static_cast<float>(RE::GetDurationOfApplicationRunTime()) / 1000.0f;
				return here && g_newFreedAt[a_town] > 0 && now - g_newFreedAt[a_town] < 900.0f;
			}
			logger::warn("talk: unknown gate '{}'", c);
			return false;
		}
		bool Gate(std::string_view g, int a_town)
		{
			while (!g.empty()) {
				const auto amp = g.find('&');
				std::string_view c = g.substr(0, amp);
				const bool neg = c.ends_with("=0") && !c.starts_with("freed");
				if (neg) c.remove_suffix(2);
				if (Gate1(c, a_town) == neg) return false;
				if (amp == std::string_view::npos) break;
				g.remove_prefix(amp + 1);
			}
			return true;
		}
		void SayTalk(int a_p, bool a_all)   // its lines, one after another (a greeting or goodbye: one of them)
		{
			const auto& p = gen::kTalkPrompts[a_p];
			if (!p.count) return;
			const int one = a_all ? -1 : p.first + std::rand() % p.count;
			for (int i = p.first; i < p.first + p.count; ++i) {
				if (one >= 0 && i != one) continue;
				const auto& l = gen::kTalkLines[i];
				g_say.push_back({ gen::Sound{}, std::string(l.text), l.secs, kTalkCard + a_p, std::string(l.key) });
			}
		}
		void Interrupt()
		{
			g_say.clear(), g_sayT = 0, g_sayCard = -1;
			StopTalk();
			Audio::Voice(std::nullopt);
		}
	}

	std::vector<TalkRow> TalkRows(int a_town, int a_topic)
	{
		std::vector<TalkRow> out;
		for (int i = 0; i < static_cast<int>(gen::kTalkPrompts.size()); ++i) {
			const auto& p = gen::kTalkPrompts[i];
			if (p.topic != a_topic || !Gate(p.gate, a_town)) continue;
			out.push_back({ i, std::string(p.ask), TalkHeard(i) });
		}
		return out;
	}
	int TalkNew(int a_town, int a_topic)
	{
		int n = 0;
		for (const auto& r : TalkRows(a_town, a_topic)) n += r.heard ? 0 : 1;
		return n;
	}
	void TalkOpen(int a_town)
	{
		// (Noah, 2026-11-24: "the voice shouldn't speak when clicking the dropdown menu speak with the voice") opening the section
		// says nothing: Felldir speaks when he is asked
		g_greeted = a_town;
	}
	void TalkTopic(int a_town, int a_topic)
	{
		if (TalkRows(a_town, a_topic).empty() || TalkNew(a_town, a_topic) > 0 || ((g_nothingSaid >> a_topic) & 1)) return;
		g_nothingSaid |= 1 << a_topic;   // (once a visit)
		if (const int p = TalkPrompt("nothing_new"); p >= 0) Interrupt(), SayTalk(p, false);   // (all of it heard: "nothing more... not yet")
	}
	void TalkAsk(int a_town, int a_prompt)
	{
		if (a_prompt < 0 || a_prompt >= static_cast<int>(gen::kTalkPrompts.size())) return;
		Interrupt();
		g_talkVisit = a_town;
		SetTalkHeard(a_prompt);
		SayTalk(a_prompt, true);
		logger::info("talk: asked \"{}\"", gen::kTalkPrompts[a_prompt].ask);
	}
	void TalkBye(int a_town)
	{
		g_greeted = -1, g_nothingSaid = 0;
		if (g_talkVisit < 0 || g_talkVisit != a_town) return;
		g_talkVisit = -1;
		// (Noah: "when I exit the echo altar it cuts off whatever it was saying and plays the goodbye line") an answer still being
		// said is let finish, with no goodbye after it; a goodbye only when he had fallen quiet
		if (Speaking() >= 0) return;
		if (const int p = TalkPrompt("bye"); p >= 0) SayTalk(p, false);
	}
	int TalkSaying()
	{
		const int c = Speaking();
		return c >= kTalkCard ? c - kTalkCard : -1;
	}

	// (the test: "spirit cats 2 of 3" went to the log only - banners are off) the step under way, under the compass while you're in
	// its city: the quest, the step's name and its count (never what it wants - that's the whispers' to say)
	bool Strip(std::string& a_title, std::string& a_goal, float& a_frac, float& a_timer)
	{
		const int e = g_live.egg;
		if (e < 0 || Done(e) || g_live.step != Step(e) || Curse::ZoneTown() != CurTown(e)) return false;
		const auto& s = gen::kEggs[e].steps[Step(e)];
		const auto  mech = Mech(e, Step(e));
		const int   prog = Prog(e);
		a_title = std::string(gen::kEggs[e].title);
		std::transform(a_title.begin(), a_title.end(), a_title.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		a_goal = std::string(s.title);
		a_frac = s.n > 0 ? static_cast<float>(prog) / s.n : 0.0f;
		switch (s.type) {
		case gen::kEggTouch:
		case gen::kEggOrder:
		case gen::kEggKills:
		case gen::kEggClasskills:
			if (mech == "elemsplit") {
				const auto& k = g_live.elKills;
				a_goal += fmt::format("   fire {}  frost {}  storm {}", k[0], k[1], k[2]);
			} else {
				a_goal += fmt::format("   {} / {}", prog, s.n);
			}
			if (s.seconds > 0 && g_live.timer >= 0) a_timer = std::max(0.0f, s.seconds - g_live.timer);
			break;
		case gen::kEggAltar:
			a_goal += mech == "dawn" ? "   - before dawn" : "";
			a_frac = 0;
			break;
		case gen::kEggSurvive:
			if (g_live.started) {
				a_goal += fmt::format("   {} remain", g_live.foes.size());
				a_frac = s.n > 0 ? 1.0f - static_cast<float>(g_live.foes.size()) / s.n : 0.0f;
			} else {
				a_frac = 0;
			}
			break;
		case gen::kEggBoss:
			if (mech == "chains") {
				const int left = static_cast<int>(std::count_if(g_live.marks.begin(), g_live.marks.end(), [](const Mark& m) { return !m.taken; }));
				if (left > 0) a_goal += fmt::format("   {} chains hold him", left);
			}
			a_frac = 0;
			if (auto b = g_live.boss.get(); b && !b->IsDead()) {
				auto* av = b->AsActorValueOwner();
				a_frac = 1.0f - av->GetActorValue(RE::ActorValue::kHealth) / std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
			}
			break;
		default: break;
		}
		return true;
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
			SetBit(12 + e, true);   // (begun: its sign skipped)
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
		} else if (a_cmd == "eggsign") {   // eggsign: your class's sign shown at once (its secret not yet begun; night or day)
			g_dev = true, g_devEgg = -1, g_signNow = true;
			for (int e = 0; e < kCount; ++e) if (gen::kEggs[e].cls == Classes::Get()) SetBit(12 + e, false), SetRaw(e, 0), SetTowns(e, 0);
			ClearSign(true);
			g_sign = {};
			logger::info("dev: eggsign - {}'s sign comes now", SignQuest() >= 0 ? gen::kEggs[SignQuest()].title : "no");
		} else if (a_cmd == "eggreset") {
			for (int e = 0; e < kCount; ++e) SetRaw(e, 0), SetTowns(e, 0), SetOpenSaid(e, false), SetBit(12 + e, false);
			Clear();
			g_say.clear(), g_sayT = 0, g_whCd = 0, g_visited = -1, g_devEgg = -1;
			logger::info("dev: eggreset - every wonder quest back to its start");
		}
	}
}
