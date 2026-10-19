// Stage 2 of the story (sheet: echo): the Echo Stones, the hunt for the wonder weapons' parts, and the Reliquary.
//
// A freed city's altar raises an Echo Stone behind it (dressing: echo_stone). Walk up to it and it gives you the
// city's page, The Broken Words: <City> (a note in your pack; its words are shown as you take it), and from then on
// your compass marks every part that page names. A part is one of:
//   boss   dropped by that city's Barrow-Jarl (given as soon as the city is freed)
//   relic  a reliquary of Tongue-fire at a landmark: it wakes only at night - its guardians rise as you come near;
//          kill them, then walk up to it
//   kill   strike down the dead near a landmark at night until it has drunk enough (a counter under the compass)
// Each wonder needs its three parts, forged on its pedestal in the Reliquary of the Tongues: a hall under High
// Hrothgar behind a sealed door in the Greybeards' great hall (both doors move you between the hall and the room).
// A freed city's sky also clears at night.
//
// Saved with the game: globals UN_EchoPages (bit per town), UN_EchoParts (bit per part), UN_EchoForged (bit per
// wonder) and each kill site's count (UN_EchoKill<part>).
#include "BO3Z.h"

namespace bo3z
{
	namespace
	{
		constexpr RE::FormID kHighHrothgar = 0x00087764;   // Skyrim.esm: the HighHrothgar interior
		constexpr RE::FormID kTamriel = 0x0000003C;
		constexpr RE::FormID kXMarker = 0x0000003B;
		const RE::NiPoint3   kHHFront{ 53664.0f, -36000.0f, 23581.0f };  // High Hrothgar's front doors, outside (Tamriel)
		constexpr int        kParts = static_cast<int>(gen::kEchoParts.size());
		constexpr int        kWonders = static_cast<int>(gen::kEchoWonders.size());
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
		bool PageRead(int a_town) { return (Read(gen::kEchoGlobPages) >> a_town) & 1; }
		bool Held(int a_part) { return (Read(gen::kEchoGlobParts) >> a_part) & 1; }
		bool Forged(int a_w) { return (Read(gen::kEchoGlobForged) >> a_w) & 1; }
		int  HeldOf(int a_w)
		{
			int n = 0;
			for (int i = 0; i < kParts; ++i) {
				n += gen::kEchoParts[i].wonder == a_w && Held(i);
			}
			return n;
		}
		int  NeedOf(int a_w)
		{
			int n = 0;
			for (const auto& p : gen::kEchoParts) {
				n += p.wonder == a_w;
			}
			return n;
		}
		bool Ready(int a_w) { return !Forged(a_w) && HeldOf(a_w) >= NeedOf(a_w); }
		int  Diff() { return std::clamp(Settings::Difficulty(), 0, 5); }
		bool Hunting(int a_part)  // its page is read and it's still out there
		{
			const auto& p = gen::kEchoParts[a_part];
			return p.how != gen::kEchoBoss && PageRead(p.page) && !Held(a_part);
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
		void Remove(RE::ObjectRefHandle& a_h)
		{
			if (auto r = a_h.get()) {
				r->Disable();
				r->SetDelete(true);
			}
			a_h = {};
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
		// out in the world: the same, searched round the player
		RE::TESObjectREFR* EnsureNear(RE::PlayerCharacter* a_p, RE::ObjectRefHandle& a_h, std::uint32_t a_base, const RE::NiPoint3& a_at, float a_yawDeg, float a_scale = 1.0f)
		{
			if (auto r = a_h.get(); r && !r->IsDeleted() && !r->IsDisabled() && r->GetPosition().GetDistance(a_at) < 60.0f) {
				return r.get();
			}
			auto* base = Forms::Ours(a_base);
			if (!base) {
				return nullptr;
			}
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_p, a_p->GetPosition().GetDistance(a_at) + 300.0f, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled() && r->GetPosition().GetDistance(a_at) < 60.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!found) {
				auto ref = a_p->PlaceObjectAtMe(base, false);
				if (!ref) {
					return nullptr;
				}
				ref->SetPosition(a_at);
				ref->SetAngle({ 0, 0, Rad(a_yawDeg) });
				if (a_scale != 1.0f) {
					ref->SetScale(a_scale);
				}
				found = ref.get();
			}
			a_h = found->CreateRefHandle();
			return found;
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

		struct Site
		{
			bool                         spotSet{ false };
			RE::NiPoint3                 spot;
			RE::ObjectRefHandle          relic, light, beam;
			std::vector<RE::ActorHandle> guards;
			int                          stage{ 0 };      // relic: 0 asleep, 1 guarded, 2 open
			int                          night{ -1 };     // the night its guardians rose
			float                        retry{ 0 };
			bool                         told{ false };
		};
		std::array<Site, kParts> g_site;
		RE::NiPoint3 SiteAt(int a_part)
		{
			const auto& p = gen::kEchoParts[a_part];
			return g_site[a_part].spotSet ? g_site[a_part].spot : RE::NiPoint3{ p.x, p.y, p.z };
		}
		// the relic's spot: in front of the barrow's door, on walkable ground (found once, the first time you come near)
		void FindSpot(RE::PlayerCharacter* a_p, int a_part)
		{
			auto& s = g_site[a_part];
			const auto& q = gen::kEchoParts[a_part];
			if (s.spotSet) {
				return;
			}
			const RE::NiPoint3 door{ q.x, q.y, q.z };
			for (const float side : { 1.0f, -1.0f }) {
				if (auto at = Walkable(a_p, Turn(door, q.yaw, 0, side * 320.0f, 0), 500.0f)) {
					s.spot = *at, s.spotSet = true;
					return;
				}
			}
			if (auto at = Walkable(a_p, door, 700.0f)) {
				s.spot = *at, s.spotSet = true;
			}
		}

		// ---- the wonder weapons (stage 3): each is one of the Coffer's spells in your hands (sheet: weapons/spellcraft) ----
		int GunOfWonder(int a_w)
		{
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (gen::kGuns[i].id == gen::kEchoWonders[a_w].id) {
					return i;
				}
			}
			return -1;
		}
		std::uint32_t OrbOfWonder(int a_w)  // its emblem burning in the air (the spell's logo model)
		{
			for (const auto& r : gen::kSpellcraft) {
				if (r.id == gen::kEchoWonders[a_w].id) {
					return r.orbStat;
				}
			}
			return 0;
		}
		int  HeldWonder() { return Read(gen::kEchoGlobHeld) - 1; }
		void SetHeld(int a_w)
		{
			Write(gen::kEchoGlobHeld, a_w + 1);
			Party::SetWonder(a_w);
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

		// ---- the Reliquary ---------------------------------------------------------------------------------------
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
		void BuildReliquary(RE::PlayerCharacter* a_p)
		{
			auto* cell = HH();
			if (a_p->GetParentCell() != cell) {
				return;
			}
			Ensure(a_p, cell, gen::kRelDoorIn, P(gen::kRelDoor), gen::kRelDoor.yaw);
			Ensure(a_p, cell, gen::kRelRoom, Room(0, 0, 0), 0);
			Ensure(a_p, cell, gen::kRelDoorOut, Room(gen::kRelExit.x, gen::kRelExit.y + 52.0f, gen::kRelExit.z), gen::kRelExit.yaw);
			Ensure(a_p, cell, gen::kRelStone, Room(0, 150, 0), 180.0f);
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
				int k = 0;
				for (int i = 0; i < kParts; ++i) {
					if (gen::kEchoParts[i].wonder == w) {
						if (Held(i)) {
							Ensure(a_p, cell, gen::kEchoWonders[w].gem, SocketAt(w, k), 0);
						}
						++k;
					}
				}
				if (Forged(w)) {
					Ensure(a_p, cell, gen::kRelBeam, Room(s.x, s.y, s.z + 100.0f), 0, 0.22f);
					// the wonder rests over its pedestal while no one in your game carries it: its emblem burning in the air
					const auto orbAt = Room(s.x, s.y, s.z + 165.0f);
					if (HeldWonder() != w) {
						if (auto* o = Ensure(a_p, cell, OrbOfWonder(w), orbAt, s.yaw + 180.0f, 2.2f)) {
							o->SetAngle({ 0, 0, o->GetAngleZ() + 0.03f });  // (turning slowly)
						}
					} else if (auto* base = Forms::Ours(OrbOfWonder(w))) {
						cell->ForEachReference([&](RE::TESObjectREFR* r) {
							if (r && r->GetBaseObject() == base && !r->IsDeleted() && r->GetPosition().GetDistance(orbAt) < 60.0f) {
								r->Disable();
								r->SetDelete(true);
							}
							return RE::BSContainer::ForEachResult::kContinue;
						});
					}
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

		std::string Missing(int a_w)
		{
			std::string out;
			for (int i = 0; i < kParts; ++i) {
				const auto& q = gen::kEchoParts[i];
				if (q.wonder != a_w || Held(i)) {
					continue;
				}
				out += (out.empty() ? "" : ";  ") + std::string(q.label) + (PageRead(q.page) ? " - " + std::string(q.site) : " - read " + std::string(gen::kTowns[q.page]) + "'s Echo Stone");
			}
			return out;
		}

		void Forge(RE::PlayerCharacter* a_p, int a_w)
		{
			const auto& w = gen::kEchoWonders[a_w];
			if (Forged(a_w)) {
				const int gun = GunOfWonder(a_w);
				if (gun < 0) {
					return;
				}
				if (HeldWonder() == a_w) {  // give it back: it rests on its pedestal again (for you, or a friend)
					if (auto* sp = Forms::GunSpell(gun)) {
						a_p->RemoveSpell(sp);
					}
					SetHeld(-1);
					Forms::Play(gen::Sound::ward_lit);
					Hud::Banner(Upper(std::string(w.label)) + " RESTS ON ITS PEDESTAL", 3.5f);
					BuildReliquary(a_p);
					return;
				}
				if (const auto who = Party::WonderHolder(a_w); !who.empty()) {
					Hud::Banner(std::string(w.label) + " is in " + who + "'s hands. Each wonder answers to one wielder at a time.", 5.0f);
					return;
				}
				if (const int old = HeldWonder(); old >= 0) {  // one wonder at a time: the one you carry goes back to its pedestal
					if (auto* sp = Forms::GunSpell(GunOfWonder(old))) {
						a_p->RemoveSpell(sp);
					}
					Hud::Banner(std::string(gen::kEchoWonders[old].label) + " returns to its pedestal", 3.0f);
				}
				SetHeld(a_w);
				Gun::Give(gun);
				Forms::Play(gen::Sound::shout_learned);
				Gun::FxBlast("light", a_p->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f });
				Hud::Lore(Upper(std::string(w.label)), std::string(gen::kGuns[gun].abilityText) + " Return it to this pedestal to take up another wonder - only one answers to you at a time.", 9.0f);
				BuildReliquary(a_p);
				logger::info("echo: took up {}", w.id);
				return;
			}
			if (!Ready(a_w)) {
				Hud::Banner(std::string(w.label) + ": " + std::to_string(HeldOf(a_w)) + " of " + std::to_string(NeedOf(a_w)) + " parts.  Missing: " + Missing(a_w), 7.0f);
				return;
			}
			Write(gen::kEchoGlobForged, Read(gen::kEchoGlobForged) | (1 << a_w));
			Forms::Play(gen::Sound::forge_ritual);
			Forms::Play(gen::Sound::curse_lifted);
			const auto& s = gen::kRelPedestals[a_w];
			Gun::FxBlast("light", Room(s.x, s.y, s.z + 120.0f));
			Hud::Lore(Upper(std::string(w.label)) + " IS FORGED", std::string(w.text) + " Its three parts burn as one on the pedestal. Activate the pedestal again to take it up.", 10.0f);
			Points::Add(3000);
			BuildReliquary(a_p);
			logger::info("echo: {} forged", w.id);
		}

		// ---- parts and pages -------------------------------------------------------------------------------------
		void Award(int a_part, bool a_loud)
		{
			if (Held(a_part)) {
				return;
			}
			Write(gen::kEchoGlobParts, Read(gen::kEchoGlobParts) | (1 << a_part));
			const auto& q = gen::kEchoParts[a_part];
			const auto& w = gen::kEchoWonders[q.wonder];
			const int   n = HeldOf(q.wonder), need = NeedOf(q.wonder);
			if (a_loud) {
				Forms::Play(gen::Sound::relic_found);
				Hud::Banner("PART FOUND - " + Upper(std::string(q.label)) + "  (" + std::string(w.label) + " " + std::to_string(n) + "/" + std::to_string(need) +
				                (n >= need ? ": take it to the Reliquary at High Hrothgar)" : ")"),
					7.0f);
				Points::Add(1500);
			}
			auto& s = g_site[a_part];
			Remove(s.relic), Remove(s.light), Remove(s.beam);
			s.guards.clear();
			logger::info("echo: part {} found ({} {}/{})", q.id, w.id, n, need);
		}

		void TakePage(RE::PlayerCharacter* a_p, int a_town)
		{
			Write(gen::kEchoGlobPages, Read(gen::kEchoGlobPages) | (1 << a_town));
			if (auto* book = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectBOOK>(gen::kEchoPages[a_town], gen::kPlugin)) {
				a_p->AddObjectToContainer(book, nullptr, 1, nullptr);
			}
			Forms::Play(gen::Sound::quest_start);
			std::string text = "The stone speaks with the Tongues' voice. ";
			for (int i = 0; i < kParts; ++i) {
				const auto& q = gen::kEchoParts[i];
				if (q.page != a_town) {
					continue;
				}
				const std::string w(gen::kEchoWonders[q.wonder].label);
				text += q.how == gen::kEchoBoss  ? std::string(q.label) + " (" + w + ") is yours: it fell with the Barrow-Jarl. "
				        : q.how == gen::kEchoRelic ? std::string(q.label) + " (" + w + ") waits at " + std::string(q.site) + " - at night, kill its guardians and take it. "
				                                   : std::string(q.label) + " (" + w + ") - at night, kill the dead at " + std::string(q.site) + ". ";
			}
			text += "Your compass marks the way. Forge the parts at High Hrothgar, behind the sealed door.";
			Hud::Lore("THE BROKEN WORDS: " + Upper(std::string(gen::kTowns[a_town])), text, 16.0f);
			logger::info("echo: page of {} taken", gen::kTowns[a_town]);
		}

		RE::TESObjectREFR* StoneOf(int a_town)
		{
			auto* altar = Shops::Placed(a_town, 0);
			auto* base = Forms::Ours(gen::kRelStone);
			if (!altar || !base) {
				return nullptr;
			}
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(altar, 700.0f, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled()) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return found;
		}

		// ---- the hunt: relics and kill sites ---------------------------------------------------------------------
		int Night() { return Save::Get(gen::kAV_round); }
		void TickRelic(RE::PlayerCharacter* a_p, int a_part, float a_dt)
		{
			auto&       s = g_site[a_part];
			const auto& q = gen::kEchoParts[a_part];
			const float dist2d = std::hypot(a_p->GetPosition().x - q.x, a_p->GetPosition().y - q.y);
			if (dist2d > 6000.0f) {
				s.relic = {}, s.light = {}, s.beam = {};
				return;
			}
			FindSpot(a_p, a_part);
			const auto at = SiteAt(a_part);
			EnsureNear(a_p, s.relic, gen::kRelRelic, at, q.yaw);
			EnsureNear(a_p, s.light, gen::kRelRelicLight, at + RE::NiPoint3{ 0, 0, 180.0f }, 0);
			EnsureNear(a_p, s.beam, gen::kRelBeam, at + RE::NiPoint3{ 0, 0, 60.0f }, 0, 0.35f);
			const float d = a_p->GetPosition().GetDistance(at);
			std::erase_if(s.guards, [](const RE::ActorHandle& h) { auto a = h.get(); return !a || a->IsDead() || a->IsDeleted() || a->IsDisabled(); });
			const bool night = Director::Fighting();
			if (s.stage == 1 && s.guards.empty()) {
				s.stage = 2;
				Forms::Play(gen::Sound::quest_step);
				Hud::Banner("THE RELIQUARY OPENS - take " + std::string(q.label), 5.0f);
			}
			if (s.stage == 2 && d < gen::kEchoTake) {
				Award(a_part, true);
				s.stage = 0;
				return;
			}
			if (!night) {
				if (s.stage == 1) {
					s.stage = 0;  // (dawn: the guardians crumbled with the rest)
				}
				if (d < gen::kEchoTake * 2.0f && !s.told) {
					s.told = true;
					Hud::Banner(std::string(q.label) + ": the reliquary is cold. It wakes only at night.", 5.0f);
				}
				return;
			}
			s.told = false;
			if (s.stage == 0 && d < gen::kEchoWake && s.night != Night()) {
				if (Coop::Client()) {  // (a friend's game raises no dead: the reliquary simply opens)
					s.stage = 2;
					return;
				}
				if ((s.retry -= a_dt) > 0) {
					return;
				}
				s.retry = 2.0f;
				const int n = gen::kEchoGuards[Diff()], el = gen::kEchoElites[Diff()];
				for (int i = 0; i < n + el; ++i) {
					const float a = 6.2832f * i / std::max(1, n + el), r = 450.0f + 250.0f * (i % 2);
					const auto  spot = Walkable(a_p, at + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 500.0f);
					if (auto h = Director::RaiseTrial(spot ? *spot : at, i >= n); h) {
						s.guards.push_back(h);
					}
				}
				if (!s.guards.empty()) {
					s.stage = 1, s.night = Night();
					Forms::Play(gen::Sound::champ_arrive);
					Hud::Banner("THE GUARDIANS OF " + Upper(std::string(q.label)) + " RISE", 4.0f);
					logger::info("echo: {} guardians rose ({})", q.id, s.guards.size());
				}
			}
		}

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

		// ---- Stage 4: the Three Words (sheet: echo.words) --------------------------------------------------------------
		// Once all four wonders are forged, Alduin's words wake one after another. Each is fought at night at its place:
		// come near and its siege begins (its waves of dead round the place), then its bearer rises; kill the bearer and
		// the word is silenced for good, its power yours (Echo::WordSilenced), and the next word wakes.
		constexpr int kWordCount = static_cast<int>(gen::kWords.size());
		constexpr int kWordsTold = 1 << 8;
		bool AllForged() { return (Read(gen::kEchoGlobForged) & ((1 << kWonders) - 1)) == (1 << kWonders) - 1; }
		bool Silenced(int a_w) { return (Read(gen::kEchoGlobWords) >> a_w) & 1; }
		int  NextWord()
		{
			if (!AllForged()) {
				return -1;
			}
			for (int i = 0; i < kWordCount; ++i) {
				if (!Silenced(i)) {
					return i;
				}
			}
			return -1;
		}
		RE::NiPoint3 WordAt(int a_w) { return { gen::kWords[a_w].x, gen::kWords[a_w].y, gen::kWords[a_w].z }; }
		struct Siege
		{
			int                          word{ -1 };
			int                          stage{ 0 };   // 0 asleep, 1 its waves, 2 its bearer walks
			int                          wave{ 0 };
			std::vector<RE::ActorHandle> dead;
			float                        retry{ 0 };
			int                          night{ -1 };
			bool                         told{ false };
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
			Hud::Banner(std::string(W.word) + " - WAVE " + std::to_string(g_siege.wave + 1) + " OF " + std::to_string(W.nWaves), 3.5f);
			logger::info("words: {} wave {} risen ({} dead)", W.word, g_siege.wave + 1, g_siege.dead.size());
		}
		void TickWords(RE::PlayerCharacter* a_p, float a_dt)
		{
			const int w = NextWord();
			if (w < 0) {
				if (g_siege.stage != 0) {
					EndSiege("no word left");
				}
				return;
			}
			// the arc told once: the moment the fourth wonder is forged (or the first load after it)
			if (!(Read(gen::kEchoGlobWords) & kWordsTold)) {
				Write(gen::kEchoGlobWords, Read(gen::kEchoGlobWords) | kWordsTold);
				Forms::Play(gen::Sound::quest_start);
				Hud::Lore("THE THREE WORDS",
					"With the four wonders forged, the Tongues' altars hear Alduin's three words still ringing through Skyrim: GRAH, DIR, LEIN - Battle, Death, World. "
					"Each lives on in a place of the old dragon cult, and each has a bearer. Silence them one by one, at night, wonders in hand. " + std::string(gen::kWords[w].wake),
					18.0f);
				logger::info("words: the arc begins with {}", gen::kWords[w].word);
			}
			if (g_siege.word != w) {
				EndSiege("a new word");
				g_siege.word = w, g_siege.told = false;
			}
			if (!InTamriel(a_p)) {
				return;
			}
			const auto& W = gen::kWords[w];
			const auto  at = WordAt(w);
			const float d = std::hypot(a_p->GetPosition().x - at.x, a_p->GetPosition().y - at.y);
			const bool  night = Director::Fighting();
			if (!night) {
				if (g_siege.stage != 0) {
					EndSiege("dawn");
				}
				if (d < gen::kWordWake && !g_siege.told) {
					g_siege.told = true;
					Hud::Banner(std::string(W.word) + " sleeps by day. Its bearer answers only at night.", 5.0f);
				}
				return;
			}
			if (g_siege.stage != 0 && d > gen::kWordLeash) {
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
				Hud::Lore(std::string(W.word) + " - " + Upper(std::string(W.meaning)), std::string(W.wake), 10.0f);
				RaiseWave(a_p, w);
				return;
			}
			if (g_siege.stage == 1 && g_siege.dead.empty()) {
				if (++g_siege.wave < W.nWaves) {
					RaiseWave(a_p, w);
					return;
				}
				// the waves are down: the bearer rises at the heart of the place
				const auto spot = Walkable(a_p, at, 700.0f);
				if (Director::RaiseChampion(-1, W.bearerRow, W.bearerName, W.health, (spot ? *spot : at) + RE::NiPoint3{ 0, 0, 40.0f })) {
					g_siege.stage = 2;
					Gun::FxBlast("dark", spot ? *spot : at);
					logger::info("words: {} - its bearer {} rises", W.word, W.bearerName);
				} else {
					g_siege.retry = 2.0f;
				}
				return;
			}
		}
		void SilenceWord(int a_w)
		{
			if (a_w < 0 || Silenced(a_w)) {
				return;
			}
			Write(gen::kEchoGlobWords, Read(gen::kEchoGlobWords) | (1 << a_w));
			const auto& W = gen::kWords[a_w];
			Forms::Play(gen::Sound::shout_learned);
			Forms::Play(gen::Sound::curse_lifted);
			Points::Add(5000);
			const int next = NextWord();
			Hud::Lore(std::string(W.word) + " IS SILENCED",
				std::string(W.silenced) + "  " + std::string(W.power) + (next >= 0 ? "  The next word stirs: " + std::string(gen::kWords[next].site) + "." : ""), 16.0f);
			logger::info("words: {} silenced", W.word);
			EndSiege("silenced");
			if (next >= 0) {
				g_siege.word = next, g_siege.told = false;
			}
		}

		float g_slow = 0;
		bool  g_loaded = false;  // the first pass after a load: the Barrow-Jarls' parts already won are given quietly
	}

	namespace Echo
	{
		void Reset()
		{
			for (auto& s : g_site) {
				s = Site{};
			}
			g_probe = {}, g_arrive = {}, g_leave = {};
			for (auto& al : g_allies) {
				if (auto a = al.h.get()) {
					a->Disable();
					a->SetDelete(true);
				}
			}
			g_allies.clear();
			g_loaded = false, g_clear = false, g_slow = 0;
			g_siege = Siege{};
		}

		int  WonderHeld() { return Glob(gen::kEchoGlobHeld) ? HeldWonder() : -1; }
		bool WordSilenced(int a_w) { return Glob(gen::kEchoGlobWords) && a_w >= 0 && a_w < kWordCount && Silenced(a_w); }
		bool CurseBroken() { return Glob(gen::kEchoGlobWords) && (Read(gen::kEchoGlobWords) & ((1 << kWordCount) - 1)) == (1 << kWordCount) - 1; }
		void OnBearerSlain()
		{
			if (g_siege.stage == 2) {
				SilenceWord(g_siege.word);
			}
		}
		bool IsAlly(RE::Actor* a_actor)
		{
			return a_actor && std::any_of(g_allies.begin(), g_allies.end(), [&](const Ally& al) { return al.h.get().get() == a_actor; });
		}
		void Allies(int a_kind, int a_count, float a_seconds, const RE::NiPoint3& a_at)
		{
			static constexpr std::array<std::pair<std::string_view, RE::FormID>, 2> kBase{ { { "Dawnguard.esm", 0x0045B9 },   // DLC1SoulCairnBonemanSummon: a bone warrior
				{ "Skyrim.esm", 0x0F608C } } };                                                                              // PlayerWolfSpirit: a spectral wolf
			auto* p = RE::PlayerCharacter::GetSingleton();
			const auto& [plugin, id] = kBase[std::clamp(a_kind, 0, 1)];
			auto* npc = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESNPC>(id, plugin);
			if (!p || !npc) {
				logger::warn("echo: no ally base for kind {}", a_kind);
				return;
			}
			static auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
			while (static_cast<int>(g_allies.size()) + a_count > 6) {  // (six at most: the oldest go first)
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

		bool StoneWaits(int a_town) { return a_town >= 0 && a_town < kTownCount && Curse::Freed(a_town) && !PageRead(a_town); }

		void Tick(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !p->Is3DLoaded() || !Glob(gen::kEchoGlobParts)) {
				return;
			}
			if ((g_slow -= a_delta) > 0) {
				return;
			}
			g_slow = 0.5f;
			TickAllies(0.5f);
			// the wonder in your hands: lost (dropped from a slot, a new character) - it rests on its pedestal again
			if (const int w = HeldWonder(); w >= 0) {
				const int gun = GunOfWonder(w);
				if (gun < 0 || !Gun::Owns(gun)) {
					SetHeld(-1);
				} else {
					Party::SetWonder(w);
				}
			} else {
				Party::SetWonder(-1);
			}
			// the Barrow-Jarls' parts: yours as soon as their city is freed (yours or the party's)
			for (int i = 0; i < kParts; ++i) {
				const auto& q = gen::kEchoParts[i];
				if (q.how == gen::kEchoBoss && q.town >= 0 && Curse::Freed(q.town) && !Held(i)) {
					Award(i, g_loaded);
				}
			}
			g_loaded = true;
			// the Echo Stone of the freed city you're in
			const int z = Curse::ZoneTown();
			if (StoneWaits(z)) {
				if (auto* st = StoneOf(z); st && p->GetPosition().GetDistance(st->GetPosition()) < gen::kEchoStoneTake + 60.0f) {
					TakePage(p, z);
				}
			}
			// the Reliquary
			if (p->GetParentCell() == HH()) {
				BuildReliquary(p);
			}
			// the hunt
			if (InTamriel(p)) {
				for (int i = 0; i < kParts; ++i) {
					if (Hunting(i) && gen::kEchoParts[i].how == gen::kEchoRelic) {
						TickRelic(p, i, 0.5f);
					}
				}
			}
			TickSky();
			TickWords(p, 0.5f);
		}

		void OnUndeadDeath(RE::Actor* a_actor)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!a_actor || !p || !InTamriel(p)) {
				return;
			}
			for (int i = 0; i < kParts; ++i) {
				const auto& q = gen::kEchoParts[i];
				if (q.how != gen::kEchoKill || !Hunting(i)) {
					continue;
				}
				const auto at = a_actor->GetPosition();
				if (std::hypot(at.x - q.x, at.y - q.y) > gen::kEchoKillRadius) {
					continue;
				}
				const int n = Read(q.killGlobal) + 1, need = gen::kEchoKillN[Diff()];
				Write(q.killGlobal, n);
				if (n >= need) {
					Award(i, true);
				} else if (n == 1 || n % 10 == 0) {
					Forms::Play(gen::Sound::quest_step);
				}
			}
		}

		bool OnActivate(RE::TESObjectREFR* a_ref)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!p || !base) {
				return false;
			}
			if (base == Forms::Ours(gen::kRelDoorIn)) {
				if (Read(gen::kEchoGlobPages) == 0 && Read(gen::kEchoGlobParts) == 0) {
					Hud::Banner("The door is sealed with the Tongues' sigil. Free a city and listen to its Echo Stone.", 5.0f);
					return true;
				}
				BuildReliquary(p);
				Go(p, g_arrive);
				Hud::Banner("THE RELIQUARY OF THE TONGUES", 3.5f);
				return true;
			}
			if (base == Forms::Ours(gen::kRelDoorOut)) {
				BuildReliquary(p);
				Go(p, g_leave);
				return true;
			}
			for (int w = 0; w < kWonders; ++w) {
				if (base == Forms::Ours(gen::kEchoWonders[w].pedestal)) {
					Forge(p, w);
					return true;
				}
			}
			return false;
		}

		bool Target(RE::PlayerCharacter* a_p, RE::NiPoint3& a_at, std::string& a_label)
		{
			if (!Glob(gen::kEchoGlobParts)) {
				return false;
			}
			// in the Reliquary: a wonder ready to forge; in the great hall: the sealed door
			if (a_p->GetParentCell() == HH()) {
				if (InRoom(a_p)) {
					for (int w = 0; w < kWonders; ++w) {
						if (Ready(w)) {
							const auto& s = gen::kRelPedestals[w];
							a_at = Room(s.x, s.y, s.z + 160.0f);
							a_label = "Forge " + std::string(gen::kEchoWonders[w].label);
							return true;
						}
					}
					return false;
				}
				if (Read(gen::kEchoGlobPages) != 0 || Read(gen::kEchoGlobParts) != 0) {
					a_at = P(gen::kRelDoor) + RE::NiPoint3{ 0, 0, 300.0f };
					a_label = "The Reliquary";
					return true;
				}
				return false;
			}
			const int z = Curse::ZoneTown();
			if (StoneWaits(z)) {
				if (auto* st = StoneOf(z)) {
					a_at = st->GetPosition() + RE::NiPoint3{ 0, 0, 330.0f };
					a_label = "Echo Stone - take the page";
					return true;
				}
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
			for (int i = 0; i < kParts; ++i) {
				if (Hunting(i)) {
					consider(SiteAt(i) + RE::NiPoint3{ 0, 0, 260.0f }, std::string(gen::kEchoParts[i].label) + " - " + std::string(gen::kEchoParts[i].site));
				}
			}
			if (const int wd = NextWord(); wd >= 0) {
				const auto& W = gen::kWords[wd];
				RE::NiPoint3 bat;
				consider(g_siege.stage == 2 && Director::ChampionPos(bat) ? bat : WordAt(wd) + RE::NiPoint3{ 0, 0, 300.0f },
					std::string(W.word) + " - " + (g_siege.stage == 2 ? std::string(W.bearerName) : std::string(W.site)));
			}
			for (int w = 0; w < kWonders; ++w) {
				if (Ready(w)) {
					consider(kHHFront + RE::NiPoint3{ 0, 0, 250.0f }, "High Hrothgar - forge " + std::string(gen::kEchoWonders[w].label));
					break;
				}
			}
			return best < FLT_MAX;
		}

		bool Strip(std::string& a_title, std::string& a_goal, float& a_frac)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !Glob(gen::kEchoGlobParts) || !InTamriel(p)) {
				return false;
			}
			if (const int wd = NextWord(); wd >= 0) {
				const auto& W = gen::kWords[wd];
				const float d = std::hypot(p->GetPosition().x - W.x, p->GetPosition().y - W.y);
				if (d < gen::kWordLeash) {
					a_title = std::string(W.word) + " - " + Upper(std::string(W.meaning));
					a_goal = g_siege.stage == 2   ? "Kill " + std::string(W.bearerName)
					         : g_siege.stage == 1 ? fmt::format("Wave {} of {}  -  {} left", g_siege.wave + 1, W.nWaves, g_siege.dead.size())
					         : Director::Fighting() ? "Go to " + std::string(W.site) + " - its dead will rise"
					                                : std::string(W.site) + " wakes only at night";
					a_frac = g_siege.stage == 2 ? 1.0f : static_cast<float>(g_siege.wave) / std::max(1, W.nWaves);
					return true;
				}
			}
			for (int i = 0; i < kParts; ++i) {
				if (!Hunting(i)) {
					continue;
				}
				const auto& q = gen::kEchoParts[i];
				const float d = std::hypot(p->GetPosition().x - q.x, p->GetPosition().y - q.y);
				if (q.how == gen::kEchoKill && d < gen::kEchoKillRadius) {
					const int n = Read(q.killGlobal), need = gen::kEchoKillN[Diff()];
					a_title = Upper(std::string(q.label));
					a_goal = Director::Fighting() ? fmt::format("Kill the dead at {}  {} / {}", q.site, n, need) : fmt::format("{} wakes at night  {} / {}", q.site, n, need);
					a_frac = static_cast<float>(n) / std::max(1, need);
					return true;
				}
				if (q.how == gen::kEchoRelic && d < gen::kEchoWake + 800.0f) {
					const auto& s = g_site[i];
					a_title = Upper(std::string(q.label));
					a_goal = s.stage == 2 ? "The reliquary is open - take the part" : s.stage == 1 ? fmt::format("Slay its guardians  -  {} left", s.guards.size()) : Director::Fighting() ? "Go to the reliquary - its guardians will rise" : "The reliquary wakes only at night";
					a_frac = s.stage == 2 ? 1.0f : 0.0f;
					return true;
				}
			}
			return false;
		}
	}
}
