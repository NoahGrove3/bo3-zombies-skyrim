// The story after the altars (sheet: echo).
//
// Act II - the Reliquary: once six cities are free each path's secret wakes (src/Eggs.cpp). Win it and the Greybeards
// call: the path's wonder waits on its pedestal in the Reliquary of the Tongues, a hall under High Hrothgar behind a
// sealed door in the Greybeards' great hall (both doors move you between the hall and the room). A wonder answers only
// to its own path.
// Act III - the three dragons: with all four wonders won (yours, or pooled with your party), Alduin's three words wake
// as dragons at their places - each comes at night after a siege of the dead, and each slain gives a lasting power.
// Act IV - the Echo of Alduin: with the three dead, it waits on the summit of the Throat of the World. Its fall breaks
// the curse (no more of its nights) and the Reliquary becomes the Hall of the Awakened.
// A freed city's sky also clears at night.
//
// Saved with the game: UN_EchoWords (a bit per words row, and the story's flags); the wonders themselves in the
// classes' co-save (src/Classes.cpp).
#include "BO3Z.h"
#include "CofferSpells.h"

namespace bo3z
{
	namespace
	{
		constexpr RE::FormID kHighHrothgar = 0x00087764;   // Skyrim.esm: the HighHrothgar interior
		constexpr RE::FormID kTamriel = 0x0000003C;
		constexpr RE::FormID kXMarker = 0x0000003B;
		const RE::NiPoint3   kHHFront{ 53664.0f, -36000.0f, 23581.0f };  // High Hrothgar's front doors, outside (Tamriel)
		constexpr int        kWonders = static_cast<int>(gen::kEchoWonders.size());  // the Reliquary's four pedestals, in class order
		constexpr int        kTownCount = static_cast<int>(gen::kTowns.size());

		RE::TESGlobal* Glob(std::uint32_t a_id) { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(a_id, gen::kPlugin); }
		int            Read(std::uint32_t a_id)
		{
			auto* g = Glob(a_id);
			return g ? static_cast<int>(std::lround(g->value)) : 0;
		}
		void Write(std::uint32_t a_id, int a_v)
		{
			if (auto* g = Glob(a_id)) {
				g->value = static_cast<float>(a_v);
			}
		}
		std::string Upper(std::string a_s)
		{
			for (auto& c : a_s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return a_s;
		}
		float Rad(float a_deg) { return a_deg * 0.017453293f; }
		// Skyrim: angle z is clockwise from +y (north); local y = forward
		RE::NiPoint3 Turn(const RE::NiPoint3& a_o, float a_yawDeg, float a_x, float a_y, float a_z)
		{
			const float a = Rad(a_yawDeg);
			return { a_o.x + a_x * std::cos(a) + a_y * std::sin(a), a_o.y - a_x * std::sin(a) + a_y * std::cos(a), a_o.z + a_z };
		}
		RE::NiPoint3 P(const gen::RelSpot& a_s) { return { a_s.x, a_s.y, a_s.z }; }
		RE::TESObjectCELL* HH()
		{
			static RE::TESObjectCELL* c = RE::TESForm::LookupByID<RE::TESObjectCELL>(kHighHrothgar);
			return c;
		}
		bool InTamriel(RE::PlayerCharacter* a_p)
		{
			auto* ws = a_p->GetWorldspace();
			return ws && ws->GetFormID() == kTamriel;
		}
		// a piece we keep in a cell: found again where it should stand, else placed
		RE::TESObjectREFR* Ensure(RE::PlayerCharacter* a_p, RE::TESObjectCELL* a_cell, std::uint32_t a_base, const RE::NiPoint3& a_at, float a_yawDeg, float a_scale = 1.0f)
		{
			auto* base = Forms::Ours(a_base);
			if (!base || !a_cell) {
				return nullptr;
			}
			RE::TESObjectREFR* found = nullptr;
			a_cell->ForEachReference([&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled() && r->GetPosition().GetDistance(a_at) < 60.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (found) {
				return found;
			}
			auto ref = a_p->PlaceObjectAtMe(base, false);
			if (!ref) {
				return nullptr;
			}
			ref->SetPosition(a_at);
			ref->SetAngle({ 0, 0, Rad(a_yawDeg) });
			if (a_scale != 1.0f) {
				ref->SetScale(a_scale);
			}
			return ref.get();
		}
		// ---- where things are --------------------------------------------------------------------------------------
		RE::ObjectRefHandle g_probe;
		std::optional<RE::NiPoint3> Walkable(RE::PlayerCharacter* a_p, const RE::NiPoint3& a_at, float a_maxShift)
		{
			auto probe = g_probe.get();
			if (!probe || probe->IsDeleted() || probe->GetWorldspace() != a_p->GetWorldspace()) {
				auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
				auto  ref = xm ? a_p->PlaceObjectAtMe(xm, false) : nullptr;
				if (!ref) {
					return std::nullopt;
				}
				g_probe = ref->CreateRefHandle();
				probe = ref;
			}
			RE::NiPoint3 at = a_at;
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(at, land)) {
				at.z = std::max(land, at.z - 300.0f);
			}
			probe->SetPosition(at);
			probe->MoveToNearestNavmesh();
			const auto got = probe->GetPosition();
			if (std::hypot(got.x - at.x, got.y - at.y) > a_maxShift) {
				return std::nullopt;
			}
			return got;
		}

		void RunOn(RE::TESObjectREFR* a_ref, const std::string& a_cmd)  // a console command on that reference
		{
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>(); factory && a_ref) {
				if (auto* script = factory->Create()) {
					script->SetCommand(a_cmd);
					script->CompileAndRun(a_ref);
					delete script;
				}
			}
		}
		struct Ally
		{
			RE::ActorHandle h;
			float           t;
			bool            thrall{ false };  // (Alduin's Bane's: counted against its own cap)
		};
		std::vector<Ally> g_allies;
		void TickAllies(float a_dt)
		{
			for (auto& al : g_allies) {
				auto a = al.h.get();
				al.t -= a_dt;
				if (!a || a->IsDeleted()) {
					al.t = 0;
					continue;
				}
				if (al.t <= 0 || a->IsDead()) {
					Gun::FxBlast("dark", a->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f });  // back into the earth
					a->Disable();
					a->SetDelete(true);
					al.t = 0;
				}
			}
			std::erase_if(g_allies, [](const Ally& al) { return al.t <= 0; });
		}


		// ---- the story's state (UN_EchoWords): bit per words row (0-2 the three dragons, 3 the Echo of Alduin this cycle),
		// and its flags
		constexpr int kWordCount = static_cast<int>(gen::kWords.size());
		constexpr int kDragons = 3, kEchoRow = 3;
		constexpr int kWordsTold = 1 << 21, kFlagBone = 1 << 9, kFlagEndless = 1 << 10, kFlagRobes = 1 << 11, kFlagHallOpen = 1 << 17;
		constexpr int kFlagWondersAll = 1 << 18, kFlagEchoTold = 1 << 19, kFlagCleaned = 1 << 20;
		constexpr int kNgShift = 12, kNgMask = 0xF << kNgShift;
		bool Flag(int a_f) { return (Read(gen::kEchoGlobWords) & a_f) != 0; }
		void SetFlag(int a_f, bool a_on) { Write(gen::kEchoGlobWords, a_on ? (Read(gen::kEchoGlobWords) | a_f) : (Read(gen::kEchoGlobWords) & ~a_f)); }
		bool Silenced(int a_w) { return (Read(gen::kEchoGlobWords) >> a_w) & 1; }
		bool DragonsDead() { return Silenced(0) && Silenced(1) && Silenced(2); }
		bool Broken() { return Silenced(kEchoRow); }  // (the Echo of Alduin slain this cycle)
		int  Cycle() { return (Read(gen::kEchoGlobWords) & kNgMask) >> kNgShift; }
		int  FreedCount()
		{
			int n = 0;
			for (int t = 0; t < kTownCount; ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		// Act III: the four class wonders won between you and your party (latched once true)
		bool AllWonders()
		{
			if (Flag(kFlagWondersAll)) return true;
			if (((Classes::WonderMask() | Party::PartyWonders()) & 0xF) == 0xF) {
				SetFlag(kFlagWondersAll, true);
				logger::info("echo: all four wonders won - the three dragons wake");
				return true;
			}
			return false;
		}
		// a row awake: a dragon once the four wonders are won; the Echo once the three dragons are dead and every altar is
		// free (in a cycle begun again at the Seat, freed again)
		bool Awake(int a_w)
		{
			if (a_w < 0 || a_w >= kWordCount || Silenced(a_w)) return false;
			if (a_w < kDragons) return AllWonders();
			return DragonsDead() && FreedCount() >= kTownCount;  // (the curse's nights end with it: no city may be left under the curse)
		}
		RE::TESObjectWEAP* Bane() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kFangWeap, gen::kPlugin); }
		RE::TESObjectARMO* RegaliaBody() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectARMO>(0x02B0F7, "Dragonborn.esm"); }

		// ---- the Reliquary (Act II): a pedestal for each class's wonder, its gems and beam lit once it is won ------------
		RE::ObjectRefHandle g_arrive, g_leave;  // the markers the doors move you to
		RE::NiPoint3 Room(float a_x, float a_y, float a_z) { return P(gen::kRelRoomAt) + RE::NiPoint3{ a_x, a_y, a_z }; }
		bool InRoom(RE::PlayerCharacter* a_p)
		{
			return a_p->GetParentCell() == HH() && a_p->GetPosition().z < gen::kRelRoomAt.z + 1200.0f;
		}
		RE::NiPoint3 SocketAt(int a_w, int a_k)
		{
			const auto& s = gen::kRelPedestals[a_w];
			const float a = 1.5708f + 6.2832f * a_k / 3.0f;
			return Turn(Room(s.x, s.y, s.z), s.yaw, 28.0f * std::cos(a), 28.0f * std::sin(a), 106.0f);
		}
		// (the UI audit: "the Destruction") the path as a person: the Summoner, the Battle-mage, the Ranger, the Spellsword
		std::string PathName(int a_c) { static constexpr const char* k[4] = { "Summoner", "Battle-mage", "Ranger", "Spellsword" }; return a_c >= 0 && a_c < 4 ? k[a_c] : "?"; }
		bool Won(int a_c) { return Classes::WonderState(a_c) >= 4 || ((Party::PartyWonders() >> a_c) & 1); }
		bool Waiting(int a_c) { return Classes::WonderState(a_c) == 5; }
		bool AnyCalled()  // the Greybeards have called you at least once: the sealed door opens
		{
			for (int c = 0; c < kWonders; ++c) if (Classes::WonderState(c) >= 4) return true;
			return Flag(kFlagWondersAll) || Flag(kFlagHallOpen);
		}
		struct HallSpot
		{
			std::uint32_t id;
			float         x, y, yaw;
		};
		const std::array<HallSpot, 5>& HallSpots()
		{
			static const std::array<HallSpot, 5> s{ { { gen::kHallFang, 0.0f, 430.0f, 180.0f }, { gen::kHallRobes, -540.0f, -380.0f, 90.0f },
				{ gen::kHallSkull, 540.0f, -380.0f, 270.0f }, { gen::kHallEndless, -230.0f, -440.0f, 0.0f }, { gen::kHallThrone, 230.0f, -440.0f, 0.0f } } };
			return s;
		}
		float g_throneAsk = 0;  // (the Seat asks twice: a first touch warns, a second within a few seconds begins again)
		void Unplace(RE::TESObjectCELL* a_cell, std::uint32_t a_base, const RE::NiPoint3& a_at)
		{
			auto* base = Forms::Ours(a_base);
			if (!base || !a_cell) return;
			a_cell->ForEachReference([&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && r->GetPosition().GetDistance(a_at) < 60.0f) {
					r->Disable();
					r->SetDelete(true);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void BuildReliquary(RE::PlayerCharacter* a_p)
		{
			auto* cell = HH();
			if (a_p->GetParentCell() != cell) {
				return;
			}
			Ensure(a_p, cell, gen::kRelDoorIn, P(gen::kRelDoor), gen::kRelDoor.yaw);
			Ensure(a_p, cell, gen::kRelRoom, Room(0, 0, 0), 0);
			Ensure(a_p, cell, gen::kRelDoorOut, Room(gen::kRelExit.x, gen::kRelExit.y + 52.0f, gen::kRelExit.z), gen::kRelExit.yaw);
			Unplace(cell, gen::kRelStone, Room(0, 150, 0));  // (the old Echo Stone: gone with the parts)
			for (const auto& [x, y] : std::array<std::pair<float, float>, 4>{ { { -540.0f, 50.0f }, { 540.0f, 50.0f }, { -200.0f, 520.0f }, { 200.0f, 520.0f } } }) {
				Ensure(a_p, cell, gen::kRelBrazier, Room(x, y, 0), 0);
			}
			for (const float x : { -300.0f, 300.0f }) {
				Ensure(a_p, cell, gen::kRelBanner, Room(x, 548, 0), 180.0f);
				Ensure(a_p, cell, gen::kRelBlue, Room(x * 1.4f, 60, 210), 0);
			}
			Ensure(a_p, cell, gen::kRelWarm, Room(0, 0, 520), 0);
			for (int w = 0; w < kWonders; ++w) {
				const auto& s = gen::kRelPedestals[w];
				Ensure(a_p, cell, gen::kEchoWonders[w].pedestal, Room(s.x, s.y, s.z), s.yaw);
				// a wonder won: its three gems burn in the pedestal and its beam rises; one still waiting to be taken up
				// shines brighter, a light over it
				const bool won = Won(w) || Waiting(w);
				for (int k = 0; k < 3; ++k) {
					if (won) Ensure(a_p, cell, gen::kEchoWonders[w].gem, SocketAt(w, k), 0);
					else Unplace(cell, gen::kEchoWonders[w].gem, SocketAt(w, k));
				}
				if (won) Ensure(a_p, cell, gen::kRelBeam, Room(s.x, s.y, s.z + 100.0f), 0, Waiting(w) ? 0.34f : 0.22f);
				if (Waiting(w)) Ensure(a_p, cell, gen::kRelRelicLight, Room(s.x, s.y, s.z + 220.0f), 0);
				else Unplace(cell, gen::kRelRelicLight, Room(s.x, s.y, s.z + 220.0f));
			}
			if (Flag(kFlagHallOpen)) {   // the Hall of the Awakened
				for (const auto& h : HallSpots()) {
					Ensure(a_p, cell, h.id, Room(h.x, h.y, 0), h.yaw);
					Ensure(a_p, cell, gen::kRelBeam, Room(h.x, h.y, 100.0f), 0, 0.18f);
				}
				// Alduin's Bane stands on its pedestal, blade up, while it isn't in your pack
				auto* bane = Bane();
				const auto baneAt = Room(0.0f, 430.0f, 175.0f);
				if (bane && a_p->GetItemCount(bane) <= 0) {
					if (auto* r = Ensure(a_p, cell, gen::kHallFangShow, baneAt, 180.0f)) {
						r->SetAngle({ -1.5708f, 0, Rad(180.0f) });
					}
				} else {
					Unplace(cell, gen::kHallFangShow, baneAt);
				}
			}
			auto marker = [&](RE::ObjectRefHandle& a_h, const RE::NiPoint3& a_at, float a_yaw) {
				auto m = a_h.get();
				if (!m || m->IsDeleted() || m->GetParentCell() != cell) {
					auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
					auto  ref = xm ? a_p->PlaceObjectAtMe(xm, false) : nullptr;
					if (!ref) {
						return;
					}
					a_h = ref->CreateRefHandle();
					m = ref;
				}
				m->SetPosition(a_at);
				m->SetAngle({ 0, 0, Rad(a_yaw) });
			};
			marker(g_arrive, Room(gen::kRelArrive.x, gen::kRelArrive.y, gen::kRelArrive.z + 4.0f), gen::kRelArrive.yaw);
			marker(g_leave, P(gen::kRelLeave) + RE::NiPoint3{ 0, 0, 4.0f }, gen::kRelLeave.yaw);
		}
		void Go(RE::PlayerCharacter* a_p, RE::ObjectRefHandle& a_to)
		{
			if (auto m = a_to.get()) {
				Forms::Play(gen::Sound::ward_lit);
				a_p->MoveTo(m.get());
			}
		}
		// a pedestal: its wonder taken up (it answers only to its own path), or how far that path's secret has come
		void Pedestal(RE::PlayerCharacter* a_p, int a_c)
		{
			const std::string name(Classes::WonderName(a_c));
			const int         st = Classes::WonderState(a_c);
			const int         mine = Classes::Get();
			if (st == 4 && mine != a_c) {
				Hud::Banner(name + " is yours - it answers only to the " + PathName(a_c) + ".", 4.0f);
				return;
			}
			if (st == 5 || st == 4) {
				if (mine != a_c) {
					Hud::Banner(name + " answers only to the " + PathName(a_c) + ". Change your path at an Echo Altar to take it up.", 5.0f);
					return;
				}
				if (Classes::TakeWonder(a_c)) {
					Gun::FxBlast("light", Room(gen::kRelPedestals[a_c].x, gen::kRelPedestals[a_c].y, gen::kRelPedestals[a_c].z + 140.0f));
					BuildReliquary(a_p);
					logger::info("echo: {} taken up from its pedestal", name);
				} else {
					Hud::Banner(name + " is yours. Its pedestal keeps its place in the Reliquary.", 3.5f);
				}
				return;
			}
			if ((Party::PartyWonders() >> a_c) & 1) {
				// (the story audit: after the curse breaks there are no nights to win it in - the Brazier brings them back)
				Hud::Banner(name + " was won by your party. Win it on your own path to carry it." +
				                (Broken() && !Flag(kFlagEndless) ? std::string(" Light the Brazier of the Endless Night in the Hall to bring the nights back.") : std::string()),
					6.0f);
				return;
			}
			const auto line = Classes::WonderLine();
			Hud::Banner(name + " - the " + PathName(a_c) + "'s wonder. " +
			                (mine == a_c && !line.empty() ? line : std::string("It waits for that path's secret to be won.")),
				6.0f);
		}

		// ---- a freed city's clear night ------------------------------------------------------------------------------
		// ---- a freed city's clear night ------------------------------------------------------------------------------
		bool g_clear = false;
		void TickSky()
		{
			static constexpr std::array<RE::FormID, 10> kClear{ 0x0000081A, 0x0010A240, 0x0010A234, 0x0010A244, 0x0010A237, 0x0010A23B, 0x0000081A, 0x0010A230, 0x0010A244, 0x0010A244 };
			const int  z = Curse::ZoneTown();
			const bool want = z >= 0 && z < kTownCount && Curse::Freed(z) && Director::NightNow() && Director::EventNow() < 0 && !Coop::Client();
			auto*      sky = RE::Sky::GetSingleton();
			if (!sky || sky->mode.get() != RE::Sky::Mode::kFull) {
				return;
			}
			if (want) {
				auto* w = RE::TESForm::LookupByID<RE::TESWeather>(kClear[std::min(z, 9)]);
				if (!w) {
					w = RE::TESForm::LookupByID<RE::TESWeather>(0x0000081A);
				}
				if (w && sky->currentWeather != w) {
					sky->ForceWeather(w, false);
					if (!g_clear) {
						logger::info("echo: {} is freed - its night sky clears", gen::kTowns[z]);
					}
				}
				g_clear = true;
			} else {
				g_clear = false;
			}
		}

		// ---- Act III and IV: the three dragons and the Echo of Alduin (sheet: echo.words) ---------------------------------
		// With all four class wonders won, the three dragons wake together, each at its own place. Come near one at night and
		// its siege begins (its waves of dead round the place); when the last wave falls the dragon comes. Kill it and its
		// word is silenced for good - its power yours (Echo::WordSilenced). With all three dead the Echo of Alduin waits on
		// the summit of the Throat of the World the same way; its fall breaks the curse.
		RE::NiPoint3 WordAt(int a_w) { return { gen::kWords[a_w].x, gen::kWords[a_w].y, gen::kWords[a_w].z }; }
		int Night() { return Save::Get(gen::kAV_round); }
		struct Siege
		{
			int                          word{ -1 };
			int                          stage{ 0 };   // 0 asleep, 1 its waves, 2 its dragon flies
			int                          wave{ 0 };
			std::vector<RE::ActorHandle> dead;
			float                        retry{ 0 };
			int                          night{ -1 };
			std::array<bool, 4>          told{};
			int                          flying{ -1 };  // the row whose dragon is up (credited when it dies, wherever the siege is)
		} g_siege;
		void EndSiege(const char* a_why)
		{
			if (g_siege.stage != 0) {
				logger::info("words: the siege of {} ends ({})", g_siege.word >= 0 ? gen::kWords[g_siege.word].word : "?", a_why);
			}
			g_siege.stage = 0, g_siege.wave = 0, g_siege.dead.clear();
		}
		void RaiseWave(RE::PlayerCharacter* a_p, int a_w)
		{
			const auto& W = gen::kWords[a_w];
			if (g_siege.wave < 0 || g_siege.wave >= W.nWaves) return;
			const auto  at = WordAt(a_w);
			const int   n = W.waves[g_siege.wave], el = W.elites[g_siege.wave];
			for (int i = 0; i < n + el; ++i) {
				const float a = 6.2832f * i / std::max(1, n + el) + 0.3f * g_siege.wave, r = 600.0f + 300.0f * (i % 3);
				const auto  spot = Walkable(a_p, at + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 600.0f);
				if (auto h = Director::RaiseTrial(spot ? *spot : at, i >= n); h) {
					g_siege.dead.push_back(h);
				}
			}
			Forms::Play(gen::Sound::champ_arrive);
			Hud::Banner(fmt::format("{} - WAVE {} OF {}", W.word, g_siege.wave + 1, W.nWaves), 3.5f);
			logger::info("words: {} wave {} risen ({} dead)", W.word, g_siege.wave + 1, g_siege.dead.size());
		}
		// the row whose place you're at (the siege under way keeps its own)
		int Here(RE::PlayerCharacter* a_p)
		{
			if (g_siege.stage != 0 && g_siege.word >= 0) return g_siege.word;
			int   best = -1;
			float bd = gen::kWordWake * 2.0f;
			for (int w = 0; w < kWordCount; ++w) {
				if (!Awake(w)) continue;
				const auto  at = WordAt(w);
				const float d = std::hypot(a_p->GetPosition().x - at.x, a_p->GetPosition().y - at.y);
				if (d < bd) bd = d, best = w;
			}
			return best;
		}
		void TellArc()
		{
			if (AllWonders() && !Flag(kWordsTold)) {
				SetFlag(kWordsTold, true);
				Forms::Play(gen::Sound::quest_start);
				Hud::Lore("THE THREE DRAGONS",
					"All four wonders are won, and Alduin's three words answer them. Each was given to a dragon to keep: SLEN to Slenvahrin at the cult's old shrine in "
					"the Reach, TIID to Tiidrah in the courtyard of Labyrinthian, VO to Vonahlok on Mount Anthor. Go to them at night - the dead will gather first. "
					"Slay all three, wake every Echo Altar, and only Alduin's echo is left.",
					18.0f);
				logger::info("words: Act III begins - the three dragons wake");
			}
			if (Awake(kEchoRow) && !Flag(kFlagEchoTold)) {
				SetFlag(kFlagEchoTold, true);
				Forms::Play(gen::Sound::quest_start);
				Hud::Lore("THE ECHO OF ALDUIN", std::string(gen::kWords[kEchoRow].wake), 16.0f);
				logger::info("words: Act IV - the Echo of Alduin waits on the Throat of the World");
			}
		}
		void TickWords(RE::PlayerCharacter* a_p, float a_dt)
		{
			TellArc();
			if (!InTamriel(a_p)) {
				return;
			}
			const int w = Here(a_p);
			if (w < 0) {
				if (g_siege.stage != 0) EndSiege("no word here");
				return;
			}
			if (g_siege.word != w) {
				EndSiege("another place");
				g_siege.word = w;
			}
			const auto& W = gen::kWords[w];
			const auto  at = WordAt(w);
			const float d = std::hypot(a_p->GetPosition().x - at.x, a_p->GetPosition().y - at.y);
			const bool  night = Director::Fighting();
			if (!night) {
				if (g_siege.stage != 0) {
					EndSiege("dawn");
				}
				if (d < gen::kWordWake && !g_siege.told[w]) {
					g_siege.told[w] = true;
					Hud::Banner(w == kEchoRow ? std::string("The summit is still by day. The Echo of Alduin answers only at night.")
					                          : std::string(W.bearerName) + " sleeps by day. It comes only at night.",
						5.0f);
				}
				return;
			}
			g_siege.told[w] = false;
			if (g_siege.stage == 2 && g_siege.flying >= 0 && !Director::ChampionAlive()) {
				EndSiege("its dragon is gone");  // (fallen - it is credited by its row - or vanished: the siege can begin again)
			}
			if (g_siege.stage == 1 && d > gen::kWordLeash) {  // (a dragon in the air follows you: only the waves are left behind)
				EndSiege("you left");
				Hud::Banner(std::string(W.word) + " falls quiet - you left its place", 4.0f);
				return;
			}
			std::erase_if(g_siege.dead, [](const RE::ActorHandle& h) { auto a = h.get(); return !a || a->IsDead() || a->IsDeleted() || a->IsDisabled(); });
			if (Coop::Client()) {
				return;  // (the host's game raises the siege; Skyrim Together shows it here)
			}
			if (g_siege.stage == 0 && d < gen::kWordWake) {
				if ((g_siege.retry -= a_dt) > 0) {
					return;
				}
				g_siege.retry = 3.0f;
				g_siege.stage = 1, g_siege.wave = 0, g_siege.night = Night();
				Forms::Play(gen::Sound::curse_lifted);
				Hud::Lore(w == kEchoRow ? std::string("THE ECHO OF ALDUIN") : std::string(W.word) + " - " + Upper(std::string(W.meaning)), std::string(W.wake), 10.0f);
				RaiseWave(a_p, w);
				return;
			}
			if (g_siege.stage == 1 && g_siege.dead.empty()) {
				// (the audit: a crowded field raised nobody, and the next tick counted the wave as fallen) a wave that rose no
				// one is raised again in a moment; the count never runs past the last wave
				if (g_siege.wave + 1 < W.nWaves) {
					++g_siege.wave;
					RaiseWave(a_p, w);
					if (g_siege.dead.empty()) --g_siege.wave, g_siege.retry = 2.0f;
					return;
				}
				// the waves are down: the dragon comes
				const auto spot = Walkable(a_p, at, 700.0f);
				if (Director::RaiseChampion(-1, W.bearerRow, W.bearerName, W.health, (spot ? *spot : at) + RE::NiPoint3{ 0, 0, 60.0f })  /* (out of the Curse's portal on the ground, as Grimmvar comes: then it takes wing) */) {
					g_siege.stage = 2, g_siege.flying = w;
					if (Settings::ScreenShake()) RE::ShakeCamera(0.6f, a_p->GetPosition(), 1.5f);
					logger::info("words: {} - {} comes", W.word, W.bearerName);
				} else {
					g_siege.retry = 2.0f;
				}
				return;
			}
		}
		void SilenceWord(int a_w)
		{
			if (a_w < 0 || a_w >= kWordCount || Silenced(a_w)) {
				return;
			}
			Write(gen::kEchoGlobWords, Read(gen::kEchoGlobWords) | (1 << a_w));
			Net::TellWord(a_w);
			const auto& W = gen::kWords[a_w];
			Forms::Play(gen::Sound::shout_learned);
			Forms::Play(gen::Sound::curse_lifted);
			Points::Add(a_w == kEchoRow ? 20000 : 7500);
			EndSiege("slain");
			if (a_w == kEchoRow) {
				SetFlag(kFlagHallOpen, true);
				Hud::Lore("THE CURSE OF ALDUIN IS BROKEN",
					std::string(W.silenced) + "  Beneath High Hrothgar the Reliquary wakes as the Hall of the Awakened: Alduin's Bane, the First Dragonborn's Regalia, "
					"the Bone-Wyrm's Skull for your dragon, the Brazier of the Endless Night - and the Seat of the Awakened, for when you would face it all again.",
					22.0f);
				Hud::Banner("THE CURSE OF ALDUIN IS BROKEN", 8.0f);
				Director::SkipToDawn("THE ECHO OF ALDUIN FALLS - THE CURSE IS BROKEN");
				logger::info("words: the Echo of Alduin is slain - the curse is broken (cycle {})", Cycle() + 1);
				return;
			}
			int left = 0;
			for (int i = 0; i < kDragons; ++i) left += Silenced(i) ? 0 : 1;
			Hud::Lore(std::string(W.word) + " IS SILENCED",
				std::string(W.silenced) + "  " + std::string(W.power) +
					(left > 0 ? fmt::format("  {} {} still {}.", left, left == 1 ? "dragon" : "dragons", left == 1 ? "flies" : "fly")
					          : FreedCount() >= kTownCount ? std::string("  The three dragons are dead, and your wonders wake to their Legendary form. Only Alduin's echo is left - on the summit of the Throat of the World.")
					                                       : fmt::format("  The three dragons are dead, and your wonders wake to their Legendary form. Only Alduin's echo is left - and it waits until every Echo Altar is awake ({} of {} freed).", FreedCount(), kTownCount)),
				16.0f);
			Hud::Banner(std::string(W.word) + " IS SILENCED - " + std::string(W.power), 8.0f);
			logger::info("words: {} silenced ({} dragons left)", W.word, left);
		}

		float g_slow = 0;
		// (the story's overhaul) once: the retired staffs and the Broken Words pages leave your pack
		void CleanUp(RE::PlayerCharacter* a_p)
		{
			if (Flag(kFlagCleaned)) return;
			SetFlag(kFlagCleaned, true);
			int n = 0;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (Gun::IsStaffWonder(i) && Gun::Owns(i)) {
					Gun::TakeAway(i);
					if (auto* wp = Forms::Gun(i); wp && a_p->GetItemCount(wp) > 0) a_p->RemoveItem(wp, a_p->GetItemCount(wp), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					++n;
				}
			}
			for (int t = 0; t < kTownCount; ++t) {
				if (auto* book = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectBOOK>(gen::kEchoPages[t], gen::kPlugin); book && a_p->GetItemCount(book) > 0) {
					a_p->RemoveItem(book, a_p->GetItemCount(book), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					++n;
				}
			}
			Write(gen::kRitualStand, 0);
			if (n) logger::info("echo: {} retired staffs and pages taken back", n);
		}
	}

	namespace Echo
	{
		void Reset()
		{
			g_probe = {}, g_arrive = {}, g_leave = {};
			for (auto& al : g_allies) {
				if (auto a = al.h.get()) {
					a->Disable();
					a->SetDelete(true);
				}
			}
			g_allies.clear();
			g_clear = false, g_slow = 0;
			g_siege = Siege{};
			Party::ResetWonders();  // (a friend's wonders count only in the game they were heard in)
		}

		bool WordSilenced(int a_w) { return Glob(gen::kEchoGlobWords) && a_w >= 0 && a_w < kWordCount && Silenced(a_w); }
		bool DragonsSlain() { return Glob(gen::kEchoGlobWords) && DragonsDead(); }
		bool SiegeActive() { return g_siege.stage != 0; }
		bool EchoAwake() { return Glob(gen::kEchoGlobWords) && Awake(kEchoRow); }
		int  DragonMask() { return Glob(gen::kEchoGlobWords) ? (Read(gen::kEchoGlobWords) & 0x7) : 0; }
		void HeardWords(int a_mask, bool a_live)
		{
			if (!Glob(gen::kEchoGlobWords)) return;
			for (int w = 0; w < kDragons; ++w) {
				if ((a_mask >> w) & 1 && !Silenced(w)) {
					logger::info("words: {} silenced in the host's game - silenced here too", gen::kWords[w].word);
					SilenceWord(w);
				}
			}
			if (a_live && ((a_mask >> kEchoRow) & 1) && !Silenced(kEchoRow)) {
				logger::info("words: the Echo of Alduin fell in the host's game - the curse is broken here too");
				SilenceWord(kEchoRow);
			}
		}
		bool WondersAll() { return Glob(gen::kEchoGlobWords) && AllWonders(); }
		bool CurseBroken() { return Glob(gen::kEchoGlobWords) && Broken(); }
		bool HallOpen() { return Glob(gen::kEchoGlobWords) && Flag(kFlagHallOpen); }
		bool BoneDragon() { return HallOpen() && Flag(kFlagBone); }
		bool EndlessNight() { return HallOpen() && Flag(kFlagEndless); }
		int  NewGamePlus() { return Glob(gen::kEchoGlobWords) ? Cycle() : 0; }
		bool WearsRobes()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* robes = RegaliaBody();
			auto* worn = p ? p->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody) : nullptr;
			return robes && worn == robes && HallOpen();
		}
		float SetDamageMult() { return WearsRobes() ? 1.25f : 1.0f; }
		void OnBearerSlain()
		{
			const int w = g_siege.flying >= 0 ? g_siege.flying : g_siege.stage == 2 ? g_siege.word : -1;
			g_siege.flying = -1;
			SilenceWord(w);
		}
		bool IsAlly(RE::Actor* a_actor)
		{
			return a_actor && std::any_of(g_allies.begin(), g_allies.end(), [&](const Ally& al) { return al.h.get().get() == a_actor; });
		}
		void Allies(int a_kind, int a_count, float a_seconds, const RE::NiPoint3& a_at)
		{
			static constexpr std::array<std::pair<std::string_view, RE::FormID>, 3> kBase{ { { "Dawnguard.esm", 0x0045B9 },   // DLC1SoulCairnBonemanSummon: a bone warrior
				{ "Skyrim.esm", 0x0F608C },                                                                                    // PlayerWolfSpirit: a spectral wolf
				{ "Dragonborn.esm", 0x02095D } } };                                                                            // DLC2ExpSpiderShockCloakingFriend: the Void Staff's spider                                                                              // PlayerWolfSpirit: a spectral wolf
			auto* p = RE::PlayerCharacter::GetSingleton();
			const auto& [plugin, id] = kBase[std::clamp(a_kind, 0, 2)];
			auto* npc = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESNPC>(id, plugin);
			if (!p || !npc) {
				logger::warn("echo: no ally base for kind {}", a_kind);
				return;
			}
			static auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
			a_count = std::min(a_count, 6);
			while (!g_allies.empty() && static_cast<int>(g_allies.size()) + a_count > 6) {  // (six at most: the oldest go first)
				if (auto a = g_allies.front().h.get()) {
					a->Disable();
					a->SetDelete(true);
				}
				g_allies.erase(g_allies.begin());
			}
			for (int i = 0; i < a_count; ++i) {
				const float a = 6.2832f * i / a_count + 0.4f;
				const RE::NiPoint3 want = a_at + RE::NiPoint3{ std::cos(a) * 170.0f, std::sin(a) * 170.0f, 0 };
				const auto spot = Walkable(p, want, 400.0f);
				auto ref = p->PlaceObjectAtMe(npc, false);
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor) {
					continue;
				}
				actor->SetPosition(spot ? *spot : a_at, true);
				if (playerFaction) {
					actor->AddToFaction(playerFaction, 0);
				}
				RunOn(actor, "SetPlayerTeammate 1");
				RunOn(actor, "SetAV Aggression 1");
				Gun::FxBlast(a_kind == 0 ? "dark" : "light", actor->GetPosition() + RE::NiPoint3{ 0, 0, 40.0f });
				g_allies.push_back({ actor->GetHandle(), a_seconds });
			}
			logger::info("echo: {} {} answer the call", a_count, a_kind == 0 ? "bone warriors" : "spirit-wolves");
		}

		std::vector<RE::ActorHandle> Summon(std::string_view a_plugin, RE::FormID a_id, int a_count, const RE::NiPoint3& a_at)
		{
			std::vector<RE::ActorHandle> out;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* npc = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESNPC>(a_id, a_plugin);
			if (!npc) {  // (its mod missing: a spirit-wolf comes instead)
				npc = RE::TESForm::LookupByID<RE::TESNPC>(0x0F608C);
			}
			if (!p || !npc) {
				return out;
			}
			static auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
			for (int i = 0; i < a_count; ++i) {
				const float a = 6.2832f * i / a_count + 0.8f;
				const auto  spot = Walkable(p, a_at + RE::NiPoint3{ std::cos(a) * 160.0f, std::sin(a) * 160.0f, 0 }, 400.0f);
				auto        ref = p->PlaceObjectAtMe(npc, false);
				auto*       actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor) {
					continue;
				}
				actor->SetPosition(spot ? *spot : a_at, true);
				if (playerFaction) {
					actor->AddToFaction(playerFaction, 0);
				}
				RunOn(actor, "SetPlayerTeammate 1");
				RunOn(actor, "SetAV Aggression 1");
				Gun::FxBlast("light", actor->GetPosition() + RE::NiPoint3{ 0, 0, 40.0f });
				g_allies.push_back({ actor->GetHandle(), 1e9f });  // (an ally of the night: the dead and the truce leave it be)
				out.push_back(actor->GetHandle());
			}
			logger::info("eggs: {} summoned ({:06X})", out.size(), a_id);
			return out;
		}

		void Thralls(RE::TESNPC* a_npc, int a_count, float a_seconds, const RE::NiPoint3& a_at, int a_max)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !a_npc) return;
			static auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
			int alive = 0;
			for (auto& al : g_allies) {
				if (auto a = al.h.get(); a && !a->IsDead() && al.thrall) ++alive;
			}
			for (int i = 0; i < a_count && alive < a_max; ++i, ++alive) {
				const auto spot = Walkable(p, a_at, 400.0f);
				auto  ref = p->PlaceObjectAtMe(a_npc, false);
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor) continue;
				actor->SetPosition(spot ? *spot : a_at, true);
				if (playerFaction) actor->AddToFaction(playerFaction, 0);
				RunOn(actor, "SetPlayerTeammate 1");
				RunOn(actor, "SetAV Aggression 1");
				Gun::FxBlast("dark", actor->GetPosition() + RE::NiPoint3{ 0, 0, 40.0f });
				g_allies.push_back({ actor->GetHandle(), a_seconds, true });
			}
		}

		bool StoneWaits(int) { return false; }  // (the Broken Words pages are retired with the parts)
		bool AwardNextPart() { return false; }   // (the staffs' parts are retired: a mini-boss pays in the altar's own coin)

		void Tick(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !p->Is3DLoaded() || !Glob(gen::kEchoGlobWords)) {
				return;
			}
			if ((g_slow -= a_delta) > 0) {
				return;
			}
			g_slow = 0.5f;
			g_throneAsk = std::max(0.0f, g_throneAsk - 0.5f);
			TickAllies(0.5f);
			CleanUp(p);
			Party::SetWonder(Classes::WonderMask());  // (the party pools its wonders)
			if (p->GetParentCell() == HH()) {
				BuildReliquary(p);
			}
			TickSky();
			TickWords(p, 0.5f);
		}

		void OnUndeadDeath(RE::Actor*) {}  // (the barrows' kill sites are retired with the parts)

		bool OnActivate(RE::TESObjectREFR* a_ref)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!p || !base) {
				return false;
			}
			if (base == Forms::Ours(gen::kRelDoorIn)) {
				if (!AnyCalled()) {
					Hud::Banner("The door is sealed with the Tongues' sigil. It opens when the Greybeards call you.", 5.0f);
					return true;
				}
				BuildReliquary(p);
				Go(p, g_arrive);
				Hud::Banner(Flag(kFlagHallOpen) ? "THE HALL OF THE AWAKENED" : "THE RELIQUARY OF THE TONGUES", 3.5f);
				return true;
			}
			if (base == Forms::Ours(gen::kRelDoorOut)) {
				BuildReliquary(p);
				Go(p, g_leave);
				return true;
			}
			for (int w = 0; w < kWonders; ++w) {
				if (base == Forms::Ours(gen::kEchoWonders[w].pedestal)) {
					Pedestal(p, w);
					return true;
				}
			}
			if (!Flag(kFlagHallOpen)) {
				return false;
			}
			if (base == Forms::Ours(gen::kHallFang)) {
				if (auto* bane = Bane()) {
					if (p->GetItemCount(bane) > 0) {
						p->RemoveItem(bane, p->GetItemCount(bane), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						Hud::Banner("ALDUIN'S BANE RESTS ON ITS STAND", 3.5f);
					} else {
						p->AddObjectToContainer(bane, nullptr, 1, nullptr);
						Forms::Play(gen::Sound::shout_learned);
						Hud::Lore("ALDUIN'S BANE",
							"A blade of dragon bone, cut from the Echo of Alduin. Its magic answers your path: as a Summoner, what it kills rises as a Draugr Death Overlord to fight "
							"for you; as a Battle-mage, every blow looses a master Destruction spell; as a Spellsword, every blow carries a master Grasp; as a Ranger, every swing "
							"sends a wind slash down the line that hurls the dead far away. Its kills fill it with souls - full, Alduin's shade rains meteors round you. While you "
							"hold it, kills heal you and your shouts return twice as fast. Return it to its stand whenever you like.",
							16.0f);
					}
					BuildReliquary(p);
				}
				return true;
			}
			if (base == Forms::Ours(gen::kHallRobes)) {
				auto* dh = RE::TESDataHandler::GetSingleton();
				if (!dh->LookupModByName("Dragonborn.esm")) {
					Hud::Banner("The Regalia needs Dragonborn (the Solstheim add-on).", 4.0f);
					return true;
				}
				int given = 0;
				for (const RE::FormID id : { 0x02B0F7u, 0x02B0F9u, 0x03D2AFu, 0x029A62u }) {  // the First Dragonborn's robes, gloves, boots and mask
					if (auto* a = dh->LookupForm<RE::TESObjectARMO>(id, "Dragonborn.esm"); a && p->GetItemCount(a) <= 0) {
						p->AddObjectToContainer(a, nullptr, 1, nullptr);
						++given;
					}
				}
				SetFlag(kFlagRobes, true);
				if (given) {
					Forms::Play(gen::Sound::shout_learned);
					Hud::Lore("THE FIRST DRAGONBORN'S REGALIA",
						"Robes, gloves, boots and mask of the first who devoured a dragon's soul - the strongest gear in Skyrim, for every path. While you wear the robes, everything "
						"you wield hits 25% harder, every blow on you is 25% softer, and your magic costs a quarter less and returns faster.",
						12.0f);
				} else {
					Hud::Banner("The Regalia is already yours.", 3.0f);
				}
				return true;
			}
			if (base == Forms::Ours(gen::kHallSkull)) {
				SetFlag(kFlagBone, !Flag(kFlagBone));
				Forms::Play(gen::Sound::ward_lit);
				Hud::Banner(Flag(kFlagBone) ? "YOUR DRAGON WEARS ALDUIN'S BONES" : "YOUR DRAGON WEARS ITS OWN SCALES AGAIN", 4.0f);
				return true;
			}
			if (base == Forms::Ours(gen::kHallEndless)) {
				SetFlag(kFlagEndless, !Flag(kFlagEndless));
				Forms::Play(Flag(kFlagEndless) ? gen::Sound::curse_lifted : gen::Sound::ward_lit);
				Hud::Banner(Flag(kFlagEndless) ? "THE ENDLESS NIGHT BURNS - the curse returns, and each night's last wave brings the next. A freed city is still safe."
				                               : (Broken() ? "THE BRAZIER IS COLD - the nights are only nights again" : "THE BRAZIER IS COLD - dawn comes when the waves are cleared"),
					7.0f);
				return true;
			}
			if (base == Forms::Ours(gen::kHallThrone)) {
				if (!Broken()) {
					Hud::Banner("The Seat of the Awakened is cold. It wakes when the Echo of Alduin falls again.", 5.0f);
					return true;
				}
				if (g_throneAsk <= 0) {
					g_throneAsk = 8.0f;
					Hud::Banner("BEGIN AGAIN? Every city falls back under the curse and the nights start over, the dead stronger. Your wonders, powers and gear stay yours. Touch the Seat again to begin.", 8.0f);
					return true;
				}
				g_throneAsk = 0;
				const int ng = std::min(15, NewGamePlus() + 1);
				Write(gen::kEchoGlobWords, ((Read(gen::kEchoGlobWords) & ~kNgMask) | (ng << kNgShift)) & ~(1 << kEchoRow) & ~kFlagEchoTold);
				Curse::NewGamePlus();
				Forms::Play(gen::Sound::quest_start);
				Hud::Lore("THE AWAKENED - CYCLE " + std::to_string(ng + 1),
					"The altars go dark again, every one, and the dead rise stronger than before - but you rise with all you won. Wake the Echo Altars anew, and the Echo of Alduin will be waiting on the Throat of the World.",
					12.0f);
				logger::info("echo: new game plus, cycle {}", ng + 1);
				return true;
			}
			return false;
		}

		bool Target(RE::PlayerCharacter* a_p, RE::NiPoint3& a_at, std::string& a_label)
		{
			if (!Glob(gen::kEchoGlobWords)) {
				return false;
			}
			int waiting = -1;
			for (int c = 0; c < kWonders; ++c) if (Waiting(c) && (waiting < 0 || c == Classes::Get())) waiting = c;  // (your own path's first)
			// at High Hrothgar: the sealed door; in the Reliquary, the pedestal of a wonder waiting to be taken up
			if (a_p->GetParentCell() == HH()) {
				if (InRoom(a_p)) {
					if (waiting >= 0) {
						const auto& s = gen::kRelPedestals[waiting];
						a_at = Room(s.x, s.y, s.z + 160.0f);
						a_label = "Take up " + std::string(Classes::WonderName(waiting));
						return true;
					}
					return false;
				}
				if (waiting >= 0 || (Flag(kFlagHallOpen) && Bane() && a_p->GetItemCount(Bane()) <= 0)) {
					a_at = P(gen::kRelDoor) + RE::NiPoint3{ 0, 0, 300.0f };
					a_label = Flag(kFlagHallOpen) ? "The Hall of the Awakened" : "The Reliquary";
					return true;
				}
				return false;
			}
			if (!InTamriel(a_p)) {
				return false;
			}
			float best = FLT_MAX;
			auto  consider = [&](const RE::NiPoint3& a_spot, std::string a_text) {
				const float d = a_spot.GetDistance(a_p->GetPosition());
				if (d < best) {
					best = d, a_at = a_spot, a_label = std::move(a_text);
				}
			};
			if (waiting >= 0) {
				consider(kHHFront + RE::NiPoint3{ 0, 0, 250.0f }, "High Hrothgar - " + std::string(Classes::WonderName(waiting)) + " waits in the Reliquary");
			}
			if (Flag(kFlagHallOpen) && Bane() && a_p->GetItemCount(Bane()) <= 0 && !Flag(kFlagRobes)) {
				consider(kHHFront + RE::NiPoint3{ 0, 0, 250.0f }, "High Hrothgar - the Hall of the Awakened");
			}
			for (int w = 0; w < kWordCount; ++w) {
				if (!Awake(w)) continue;
				const auto& W = gen::kWords[w];
				RE::NiPoint3 bat;
				const bool flying = g_siege.word == w && g_siege.stage == 2 && Director::ChampionPos(bat);
				consider(flying ? bat : WordAt(w) + RE::NiPoint3{ 0, 0, 300.0f }, (w == kEchoRow ? std::string("The Echo of Alduin") : std::string(W.word) + " - " + std::string(W.bearerName)) +
				                                                                    (flying ? std::string() : " (" + std::string(W.site) + ")"));
			}
			return best < FLT_MAX;
		}

		bool Strip(std::string& a_title, std::string& a_goal, float& a_frac)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !Glob(gen::kEchoGlobWords)) {
				return false;
			}
			// in the Reliquary: the nearest pedestal - its wonder, and how far its path's secret has come
			if (p->GetParentCell() == HH() && InRoom(p)) {
				int   best = -1;
				float bd = 420.0f;
				for (int w = 0; w < kWonders; ++w) {
					const auto& sp = gen::kRelPedestals[w];
					if (const float d = p->GetPosition().GetDistance(Room(sp.x, sp.y, sp.z)); d < bd) bd = d, best = w;
				}
				if (best >= 0) {
					const int st = Classes::WonderState(best);
					a_title = Upper(std::string(Classes::WonderName(best))) + "  -  " + Upper(std::string(coffer::kClassName[best]));
					a_goal = st == 5 ? (Classes::Get() == best ? std::string("Won - activate the pedestal to take it up") : "Won - it answers only to the " + PathName(best))
					         : st == 4                         ? std::string("Won and taken up")
					         : ((Party::PartyWonders() >> best) & 1) ? std::string("Won by your party")
					         : st >= 1                         ? std::string("Its path's secret is under way")
					                                           : std::string("Its path's secret wakes once six cities are free");
					int won = 0;
					for (int c = 0; c < kWonders; ++c) won += Won(c) || Waiting(c) ? 1 : 0;
					a_frac = static_cast<float>(won) / kWonders;
					return true;
				}
			}
			if (!InTamriel(p)) {
				return false;
			}
			const int wd = Here(p);
			if (wd >= 0) {
				const auto& W = gen::kWords[wd];
				const float d = std::hypot(p->GetPosition().x - W.x, p->GetPosition().y - W.y);
				if (d < gen::kWordLeash) {
					a_title = wd == kEchoRow ? std::string("THE ECHO OF ALDUIN") : std::string(W.word) + " - " + Upper(std::string(W.meaning));
					a_goal = g_siege.stage == 2   ? "Slay " + std::string(W.bearerName)
					         : g_siege.stage == 1 ? fmt::format("Wave {} of {}  -  {} left", g_siege.wave + 1, W.nWaves, g_siege.dead.size())
					         : Director::Fighting() ? "Go to " + std::string(W.site) + " - its dead will rise"
					                                : std::string(W.site) + " wakes only at night";
					a_frac = g_siege.stage == 2 ? 1.0f : static_cast<float>(g_siege.wave) / std::max(1, W.nWaves);
					return true;
				}
			}
			return false;
		}
	}
}
