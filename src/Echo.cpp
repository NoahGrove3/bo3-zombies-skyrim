// The story after the altars (sheet: echo).
//
// Act II - the Reliquary: once six cities are free each path's secret wakes (src/Eggs.cpp). Win it and the Greybeards
// call: the path's wonder floats waiting in High Hrothgar's great hall, in front of a dragon relief over a bench (TickHall). A wonder answers only
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
		constexpr int kFlagBaneOut = 1 << 16;  // (Alduin's Bane taken from its stand and not put back)
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
		std::string PathName(int a_c) { static constexpr const char* k[4] = { "Summoner", "Battle-mage", "Alchemist", "Spellsword" }; return a_c >= 0 && a_c < 4 ? k[a_c] : "?"; }
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
			Unplace(cell, gen::kRelDoorIn, P(gen::kRelDoor));  // (Noah: the way in is the portal outside the back of High Hrothgar now - the hall's sealed door is gone)
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
				if (bane && a_p->GetItemCount(bane) <= 0 && !Flag(kFlagBaneOut)) {
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
		// (Noah: "I want the entrance to the room ... to be a portal ... just outside the back of High Hrothgar. The portal should
		// bring us to a different room") the Reliquary's portal (tools/portal.py) stands on the mountain behind the monastery;
		// walk into it and you come out in the Reliquary, its exit brings you back out in front of it
		void Go(RE::PlayerCharacter* a_p, RE::ObjectRefHandle& a_to);
		RE::ObjectRefHandle g_portal;
		bool                g_toRoom = false, g_toPortal = false;
		float               g_portalCd = 0;
		RE::TESWorldSpace* Tamriel() { return RE::TESForm::LookupByID<RE::TESWorldSpace>(0x0000003C); }
		RE::TESObjectREFR* HallDoorOut() { return RE::TESForm::LookupByID<RE::TESObjectREFR>(0x000A85EA); }  // High Hrothgar's door, outside
		// a way into High Hrothgar: the far side of one of its doors standing near you (Tamriel's doors into the cell)
		RE::NiPointer<RE::TESObjectREFR> WayIntoHH(RE::PlayerCharacter* a_p)
		{
			RE::NiPointer<RE::TESObjectREFR> in;
			if (auto* door = HallDoorOut()) {
				if (auto x = door->extraList.GetTeleportLinkedDoor().get(); x && x->GetParentCell() == HH()) in = x;
			}
			if (!in) {
				RE::TES::GetSingleton()->ForEachReferenceInRange(a_p, 9500.0f, [&](RE::TESObjectREFR* x) {
					if (x && x->GetBaseObject() && x->GetBaseObject()->Is(RE::FormType::Door)) {
						if (auto y = x->extraList.GetTeleportLinkedDoor().get(); y && y->GetParentCell() == HH()) {
							in = y;
							return RE::BSContainer::ForEachResult::kStop;
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return in;
		}
		// High Hrothgar's own door out to the mountain (one of the hall's doors whose far side is in Tamriel), while you're inside
		RE::TESObjectREFR* DoorOutOfHH()
		{
			auto* cell = HH();
			RE::TESObjectREFR* out = nullptr;
			if (!cell) return nullptr;
			cell->ForEachReference([&](RE::TESObjectREFR* x) {
				if (x && x->GetBaseObject() && x->GetBaseObject()->Is(RE::FormType::Door)) {
					if (auto y = x->extraList.GetTeleportLinkedDoor().get(); y && y->GetWorldspace() == Tamriel()) {
						out = x;
						return RE::BSContainer::ForEachResult::kStop;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return out;
		}
		// (Noah: "forget the portal idea") any portal left standing behind High Hrothgar from an earlier build is taken away
		void TickPortal(RE::PlayerCharacter* a_p, float)
		{
			static bool cleared = false;
			if (cleared || a_p->GetWorldspace() != Tamriel() || a_p->GetPosition().GetDistance(P(gen::kRelPortal)) > 6000.0f) return;
			cleared = true;
			if (auto* st = Forms::Ours(gen::kRelPortalActi)) {
				RE::TES::GetSingleton()->ForEachReferenceInRange(a_p, 9500.0f, [&](RE::TESObjectREFR* x) {
					if (x && x->GetBaseObject() == st && !x->IsDeleted()) x->Disable(), x->SetDelete(true);
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		}
		void Go(RE::PlayerCharacter* a_p, RE::ObjectRefHandle& a_to)
		{
			if (auto m = a_to.get()) {
				Forms::Play(gen::Sound::ward_lit);
				a_p->MoveTo(m.get());
			}
		}

		// ---- (Noah: "just use High Hrothgar's building") the great hall: each wonder waiting to be taken up floats in front of
		// a dragon relief over its bench, slowly turning and bobbing in a soft glow; Alduin's Bane floats blade-up over the fire
		// at the hall's end; the centre pillar burns, and the Hall of the Awakened's rewards are taken there. Look at one and
		// press Activate (E, A on Xbox, X on PS5) or left click. Positions are High Hrothgar's own (its benches HHFurnitureBench01,
		// its fire pit, its centre pillar HHLightPedestal02, read from Skyrim.esm) ----
		constexpr float kHallFloor = -992.0f;
		struct Floater { RE::NiPoint3 at; float yaw; };
		const std::array<Floater, 5>& FloatSpots()
		{
			// the four benches (the wonders in class order), each a little out from its wall; Alduin's Bane over the ember bowl past the centre pillar
			// (Noah: "centered with the statues on the wall and centered with the goblet") each one's middle: out from the relief
			// over its bench, at the relief's own height; the Bane's over the ember bowl
			static const std::array<Floater, 5> k{ { { { 455.0f, -1744.0f, kHallFloor + 165.0f }, 270.0f }, { { 455.0f, -584.0f, kHallFloor + 165.0f }, 270.0f },
				{ { -767.0f, -1744.0f, kHallFloor + 165.0f }, 90.0f }, { { -767.0f, -584.0f, kHallFloor + 165.0f }, 90.0f },
				{ { -160.0f, -1870.0f, kHallFloor + 200.0f }, 0.0f } } };  // (over the raised ember bowl ringed with candles - HHLightHolder01 at -160,-1870,-928)
			return k;
		}
		const RE::NiPoint3 kPillarAt{ -155.0f, -1547.0f, kHallFloor };
		// each model's middle along its long axis (+Y) and the size it floats at: the horn, the Staff of Magnus (Skyrim's: 175 long),
		// the Philosopher's Flask (the hand's - no spin of its own), the Dragon Slayer, Alduin's Bane
		constexpr float kFloatMid[5] = { 2.3f, -20.0f, -6.3f, 33.0f, 50.0f };
		constexpr float kFloatScale[5] = { 1.6f, 0.75f, 2.6f, 1.1f, 0.95f };
		struct HallRefs { std::array<RE::ObjectRefHandle, 5> item, glow; RE::ObjectRefHandle pillar; };
		HallRefs g_hall;
		float    g_hallT = 0, g_hallSlow = 0;
		bool     g_hallSwept = false;  // (what a save kept standing in the hall: taken away once, made again fresh)
		std::array<int, 5> g_hallKind{};
		std::array<float, 5> g_glowT{};
		float g_pillarT = 0;  // (the fire's and the glows' next lighting)  // what each spot holds: 0 nothing yet, 1 the thing itself, 2 its grey likeness
		int      g_aim = -1;        // 0-3 a wonder, 4 the Bane, 5 the pillar; -1 nothing
		bool     g_hallMenu = false;
		int      g_hallFocus = 0;
		RE::NiPoint3 BaneFloatAt() { return FloatSpots()[4].at; }
		void Drop(RE::ObjectRefHandle& a_h)
		{
			if (auto r = a_h.get()) r->Disable(), r->SetDelete(true);
			a_h = {};
		}
		bool BaneShown(RE::PlayerCharacter* a_p)
		{
			auto* bane = Bane();
			return Flag(kFlagHallOpen) && bane && a_p->GetItemCount(bane) <= 0;
		}
		bool FloatShown(RE::PlayerCharacter* a_p, int a_i) { return a_i == 4 ? BaneShown(a_p) : Waiting(a_i); }  // (yours to take)
		// (Noah: "a grayed out version so that I can see when I have it unlocked") in its colours once it's unlocked - won or waiting -
		// grey until then
		bool FloatLit(int a_i) { return a_i == 4 ? Flag(kFlagHallOpen) : (Won(a_i) || Waiting(a_i)); }
		// the Hall's rewards (the pillar's rows, and the old stations' activators)
		std::string HallRegalia(RE::PlayerCharacter* a_p)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh->LookupModByName("Dragonborn.esm")) return "needs Dragonborn";
			int given = 0;
			const bool again = Flag(kFlagRobes);  // (the audit: once given, a missing piece is made again for 2,500 Cursed Essence)
			for (const RE::FormID id : { 0x02B0F7u, 0x02B0F9u, 0x03D2AFu, 0x029A62u }) {  // the First Dragonborn's robes, gloves, boots and mask
				if (auto* a = dh->LookupForm<RE::TESObjectARMO>(id, "Dragonborn.esm"); a && a_p->GetItemCount(a) <= 0) {
					if (again && !Points::Spend(2500)) return "2,500 Cursed Essence a lost piece";
					Forms::Give(a_p, a, 1);
					++given;
				}
			}
			SetFlag(kFlagRobes, true);
			if (given) Forms::Play(gen::Sound::shout_learned);
			return given ? "TAKEN" : "YOURS";
		}
		void HallBones()
		{
			SetFlag(kFlagBone, !Flag(kFlagBone));
			Forms::Play(gen::Sound::ward_lit);
		}
		void HallEndless()
		{
			SetFlag(kFlagEndless, !Flag(kFlagEndless));
			Forms::Play(Flag(kFlagEndless) ? gen::Sound::curse_lifted : gen::Sound::ward_lit);
		}
		bool HallBeginAgain()  // (asks twice: a first press warns, a second within a few seconds begins again)
		{
			if (!Broken()) return false;
			if (g_throneAsk <= 0) {
				g_throneAsk = 8.0f;
				return false;
			}
			g_throneAsk = 0;
			const int ng = std::min(15, Cycle() + 1);
			Write(gen::kEchoGlobWords, ((Read(gen::kEchoGlobWords) & ~kNgMask) | (ng << kNgShift)) & ~(1 << kEchoRow) & ~kFlagEchoTold);
			Curse::NewGamePlus();
			Forms::Play(gen::Sound::quest_start);
			logger::info("echo: new game plus, cycle {}", ng + 1);
			return true;
		}
		bool TakeBane(RE::PlayerCharacter* a_p)
		{
			auto* bane = Bane();
			if (!bane || a_p->GetItemCount(bane) > 0) return false;
			// (the audit: dropped or stashed, a new one was free - as many as you liked) a lost Bane is forged again for 10,000
			constexpr int kReforge = 10000;
			if (Flag(kFlagBaneOut) && !Points::Spend(kReforge)) return false;
			SetFlag(kFlagBaneOut, true);
			Forms::Give(a_p, bane, 1);
			Forms::Play(gen::Sound::shout_learned);
			return true;
		}
		std::string AimLabel(RE::PlayerCharacter* a_p, int a_i)
		{
			if (a_i == 5) return "The Endless Flame";
			if (a_i < 5 && !FloatShown(a_p, a_i)) {  // (nothing to take: its grey likeness, or one already yours)
				if (a_i == 4) return a_p->GetItemCount(Bane()) > 0 ? "Alduin's Bane - in your hands" : "Alduin's Bane - wakes when the Echo of Alduin falls";
				const int st = Classes::WonderState(a_i);
				return std::string(Classes::WonderName(a_i)) + (st >= 4 || Won(a_i) ? " - yours" : " - not yet won");
			}
			if (a_i == 4) return Flag(kFlagBaneOut) ? "Forge Alduin's Bane again (10,000 Cursed Essence)" : "Take Alduin's Bane";
			const std::string name(Classes::WonderName(a_i));
			if (Classes::Get() != a_i) return name + " - it answers only to the " + PathName(a_i);
			(void)a_p;
			return "Take " + name;
		}
		std::vector<Hud::State::HallRow> HallRows()
		{
			const bool open = Flag(kFlagHallOpen);
			const std::string locked = "Wakes when the Echo of Alduin falls";
			std::vector<Hud::State::HallRow> r;
			r.push_back({ "THE FIRST DRAGONBORN'S REGALIA", open ? "Robes, gloves, boots and mask: everything you wield hits 25% harder, every blow on you is 25% softer, magic costs a quarter less" : locked,
				!open ? "LOCKED" : Flag(kFlagRobes) ? "TAKEN" : "", open });
			r.push_back({ "ALDUIN'S BONES", open ? "Your glider's dragon wears Alduin's own bones, or its own scales again" : locked,
				!open ? "LOCKED" : Flag(kFlagBone) ? "WORN" : "", open });
			r.push_back({ "THE ENDLESS NIGHT", open ? "Light it and the curse returns: each night's last wave brings the next. A freed city stays safe" : locked,
				!open ? "LOCKED" : Flag(kFlagEndless) ? "BURNING" : "COLD", open });
			const bool again = open && Broken();
			r.push_back({ "BEGIN AGAIN", again ? (g_throneAsk > 0 ? "Press again to begin: every city falls back under the curse, the dead stronger. Your wonders, powers and gear stay yours"
			                                                      : "New Game+: the nights start over with the dead stronger - you keep your wonders, powers and gear")
			                               : "Wakes when the Echo of Alduin falls",
				!again ? std::string("LOCKED") : "CYCLE " + std::to_string(Cycle() + 1), again });
			r.push_back({ "LEAVE THE FLAME", "", "", true });
			return r;
		}
		void PushHallHud(RE::PlayerCharacter* a_p)
		{
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.hallAim = g_aim >= 0 && !g_hallMenu ? AimLabel(a_p, g_aim) : std::string();
			h.hallMenu = g_hallMenu;
			if (g_hallMenu) {
				h.hallTitle = "THE ENDLESS FLAME";
				h.hallRows = HallRows();
				h.hallFocus = g_hallFocus;
			}
		}
		void ClearHall()
		{
			g_hallKind = {}, g_glowT = {}, g_pillarT = 0;
			for (auto& x : g_hall.item) Drop(x);
			for (auto& x : g_hall.glow) Drop(x);
			Drop(g_hall.pillar);
		}
		// every frame in the hall: the floating things turn and bob, and what's in your sights is found
		void TickHall(RE::PlayerCharacter* a_p, float a_dt)
		{
			if (Save::SinceWorldReady() < 4.0f) return;  // (nothing placed while a save is still coming up)
			const bool here = a_p->GetParentCell() == HH() && !InRoom(a_p);
			if (!here) {
				g_hallSwept = false;
				if (g_hall.pillar || g_aim >= 0 || g_hallMenu) {
					ClearHall();
					g_aim = -1, g_hallMenu = false;
					PushHallHud(a_p);
				}
				return;
			}
			g_hallT += a_dt;
			if (!g_hallSwept) {
				g_hallSwept = true;
				std::vector<RE::TESBoundObject*> ours;
				for (auto id : gen::kHallFloat) ours.push_back(Forms::Ours(id));
				for (auto id : gen::kHallGhost) ours.push_back(Forms::Ours(id));
				for (auto id : { gen::kHallGlow, gen::kHallGlowTall, gen::kHallPillar }) ours.push_back(Forms::Ours(id));
				a_p->GetParentCell()->ForEachReference([&](RE::TESObjectREFR* r) {
					if (r && !r->IsDeleted() && std::find(ours.begin(), ours.end(), r->GetBaseObject()) != ours.end()) r->Disable(), r->SetDelete(true);
					return RE::BSContainer::ForEachResult::kContinue;
				});
				g_hall = {}, g_hallKind = {};
			}
			if ((g_hallSlow -= a_dt) <= 0) {  // what stands, twice a second
				g_hallSlow = 0.5f;
				// (Noah: "a grayed out version so that I can see when I have it unlocked") each spot always holds something: the
				// wonder itself in its glow when it's yours to take, its grey likeness until then
				for (int i = 0; i < 5; ++i) {
					const int want = FloatLit(i) ? 1 : 2;
					auto r = g_hall.item[i].get();
					if (want != g_hallKind[i] || !r || r->IsDeleted() || r->GetParentCell() != a_p->GetParentCell()) {
						Drop(g_hall.item[i]), Drop(g_hall.glow[i]);
						if (auto* b = Forms::Ours(want == 1 ? gen::kHallFloat[i] : gen::kHallGhost[i])) g_hall.item[i] = Gun::PlaceAt(b, FloatSpots()[i].at, 0);
						g_glowT[i] = 0;  // (its glow lit again at once)
						g_hallKind[i] = want;
						logger::info("echo: the hall's spot {} holds {} ({})", i, want == 1 ? "the thing itself" : "its grey likeness", g_hall.item[i] ? "placed" : "NOT PLACED");
					}
				}
				// (Noah: "the Endless Flame pillar should be animated with flames" - placed as an object, a model of only light never
				// drew) the fire and the glows burn as the flasks' ground fires do: lingering hazards wearing the model, lit again
				// just before each burns out
				if ((g_pillarT -= 0.5f) <= 0) {
					Gun::Hazard("CurseOfAlduin\\fx\\pillar_fire.nif", kPillarAt, 1.0f, 60.0f);
					g_pillarT = 59.5f;
					logger::info("echo: the Endless Flame burns");
				}
				for (int i = 0; i < 5; ++i) {
					if (g_hallKind[i] != 1 || (g_glowT[i] -= 0.5f) > 0) continue;
					Gun::Hazard(i == 4 ? "CurseOfAlduin\\fx\\hall_glow_tall.nif" : "CurseOfAlduin\\fx\\hall_glow.nif", FloatSpots()[i].at, 1.0f, 20.0f);
					g_glowT[i] = 19.5f;
				}
				// (they never drew: a placed thing fades in from nothing, and a model of only light never finished) drawn whole, at once
				const auto whole = [](RE::ObjectRefHandle& a_h) {
					auto r = a_h.get();
					auto* o = r ? r->Get3D() : nullptr;
					if (!o) return;
					o->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw, RE::NiAVObject::Flag::kIgnoreFade);
					if (auto* fn = o->AsFadeNode()) fn->GetRuntimeData().currentFade = 1.0f;
				};
				for (int i = 0; i < 5; ++i) whole(g_hall.item[i]);
			}
			// (Noah: "slowly rotating, facing upright, and bobbing up and down slowly") each stood upright - its long axis (+Y) up -
			// turning slowly about it and bobbing; its drawn model moved by hand every frame (as the thrown flasks are: a placed
			// thing's own angle never reached its model)
			for (int i = 0; i < 5; ++i) {
				auto r = g_hall.item[i].get();
				auto* o = r ? r->Get3D() : nullptr;
				if (!o) continue;
				const float bob = 4.0f * std::sin(g_hallT * 0.9f + i * 1.3f);
				const float a = std::fmod(g_hallT * 0.35f + i * 1.1f, 6.2832f), c = std::cos(a), sn = std::sin(a);
				RE::NiMatrix3 R;  // Rz(a) * (Y up to Z)
				R.entry[0][0] = c, R.entry[0][1] = 0, R.entry[0][2] = sn;
				R.entry[1][0] = sn, R.entry[1][1] = 0, R.entry[1][2] = -c;
				R.entry[2][0] = 0, R.entry[2][1] = 1, R.entry[2][2] = 0;
				const float sc = kFloatScale[i];
				const RE::NiPoint3 mid = FloatSpots()[i].at + RE::NiPoint3{ 0, 0, bob };
				o->local.rotate = R;
				o->local.scale = sc;
				o->local.translate = mid - R * RE::NiPoint3{ 0, kFloatMid[i] * sc, 0 };
				RE::NiUpdateData ud;
				ud.time = 0.0f;
				o->Update(ud);
			}
			// in your sights: the nearest along your look, close enough to reach
			g_aim = -1;
			if (!g_hallMenu) {
				auto* cam = RE::PlayerCamera::GetSingleton();
				if (cam && cam->cameraRoot) {
					const auto& m = cam->cameraRoot->world.rotate;
					const RE::NiPoint3 fwd{ m.entry[0][1], m.entry[1][1], m.entry[2][1] };
					const RE::NiPoint3 eye = cam->cameraRoot->world.translate;
					float best = FLT_MAX;
					for (int i = 0; i < 6; ++i) {
						if (i < 5 && !g_hall.item[i]) continue;
						const RE::NiPoint3 c = i < 5 ? FloatSpots()[i].at : kPillarAt + RE::NiPoint3{ 0, 0, 110.0f };
						const RE::NiPoint3 to = c - eye;
						const float along = to.Dot(fwd);
						if (along <= 0 || a_p->GetPosition().GetDistance(c) > (i == 5 ? 320.0f : 260.0f)) continue;
						const float off = (eye + fwd * along).GetDistance(c);
						if (off < (i == 5 ? 90.0f : i == 4 ? 70.0f : 50.0f) && along < best) best = along, g_aim = i;
					}
				}
			} else if (a_p->GetPosition().GetDistance(kPillarAt) > 420.0f) {
				g_hallMenu = false;  // (walking away closes it)
			}
			PushHallHud(a_p);
		}
		void UseAim(RE::PlayerCharacter* a_p)
		{
			if (g_aim >= 0 && g_aim < 5 && !FloatShown(a_p, g_aim)) return;  // (nothing to take: grey, or yours already)
			if (g_aim == 5) {
				g_hallMenu = true, g_hallFocus = 0;
				Forms::Play(gen::Sound::ward_lit);
			} else if (g_aim == 4) {
				(void)TakeBane(a_p);  // (it keeps floating, lit: yours)
			} else if (g_aim >= 0 && Classes::Get() == g_aim && Classes::TakeWonder(g_aim)) {
				logger::info("echo: {} taken up in the hall", Classes::WonderName(g_aim));
			}
			PushHallHud(a_p);
		}
		void HallPick(RE::PlayerCharacter* a_p)
		{
			const int last = static_cast<int>(HallRows().size()) - 1;
			if (g_hallFocus == last) {
				g_hallMenu = false;
			} else if (Flag(kFlagHallOpen)) {
				switch (g_hallFocus) {
				case 0: (void)HallRegalia(a_p); break;
				case 1: HallBones(); break;
				case 2: HallEndless(); break;
				case 3: if (HallBeginAgain()) g_hallMenu = false; break;
				}
			}
			PushHallHud(a_p);
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
			bool                         owed{ false };  // (the wave of this index rose no one yet: raised again)
			int                          night{ -1 };
			std::array<bool, 4>          told{};
			int                          flying{ -1 };  // the row whose dragon is up (credited when it dies, wherever the siege is)
		} g_siege;
		void EndSiege(const char* a_why)
		{
			if (g_siege.stage != 0) {
				logger::info("words: the siege of {} ends ({})", g_siege.word >= 0 ? gen::kWords[g_siege.word].word : "?", a_why);
			}
			g_siege.stage = 0, g_siege.wave = 0, g_siege.dead.clear(), g_siege.owed = false;
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
			g_siege.owed = g_siege.dead.empty();
			if (g_siege.owed) {
				g_siege.retry = 2.0f;  // (the field is full: in a moment)
				return;
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
				if ((g_siege.retry -= a_dt) > 0) return;
				if (g_siege.owed) {
					RaiseWave(a_p, w);
					return;
				}
				if (g_siege.wave + 1 < W.nWaves) {
					++g_siege.wave;
					RaiseWave(a_p, w);
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
				if (!Coop::Client()) Director::SkipToDawn("THE ECHO OF ALDUIN FALLS - THE CURSE IS BROKEN");
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
			g_hall = {}, g_hallSwept = false, g_aim = -1, g_hallMenu = false, g_hallKind = {};
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
			// (the review: the lobby works on the main menu - words heard there were written into the game before a load)
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			if (!Glob(gen::kEchoGlobWords) || !p || !p->Is3DLoaded() || !ui || ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) return;
			for (int w = 0; w < kDragons; ++w) {
				if ((a_mask >> w) & 1 && !Silenced(w)) {
					logger::info("words: {} silenced in the host's game - silenced here too", gen::kWords[w].word);
					SilenceWord(w);
				}
			}
			if (a_live && ((a_mask >> kEchoRow) & 1) && Awake(kEchoRow)) {  // (only where the Echo waits in this game's own cycle)
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
			TickHall(p, a_delta);
			if ((g_slow -= a_delta) > 0) {
				return;
			}
			g_slow = 0.5f;
			g_throneAsk = std::max(0.0f, g_throneAsk - 0.5f);
			TickAllies(0.5f);
			CleanUp(p);
			Party::SetWonder(Classes::WonderMask());  // (the party pools its wonders)
			TickPortal(p, 0.5f);  // (the Reliquary room and its portal are retired: the hall itself holds everything - TickHall)
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
			if (base == Forms::Ours(gen::kRelDoorOut)) {  // (out through the portal: in front of it on the mountain)
				auto* in = DoorOutOfHH();
				auto  door = in ? in->extraList.GetTeleportLinkedDoor().get() : RE::NiPointer<RE::TESObjectREFR>{};
				if (door) {
					Forms::Play(gen::Sound::ward_lit);
					g_toPortal = true;
					p->MoveTo(door.get());
				} else {
					logger::warn("echo: no door out of High Hrothgar - leaving by the hall");
					BuildReliquary(p);
					Go(p, g_leave);
				}
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
			if (base == Forms::Ours(gen::kHallFang)) { (void)TakeBane(p); return true; }
			if (base == Forms::Ours(gen::kHallRobes)) { (void)HallRegalia(p); return true; }
			if (base == Forms::Ours(gen::kHallSkull)) { HallBones(); return true; }
			if (base == Forms::Ours(gen::kHallEndless)) { HallEndless(); return true; }
			if (base == Forms::Ours(gen::kHallThrone)) { (void)HallBeginAgain(); return true; }
			return false;
		}

		bool Target(RE::PlayerCharacter* a_p, RE::NiPoint3& a_at, std::string& a_label)
		{
			if (!Glob(gen::kEchoGlobWords)) {
				return false;
			}
			int waiting = -1;
			for (int c = 0; c < kWonders; ++c) if (Waiting(c) && (waiting < 0 || c == Classes::Get())) waiting = c;  // (your own path's first)
			// in High Hrothgar's hall: your own wonder waiting over its bench, or Alduin's Bane over the fire
			if (a_p->GetParentCell() == HH()) {
				const int mine = Classes::Get();
				if (mine >= 0 && mine < kWonders && Waiting(mine)) {
					a_at = FloatSpots()[mine].at + RE::NiPoint3{ 0, 0, 90.0f };
					a_label = "Take up " + std::string(Classes::WonderName(mine));
					return true;
				}
				if (BaneShown(a_p) && !Flag(kFlagBaneOut)) {
					a_at = BaneFloatAt() + RE::NiPoint3{ 0, 0, 200.0f };
					a_label = "Alduin's Bane";
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
				consider(kHHFront + RE::NiPoint3{ 0, 0, 250.0f }, "High Hrothgar - " + std::string(Classes::WonderName(waiting)) + " waits in the great hall");
			}
			if (Flag(kFlagHallOpen) && Bane() && a_p->GetItemCount(Bane()) <= 0 && !Flag(kFlagRobes)) {
				consider(kHHFront + RE::NiPoint3{ 0, 0, 250.0f }, "High Hrothgar - Alduin's Bane and the Endless Flame");
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

	namespace
	{
		enum class HallKey { kNone, kUp, kDown, kPick, kBack };
		HallKey HallKeyOf(const RE::ButtonEvent* a_b)
		{
			if (!a_b) return HallKey::kNone;
			const auto dev = a_b->GetDevice();
			const auto code = a_b->GetIDCode();
			const bool kb = dev == RE::INPUT_DEVICE::kKeyboard, pad = dev == RE::INPUT_DEVICE::kGamepad;
			if ((kb && (code == 0x11 || code == 0xC8)) || (pad && code == 0x0001)) return HallKey::kUp;
			if ((kb && (code == 0x1F || code == 0xD0)) || (pad && code == 0x0002)) return HallKey::kDown;
			if (a_b->QUserEvent() == RE::UserEvents::GetSingleton()->activate || (kb && (code == 0x1C || code == 0x9C)) || (dev == RE::INPUT_DEVICE::kMouse && code == 0) || (pad && code == 0x1000))
				return HallKey::kPick;
			if ((kb && (code == 0x01 || code == 0x0F)) || (pad && code == 0x2000)) return HallKey::kBack;
			return HallKey::kNone;
		}
	}
	// (Noah: "to pick it up, players just need to press X on PS5 controller, A on Xbox controller, or left click on keyboard
	// mouse") the hall's keys: what's in your sights is taken with Activate or a left click; the pillar's menu has W/S and the
	// D-pad, Activate / Enter / left click / A, and Esc / B
	bool Echo::OnButton(const RE::ButtonEvent* a_b)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !a_b || (!g_hallMenu && g_aim < 0)) return false;
		const auto key = HallKeyOf(a_b);
		if (key == HallKey::kNone) return g_hallMenu;
		if (!a_b->IsDown()) return true;
		if (!g_hallMenu) {
			if (key != HallKey::kPick) return false;
			UseAim(p);
			return true;
		}
		const int n = static_cast<int>(HallRows().size());
		if (key == HallKey::kUp) g_hallFocus = (g_hallFocus + n - 1) % n;
		else if (key == HallKey::kDown) g_hallFocus = (g_hallFocus + 1) % n;
		else if (key == HallKey::kBack) g_hallMenu = false;
		else HallPick(p);
		PushHallHud(p);
		return true;
	}
	bool Echo::Eats(RE::InputEvent* a_event)
	{
		auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
		if (!b || (!g_hallMenu && g_aim < 0)) return false;
		const auto key = HallKeyOf(b);
		if (g_hallMenu) return key != HallKey::kNone;
		return key == HallKey::kPick;
	}

	void Echo::DevGiveBane()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (auto* bane = Bane(); p && bane) {
			p->AddObjectToContainer(bane, nullptr, 1, nullptr);
			RE::ActorEquipManager::GetSingleton()->EquipObject(p, bane);
			logger::info("dev: Alduin's Bane given");
		}
	}
}
