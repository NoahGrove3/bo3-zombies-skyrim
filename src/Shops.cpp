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
			float               ang{ 0 };       // turned so far (radians)
			float               baseZ{ 0 };     // its node's height when first seen
			bool                based{ false };
			RE::NiNode*         node{ nullptr };  // (a fresh 3D after a reload is based again)
		};
		std::array<bool, gen::kLocations.size()> g_dressed{};
		struct Pulse { RE::ObjectRefHandle shrine; float t; };
		std::optional<Pulse>                     g_pulse;  // a freed shrine's wave (FreedPulse)
		std::vector<Spinner>                     g_spinners;
		RE::ObjectRefHandle                      g_orbAim;  // the shrine whose orb is in the crosshair (Activate opens it)
		bool                                     g_humming = false;  // the shrine's hum is playing (a load stops every loop)

		// The shrine's roll: it gathers power (Charging), every spell in the pool flickers over the crown, slowing
		// (Spinning), then one is held there as a ball of light until you take it (Offering).
		enum class Show { Idle, Charging, Spinning, Offering };
		struct ShowState
		{
			Show                phase{ Show::Idle };
			RE::ObjectRefHandle chest, display;
			int                 gun{ -1 }, shown{ -1 };
			float               t{ 0 }, step{ 0 };
			bool                bow{ false };  // the bow wheel (gun/shown: Bows indices)
			bool                prize{ false };  // (Noah: a class row's win rolls too) the reel turns the class's spells, it lands on pz
			Classes::Prize      pz;
		} g_show;
		bool g_choosing = false;  // a third spell won: which of your two it replaces (Choose)
		constexpr float kChargeTime = 0.35f;  // (a breath before the first beam)
		constexpr float kOrbZ = 300.0f;   // just over the altar's heart stone (its centre at z 175, its top ~266)
		constexpr float kOrbScale = 1.6f;

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

		float CompactOf(int a_town);  // a fixed station's layout scale (1 = full size)
		void  Dress(std::size_t a_index, RE::TESObjectREFR* a_main)
		{
			const float c = CompactOf(gen::kLocations[a_index].town);
			// (run on every scan: a piece that went missing - or was left disabled by an older stand - is put back)
			g_dressed[a_index] = true;
			const int obj = gen::kLocations[a_index].object;
			// (Noah: the freed look once its trials are done and every wave of that night is slain) the city's curse lifted
			// and no night being fought
			// (its last night ends the moment the Dragon Priest falls: freed is complete; the night's own fighting elsewhere
			// no longer hides it - Noah: the freed rune circle stood still on later nights)
			const bool freed = Curse::Freed(gen::kLocations[a_index].town);
			for (const auto& d : gen::kDress) {
				if (d.object != obj) {
					continue;
				}
				auto* base = Forms::Ours(d.formId);
				if (!base) {
					continue;
				}
				// (Noah: the stones weren't over the columns) the altar's own pieces stay where its model has them; only what
				// stands out round it closes in on a cramped stand
				const float cc = std::hypot(d.x, d.y) > 250.0f ? c : 1.0f;
				auto want = Local(a_main, d.x * cc, d.y * cc, d.z);
				if (d.formId == gen::kRelStone) {  // (the old Echo Stone pillar is gone: its page waits on the altar)
					RE::TES::GetSingleton()->ForEachReferenceInRange(a_main, 700.0f, [&](RE::TESObjectREFR* r) {
						if (r && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled()) {
							r->Disable();
							r->SetDelete(true);
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
					continue;
				}
				if (d.held && freed) {  // the cursed look goes (the heart stone turns to light: its lit twin takes its place)
					RE::TES::GetSingleton()->ForEachReferenceInRange(a_main, 400.0f, [&](RE::TESObjectREFR* r) {
						if (r && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled()) {
							r->Disable();
							r->SetDelete(true);
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
					continue;
				}
				if (d.freed && !freed) {
					continue;
				}
				if (d.freed && !d.light && d.z == 0.0f && (d.x != 0.0f || d.y != 0.0f)) {
					// (Noah: the freed altar's banners and braziers floated) stood on the ground where they are, not at the altar's height
					if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(want, land) && std::abs(land - want.z) < 160.0f) {
						want.z = land - 6.0f;
					}
				}
				RE::TESObjectREFR* found = nullptr;
				RE::TES::GetSingleton()->ForEachReferenceInRange(a_main, 400.0f, [&](RE::TESObjectREFR* r) {
					if (r && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled() && r->GetPosition().GetDistance(want) < 80.0f) {
						found = r;
						if (r->GetPosition().GetDistance(want) > 2.0f) {
							r->SetPosition(want);  // (the crystal and the rocks were raised: an older stand's follow)
						}
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
						ref->SetScale(d.scale * cc);
					}
					found = ref.get();
					if (d.freed) logger::info("shops: {} freed piece {:03X} placed at z {:.0f}", gen::kTowns[gen::kLocations[a_index].town], d.formId, want.z);
				}
				if (d.spin != 0) {
					const auto h = found->CreateRefHandle();
					if (std::none_of(g_spinners.begin(), g_spinners.end(), [&](const Spinner& s) { return s.ref == h; })) {
						g_spinners.push_back({ h, d.spin });
					}
				}
				if (obj >= 0 && gen::kObjects[obj].kind == 0 && !d.light) {
					// (Noah: a second one inside it) one of each of the altar's pieces: copies left where an older layout put
					// them go
					RE::TES::GetSingleton()->ForEachReferenceInRange(a_main, 450.0f, [&](RE::TESObjectREFR* r) {
						if (r && r != found && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled()) {
							r->Disable();
							r->SetDelete(true);
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
				}
			}
		}

		// what the shrine shows of a spell: the spell itself, as Skyrim's Magic menu shows it (its first effect's
		// display object - a fireball, a storm, a rune), else its element's emblem
		RE::TESBoundObject* SpellShow(int a_index, bool* a_own = nullptr)
		{
			if (auto* sp = Arcana::Spell(a_index)) {
				for (auto* e : sp->effects) {
					if (e && e->baseEffect && e->baseEffect->menuDispObject) {
						if (a_own) *a_own = false;
						return e->baseEffect->menuDispObject;
					}
				}
			}
			if (a_own) *a_own = true;
			return Forms::Ours(gen::kShowElement[Arcana::ElementOf(a_index)]);
		}
		// the heart stone's runes, k times their own light (its shader's emissive; the first value seen is its own)
		RE::ObjectRefHandle g_glowHeart;
		void HeartGlow(RE::TESObjectREFR* a_heart, float a_k)
		{
			static std::unordered_map<RE::BSLightingShaderProperty*, float> base;
			if (auto* root = a_heart ? a_heart->Get3D() : nullptr) {
				RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
					if (auto* lp = netimmerse_cast<RE::BSLightingShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get())) {
						auto [it, fresh] = base.try_emplace(lp, lp->emissiveMult);
						lp->emissiveMult = it->second * a_k;
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
		}
		// (Noah: the prompt only on the altar itself) standing on its platform: within its round floor, at its height
		bool OnAltarBase(RE::TESObjectREFR* a_altar)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!a_altar || !p) {
				return false;
			}
			const auto a = a_altar->GetPosition(), q = p->GetPosition();
			const float floor = a.z + 74.0f * a_altar->GetScale();  // (the platform's top over the altar's origin: tools/echoaltar.py)
			return std::hypot(q.x - a.x, q.y - a.y) < 215.0f * a_altar->GetScale() && q.z > floor - 70.0f && q.z < floor + 60.0f;
		}
		// (Noah: the real bows and spells roll by, with their names - not runes) what the roll shows over the heart stone:
		// the spell's own display model (Skyrim's Magic menu one) or its element's emblem; a bow, the bow itself, upright
		RE::TESBoundObject* ShowBase(int a_index, bool a_bow, bool* a_own)
		{
			if (a_bow) {
				*a_own = false;
				return a_index >= 0 && a_index < static_cast<int>(gen::kBowShowStat.size()) ? Forms::Ours(gen::kBowShowStat[a_index]) : nullptr;
			}
			return SpellShow(a_index, a_own);
		}
		float g_showPitch = 0.0f;
		void ShowModel(RE::TESBoundObject* a_base, float a_rise, float a_scale = -1.0f, float a_pitch = 0.0f)
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
			g_showPitch = a_pitch;
			ref->SetPosition(Local(chest.get(), 0, 0, kOrbZ * chest->GetScale() + a_rise));
			ref->SetAngle({ a_pitch, 0, chest->GetAngleZ() + RE::NI_PI / 2 });
			ref->SetScale(a_scale > 0 ? a_scale : kOrbScale);
			g_show.display = ref->CreateRefHandle();
		}
		void ShowAt(int a_index, float a_rise, bool a_final)
		{
			bool own = true;
			auto* m = ShowBase(a_index, g_show.bow, &own);
			if (g_show.bow) {
				ShowModel(m, a_rise, a_final ? 1.15f : 1.0f, -RE::NI_PI / 2);  // (a bow stood on end, its string toward you)
			} else {
				ShowModel(m, a_rise, own ? -1.0f : (a_final ? 1.8f : 1.6f));
			}
		}

		// Skyrim's own interface sounds (Skyrim.esm), for the altar's menu
		namespace UiSnd
		{
			constexpr RE::FormID kMove = 0x0006CC6F, kChoose = 0x000643FE, kFail = 0x0006D1C6, kLeave = 0x0003F2BF, kLand = 0x00068D83, kOpen = 0x0003F2BD;
		}
		void PlayUi(RE::FormID a_id, float a_vol = 1.0f)
		{
			auto* d = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(a_id);
			auto* am = RE::BSAudioManager::GetSingleton();
			RE::BSSoundHandle h;
			if (d && am && am->GetSoundHandle(h, static_cast<RE::BSISoundDescriptor*>(d))) {
				if (a_vol < 0.999f) h.SetVolume(a_vol);
				h.Play();
			}
		}

	}

	// the reel: where it stands at spin progress k (0..1) - eased so it slows to a stop on the target
	float Shops::ReelPos(float a_k) { const float r = 1.0f - std::clamp(a_k, 0.0f, 1.0f); return Shops::kReelPass * (1.0f - r * r * r); }
	int   Shops::ReelSpell(int a_target, float a_pos, int a_slot)
	{
		const int n = std::max(1, g_show.bow ? Bows::Count() : Arcana::Count());
		const int i = a_target - Shops::kReelPass + static_cast<int>(std::floor(a_pos)) + a_slot;
		return ((i % n) + n) % n;
	}

	namespace
	{
		int ReelFront(int a_target, float a_k) { return Shops::ReelSpell(a_target, Shops::ReelPos(a_k), 0); }

		std::vector<int> RollPool()
		{
			std::vector<int> pool;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (gen::kGuns[i].boxWeight > 0 && !gen::kGuns[i].retired) {
					pool.push_back(i);
				}
			}
			return pool;
		}

		// ---- the roll's beams, in the world (Noah: hyper-realistic): Skyrim's own lightning bolt beam from each column's
		// rune stone into the heart stone (UN_AltarBeamSpell: ShockBoltAim and its MAGShock02 impacts, harmless), cast
		// between markers set at the stones' moving 3D; each beam strikes with the bolt's crack and then keeps crackling
		// (re-cast) until the roll lands
		struct AltarParts
		{
			RE::TESObjectREFR* heart{ nullptr };
			std::array<RE::TESObjectREFR*, 6> rocks{};
		};
		AltarParts FindAltarParts(RE::TESObjectREFR* a_chest)
		{
			AltarParts o;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_chest, 420.0f, [&](RE::TESObjectREFR* r) {
				if (!r || r->IsDisabled() || r->IsDeleted()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* b = r->GetBaseObject();
				if (b && (b == Forms::Ours(gen::kHeartStone) || b == Forms::Ours(gen::kHeartStoneLit))) {
					o.heart = r;
				}
				for (int q = 0; q < 6; ++q) {
					if (b && b == Forms::Ours(gen::kPillarRock[q])) o.rocks[q] = r;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return o;
		}
		std::array<RE::ObjectRefHandle, 7> g_beamMk;  // a marker at each rune stone (0-5) and in the heart (6)
		std::array<float, 6>               g_beamRecast{};
		int                                g_beamStruck = 0;
		RE::TESObjectREFR* BeamMarker(int a_i, RE::TESObjectREFR* a_near, const RE::NiPoint3& a_at)
		{
			auto m = g_beamMk[a_i].get();
			if (!m || m->IsDeleted() || m->GetParentCell() != a_near->GetParentCell()) {
				auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000003B);
				auto  ref = xm ? a_near->PlaceObjectAtMe(xm, false) : nullptr;
				if (!ref) return nullptr;
				g_beamMk[a_i] = ref->CreateRefHandle();
				m = ref;
			}
			m->SetPosition(a_at);
			return m.get();
		}
		bool AltarBeam(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
		{
			static auto* spell = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(gen::kAltarBeamSpell, gen::kPlugin);
			auto* c = spell && a_from ? a_from->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
			if (!c) return false;
			c->CastSpellImmediate(spell, false, a_to, 1.0f, false, 0.0f, nullptr);
			return true;
		}
		void StrikeSound(const RE::NiPoint3& a_at, float a_vol)
		{
			auto* d = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x0003F206);  // MAGShockFFFireSD: the bolt's crack
			auto* am = RE::BSAudioManager::GetSingleton();
			RE::BSSoundHandle h;
			if (d && am && am->GetSoundHandle(h, static_cast<RE::BSISoundDescriptor*>(d))) {
				h.SetPosition(a_at);
				h.SetVolume(a_vol);
				h.Play();
			}
		}
		bool g_beams3D = false;  // the world's beams are flying (the HUD's drawn ones stand down)
		void TickBeams(RE::TESObjectREFR* a_chest, float a_delta, float a_k)
		{
			const int struck = std::clamp(static_cast<int>(a_k * 6.0f + 1e-4f), 0, 6);
			const auto parts = FindAltarParts(a_chest);
			if (!parts.heart) {
				g_beams3D = false;
				return;
			}
			const auto at = [](RE::TESObjectREFR* r) { return r->Get3D() ? r->Get3D()->world.translate : r->GetPosition(); };
			auto* heart = BeamMarker(6, a_chest, at(parts.heart));
			if (!heart) {
				g_beams3D = false;
				return;
			}
			bool any = false;
			for (int q = 0; q < 6; ++q) {
				if (!parts.rocks[q] || q >= struck) continue;
				const auto rp = at(parts.rocks[q]);
				auto*      mk = BeamMarker(q, a_chest, rp);
				if (!mk) continue;
				const bool fresh = q >= g_beamStruck;
				if (fresh) {
					StrikeSound(rp, 0.9f);
					g_beamRecast[q] = 0;
				}
				if ((g_beamRecast[q] -= a_delta) <= 0) {  // (a bolt lives about a third of a second: struck again, out of step with the others)
					g_beamRecast[q] = 0.22f + 0.05f * static_cast<float>(q % 3);
					any = AltarBeam(mk, heart) || any;
				} else {
					any = true;
				}
			}
			g_beamStruck = struck;
			g_beams3D = any || g_beams3D;
		}
		void EndBeams()
		{
			g_beamStruck = 0, g_beamRecast = {}, g_beams3D = false;
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
			case Show::Charging:
				if (g_show.t >= kChargeTime) {
					g_show.t = 0;
					g_show.step = 0;
					g_show.phase = Show::Spinning;
					Forms::PlayAt(gen::Sound::coffer_spin, chest.get());
				}
				break;
			case Show::Spinning:
				{
					// the reel (drawn by the HUD as names over the heart stone, src/Hud.cpp): kReelPass spells go by, slowing
					// like a wheel of fortune, while the six column stones beam into the heart one at a time; the sixth beam
					// connecting stops it on the one won, and only then does the spell (or bow) itself appear
					const float k = std::min(1.0f, g_show.t / gen::kShowSpin);
					const int   front = ReelFront(g_show.gun, k);
					g_show.step -= a_delta;
					if (front != g_show.shown && g_show.step <= 0) {
						g_show.shown = front;
						g_show.step = 0.045f;
						Forms::PlayAt(gen::Sound::coffer_spin, chest.get());
					}
					TickBeams(chest.get(), a_delta, k);  // (the stones strike the heart one by one: the sixth lands the roll)
					if (g_show.t >= gen::kShowSpin) {
						TickBeams(chest.get(), a_delta, 1.0f);
						EndBeams();
						g_show.t = 0;
						g_show.shown = g_show.gun;
						if (!g_show.prize) ShowAt(g_show.gun, gen::kShowRise, true);
						Forms::PlayAt(gen::Sound::coffer_land, chest.get());
						PlayUi(UiSnd::kLand, 0.8f);  // (a bright chord as it lands)
						if (g_show.prize) {
							Hud::Banner(g_show.pz.name + "  -  Activate the altar to take it", 4.0f);
							g_show.phase = Show::Offering;
							break;
						}
						if (g_show.bow) {
							Hud::Banner(std::string(Bows::Name(g_show.gun)) + "  -  Activate the altar to take it" + (Bows::Carried() >= 0 ? " (your " + std::string(Bows::Name(Bows::Carried())) + " goes back)" : std::string()), 4.0f);
							g_show.phase = Show::Offering;
							break;
						}
						Hud::Banner((Arcana::TierOf(g_show.gun) >= 3 ? std::string("LEGENDARY: ") : std::string()) + std::string(Arcana::Name(g_show.gun)) + "  -  Activate the shrine to take it", 4.0f);
						if (Arcana::TierOf(g_show.gun) >= 3) {
							Forms::PlayAt(gen::Sound::champ_arrive, chest.get());  // (a legendary spell announces itself)
						}
						g_show.phase = Show::Offering;
					}
					break;
				}
			case Show::Offering:
				if (auto d = g_show.display.get()) {
					// (a loaded static never turns by its angle: its node is turned instead - slowly round, upright)
					static float spin = 0;
					spin += a_delta * 1.2f;
					if (auto* node = d->Get3D()) {
						node->local.rotate.SetEulerAnglesXYZ(g_showPitch, 0.0f, -(chest->GetAngleZ() + RE::NI_PI / 2 + spin));
						RE::NiUpdateData ud;
						node->Update(ud);
					}
					d->SetPosition(Local(chest.get(), 0, 0, kOrbZ * chest->GetScale() + gen::kShowRise + 4.0f * std::sin(g_show.t * 3.0f)));
				}
				if (g_choosing) {
					g_show.t = std::min(g_show.t, gen::kShowOffer * 0.5f);  // (it waits while you choose)
				}
				if (g_show.t >= gen::kShowOffer) {
					Remove(g_show.display);
					Hud::Banner("The shrine took it back.", 2.5f);
					g_show.phase = Show::Idle;
				}
				break;
			default: break;
			}
		}

		int ActiveBoxTown()
		{
			const int v = Save::Get(gen::kAV_box);
			const int town = ((v >> 8) & 0x1F) - 1;
			return town >= 0 ? town : 0;  // Riverwood first
		}
		// ---- Brew alley layout (sheets: layout, locations) ----
		// Per town: find open, flat, walkable ground near the map marker for the whole alley, clear out
		// anything we placed there under an older layout, then place every piece on its slot.
		struct TownPlan
		{
			enum class State { Unknown, Searching, Ready } state{ State::Unknown };
			RE::NiPoint3 center{};
			float        yaw{ 0 };
			int          candidate{ 0 };
			float        top{ 0 };        // the stand's floor: every machine stands at this height
			bool         topSet{ false };
			float        compact{ 1.0f };  // fixed stations in city streets: the layout drawn in
		};
		std::array<TownPlan, gen::kTowns.size()> g_towns{};
		std::optional<float> GroundTop(RE::TESObjectREFR* a_anchor, const TownPlan& a_plan, float* a_low = nullptr);
		RE::ObjectRefHandle                       g_probe;

		// Stands placed under the outskirts layout (v2): one bit per town in UN_StandLayout (saved with the game).
		void MarkLayout(int a_town)
		{
			if (auto* g = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(gen::kStandLayoutGlobal, gen::kPlugin)) {
				g->value = static_cast<float>(static_cast<int>(g->value) | (1 << a_town));
			}
		}

		float CompactOf(int a_town) { return a_town >= 0 && a_town < static_cast<int>(g_towns.size()) ? g_towns[a_town].compact : 1.0f; }

		// The town's own reference in Skyrim (a walled city's gate outside, a town's map marker): where its world-map
		// icon goes and how the town is recognised.
		RE::TESObjectREFR* MapAnchor(int a_town)
		{
			for (const auto& l : gen::kLocations) {
				if (l.town == a_town) {
					return RE::TESForm::LookupByID<RE::TESObjectREFR>(l.anchor);
				}
			}
			return nullptr;
		}

		// Where the station is built from. A fixed station (sheet: stations) is anchored on a marker of ours set at
		// its spot the first time you come near it (inside a walled city: in the city's own world); the marker stays
		// in the save and is found again next time.
		std::array<RE::ObjectRefHandle, gen::kTowns.size()> g_anchors{};
		RE::TESObjectREFR* TownAnchor(int a_town)
		{
			const auto& st = gen::kStations[a_town];
			if (!st.fixed) {
				return MapAnchor(a_town);
			}
			if (auto a = g_anchors[a_town].get(); a && !a->IsDeleted()) {
				return a.get();
			}
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* ws = p ? p->GetWorldspace() : nullptr;
			const RE::NiPoint3 at{ st.x, st.y, st.z };
			if (!ws || ws->GetFormID() != st.world || p->GetPosition().GetDistance(at) > 6000.0f) {
				return nullptr;
			}
			auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);  // XMarker
			if (!xm) {
				return nullptr;
			}
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(p, 7000.0f, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == xm && (r->GetFormID() >> 24) == 0xFF && !r->IsDeleted() && r->GetPosition().GetDistance(at) < 5.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!found) {
				auto ref = p->PlaceObjectAtMe(xm, true);
				if (!ref) {
					return nullptr;
				}
				ref->SetPosition(at);
				found = ref.get();
				logger::info("shops: {} station anchor set at ({:.0f},{:.0f},{:.0f})", gen::kTowns[a_town], at.x, at.y, at.z);
			}
			g_anchors[a_town] = found->CreateRefHandle();
			return found;
		}

		RE::NiPoint3 SlotPos(const TownPlan& a_plan, int a_slot)
		{
			const auto& s = gen::kSlots[a_slot];
			const float a = a_plan.yaw, x = s.x * a_plan.compact, y = s.y * a_plan.compact;
			return { a_plan.center.x + x * std::cos(a) + y * std::sin(a), a_plan.center.y - x * std::sin(a) + y * std::cos(a), a_plan.center.z };
		}
		float SlotFacing(const TownPlan& a_plan, int a_slot) { return a_plan.yaw + gen::kSlots[a_slot].faceDeg * RE::NI_PI / 180.0f; }

		bool IsOurBase(RE::TESForm* a_base)
		{
			static std::unordered_set<RE::TESForm*> ours;
			if (ours.empty()) {
				for (int i = 0; i < static_cast<int>(gen::kObjects.size()); ++i) {
					ours.insert(Forms::Object(i));
				}
				for (const auto& d : gen::kDress) {
					ours.insert(Forms::Ours(d.formId));
				}
				ours.erase(nullptr);
			}
			return a_base && ours.contains(a_base);
		}

		// Highest solid surface (statics, terrain) under (x,y), casting down from well above a_top.
		bool IsOurBase(RE::TESForm* a_base);
		std::optional<float> FloorZ(RE::TESObjectREFR* a_anchor, const RE::NiPoint3& a_xy, float a_top)
		{
			auto* cell = a_anchor ? a_anchor->GetParentCell() : nullptr;
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return std::nullopt;
			}
			const float s = RE::bhkWorld::GetWorldScale();
			// (Noah: the altar floated higher each visit) the ground under our own pieces: a ray that lands on the altar
			// (its platform, its columns) or anything else of ours goes on down through it
			float top = a_top + 300.0f;
			for (int pass = 0; pass < 6; ++pass) {
				const RE::NiPoint3 from{ a_xy.x, a_xy.y, top }, to{ a_xy.x, a_xy.y, a_top - 2000.0f };
				RE::bhkPickData pick;
				pick.rayInput.from = RE::hkVector4(from * s);
				pick.rayInput.to = RE::hkVector4(to * s);
				pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
				{
					RE::BSReadLockGuard lock(world->worldLock);
					world->PickObject(pick);
				}
				if (!pick.rayOutput.HasHit()) {
					return std::nullopt;
				}
				const float z = from.z + (to.z - from.z) * pick.rayOutput.hitFraction;
				auto* hitRef = pick.rayOutput.rootCollidable ? RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable) : nullptr;
				// (Noah: Whiterun's altar rose three times while he stood by it) a person, a creature, a tree's crown or a
				// loose thing is not the floor either: the ray goes on down through them too
				auto* hb = hitRef ? hitRef->GetBaseObject() : nullptr;
				const bool  notFloor = hitRef && (IsOurBase(hb) || hitRef->As<RE::Actor>() || (hb && (hb->Is(RE::FormType::Tree) || hb->Is(RE::FormType::Flora) || hb->Is(RE::FormType::Misc) || hb->Is(RE::FormType::Weapon) || hb->Is(RE::FormType::Armor) || hb->Is(RE::FormType::Projectile))));
				if (notFloor) {
					top = z - 2.0f;
					continue;
				}
				return z;
			}
			return std::nullopt;
		}

		// Nearest walkable point (navmesh) to a_at, via an invisible marker; nullopt if there is none close.
		std::optional<RE::NiPoint3> Snap(RE::TESObjectREFR* a_anchor, const RE::NiPoint3& a_at)
		{
			auto probe = g_probe.get();
			if (!probe) {
				auto* marker = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);  // XMarker
				auto  ref = marker ? a_anchor->PlaceObjectAtMe(marker, false) : nullptr;
				if (!ref) {
					return std::nullopt;
				}
				g_probe = ref->CreateRefHandle();
				probe = ref;
			}
			RE::NiPoint3 at = a_at;
			float        land = 0;
			if (RE::TES::GetSingleton()->GetLandHeight(at, land)) {
				at.z = land;
			}
			probe->SetPosition(at);
			probe->MoveToNearestNavmesh();
			auto got = probe->GetPosition();
			const float dxy = std::hypot(got.x - at.x, got.y - at.y);
			if (dxy > gen::kPlanSnap) {
				return std::nullopt;
			}
			// Navmesh/land heights sit under town paving and bridges: stand on what the physics world says is there.
			// only just above the navmesh: a roof or awning overhead must not count as the floor
			if (auto z = FloorZ(a_anchor, got, got.z - 220.0f); z && *z > got.z - 150.0f && *z < got.z + 80.0f) {
				got.z = *z;
			}
			return got;
		}

		struct Best
		{
			float    score{ FLT_MAX };
			TownPlan plan;
		};
		std::array<Best, gen::kTowns.size()> g_best{};

		// What stands around a town, gathered once per search: doors, buildings/walls/cliffs (big) and fences, rocks,
		// carts, trees (clutter). The stand goes out where none of them are close: open ground just outside town.
		struct Obstacle
		{
			float x, y, r;
			int   kind;  // 0 door, 1 big, 2 clutter
		};
		std::array<std::vector<Obstacle>, gen::kTowns.size()> g_obstacles{};
		constexpr float kStandRadius = 370.0f;  // half-diagonal of the platform
		constexpr float kStandLift = 18.0f;  // the dais floor above the highest ground under it (its skirt reaches 100 down)
		constexpr float kAltarSink = kStandLift + 6.0f + 40.0f;  // the altar's own origin under the stand's floor line (its dais flush with the ground)

		void GatherObstacles(int a_town, RE::TESObjectREFR* a_anchor)
		{
			auto& list = g_obstacles[a_town];
			list.clear();
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				auto* base = r ? r->GetBaseObject() : nullptr;
				if (!base || r->IsDisabled() || r->IsDeleted() || IsOurBase(base) || r->As<RE::Actor>()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto p = r->GetPosition();
				const auto t = base->GetFormType();
				if (t == RE::FormType::Door) {
					list.push_back({ p.x, p.y, 0.0f, 0 });
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (t != RE::FormType::Static && t != RE::FormType::MovableStatic && t != RE::FormType::Tree && t != RE::FormType::Furniture &&
					t != RE::FormType::Container && t != RE::FormType::Activator && t != RE::FormType::Light) {
					return RE::BSContainer::ForEachResult::kContinue;  // grass, flora, items: tidied off the footprint anyway
				}
				auto* bo = base->As<RE::TESBoundObject>();
				if (!bo) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto& b = bo->boundData;
				const float sc = std::max(0.1f, r->GetScale());
				const float wx = (b.boundMax.x - b.boundMin.x) * sc, wy = (b.boundMax.y - b.boundMin.y) * sc, wz = (b.boundMax.z - b.boundMin.z) * sc;
				const float horiz = std::max(wx, wy), size = std::max(horiz, wz);
				if (size < 70.0f) {
					return RE::BSContainer::ForEachResult::kContinue;  // pebbles, cups, small clutter
				}
				if (t == RE::FormType::Tree) {
					list.push_back({ p.x, p.y, std::min(horiz * 0.15f, 40.0f) - 120.0f, 2 });  // just the trunk (the canopy is the sky test's job); forests are fine
				} else {
					list.push_back({ p.x, p.y, horiz * 0.5f, size > 450.0f ? 1 : 2 });
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			logger::info("shops: {} has {} doors/buildings/clutter around it", gen::kTowns[a_town], list.size());
		}

		// How much the surroundings crowd a stand centred at a_at: 0 = open ground, nothing near.
		float Crowding(int a_town, const RE::NiPoint3& a_at, int* a_doors = nullptr)
		{
			float pen = 0;
			int   doors = 0;
			for (const auto& o : g_obstacles[a_town]) {
				const float d = std::hypot(o.x - a_at.x, o.y - a_at.y);
				const float need = o.kind == 0 ? gen::kPlanDoor : kStandRadius + o.r + (o.kind == 1 ? gen::kPlanBuilding : gen::kPlanClutter);
				if (d < need) {
					pen += (need - d) * (o.kind == 0 ? 2.0f : o.kind == 1 ? 1.5f : 1.0f);
					doors += o.kind == 0;
				}
			}
			if (a_doors) {
				*a_doors = doors;
			}
			return pen;
		}

		// Score a candidate alley (lower is better); 0 failures + flat + no doors near = accept immediately.
		// Clear sky over the whole stand: cast down from high above on a grid; trees, porches and roofs stand
		// out as hits far above the ground and rule the spot out.
		// a_overhead > 0 (an existing stand, whose own wall and machines are in the way, up to ~350): only count things that far above the floor.
		bool Obstructed(RE::TESObjectREFR* a_anchor, const TownPlan& a_plan, float a_overhead = 0.0f)
		{
			float glo = FLT_MAX, ghi = -FLT_MAX;
			constexpr int NX = 12, NY = 7;  // ~60 units apart: fences and posts can't slip between the rays
			for (int gx = 0; gx < NX; ++gx) {
				for (int gy = 0; gy < NY; ++gy) {
					const float x = -gen::kStandHalfX + gx * (2 * gen::kStandHalfX / (NX - 1)), y = gen::kStandFrontY + gy * ((gen::kStandBackY - gen::kStandFrontY) / (NY - 1));
					const float a = a_plan.yaw;
					const RE::NiPoint3 at{ a_plan.center.x + x * std::cos(a) + y * std::sin(a), a_plan.center.y - x * std::sin(a) + y * std::cos(a), a_plan.center.z };
					if (auto z = FloorZ(a_anchor, at, a_plan.center.z + 700.0f)) {
						glo = std::min(glo, *z), ghi = std::max(ghi, *z);
					}
				}
			}
			if (a_overhead > 0) {
				return ghi > a_plan.top + a_overhead;
			}
			return ghi > glo && ghi - glo > 35.0f;
		}

		bool TryCandidate(int a_town, RE::TESObjectREFR* a_anchor, int a_index)
		{
			const int perDist = gen::kPlanAngles * static_cast<int>(gen::kPlanYaw.size());
			const int di = a_index / perDist, rest = a_index % perDist;
			const int ai = rest / static_cast<int>(gen::kPlanYaw.size()), yi = rest % static_cast<int>(gen::kPlanYaw.size());
			const float ang = ai * 2.0f * RE::NI_PI / gen::kPlanAngles;
			const auto  a = a_anchor->GetPosition();
			TownPlan    plan;
			plan.center = { a.x + std::sin(ang) * gen::kPlanDist[di], a.y + std::cos(ang) * gen::kPlanDist[di], a.z };
			plan.yaw = gen::kPlanYaw[yi] * RE::NI_PI / 180.0f;
			float lo = FLT_MAX, hi = -FLT_MAX;
			int   fails = 0, doors = 0;
			const float crowd = Crowding(a_town, plan.center, &doors);
			for (const auto& l : gen::kLocations) {
				if (l.town != a_town) {
					continue;
				}
				const auto want = SlotPos(plan, l.slot);
				if (auto p = Snap(a_anchor, want)) {
					lo = std::min(lo, p->z), hi = std::max(hi, p->z);
				} else {
					++fails;
				}
			}
			int blocked = Obstructed(a_anchor, plan) ? 1 : 0;
			float slope = 0;
			{
				float low = 0;
				if (auto g = GroundTop(a_anchor, plan, &low); !g || (*g - kStandLift) - low > gen::kStandMaxSlope) {
					fails += 3;  // the ground drops away under part of the stand (or isn't there): never acceptable
				} else {
					slope = std::max(0.0f, *g - kStandLift - low);
				}
			}
			const float spread = hi > lo ? hi - lo : 0;
			auto        c = Snap(a_anchor, plan.center);
			plan.center.z = c ? c->z : (lo < hi ? (lo + hi) * 0.5f : a.z);
			plan.state = TownPlan::State::Ready;
			const float score = fails * 1.0e7f + blocked * 1.0e5f + std::min(crowd * 2.0f, 5.0e4f) + std::max(0.0f, slope - gen::kPlanFlat) * 20.0f + std::max(0.0f, spread - gen::kPlanHeight) * 5.0f + gen::kPlanDist[di] * 0.05f;
			if (score < g_best[a_town].score) {
				g_best[a_town] = { score, plan };
			}
			if (fails == 0 && !blocked && crowd <= 0.0f && slope <= gen::kPlanFlat && spread <= gen::kPlanHeight) {
				g_towns[a_town] = plan;
				MarkLayout(a_town);
				logger::info("shops: {} alley at ({:.0f},{:.0f}) yaw {:.0f} (candidate {})", gen::kTowns[a_town], plan.center.x, plan.center.y, gen::kPlanYaw[yi], a_index);
				return true;
			}
			return false;
		}

		void Sweep(int a_town, RE::TESObjectREFR* a_anchor)
		{
			int n = 0;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				if (r && !r->IsDeleted() && IsOurBase(r->GetBaseObject())) {
					r->Disable();
					r->SetDelete(true);
					++n;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
				if (gen::kLocations[i].town == a_town) {
					g_placed[i] = {};
					g_dressed[i] = false;
				}
			}
			std::erase_if(g_spinners, [](const Spinner& s) { return !s.ref.get(); });
			logger::info("shops: cleared {} old pieces in {}", n, gen::kTowns[a_town]);
		}

		// Existing stand from an earlier session? The Coffer with the stand under it marks it; older layouts
		// (separate machines on the ground) are swept and rebuilt as a stand.
		void ClearSite(int a_town, RE::TESObjectREFR* a_anchor);
		bool AdoptExisting(int a_town, RE::TESObjectREFR* a_anchor)
		{
			RE::TESForm* coffer = nullptr;
			int          slot = -1;
			for (const auto& l : gen::kLocations) {
				if (l.town == a_town && gen::kObjects[l.object].kind == 0 && !gen::kObjects[l.object].retired) {
					coffer = Forms::Object(l.object), slot = l.slot;
				}
			}
			auto* stand = Forms::Ours(gen::kStandStat);
			if (!coffer || !stand || slot < 0) {
				return false;
			}
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				if (r && !r->IsDeleted() && r->GetBaseObject() == coffer) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!found) {
				return false;
			}
			bool hasStand = false;
			RE::TES::GetSingleton()->ForEachReferenceInRange(found, 300.0f, [&](RE::TESObjectREFR* r) {
				if (r && !r->IsDeleted() && r->GetBaseObject() == stand) {
					hasStand = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!hasStand) {
				logger::info("shops: {} has the old ground layout, rebuilding as a stand", gen::kTowns[a_town]);
				return false;
			}
			TownPlan plan;
			plan.yaw = found->GetAngleZ() - gen::kSlots[slot].faceDeg * RE::NI_PI / 180.0f;
			const auto& s = gen::kSlots[slot];
			const auto  p = found->GetPosition();
			plan.center = { p.x - (s.x * std::cos(plan.yaw) + s.y * std::sin(plan.yaw)), p.y - (-s.x * std::sin(plan.yaw) + s.y * std::cos(plan.yaw)), p.z };
			// the floor line is kAltarSink over the altar (measuring it from the altar itself lifted the whole stand
			// 24 every load - Noah's rising altar)
			plan.top = p.z + kAltarSink, plan.topSet = true;
			plan.state = TownPlan::State::Ready;
			plan.center.z = plan.top;
			float low = 0;
			auto g = GroundTop(a_anchor, plan, &low);
			if (g && std::abs(*g - (p.z + kStandLift + 6.0f)) <= 8.0f) {
				// an altar from before the sink: the stand is right, only the altar (and what it carries) goes down
				plan.top = plan.center.z = p.z + kStandLift + 6.0f;
				auto q = p;
				q.z = plan.top - kAltarSink;
				found->SetPosition(q);
				logger::info("shops: {} altar sunk flush with the ground", gen::kTowns[a_town]);
			}
			if (g && std::abs(*g - plan.top) > 8.0f && *g - plan.top <= kStandLift + 12.0f && plan.top - *g <= 60.0f) {
				// a stand from before the dais's height changed: lift or lower it and everything on it in place
				const float dz = *g - plan.top;
				int         moved = 0;
				RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
					if (r && !r->IsDeleted() && IsOurBase(r->GetBaseObject()) && std::hypot(r->GetPosition().x - plan.center.x, r->GetPosition().y - plan.center.y) < 600.0f) {
						auto q = r->GetPosition();
						q.z += dz;
						r->SetPosition(q);
						++moved;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				plan.top = plan.center.z = plan.top + dz;
				logger::info("shops: {} stand raised {:.0f} above the grass ({} pieces)", gen::kTowns[a_town], dz, moved);
				g = plan.top;
			}
			if (!g || std::abs(*g - plan.top) > 30.0f || (*g - kStandLift) - low > gen::kStandMaxSlope) {
				logger::info("shops: {} stand floats {:.0f} off the ground, rebuilding", gen::kTowns[a_town], g ? plan.top - *g : 999.0f);
				return false;
			}
			if (auto* v2 = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(gen::kStandLayoutGlobal, gen::kPlugin); v2 && !((static_cast<int>(v2->value) >> a_town) & 1)) {
				logger::info("shops: {} stand predates the outskirts layout, moving it", gen::kTowns[a_town]);
				return false;
			}
			if (g && *g - plan.top > 8.0f) {
				logger::info("shops: {} stand sits {:.0f} into the ground, rebuilding", gen::kTowns[a_town], *g - plan.top);
				return false;
			}
			if (Obstructed(a_anchor, plan, 440.0f)) {
				logger::info("shops: {} stand is boxed in by trees or buildings, moving it", gen::kTowns[a_town]);
				return false;
			}
			GatherObstacles(a_town, a_anchor);
			if (const float c = Crowding(a_town, plan.center); c > 0.0f || (g && (*g - kStandLift) - low > gen::kPlanFlat + 10.0f)) {
				logger::info("shops: {} stand is in town (crowding {:.0f}), moving it out to open ground", gen::kTowns[a_town], c);
				return false;
			}
			g_towns[a_town] = plan;
			ClearSite(a_town, a_anchor);
			// machines that were retired (wall-buys, the old ammo cache) leave the stand
			int gone = 0;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				for (std::size_t i = 0; r && i < gen::kObjects.size(); ++i) {
					if (gen::kObjects[i].retired && r->GetBaseObject() == Forms::Object(static_cast<int>(i)) && !r->IsDisabled()) {
						r->Disable();
						r->SetDelete(true);
						++gone;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (gone) {
				logger::info("shops: removed {} retired machines from the {} stand", gone, gen::kTowns[a_town]);
			}
			return true;
		}

		// Tidy the site: small statics, plants and furniture standing on the stand's footprint are hidden (never
		// buildings, doors, containers, actors, lights, quest-persistent objects or our own pieces).
		void ClearSite(int a_town, RE::TESObjectREFR* a_anchor)
		{
			const auto& plan = g_towns[a_town];
			int         n = 0;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				auto* base = r ? r->GetBaseObject() : nullptr;
				if (!base || r->IsDisabled() || r->IsDeleted() || IsOurBase(base) || (r->GetFormFlags() & RE::TESForm::RecordFlags::kPersistent)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto t = base->GetFormType();
				if (t != RE::FormType::Static && t != RE::FormType::MovableStatic && t != RE::FormType::Tree && t != RE::FormType::Flora &&
					t != RE::FormType::Furniture && t != RE::FormType::Misc && t != RE::FormType::Grass) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* bo = base->As<RE::TESBoundObject>();
				if (bo) {
					const auto& b = bo->boundData;
					const int   size = std::max({ b.boundMax.x - b.boundMin.x, b.boundMax.y - b.boundMin.y, b.boundMax.z - b.boundMin.z }) * static_cast<int>(std::max(1.0f, r->GetScale()));
					if (size > 450) {
						return RE::BSContainer::ForEachResult::kContinue;  // something big (a building, a wall): leave it
					}
				}
				// in stand space?
				const auto  p = r->GetPosition();
				const float dx = p.x - plan.center.x, dy = p.y - plan.center.y, a = plan.yaw;
				const float lx = dx * std::cos(a) - dy * std::sin(a), ly = dx * std::sin(a) + dy * std::cos(a);
				// the footprint, plus the approach in front of it (bushes there hide the machines from the path)
				if (std::abs(lx) <= gen::kStandHalfX + 160.0f && ly >= gen::kStandFrontY - 420.0f && ly <= gen::kStandBackY + 80.0f && std::abs(p.z - plan.top) < 300.0f) {
					r->Disable();
					++n;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (n) {
				logger::info("shops: cleared {} bits of clutter off the {} stand", n, gen::kTowns[a_town]);
			}
		}

		// The stand's floor: the ground under its footprint, measured from the walkable (navmesh) height so roofs,
		// branches and awnings above are never mistaken for ground. Highest ground point wins (no corner digs in).
		std::optional<float> GroundTop(RE::TESObjectREFR* a_anchor, const TownPlan& a_plan, float* a_low)
		{
			float ref = a_plan.center.z;
			if (auto c = Snap(a_anchor, a_plan.center)) {
				ref = c->z;
			}
			const float hx = gen::kStandHalfX * a_plan.compact, fy = gen::kStandFrontY * a_plan.compact, by = gen::kStandBackY * a_plan.compact;
			float hi = -FLT_MAX, lo = FLT_MAX;
			int   got = 0;
			for (int gx = 0; gx < 5; ++gx) {
				for (int gy = 0; gy < 4; ++gy) {
					const float x = -hx + gx * (2 * hx / 4), y = fy + gy * ((by - fy) / 3), a = a_plan.yaw;
					const RE::NiPoint3 at{ a_plan.center.x + x * std::cos(a) + y * std::sin(a), a_plan.center.y - x * std::sin(a) + y * std::cos(a), ref };
					if (auto z = FloorZ(a_anchor, at, ref - 220.0f); z && *z > ref - 300.0f && *z < ref + 80.0f) {
						hi = std::max(hi, *z), lo = std::min(lo, *z), ++got;
					}
				}
			}
			if (got < 16) {
				return std::nullopt;  // the ground drops away (ledge, cliff, water) under part of the footprint
			}
			if (hi - lo > gen::kStandMaxSlope + 40.0f) {
				return std::nullopt;  // too steep: one side buried deep or the other hanging
			}
			if (a_low) {
				*a_low = lo;
			}
			// (Noah: the altar should sit flush, easy to step on) its floor on the middle of the ground under it: the
			// stepped platform cuts into the high side and its lowest step meets the low side
			std::vector<float> zs;
			for (int gx = 0; gx < 5; ++gx) {
				for (int gy = 0; gy < 4; ++gy) {
					const float x = -hx + gx * (2 * hx / 4), y = fy + gy * ((by - fy) / 3), a = a_plan.yaw;
					const RE::NiPoint3 at{ a_plan.center.x + x * std::cos(a) + y * std::sin(a), a_plan.center.y - x * std::sin(a) + y * std::cos(a), ref };
					if (auto z = FloorZ(a_anchor, at, ref - 220.0f); z && *z > ref - 300.0f && *z < ref + 80.0f) zs.push_back(*z);
				}
			}
			std::sort(zs.begin(), zs.end());
			const float mid = zs.empty() ? hi : zs[zs.size() / 2];
			return mid + kStandLift;
		}

		void SettleStand(int a_town, RE::TESObjectREFR* a_anchor)
		{
			auto& plan = g_towns[a_town];
			if (plan.topSet) {
				return;
			}
			auto top = GroundTop(a_anchor, plan);
			if (!top) {  // no clean footprint: stand on whatever is under its centre, never at the anchor's height
				if (auto c = Snap(a_anchor, plan.center)) {
					top = c->z + kStandLift;
				} else if (auto z = FloorZ(a_anchor, plan.center, plan.center.z + 3000.0f)) {
					top = *z + kStandLift;
				} else {
					float land = 0;
					RE::NiPoint3 at = plan.center;
					if (RE::TES::GetSingleton()->GetLandHeight(at, land)) {
						top = land + kStandLift;
					}
				}
			}
			plan.top = top ? *top : plan.center.z;
			plan.topSet = true;
			logger::info("shops: {} stand floor at z {:.0f}", gen::kTowns[a_town], plan.top);
			ClearSite(a_town, a_anchor);
		}


		void PlaceIfMissing(std::size_t a_index)
		{
			const auto& loc = gen::kLocations[a_index];
			if (auto ref = g_placed[a_index].get(); ref && !ref->IsDeleted()) {
				static int n = 0;
				if (++n % 8 == 0 && ref->Is3DLoaded()) {
					Dress(a_index, ref.get());  // now and then: the crystal and the rocks are still there (put back if not)
					// (the in-place lift for sunk stations is gone: it caught people and trees over Whiterun's altar and
					// floated it up - the station check on load moves a floating or sunk stand instead)
				}
				return;
			}
			const auto& plan = g_towns[loc.town];
			auto*       anchor = TownAnchor(loc.town);
			auto*       base = Forms::Object(loc.object);
			if (plan.state != TownPlan::State::Ready || !anchor || !base || gen::kObjects[loc.object].retired) {
				return;
			}
			SettleStand(loc.town, anchor);
			auto want = SlotPos(plan, loc.slot);
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && std::hypot(r->GetPosition().x - want.x, r->GetPosition().y - want.y) < 80.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			// the shrine brings its own dais - (Noah: the cone under it showed, the altar rose off the floor) sunk so its
			// cobbled skirt is all below the ground and the dais floor lies flush with it
			const float floorZ = gen::kObjects[loc.object].kind == 0 ? plan.top - kAltarSink : plan.top;
			want.z = floorZ;
			if (!found) {
				auto ref = anchor->PlaceObjectAtMe(base, false);
				if (!ref) {
					return;
				}
				ref->SetScale(gen::kObjects[loc.object].scale);
				ref->SetPosition(want);
				ref->SetAngle({ 0, 0, SlotFacing(plan, loc.slot) });
				found = ref.get();
				logger::info("shops: placed {} in {} at ({:.0f},{:.0f},{:.0f})", gen::kObjects[loc.object].id, gen::kTowns[loc.town], want.x, want.y, want.z);
			} else if (auto pos = found->GetPosition(); std::abs(pos.z - floorZ) > 4.0f) {
				pos.z = floorZ;  // keep every machine on the stand's floor
				found->SetPosition(pos);
			}
			if (std::abs(found->GetScale() - gen::kObjects[loc.object].scale) > 0.01f) {
				found->SetScale(gen::kObjects[loc.object].scale);  // (the Coffer grew)
			}
			g_placed[a_index] = found->CreateRefHandle();
			if (gen::kObjects[loc.object].kind == 0) {
				// (Noah: a second altar stood buried in the Echo Stone) one altar a town: any other copy of it - left behind
				// by an older layout - goes, with everything dressed on it
				std::vector<RE::TESObjectREFR*> extra;
				RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, gen::kPlanSweep, [&](RE::TESObjectREFR* r) {
					if (r && r != found && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled()) {
						extra.push_back(r);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				for (auto* x : extra) {
					int gone = 0;
					RE::TES::GetSingleton()->ForEachReferenceInRange(x, 340.0f, [&](RE::TESObjectREFR* r) {
						if (!r || r->IsDeleted() || r->IsDisabled() || r->GetPosition().GetDistance(found->GetPosition()) < 200.0f) {
							return RE::BSContainer::ForEachResult::kContinue;
						}
						for (const auto& d : gen::kDress) {
							if (d.object == loc.object && !d.light && r->GetBaseObject() == Forms::Ours(d.formId)) {
								r->Disable();
								r->SetDelete(true);
								++gone;
								break;
							}
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
					const auto xp = x->GetPosition();
					x->Disable();
					x->SetDelete(true);
					logger::info("shops: a second altar in {} at ({:.0f},{:.0f},{:.0f}) taken away with {} of its pieces", gen::kTowns[loc.town], xp.x, xp.y, xp.z, gone);
				}
			}
			Dress(a_index, found);
		}

		// ---- world-map markers: one "Echo Altar" per town, at the plaza once it is built ----
		std::array<RE::ObjectRefHandle, gen::kTowns.size()> g_markers{};
		std::unordered_set<RE::ExtraMapMarker*> g_madeMarkers;  // the ones we made by hand (BeforeLoad)
		float g_markerTimer = 0;

		RE::MapMarkerData* TemplateMarkerData(RE::PlayerCharacter* a_player)
		{
			for (auto& h : a_player->GetPlayerRuntimeData().currentMapMarkers) {
				if (auto r = h.get()) {
					if (auto* x = r->extraList.GetByType<RE::ExtraMapMarker>(); x && x->mapData) {
						return x->mapData;
					}
				}
			}
			return nullptr;
		}

		// Our station icons from earlier sessions. Map markers are persistent, so they live in the worldspace's
		// persistent cell wherever they stand: found there even when the town is far away and not loaded (a range
		// search only sees loaded cells, and would make a new icon every session).
		std::array<bool, gen::kTowns.size()> g_markerScanned{};
		RE::TESObjectREFR* FindOurMarker(int a_town, RE::TESObjectREFR* a_anchor, RE::TESBoundObject* a_base)
		{
			auto* ws = a_anchor->GetWorldspace();
			auto* cell = ws ? ws->persistentCell : nullptr;
			if (!cell) {
				return nullptr;
			}
			std::vector<RE::TESObjectREFR*> mine;
			cell->ForEachReference([&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == a_base && (r->GetFormID() >> 24) == 0xFF && !r->IsDeleted() && !r->IsDisabled() &&
					r->GetPosition().GetDistance(a_anchor->GetPosition()) < gen::kPlanSweep * 1.5f) {
					mine.push_back(r);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (mine.empty()) {
				return nullptr;
			}
			// the one nearest the station (or the town), any doubles from older builds go
			const RE::NiPoint3 want = g_towns[a_town].state == TownPlan::State::Ready ? g_towns[a_town].center : a_anchor->GetPosition();
			std::sort(mine.begin(), mine.end(), [&](auto* x, auto* y) { return x->GetPosition().GetDistance(want) < y->GetPosition().GetDistance(want); });
			for (std::size_t i = 1; i < mine.size(); ++i) {
				mine[i]->Disable();
				mine[i]->SetDelete(true);
			}
			if (mine.size() > 1) {
				logger::info("shops: removed {} doubled map icons for {}", mine.size() - 1, gen::kTowns[a_town]);
			}
			return mine[0];
		}

		// Before a town's station is built (it is placed when you first come near), its icon stands out in the
		// open on that town's side of the outskirts, clear of the city's own icon; it moves onto the station once
		// the station is up.
		RE::NiPoint3 PreviewSpot(int a_town, RE::TESObjectREFR* a_anchor)
		{
			const auto  p = a_anchor->GetPosition();
			const float a = 2.4f + 0.63f * static_cast<float>(a_town);  // a different side for each town
			return { p.x + std::cos(a) * 2200.0f, p.y + std::sin(a) * 2200.0f, p.z };
		}

		std::string MarkerName(int a_town)
		{
			std::string name = std::string(gen::kText_station_marker) + " - " + std::string(gen::kTowns[a_town]);
			return name;
		}

		// Has the player found this town yet? Its own map marker (the city/town icon by that name, nearest the
		// town's anchor) is discovered. Station icons and the Coffer's travels wait for it.
		std::array<RE::ObjectRefHandle, gen::kTowns.size()> g_cityMarkers{};
		std::array<bool, gen::kTowns.size()>                g_cityLooked{};
		bool TownDiscovered(int a_town)
		{
			if (a_town < 0 || a_town >= static_cast<int>(gen::kTowns.size())) {
				return false;
			}
			auto ref = g_cityMarkers[a_town].get();
			if (!ref && !g_cityLooked[a_town]) {
				// the walled cities' own icons stand in their city worldspaces (WhiterunWorld...), not out in Tamriel
				static const std::pair<std::string_view, RE::FormID> kWalled[] = { { "Whiterun", 0x0162CE }, { "Solitude", 0x04D0F4 },
					{ "Windhelm", 0x038436 }, { "Markarth", 0x01C38A }, { "Riften", 0x01C390 } };
				for (const auto& [name, id] : kWalled) {
					if (name == gen::kTowns[a_town]) {
						if (auto* r = RE::TESForm::LookupByID<RE::TESObjectREFR>(id); r && r->extraList.GetByType<RE::ExtraMapMarker>()) {
							g_cityMarkers[a_town] = r->CreateRefHandle();
							ref = RE::NiPointer<RE::TESObjectREFR>(r);
							g_cityLooked[a_town] = true;
							logger::info("shops: {} is found by its map marker {:08X}", gen::kTowns[a_town], id);
						}
					}
				}
			}
			if (!ref && !g_cityLooked[a_town]) {
				g_cityLooked[a_town] = true;
				auto* anchor = MapAnchor(a_town);
				auto* ws = anchor ? anchor->GetWorldspace() : nullptr;
				if (auto* cell = ws ? ws->persistentCell : nullptr) {
					RE::TESObjectREFR* best = nullptr;
					float              bestD = 9000.0f;
					bool               named = false;
					int                seen = 0;
					cell->ForEachReference([&](RE::TESObjectREFR* r) {
						++seen;
						auto* x = r ? r->extraList.GetByType<RE::ExtraMapMarker>() : nullptr;
						if (!x || !x->mapData || (r->GetFormID() >> 24) == 0xFF) {
							return RE::BSContainer::ForEachResult::kContinue;
						}
						const bool  isTown = x->mapData->type == RE::MARKER_TYPE::kCity || x->mapData->type == RE::MARKER_TYPE::kTown;
						const bool  sameName = std::string_view(x->mapData->locationName.fullName.c_str()) == gen::kTowns[a_town];
						const float d = r->GetPosition().GetDistance(anchor->GetPosition());
						if ((sameName && (!named || d < bestD)) || (!named && isTown && d < bestD)) {
							best = r, bestD = d, named = sameName;
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
					if (!best && seen < 200) {
						g_cityLooked[a_town] = false;  // the world isn't loaded yet: look again later
						return false;
					}
					if (best) {
						g_cityMarkers[a_town] = best->CreateRefHandle();
						ref = RE::NiPointer<RE::TESObjectREFR>(best);
						logger::info("shops: {} is found by its map marker {:08X}{}", gen::kTowns[a_town], best->GetFormID(), named ? "" : " (nearest town icon)");
					} else {
						logger::warn("shops: no map marker for {}; its station shows from the start", gen::kTowns[a_town]);
					}
				}
			}
			if (!ref) {
				return g_cityLooked[a_town];  // no marker to go by: treat as found
			}
			auto* x = ref->extraList.GetByType<RE::ExtraMapMarker>();
			return x && x->mapData && x->mapData->flags.all(RE::MapMarkerData::Flag::kCanTravelTo);
		}

		void EnsureMarker(RE::PlayerCharacter* a_player, int a_town)
		{
			auto* anchor = MapAnchor(a_town);
			auto* markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(0x10);  // MapMarker
			if (!anchor || !markerBase) {
				return;
			}
			const auto& plan = g_towns[a_town];
			const auto& st = gen::kStations[a_town];
			auto*        aws = anchor->GetWorldspace();
			// a fixed station: its icon on the spot (a walled city's world has no map of its own: there the icon
			// stands at the city gate outside)
			const bool   fixedHere = st.fixed && aws && aws->GetFormID() == st.world;
			const bool   ready = st.fixed || plan.state == TownPlan::State::Ready;
			const RE::NiPoint3 at = st.fixed ? (fixedHere ? RE::NiPoint3{ st.x, st.y, st.z } : anchor->GetPosition()) : ready ? plan.center : PreviewSpot(a_town, anchor);
			if (!TownDiscovered(a_town)) {
				// not found yet: no station icon (one from an earlier build is hidden until then)
				if (!g_markers[a_town].get() && !g_markerScanned[a_town]) {
					g_markerScanned[a_town] = true;
					if (auto* r = FindOurMarker(a_town, anchor, markerBase)) {
						g_markers[a_town] = r->CreateRefHandle();
					}
				}
				if (auto r = g_markers[a_town].get()) {
					if (auto* x = r->extraList.GetByType<RE::ExtraMapMarker>(); x && x->mapData) {
						x->mapData->flags.reset(RE::MapMarkerData::Flag::kVisible, RE::MapMarkerData::Flag::kCanTravelTo);
					}
				}
				return;
			}
			auto ref = g_markers[a_town].get();
			if (!ref && !g_markerScanned[a_town]) {
				g_markerScanned[a_town] = true;
				if (auto* r = FindOurMarker(a_town, anchor, markerBase)) {
					ref = RE::NiPointer<RE::TESObjectREFR>(r);
				}
			}
			if (!ref) {
				ref = anchor->PlaceObjectAtMe(markerBase, true);
				if (!ref) {
					return;
				}
				ref->SetPosition(at);
			}
			g_markers[a_town] = ref->CreateRefHandle();
			if (ready && ref->GetPosition().GetDistance(at) > 50.0f) {
				ref->SetPosition(at);  // onto the station once it is built
			}
			const std::string name = MarkerName(a_town);
			auto* x = ref->extraList.GetByType<RE::ExtraMapMarker>();
			if (!x) {
				auto* tmpl = TemplateMarkerData(a_player);
				if (!tmpl) {
					return;
				}
				// Clone a game-made marker record (keeps the engine's vtable), then give it our own name.
				auto* data = static_cast<RE::MapMarkerData*>(RE::malloc(0x40));  // (room to spare: this game's record may be bigger)
				std::memset(static_cast<void*>(data), 0, 0x40);
				std::memcpy(static_cast<void*>(data), tmpl, sizeof(RE::MapMarkerData));
				new (&data->locationName.fullName) RE::BSFixedString(name.c_str());
				data->flags.reset(RE::MapMarkerData::Flag::kShowAllHidden);
				data->flags.set(RE::MapMarkerData::Flag::kVisible);
				data->type = RE::MARKER_TYPE::kShrine;
				x = RE::BSExtraData::Create<RE::ExtraMapMarker>();
				x->mapData = data;
				ref->extraList.Add(x);
				g_madeMarkers.insert(x);
				logger::info("shops: map marker '{}' at ({:.0f},{:.0f}){}", name, at.x, at.y, ready ? "" : " (until the station is built)");
			} else if (x->mapData) {
				if (std::string_view(x->mapData->locationName.fullName.c_str()) != name) {
					x->mapData->locationName.fullName = name.c_str();  // the Coffer moved: the icon says where it is
				}
			}
			if (auto* md = x->mapData) {
				// seen on the map from the start; fast travel once the station is built (the preview spot has no
				// known ground under it)
				md->flags.set(RE::MapMarkerData::Flag::kVisible);
				if (ready) {
					md->flags.set(RE::MapMarkerData::Flag::kCanTravelTo);
				} else {
					md->flags.reset(RE::MapMarkerData::Flag::kCanTravelTo);
				}
			}
			auto& list = a_player->GetPlayerRuntimeData().currentMapMarkers;
			const auto handle = ref->CreateRefHandle();
			if (std::none_of(list.begin(), list.end(), [&](const RE::ObjectRefHandle& h) { return h.native_handle() == handle.native_handle(); })) {
				list.push_back(handle);
			}
		}

		void TickMarkers(RE::PlayerCharacter* a_player, float a_delta)
		{
			if ((g_markerTimer -= a_delta) > 0) {
				return;
			}
			g_markerTimer = 5.0f;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				EnsureMarker(a_player, t);
			}
		}

		// Pieces from older layouts that no town owns any more (stands that used to be inside a city, before stands
		// moved outside): gone once the player is near them.
		void SweepOldStands(RE::PlayerCharacter* a_player)
		{
			auto* ws = a_player->GetWorldspace();
			if (!ws) {
				return;
			}
			int n = 0;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, 6000.0f, [&](RE::TESObjectREFR* r) {
				if (!r || r->IsDeleted() || !IsOurBase(r->GetBaseObject())) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (auto* b = r->GetBaseObject(); b == Forms::Ours(gen::kWardBrazier) || b == Forms::Ours(gen::kWardFire) || b == Forms::Ours(gen::kWardLight)) {
					return RE::BSContainer::ForEachResult::kContinue;  // the Curse's ward-fires (Curse.cpp keeps them)
				}
				for (const auto& h : g_placed) {
					if (h.get().get() == r) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
				}
				bool owned = false;
				for (int t = 0; t < static_cast<int>(gen::kTowns.size()) && !owned; ++t) {
					auto* a = TownAnchor(t);
					if (!a || a->GetWorldspace() != ws || a->GetPosition().GetDistance(r->GetPosition()) > gen::kPlanSweep) {
						continue;
					}
					// a stand that is still being placed, or one that will be adopted: not ours to judge yet
					owned = g_towns[t].state != TownPlan::State::Ready || std::hypot(r->GetPosition().x - g_towns[t].center.x, r->GetPosition().y - g_towns[t].center.y) < 700.0f;
				}
				if (!owned) {
					r->Disable();
					r->SetDelete(true);
					++n;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (n) {
				std::erase_if(g_spinners, [](const Spinner& s) { return !s.ref.get() || s.ref.get()->IsDeleted(); });
				logger::info("shops: removed {} pieces of an old in-town stand", n);
			}
		}

		void TickTowns()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			SweepOldStands(player);
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				auto* anchor = TownAnchor(t);
				auto* cell = anchor ? anchor->GetParentCell() : nullptr;
				if (!cell || !cell->IsAttached() || anchor->GetPosition().GetDistance(player->GetPosition()) > 3200.0f) {
					continue;
				}
				auto& plan = g_towns[t];
				if (plan.state == TownPlan::State::Unknown && gen::kStations[t].fixed) {
					// its spot is known (sheet: stations): build there; machines already standing on it are kept
					const auto& st = gen::kStations[t];
					plan = TownPlan{};
					plan.state = TownPlan::State::Ready;
					plan.center = { st.x, st.y, st.z };
					plan.yaw = st.yaw * RE::NI_PI / 180.0f;
					plan.compact = st.compact;
					// the one-piece shrine (layout v3): a station built to an older layout here comes down first
					if (auto* g = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(gen::kStandLayoutGlobal, gen::kPlugin); g && !((static_cast<int>(g->value) >> (16 + t)) & 1)) {
						int gone = 0;
						RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, 1200.0f, [&](RE::TESObjectREFR* r) {
							auto* b = r ? r->GetBaseObject() : nullptr;
							if (b && IsOurBase(b) && !r->IsDeleted() && b != Forms::Ours(gen::kWardBrazier) && b != Forms::Ours(gen::kWardFire) && b != Forms::Ours(gen::kWardLight)) {
								r->Disable();
								r->SetDelete(true);
								++gone;
							}
							return RE::BSContainer::ForEachResult::kContinue;
						});
						g->value = static_cast<float>(static_cast<int>(g->value) | (1 << (16 + t)));
						for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
							if (gen::kLocations[i].town == t) {
								g_placed[i] = {};
								g_dressed[i] = false;
							}
						}
						logger::info("shops: {} old station taken down for the round shrine ({} pieces)", gen::kTowns[t], gone);
					}
					// the spot moved (sheet: stations, re-picked where paths meet with room all round): a shrine still standing
					// at the old spot comes down and is raised again at the new one
					{
						std::vector<RE::TESObjectREFR*> ours;
						RE::NiPoint3 sum{};
						std::unordered_set<RE::TESForm*> station;  // only the station's own machines (never a rite's bones)
						for (const auto& l : gen::kLocations) {
							if (l.town == t) {
								if (auto* f = Forms::Object(l.object)) {
									station.insert(f);
								}
							}
						}
						RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, 1800.0f, [&](RE::TESObjectREFR* r) {
							auto* b = r ? r->GetBaseObject() : nullptr;
							if (b && station.contains(b) && !r->IsDeleted() && !r->IsDisabled()) {
								ours.push_back(r);
								sum += r->GetPosition();
							}
							return RE::BSContainer::ForEachResult::kContinue;
						});
						if (!ours.empty()) {
							const RE::NiPoint3 c = sum / static_cast<float>(ours.size());
							if (std::hypot(c.x - plan.center.x, c.y - plan.center.y) > 140.0f) {
								for (auto* r : ours) {
									r->Disable();
									r->SetDelete(true);
								}
								// and the shrine's own body and dressing standing with them (ours, close round the old spot)
								RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, 1800.0f, [&](RE::TESObjectREFR* r) {
									auto* b = r ? r->GetBaseObject() : nullptr;
									if (b && IsOurBase(b) && !r->IsDeleted() && !r->IsDisabled() && !r->As<RE::Actor>() && std::hypot(r->GetPosition().x - c.x, r->GetPosition().y - c.y) < 420.0f &&
										b != Forms::Ours(gen::kWardBrazier) && b != Forms::Ours(gen::kWardFire) && b != Forms::Ours(gen::kWardLight)) {
										r->Disable();
										r->SetDelete(true);
									}
									return RE::BSContainer::ForEachResult::kContinue;
								});
								for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
									if (gen::kLocations[i].town == t) {
										g_placed[i] = {};
										g_dressed[i] = false;
									}
								}
								logger::info("shops: {} station moved to its new spot ({} pieces taken down at the old one)", gen::kTowns[t], ours.size());
							}
						}
					}
					MarkLayout(t);
					ClearSite(t, anchor);
					logger::info("shops: {} station at its fixed spot ({:.0f},{:.0f}) yaw {:.0f}", gen::kTowns[t], st.x, st.y, st.yaw);
				}
				if (plan.state == TownPlan::State::Unknown) {
					if (AdoptExisting(t, anchor)) {
						logger::info("shops: {} alley already built", gen::kTowns[t]);
						MarkLayout(t);
					} else {
						Sweep(t, anchor);
						GatherObstacles(t, anchor);
						plan.state = TownPlan::State::Searching;
						plan.candidate = 0;
					}
				}
				if (plan.state == TownPlan::State::Searching) {
					const int total = static_cast<int>(gen::kPlanDist.size()) * gen::kPlanAngles * static_cast<int>(gen::kPlanYaw.size());
					for (int k = 0; k < 3 && plan.state == TownPlan::State::Searching; ++k) {
						if (plan.candidate >= total) {
							// Nowhere perfect: take the best-scoring spot found.
							plan = g_best[t].score < FLT_MAX ? g_best[t].plan : TownPlan{ TownPlan::State::Ready, anchor->GetPosition(), 0, 0 };
							plan.state = TownPlan::State::Ready;
							MarkLayout(t);
							logger::warn("shops: no perfect spot in {}, best score {:.0f} at ({:.0f},{:.0f})", gen::kTowns[t], g_best[t].score, plan.center.x, plan.center.y);
							break;
						}
						TryCandidate(t, anchor, plan.candidate++);
					}
					if (plan.state == TownPlan::State::Searching) {
						return;  // keep searching next frame
					}
					if (auto probe = g_probe.get()) {
						probe->Disable();
						probe->SetDelete(true);
						g_probe = {};
					}
				}
				for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
					if (gen::kLocations[i].town == t) {
						PlaceIfMissing(i);
					}
				}
			}
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

		// Take the offered spell (Activate on the shrine while it is held over the crown).
		// (Noah: two of the altar's spells at a time - Skyrim's own spells don't count) a third won asks which of your two it
		// replaces (that one is gone for good), or you leave it on the altar for half the roll back
		int  g_lastRollCost = 0;
		bool g_menu = false;
		RE::ObjectRefHandle g_menuRef;
		void EndOffer()
		{
			Remove(g_show.display);
			g_show.phase = Show::Idle;
			g_choosing = false;
		}
		void Choose(int a_slot)  // 0/1: replace that spell, -1: keep yours
		{
			if (!g_choosing || g_show.phase != Show::Offering) {
				g_choosing = false;
				return;
			}
			const auto have = Arcana::Carried();
			if (a_slot < 0) {
				const int back = g_lastRollCost / 2;
				Points::Refund(back);
				Forms::Play(gen::Sound::ui_close);
				Hud::Banner(std::string(Arcana::Name(g_show.gun)) + " left on the altar  (+" + std::to_string(back) + ")", 2.5f);
				logger::info("shrine: left {} (refund {})", Arcana::Name(g_show.gun), back);
				EndOffer();
				return;
			}
			if (a_slot >= static_cast<int>(have.size())) {
				return;
			}
			const int old = have[a_slot];
			Arcana::Replace(old, g_show.gun);
			Forms::Play(gen::Sound::coffer_take);
			Hud::Banner(std::string(Arcana::Name(g_show.gun)) + " learned  -  " + std::string(Arcana::Name(old)) + " given back", 3.0f);
			logger::info("shrine: took {} in place of {}", Arcana::Name(g_show.gun), Arcana::Name(old));
			EndOffer();
		}
		bool TakeOffer(RE::TESObjectREFR* a_ref)
		{
			if (g_show.phase != Show::Offering || g_show.chest.get().get() != a_ref) {
				return false;
			}
			if (g_show.prize) {  // a class row's win: given now
				if (g_show.pz.give) g_show.pz.give();
				Forms::Play(gen::Sound::coffer_take);
				logger::info("shrine: took {}", g_show.pz.name);
				EndOffer();
				return true;
			}
			if (g_show.bow) {  // the bow wheel: one bow at a time, the new one takes the old one's place
				const int old = Bows::Carried();
				Bows::Give(g_show.gun);
				Forms::Play(gen::Sound::coffer_take);
				Hud::Banner(std::string(Bows::Name(g_show.gun)) + "  -  in your hands" + (old >= 0 && old != g_show.gun ? "  (" + std::string(Bows::Name(old)) + " given back)" : std::string()), 3.0f);
				logger::info("shrine: took the bow {}", Bows::Name(g_show.gun));
				EndOffer();
				return true;
			}
			if (Arcana::Carried().size() >= 2) {  // the choice, in the altar's menu
				if (!g_choosing) {
					g_choosing = true;
					g_menu = true;
					g_menuRef = a_ref->CreateRefHandle();
					Forms::Play(gen::Sound::ui_move);
				}
				return true;
			}
			Arcana::Give(g_show.gun, true);  // learned (in the Magic menu) and in your right hand
			Forms::Play(gen::Sound::coffer_take);
			Hud::Banner(std::string(Arcana::Name(g_show.gun)) + "  -  learned", 2.5f);
			logger::info("shrine: took {}", Arcana::Name(g_show.gun));
			EndOffer();
			return true;
		}

		// (Noah: harder to grow too strong) each spell called costs a quarter more than the last, until the next night
		// (Noah: each altar's price starts at its lowest, and goes back to what it was when you load a save) how many times
		// each city's altar has been rolled tonight - spells and bows apart - kept in the co-save (Save 'ROLL')
		constexpr int kRollTowns = 16;
		std::array<std::uint8_t, kRollTowns> g_spellRolls{}, g_bowRollsT{};
		int g_rollNight = -1;
		void RollNight()
		{
			const int night = Save::Get(gen::kAV_round);
			if (night != g_rollNight) g_rollNight = night, g_spellRolls.fill(0), g_bowRollsT.fill(0);
		}
		int TownOfAltar(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return -1;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()) && t < kRollTowns; ++t) {
				if (Shops::Placed(t, 0) == a_ref) return t;
			}
			return -1;
		}
		// (Noah: one roller at a time) a friend's roll at an altar holds it for everyone else until their reel is done
		struct Busy { std::string who; std::chrono::steady_clock::time_point until; };
		std::array<Busy, 16> g_busy{};
		int g_myBusy = -1;  // the altar our own roll holds (told to the party)
		bool BusyForMe(int a_town)
		{
			if (a_town < 0 || a_town >= 16 || g_busy[a_town].who.empty()) return false;
			if (std::chrono::steady_clock::now() > g_busy[a_town].until) { g_busy[a_town].who.clear(); return false; }
			Hud::Banner(g_busy[a_town].who + " is rolling at this altar - wait for their reel to stop", 2.0f);
			return true;
		}
		int CurrentTown()
		{
			auto r = g_menuRef.get();
			return TownOfAltar(r.get());
		}
		int RollBase()
		{
			const int crew = Save::Crew();
			return crew >= 0 && gen::kCrew[crew].passive == 3 ? static_cast<int>(gen::kCrew[crew].value) : gen::kObjects[0].cost;
		}
		int RollCost(int a_town = -2)
		{
			RollNight();
			const int t = a_town == -2 ? CurrentTown() : a_town;
			const int n = t >= 0 && t < kRollTowns ? g_spellRolls[t] : 0;
			return PowerUps::FireSale() ? 10 : static_cast<int>(std::lround(RollBase() * std::pow(1.25, n) / 10.0) * 10);
		}
		int BowRollCost(int a_town = -2)
		{
			RollNight();
			const int t = a_town == -2 ? CurrentTown() : a_town;
			const int n = t >= 0 && t < kRollTowns ? g_bowRollsT[t] : 0;
			return PowerUps::FireSale() ? 10 : static_cast<int>(std::lround(RollBase() * std::pow(1.25, n) / 10.0) * 10);
		}
		void SpinBow(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref || g_show.phase != Show::Idle) {
				if (g_show.phase != Show::Idle) {
					Hud::Banner("The altar is still turning", 1.5f);
				}
				return;
			}
			const int pick = Bows::Roll();
			if (pick < 0) {
				Hud::Banner("The altar has no other bow for you yet", 2.5f);
				return;
			}
			const int town = TownOfAltar(a_ref);
			if (BusyForMe(town)) return;
			const int cost = BowRollCost(town);
			if (!Points::Spend(cost)) {
				return;
			}
			g_lastRollCost = cost;
			if (town >= 0) g_bowRollsT[town] = static_cast<std::uint8_t>(std::min(250, g_bowRollsT[town] + 1));
			g_show = {};
			g_show.bow = true;
			g_show.chest = a_ref->CreateRefHandle();
			g_show.phase = Show::Charging;
			g_show.gun = pick;
			Forms::PlayAt(gen::Sound::coffer_charge, a_ref);
			logger::info("shrine: rolling the bow {}", Bows::Name(pick));
		}

		// (Noah: no roll on the class rows) a class row's win turns on the altar like a spell: the reel runs the class's own
		// spells (the stones strike the heart one by one), then lands on what was won, to be taken off the altar
		void SpinPrize(RE::TESObjectREFR* a_ref, Classes::Prize a_pz)
		{
			g_show = {};
			g_show.chest = a_ref->CreateRefHandle();
			g_show.phase = Show::Charging;
			const auto carried = Arcana::Carried();
			g_show.gun = carried.empty() ? 0 : carried.front();  // (the reel's spells: what is shown as it turns)
			g_show.prize = true;
			g_show.pz = std::move(a_pz);
			Forms::PlayAt(gen::Sound::coffer_charge, a_ref);
			logger::info("shrine: rolling {}", g_show.pz.name);
		}

		// Roll a spell: every spell you do not carry has the same chance.
		void SpinBox(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref || g_show.phase != Show::Idle) {
				if (g_show.phase != Show::Idle) {
					Hud::Banner("The shrine is still turning", 1.5f);
				}
				return;
			}
			const int pick = Arcana::Roll();  // one of the Coffer's spells you don't know yet
			if (pick < 0) {
				Hud::Banner(Classes::Get() < 0 ? std::string("Choose a class first") : Arcana::Count() ? "You carry every ladder of the " + std::string(Classes::Name(Classes::Get())) + " - raise them with the altar's upgrade" : std::string("The shrine is silent (the spell mods aren't installed)"), 3.0f);
				return;
			}
			const int town = TownOfAltar(a_ref);
			if (BusyForMe(town)) return;
			const int cost = RollCost(town);
			if (!Points::Spend(cost)) {
				return;
			}
			g_lastRollCost = cost;
			if (town >= 0) g_spellRolls[town] = static_cast<std::uint8_t>(std::min(250, g_spellRolls[town] + 1));
			g_show = {};
			g_show.chest = a_ref->CreateRefHandle();
			g_show.phase = Show::Charging;
			g_show.gun = pick;
			Forms::PlayAt(gen::Sound::coffer_charge, a_ref);
			logger::info("shrine: rolling {} (of {})", Arcana::Name(pick), Arcana::Count());
		}

		int                 g_sel = 0;
		int                 g_trialSel = 0;  // the controller's pick among the altar's trials
		// (Noah: one way to drive the altar) a focus moved up and down its rows - D-pad or W/S - and A / Enter to take it
		enum Row { kRowSpell = 0, kRowBow, kRowForge, kRowPerk, kRowShout, kRowBond, kRowTrial = 10, kRowLeave = 20 };
		int g_focus = kRowSpell;
		int g_chooseFocus = 0;  // a third spell: 0 / 1 replace that one, 2 keep yours

		void BuyPerk(int perk, RE::TESObjectREFR* a_ref)
		{
			if (perk < 0 || perk >= static_cast<int>(gen::kPerks.size())) {
				return;
			}
			if (Save::PerkStacks(perk) > 0) {  // one of each draught a life
				Hud::Banner(std::string("You already carry ") + std::string(gen::kPerks[perk].label), 2.5f);
				return;
			}
			int held = 0;  // (Noah: every draught can be held at once - the limit is the whole list)
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				held += Save::PerkStacks(i) > 0 ? 1 : 0;
			}
			if (held >= gen::kMaxPerks) {
				Hud::Banner("Your perk slots are full (" + std::to_string(gen::kMaxPerks) + " of " + std::to_string(gen::kMaxPerks) + ")", 2.5f);
				return;
			}
			if (!Points::Spend(gen::kPerks[perk].cost)) {
				return;
			}
			Forms::PlayAt(Forms::Brew(perk), a_ref);
			Perks::Grant(perk);  // at once: no potion to drink
		}

		void BuyAmmo(const gen::ObjectDef& a_obj)
		{
			std::vector<int> owned;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (Gun::Owns(i)) {
					owned.push_back(i);
				}
			}
			if (owned.empty()) {
				Hud::Banner("No weapons to restock", 2.0f);
				return;
			}
			if (!Points::Spend(a_obj.cost)) {
				return;
			}
			for (int g : owned) {
				Gun::RefillAmmo(g);
			}
			Forms::Play(gen::Sound::reload_mag);
			Hud::Banner("All ammo restocked", 2.0f);
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
			g_humming = false;
			g_placed.fill({});
			g_dressed.fill(false);
			g_towns.fill({});
			g_best.fill({});
			g_probe = {};
			g_spinners.clear();
			g_show = {};
		}

		RE::TESObjectREFR* Placed(int a_town, int a_kind)
		{
			for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
				if (gen::kLocations[i].town == a_town && gen::kObjects[gen::kLocations[i].object].kind == a_kind) {
					if (auto r = g_placed[i].get(); r && !r->IsDeleted()) {
						return r.get();
					}
				}
			}
			return nullptr;
		}
		int TownOfRef(RE::TESObjectREFR* a_ref) { return a_ref ? TownOf(a_ref) : -1; }

		bool Discovered(int a_town) { return a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size()) && TownDiscovered(a_town); }
		bool StationTarget(int a_town, RE::NiPoint3& a_at)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* ws = p ? p->GetWorldspace() : nullptr;
			if (!ws || a_town < 0 || a_town >= static_cast<int>(gen::kTowns.size())) {
				return false;
			}
			if (const auto& st = gen::kStations[a_town]; st.fixed && ws->GetFormID() == st.world) {
				a_at = { st.x, st.y, st.z + 60.0f };
				return true;
			}
			if (g_towns[a_town].state == TownPlan::State::Ready && TownAnchor(a_town) && TownAnchor(a_town)->GetWorldspace() == ws) {
				a_at = g_towns[a_town].center + RE::NiPoint3{ 0, 0, 60.0f };
				return true;
			}
			if (auto* m = MapAnchor(a_town); m && m->GetWorldspace() == ws) {
				a_at = m->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
				return true;
			}
			return false;
		}
		bool StandFrame(int a_town, RE::NiPoint3& a_center, float& a_yaw, RE::TESWorldSpace*& a_world)
		{
			auto* anchor = a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size()) ? TownAnchor(a_town) : nullptr;
			a_world = anchor ? anchor->GetWorldspace() : nullptr;
			if (!anchor) {
				return false;
			}
			const auto& plan = g_towns[a_town];
			if (plan.state == TownPlan::State::Ready) {
				a_center = plan.center;
				a_center.z = plan.topSet ? plan.top : plan.center.z;
				a_yaw = plan.yaw;
				return true;
			}
			a_center = anchor->GetPosition();
			a_yaw = 0;
			return false;
		}

		std::string BoxTown() { return std::string(gen::kTowns[ActiveBoxTown()]); }

		static bool IsMenuKey(const RE::ButtonEvent* b)
		{
			const auto code = b->GetIDCode();
			switch (b->GetDevice()) {
			case RE::INPUT_DEVICE::kKeyboard:
				for (int k = 0; k < Settings::kKeys; ++k) {
					if (static_cast<int>(code) == Settings::Key(k)) {
						return true;
					}
				}
				return code == 0x12 || code == 0xCB || code == 0xCD || code == 0xC8 || code == 0xD0 || code == 0x11 || code == 0x1E || code == 0x1F || code == 0x20 || code == 0x1C || code == 0x9C;
			case RE::INPUT_DEVICE::kMouse: return code == 8 || code == 9;
			case RE::INPUT_DEVICE::kGamepad: return code == 0x4000 || code == 0x8000 || code == 0x1000 || code == 0x2000 || code == 0x0001 || code == 0x0002 || code == 0x0004 || code == 0x0008 || code == 0x0100 || code == 0x0200 || code == 0x000A || code == 0x0009 || code == 0x0020 || code == 0x0040 || code == 0x0080;  // (+ Back, L3, R3, LT: the altar's change-class / outfit / quivers must not also wait, sneak or turn the camera)
			default: return false;
			}
		}
		// (a key that closed the menu is still eaten a moment after, so Activate does not open it again behind you)
		std::chrono::steady_clock::time_point g_closedAt{};
		bool MenuEats(RE::InputEvent* a_event)
		{
			auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
			if (!b || !IsMenuKey(b)) {
				return false;
			}
			if (b->GetDevice() == RE::INPUT_DEVICE::kKeyboard && b->IsUp()) {
				const auto c = b->GetIDCode();
				if (c == 0x11 || c == 0x1E || c == 0x1F || c == 0x20) {
					return false;  // (letting go of a walk key always reaches Skyrim: you never walk on by yourself)
				}
			}
			return g_menu || std::chrono::steady_clock::now() - g_closedAt < std::chrono::milliseconds(350);
		}

		// The shrine's keys (the menu never pauses: the dead keep coming, so everything is one press away).
		// Keyboard: 1 roll a spell, 2 reforge, 3 take the chosen perk, 4 learn the city's Thu'um, 5 bond the dragon,
		// 6 7 8 9 0 take up one of the five trials, mouse wheel or Left/Right picks a perk, Activate (E) or Backspace
		// leaves; walking away closes it too.
		// Gamepad: D-pad up/down move through the rows, D-pad left/right step into the trials or pick a perk, A takes the
		// row, B leaves; Back changes class, L3 the outfit, R3 fills the quivers.
		bool OnButton(const RE::ButtonEvent* a_event)
		{
			// the shrine's orb in the crosshair: Activate opens the shrine (Skyrim's own pick only finds its floor)
			if (!g_menu && a_event && a_event->IsDown() && a_event->QUserEvent() == RE::UserEvents::GetSingleton()->activate) {
				if (auto shrine = g_orbAim.get()) {
					auto* pick = RE::CrosshairPickData::GetSingleton();
					if (!pick || pick->target.get().get() != shrine.get()) {
						OnActivate(shrine.get());
						return true;
					}
				}
			}
			if (!g_menu || !a_event || !a_event->IsDown()) {
				return false;
			}
			const auto code = a_event->GetIDCode();
			const auto dev = a_event->GetDevice();
			const int  n = static_cast<int>(gen::kPerks.size());
			if (g_choosing) {  // the choice: 1 / X replaces the first, 2 / Y the second, Backspace / B keeps yours
				if (g_show.phase != Show::Offering) {
					g_choosing = false;
				} else if (dev == RE::INPUT_DEVICE::kKeyboard && !(code == 0x11 || code == 0xC8 || code == 0x1F || code == 0xD0 || code == 0x1E || code == 0xCB || code == 0x20 || code == 0xCD || code == 0x1C || code == 0x9C)) {
					if (static_cast<int>(code) == Settings::Key(0)) Choose(0);
					else if (static_cast<int>(code) == Settings::Key(1)) Choose(1);
					else if (static_cast<int>(code) == Settings::Key(4) || code == 0x12) Choose(-1);
					return true;
				} else if (dev == RE::INPUT_DEVICE::kGamepad && !(code == 0x0001 || code == 0x0002 || code == 0x0004 || code == 0x0008 || code == 0x1000)) {
					if (code == 0x4000) Choose(0);
					else if (code == 0x8000) Choose(1);
					else if (code == 0x2000) Choose(-1);
					return true;
				}
			}
			// (the classes overhaul) C on the keyboard, Back on a pad: change class here, for septims
			if (!g_choosing && ((dev == RE::INPUT_DEVICE::kKeyboard && code == 0x2E) || (dev == RE::INPUT_DEVICE::kGamepad && code == 0x0020))) {
				if (g_show.phase != Show::Idle) {
					Hud::Banner("Wait for the altar to finish its roll", 2.0f);
				} else {
					g_menu = false;
					g_closedAt = std::chrono::steady_clock::now();
					Classes::OpenPicker(true);
				}
				return true;
			}
			// (the class framework's outfit picker) O on the keyboard, the left stick click on a pad: the next outfit, any class
			if (!g_choosing && ((dev == RE::INPUT_DEVICE::kKeyboard && code == 0x18) || (dev == RE::INPUT_DEVICE::kGamepad && code == 0x0040))) {
				Classes::NextOutfit();
				return true;
			}
			// (the Ranger's ammo) Q on the keyboard, the right stick click on a pad: a full quiver and bolt bundles
			if (!g_choosing && ((dev == RE::INPUT_DEVICE::kKeyboard && code == 0x10) || (dev == RE::INPUT_DEVICE::kGamepad && code == 0x0080))) {
				const int cost = 400 + 100 * std::max(0, Save::Get(gen::kAV_round));
				if (Points::Get() < cost) {
					Hud::Banner("A quiver costs " + std::to_string(cost) + " septims", 2.0f);
				} else if (Bows::Restock() <= 0) {
					Hud::Banner("Your quivers are full (or you carry no bow or crossbow)", 2.5f);
				} else {
					Points::Spend(cost);
					Forms::Play(gen::Sound::reload_mag);
					Hud::Banner("Quivers filled  (-" + std::to_string(cost) + ")", 2.5f);
				}
				return true;
			}
			auto*      ref = g_menuRef.get().get();
			auto act = [&](int a) {
				switch (a) {
				case 0:
					if (!TakeOffer(ref)) {
						if (Classes::Get() == 2) SpinBow(ref);  // (the Ranger's roll is the bow wheel)
						else SpinBox(ref);
						if (g_show.phase != Show::Idle) {  // (Noah: the box sits mid-screen now - it steps aside to watch the roll)
							g_menu = false;
							g_closedAt = std::chrono::steady_clock::now();
						}
					}
					break;
				case 1:
					if (Gun::Reforge() && ref) {
						Forms::PlayAt(gen::Sound::forge_upgrade, ref);
					}
					break;
				case 2: BuyPerk(g_sel, ref); break;
				case 3: Curse::ClaimRelic(ref ? TownOf(ref) : -1); break;
				case 6:
					if (!TakeOffer(ref)) {
						if (const int cls = Classes::Get(); cls == 3 || (cls == 2 && std::uniform_int_distribution<int>(0, 9)(g_rng) < 3)) {
							// (the Spellsword's row: a spell-weapon or spellshield; the Ranger's: three times in ten a crossbow)
							const int town = TownOfAltar(ref), cost = BowRollCost(town);
							Classes::Prize pz;
							if (g_show.phase != Show::Idle) {
								Hud::Banner("The altar is still turning", 1.5f);
							} else if (Points::Get() >= cost && ref && Classes::RollGear(cls, &pz)) {
								Points::Spend(cost);
								if (town >= 0) g_bowRollsT[town] = static_cast<std::uint8_t>(std::min(250, g_bowRollsT[town] + 1));
								SpinPrize(ref, std::move(pz));
								g_menu = false, g_closedAt = std::chrono::steady_clock::now();
							} else if (Points::Get() < cost) {
								Hud::Banner("It costs " + std::to_string(cost) + " septims", 2.0f);
							}
							break;
						} else if (cls == 1) {  // (Destruction's side-picks: its support spells)
							const int town = TownOfAltar(ref), cost = BowRollCost(town);
							if (Points::Get() < cost) {
								Hud::Banner("It costs " + std::to_string(cost) + " septims", 2.0f);
							} else if (Classes::Prize pz; g_show.phase == Show::Idle && ref && Classes::RollSupport(&pz)) {
								Points::Spend(cost);
								if (town >= 0) g_bowRollsT[town] = static_cast<std::uint8_t>(std::min(250, g_bowRollsT[town] + 1));
								SpinPrize(ref, std::move(pz));
								g_menu = false, g_closedAt = std::chrono::steady_clock::now();
							}
							break;
						} else if (cls == 0) {  // (the Summoner's late Legendaries: heroes and armies)
							const int town = TownOfAltar(ref), cost = BowRollCost(town) * 2;
							if (Points::Get() < cost) {
								Hud::Banner("A hero's pact costs " + std::to_string(cost) + " septims", 2.0f);
							} else if (Classes::Prize pz; g_show.phase == Show::Idle && ref && Classes::RollHero(&pz)) {
								Points::Spend(cost);
								SpinPrize(ref, std::move(pz));
								g_menu = false, g_closedAt = std::chrono::steady_clock::now();
							}
							break;
						} else if (cls != 2) {
							Hud::Banner("Choose a class first", 2.5f);
							break;
						}
						SpinBow(ref);
						if (g_show.phase != Show::Idle) {  // (Noah: the box sits mid-screen now - it steps aside to watch the roll)
							g_menu = false;
							g_closedAt = std::chrono::steady_clock::now();
						}
					}
					break;
				case 7: Curse::StartTrial(ref ? TownOf(ref) : -1, g_trialSel); break;
				case 4:
					Forms::Play(gen::Sound::ui_close);
					g_menu = false;
					g_closedAt = std::chrono::steady_clock::now();
					break;
				case 5: Movement::BuyBond(ref); break;
				}
			};
			// the one way through it: up / down (W S, the arrows, the D-pad, the wheel) moves the focus, left / right
			// (A D) picks the perk (or steps between the altar and its trials), Enter / A takes the focused row
			const bool kb = dev == RE::INPUT_DEVICE::kKeyboard, pad = dev == RE::INPUT_DEVICE::kGamepad;
			const int  nav = (kb && (code == 0x11 || code == 0xC8)) || (pad && code == 0x0001) || (dev == RE::INPUT_DEVICE::kMouse && code == 8) ? -1
			               : (kb && (code == 0x1F || code == 0xD0)) || (pad && code == 0x0002) || (dev == RE::INPUT_DEVICE::kMouse && code == 9) ? 1
			                                                                                                                                 : 0;
			const int  side = (kb && (code == 0x1E || code == 0xCB)) || (pad && code == 0x0004) ? -1 : (kb && (code == 0x20 || code == 0xCD)) || (pad && code == 0x0008) ? 1 : 0;
			const bool pick = (kb && (code == 0x1C || code == 0x9C)) || (pad && code == 0x1000);
			if (g_choosing && g_show.phase == Show::Offering && (nav || side || pick)) {
				if (nav || side) {
					g_chooseFocus = (g_chooseFocus + (nav ? nav : side) + 3) % 3;
					PlayUi(UiSnd::kMove, 0.7f);
				} else {
					PlayUi(UiSnd::kChoose);
					Choose(g_chooseFocus == 2 ? -1 : g_chooseFocus);
				}
				return true;
			}
			if (!g_choosing && (nav || side || pick)) {
				auto* r = g_menuRef.get().get();
				const int t = r ? TownOf(r) : -1;
				std::vector<int> rows{ kRowSpell, kRowBow, kRowForge, kRowPerk };
				if (t >= 0 && Curse::ClaimableShout(t) >= 0) rows.push_back(kRowShout);
				rows.push_back(kRowBond);
				const bool trials = t >= 0 && Curse::Altar(t).on;
				if (trials) for (int q : { 0, 1, 4, 3 }) rows.push_back(kRowTrial + q);  // (the Unbroken folded into the Breath; 3 is the class trial)
				rows.push_back(kRowLeave);
				auto at = std::find(rows.begin(), rows.end(), g_focus);
				int  i = at == rows.end() ? 0 : static_cast<int>(at - rows.begin());
				if (nav) {
					i = (i + nav + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
					g_focus = rows[i];
					if (g_focus >= kRowTrial && g_focus < kRowTrial + 5) g_trialSel = g_focus - kRowTrial;
					PlayUi(UiSnd::kMove, 0.7f);  // (a soft stone tick)
				} else if (side) {
					if (g_focus == kRowPerk) {
						g_sel = (g_sel + side + n) % n;
					} else if (trials && g_focus >= kRowTrial && g_focus < kRowTrial + 5) {
						g_focus = kRowSpell;  // back to the altar's own column
					} else if (trials) {
						g_focus = kRowTrial + std::clamp(g_trialSel, 0, 4);  // over to its trials
					}
					PlayUi(UiSnd::kMove, 0.7f);
				} else {
					// (Noah: sounds for the altar's menu) a deep rune chime when something is done, a dull thud when it can't be
					// (too few septims, nothing to reforge, a bond not yet open); leaving has its own soft whoosh
					const int  pts0 = Points::Get();
					const auto ph0 = g_show.phase;
					const bool menu0 = g_menu;
					const int  trial0 = g_trialSel;
					switch (g_focus) {
					case kRowSpell: act(0); break;
					case kRowBow: act(6); break;
					case kRowForge: act(1); break;
					case kRowPerk: act(2); break;
					case kRowShout: act(3); break;
					case kRowBond: act(5); break;
					case kRowLeave: act(4); break;
					default:
						if (g_focus >= kRowTrial && g_focus < kRowTrial + 5) {
							g_trialSel = g_focus - kRowTrial;
							act(7);
						}
					}
					(void)trial0;
					if (g_focus == kRowLeave || (menu0 && !g_menu)) {
						PlayUi(UiSnd::kLeave);
					} else if (Points::Get() != pts0 || g_show.phase != ph0 || (g_focus >= kRowTrial && g_focus < kRowTrial + 5) || g_focus == kRowShout) {
						PlayUi(UiSnd::kChoose);
					} else {
						PlayUi(UiSnd::kFail);
					}
				}
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kKeyboard) {
				for (int k = 0; k < Settings::kKeys; ++k) {  // 1 2 3 4 Backspace unless rebound (Mod Configuration > The Curse of Alduin)
					if (static_cast<int>(code) == Settings::Key(k)) {
						act(k);
						return true;
					}
				}
				if (code >= 0x07 && code <= 0x0A) {  // 6 7 8 9: the trials, as listed
					static constexpr int kKeyTrial[4] = { 0, 1, 4, 3 };
					g_trialSel = kKeyTrial[static_cast<int>(code) - 0x07];
					Curse::StartTrial(ref ? TownOf(ref) : -1, g_trialSel);
					return true;
				}
				switch (code) {
				case 0xCB: g_sel = (g_sel + n - 1) % n; Forms::Play(gen::Sound::ui_move); return true;  // left arrow
				case 0xCD: g_sel = (g_sel + 1) % n; Forms::Play(gen::Sound::ui_move); return true;      // right arrow
				case 0x12:                                      // E (Activate): takes a waiting spell, else leaves
					if (!TakeOffer(ref)) {
						act(4);
					}
					return true;
				default: return false;
				}
			}
			if (dev == RE::INPUT_DEVICE::kMouse && (code == 8 || code == 9)) {
				g_sel = (g_sel + (code == 8 ? n - 1 : 1)) % n;
				Forms::Play(gen::Sound::ui_move);
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kGamepad) {
				switch (code) {
				case 0x2000: act(4); PlayUi(UiSnd::kLeave); return true;  // B (circle): leave
				case 0x4000:
				case 0x8000:
				case 0x000A:
				case 0x0100:
				case 0x0200: return true;  // (one way through the altar now: the D-pad and A)
				default: return false;
				}
			}
			return false;
		}

		// dev: press one of the shrine's actions (0 call, 1 reforge, 2 bless, 3 relic, 4 leave, 5 bond) as its key would
		void DevAct(int a_action)
		{
			static constexpr std::uint32_t kCodes[] = { 0x02, 0x03, 0x04, 0x05, 0x0E, 0x06, 0x30 };
			if (a_action < 0 || a_action > 6) {
				return;
			}
			auto* ev = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, "", kCodes[a_action], 1.0f, 0.0f);
			logger::info("dev: shrine action {} -> {}", a_action, OnButton(ev) ? "taken" : "shrine not open");
			RE::free(ev);
		}

		void Tick(float a_delta)
		{
			if (g_menu) {
				auto r = g_menuRef.get();
				auto* player = RE::PlayerCharacter::GetSingleton();
				static float off = 0;
				off = r && OnAltarBase(r.get()) ? 0.0f : off + a_delta;
				if (!r || !player || off > 0.6f) {  // (stepped off the altar: its menu closes)
					g_menu = false;
					off = 0;
				}
			}
			if (!g_menu) {
				g_choosing = false;  // (walked away: the spell waits on the altar a while yet)
			}
			static float trim = 0;
			if ((trim -= a_delta) <= 0) {  // a character from before the two-spell rule keeps the two in hand
				trim = 3.0f;
				if (auto have = Arcana::Carried(); have.size() > 2 && !Gun::TestKitOn()) {  // (a test character keeps all 17)
					for (std::size_t i = 2; i < have.size(); ++i) {
						Arcana::Forget(have[i]);
					}
					Hud::Banner("The altars let you carry two of their spells - the rest went back", 4.0f);
				}
			}
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.perkMenu = g_menu;
				h.perkSel = g_sel;
				h.altarFocus = g_focus, h.chooseFocus = g_chooseFocus;
				h.rollPhase = static_cast<int>(g_show.phase);
				h.rollT = g_show.phase == Show::Charging ? std::min(1.0f, g_show.t / kChargeTime) : g_show.phase == Show::Spinning ? std::min(1.0f, g_show.t / gen::kShowSpin) : g_show.phase == Show::Offering ? std::min(1.0f, g_show.t / gen::kShowOffer) : 0.0f;
				h.rollGun = g_show.phase == Show::Idle || g_show.phase == Show::Charging || g_show.shown < 0 ? -1 : (g_show.bow ? Bows::kIdBase : Arcana::kIdBase) + g_show.shown;
				h.rollBow = g_show.bow && g_show.phase != Show::Idle;
				h.rollPrize = g_show.prize && g_show.phase == Show::Offering ? g_show.pz.name : std::string();
				h.bowRollCost = BowRollCost();
				h.bowCarried = Bows::Carried();
				h.rollTarget = g_show.phase == Show::Spinning || g_show.phase == Show::Offering ? g_show.gun : -1;
				h.rollCost = RollCost();
				// where the shrine's crown is on the screen: the roll shows there too, over the crystal itself
				h.rollOnScreen = false;
				h.beamLit = 0, h.beamGrow = 0;
				if (g_show.phase == Show::Idle || g_show.phase == Show::Offering) {
					if (auto hr = g_glowHeart.get()) {
						HeartGlow(hr.get(), 1.0f);  // (its runes back to their own light)
					}
					g_glowHeart = {};
				}
				if (auto chest = g_show.chest.get(); chest && g_show.phase != Show::Idle && g_show.phase != Show::Offering) {
					// (the roll: the six column stones light beams into the heart stone one after another as it gathers, all
					// six while it spins; the stone's runes burn brighter the faster it turns)
					auto* cam = RE::Main::WorldRootCamera();
					// (one at a time across the spin: beam q leaves its stone at q/6 of the way and strikes the heart at (q+1)/6)
					const float kk = g_show.phase == Show::Spinning ? std::clamp(g_show.t / gen::kShowSpin, 0.0f, 1.0f) * 6.0f : -1.0f;
					const float lit = kk < 0 ? 0.0f : std::min(6.0f, std::floor(kk) + 1.0f);
					h.beamGrow = kk < 0 ? 0.0f : kk >= 6.0f ? 1.0f : kk - std::floor(kk);
					RE::TESObjectREFR* heart = nullptr;
					std::array<RE::TESObjectREFR*, 6> rocks{};
					RE::TES::GetSingleton()->ForEachReferenceInRange(chest.get(), 420.0f, [&](RE::TESObjectREFR* r) {
						if (!r || r->IsDisabled() || r->IsDeleted()) {
							return RE::BSContainer::ForEachResult::kContinue;
						}
						auto* b = r->GetBaseObject();
						if (b && (b == Forms::Ours(gen::kHeartStone) || b == Forms::Ours(gen::kHeartStoneLit))) {
							heart = r;
						}
						for (int q = 0; q < 6; ++q) {
							if (b && b == Forms::Ours(gen::kPillarRock[q])) rocks[q] = r;
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
					auto screen = [&](const RE::NiPoint3& a_p, float& x, float& y) {
						float z = 0;
						cam->WorldPtToScreenPt3(a_p, x, y, z, 1e-5f);
						y = 1.0f - y;
						return z > 0;
					};
					if (cam && heart) {
						const auto hp = heart->Get3D() ? heart->Get3D()->world.translate : heart->GetPosition();
						if (screen(hp, h.beamToX, h.beamToY)) {
							for (int q = 0; q < 6; ++q) {
								if (rocks[q] && q < static_cast<int>(lit) && !g_beams3D) {  // (drawn only if the world's beams can't fly)
									const auto rp = rocks[q]->Get3D() ? rocks[q]->Get3D()->world.translate : rocks[q]->GetPosition();
									if (screen(rp, h.beamFrom[q * 2], h.beamFrom[q * 2 + 1])) {
										h.beamLit = q + 1;
									}
								}
							}
							if (h.beamLit < static_cast<int>(lit)) {
								h.beamGrow = 1.0f;  // (the newest stone is off the screen: what shows is all struck)
							}
						}
						// its runes: the stone's own glow, brighter the further the roll has gone
						const float k = std::clamp(std::floor(std::max(0.0f, kk) + 1e-4f), 0.0f, 6.0f) / 6.0f;  // (a step brighter with each beam struck)
						const float sinceStrike = kk >= 1.0f ? kk - std::floor(kk) : 1.0f;  // (each strike: the runes flare white-hot, then settle)
						HeartGlow(heart, 1.0f + 2.5f * k + 4.0f * std::max(0.0f, 1.0f - sinceStrike * 5.0f));
						g_glowHeart = heart->CreateRefHandle();
					}
				}
				if (auto chest = g_show.chest.get(); chest && g_show.phase != Show::Idle) {
					if (auto* cam = RE::Main::WorldRootCamera()) {
						const auto at = Local(chest.get(), 0, 0, kOrbZ * chest->GetScale());
						float x = 0, y = 0, z = 0;
						cam->WorldPtToScreenPt3(at, x, y, z, 1e-5f);
						if (z > 0 && x > 0 && x < 1 && y > 0 && y < 1) {
							h.rollOnScreen = true, h.rollScreenX = x, h.rollScreenY = 1.0f - y;
							h.rollDist = at.GetDistance(RE::PlayerCharacter::GetSingleton()->GetPosition());
						}
					}
				}
				h.pulseOn = false;
				if (g_pulse && (g_pulse->t -= a_delta) > 0) {
					h.pulseT = g_pulse->t;
					if (auto r = g_pulse->shrine.get(); r) {
						if (auto* cam = RE::Main::WorldRootCamera()) {
							float x = 0, y = 0, z = 0;
							cam->WorldPtToScreenPt3(r->GetPosition() + RE::NiPoint3{ 0, 0, 175.0f }, x, y, z, 1e-5f);
							h.pulseOn = true, h.pulseX = z > 0 ? x : 0.5f, h.pulseY = z > 0 ? 1.0f - y : 0.5f;
						}
					}
				} else {
					g_pulse.reset();
				}
				h.shrineShout = -1;
				h.dragonBond = Save::DragonBond();
				h.bondLocked = Movement::BondLocked();
				h.trialsOn = false;
				h.whispers.clear();
				h.chooseOn = g_choosing && g_show.phase == Show::Offering;
				if (h.chooseOn) {
					const auto have = Arcana::Carried();
					h.chooseNew = g_show.gun;
					h.chooseA = have.size() > 0 ? have[0] : -1;
					h.chooseB = have.size() > 1 ? have[1] : -1;
					h.chooseRefund = g_lastRollCost / 2;
				}
				if (g_menu) {
					if (auto r = g_menuRef.get()) {
						const int t = TownOf(r.get());
						h.shrineShout = Curse::ClaimableShout(t);
						h.whispers = Eggs::Whispers(t);  // (Noah: the hidden quest's whispers, a line more for each step found)
						if (const auto v = Curse::Altar(t); v.on) {  // the city still held: its trials
							h.trialsOn = true;
							h.trialDone = v.done, h.trialActive = v.active, h.trialSel = g_trialSel;
							h.trialDesc = v.desc;
							h.trialCity = std::string(gen::kTowns[t]);
							h.trialBoss = v.boss, h.trialBossLine = v.bossLine;
						}
					}
				}
			}
			auto rolling = g_show.phase == Show::Charging || g_show.phase == Show::Spinning ? g_show.chest.get() : RE::NiPointer<RE::TESObjectREFR>{};
			static float clock = 0;
			clock += a_delta;
			RE::NiPoint3 camPos{}, orbAt{};
			float        orbDist = 1e9f;
			RE::NiPointer<RE::TESObjectREFR> orbRef;
			if (auto* pc = RE::PlayerCamera::GetSingleton(); pc && pc->cameraRoot) {
				camPos = pc->cameraRoot->world.translate;
			}
			for (auto& s : g_spinners) {
				if (auto r = s.ref.get(); r && r->Is3DLoaded()) {
					// the shrine's heart turns faster while it rolls (the crystal races, the shards whirl). Statics never move
					// by their angle while loaded, so their 3D node is turned (and the rocks bob) directly.
					const float boost = rolling && r->GetPosition().GetDistance(rolling->GetPosition()) < 400.0f ? (g_show.phase == Show::Charging ? 3.0f : 7.0f) : 1.0f;
					auto* node = r->Get3D() ? r->Get3D()->AsNode() : nullptr;
					if (!node) {
						continue;
					}
					if (!s.based || s.node != node) {
						s.baseZ = node->local.translate.z, s.based = true, s.node = node, s.ang = r->GetAngleZ();
					}
					s.ang += s.degPerSec * boost * RE::NI_PI / 180.0f * a_delta;
					node->local.rotate.SetEulerAnglesXYZ(0.0f, 0.0f, -s.ang);
					const float ph = static_cast<float>(reinterpret_cast<std::uintptr_t>(node) % 628) * 0.01f;
					node->local.translate.z = s.baseZ + std::sin(clock * (s.degPerSec > 20.0f ? 1.3f : 0.7f) + ph) * (s.degPerSec > 20.0f ? 4.0f : 9.0f);  // floating
					RE::NiUpdateData ud;
					node->Update(ud);
					if (auto* hb = r->GetBaseObject(); hb && (hb == Forms::Ours(gen::kHeartStone) || hb == Forms::Ours(gen::kHeartStoneLit))) {  // the altar's heart stone: the nearest one is the one you look into
						const float d = node->world.translate.GetDistance(camPos);
						if (d < orbDist) {
							orbDist = d, orbAt = node->world.translate, orbRef = r;
						}
					}
				}
			}
			// near a shrine you hear it: a deep, breathing hum with a crystal shimmer (Noah: over a wide area, swelling slowly
			// as you walk toward it and dying away as you walk off) - heard from about 30 m, full within 4 m of the orb, its
			// loudness following your distance on an eased curve and never jumping (it moves at most 0.35 a second)
			{
				bool&        humming = g_humming;
				static float vol = 0;
				constexpr float kFar = 2200.0f, kNear = 260.0f;
				float want = 0;
				if (orbRef && orbDist < kFar) {
					const float t = std::clamp((kFar - orbDist) / (kFar - kNear), 0.0f, 1.0f);
					want = t * t * (3 - 2 * t);
					want *= want;  // (faint for a long way, rising as you come close)
				}
				if (!humming) vol = 0;
				const float step = 0.35f * a_delta;
				vol += std::clamp(want - vol, -step, step);
				if (!humming && want > 0.001f && orbRef) {
					// (on you - its loudness is the distance: a sound placed nowhere played at the world's origin, unheard)
					Audio::LoopStart(Audio::kLoopShrine, gen::Sound::shrine_hum, RE::PlayerCharacter::GetSingleton());
					humming = true;
				}
				if (humming) {
					Audio::LoopVolume(Audio::kLoopShrine, vol);
					if (vol <= 0.001f && want <= 0.001f) {
						Audio::LoopStop(Audio::kLoopShrine, 0);
						humming = false;
					}
				}
			}
			// the orb's aura of cold flame (the main menu's, drawn round the orb on screen): while it is near and in sight
			{
				static float losT = 0, seen = 0;
				static bool  los = false;
				auto*        cam = RE::PlayerCamera::GetSingleton();
				auto*        pl = RE::PlayerCharacter::GetSingleton();
				bool         on = false;
				float        x = 0, y = 0, rr = 0;
				if (orbRef && cam && pl && orbDist < 4500.0f) {
					if ((losT -= a_delta) <= 0) {
						losT = 0.2f;
						bool unused = false;
						los = orbDist < 700.0f || pl->HasLineOfSight(orbRef.get(), unused);
					}
					float zc = 0, x2 = 0, y2 = 0, z2 = 0;
					auto* wc = RE::Main::WorldRootCamera();
					if (wc) {
						wc->WorldPtToScreenPt3(orbAt, x, y, zc, 1e-5f);
					}
					RE::NiPoint3 top = orbAt;
					top.z += 22.0f;  // the orb's radius
					if (wc) {
						wc->WorldPtToScreenPt3(top, x2, y2, z2, 1e-5f);
					}
					rr = std::abs(y2 - y);
					on = zc > 0 && los && x > -0.2f && x < 1.2f && y > -0.2f && y < 1.2f;
					y = 1.0f - y;
				}
				// looking straight at the orb (close enough to touch the shrine) picks the shrine, as its floor does
				g_orbAim = {};
				if (orbRef && cam && cam->cameraRoot && orbDist < 650.0f && !g_menu) {
					const auto& m = cam->cameraRoot->world.rotate;
					const RE::NiPoint3 fwd{ m.entry[0][1], m.entry[1][1], m.entry[2][1] };
					const RE::NiPoint3 to = orbAt - camPos;
					const float along = to.Dot(fwd);
					const RE::NiPoint3 closest = camPos + fwd * along;
					if (along > 0 && closest.GetDistance(orbAt) < 55.0f) {
						for (std::size_t t = 0; t < gen::kTowns.size(); ++t) {
							if (auto* shrine = Placed(static_cast<int>(t), 0); shrine && shrine->GetPosition().GetDistance(orbAt) < 400.0f && OnAltarBase(shrine)) {
								g_orbAim = shrine->CreateRefHandle();
								break;
							}
						}
					}
				}
				seen = on ? std::min(1.0f, seen + a_delta * 3.0f) : std::max(0.0f, seen - a_delta * 3.0f);
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.orbOn = seen > 0.01f && rr > 0;
				if (on) {
					h.orbX = x, h.orbY = y, h.orbR = rr;
				}
				h.orbFade = seen * std::clamp((4500.0f - orbDist) / 1500.0f, 0.0f, 1.0f);
				h.orbAim = static_cast<bool>(g_orbAim);
				// (the shrine's floor under the crosshair counts too: ours is the only prompt there) - and anything else of
				// ours under it (the shrine's dressing, its bottles): Skyrim's own prompt stays away from all of them
				h.ownAim = false;
				if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
					if (auto ref = pick->GetActiveTarget().get()) {
						if (auto* base = ref->GetBaseObject(); base && base->GetFile(0) && base->GetFile(0)->GetFilename() == "CurseOfAlduin.esp") {
							h.ownAim = true;
						}
						for (std::size_t t = 0; !h.orbAim && !g_menu && t < gen::kTowns.size(); ++t) {
							if (Placed(static_cast<int>(t), 0) == ref.get() && OnAltarBase(ref.get())) {
								h.orbAim = true;
							}
						}
					}
				}
			}
			TickShow(a_delta);
			{  // our roll holds the altar for the party while it turns (and lets go when it's done)
				const int busy = g_show.phase != Show::Idle ? TownOfAltar(g_show.chest.get().get()) : -1;
				static float resend = 0;
				if (busy != g_myBusy || (busy >= 0 && (resend -= a_delta) <= 0)) {
					if (g_myBusy >= 0 && busy != g_myBusy) Net::AltarBusy(g_myBusy, false);
					if (busy >= 0) Net::AltarBusy(busy, true), resend = 5.0f;
					g_myBusy = busy;
				}
			}
			if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
				TickMarkers(pl, a_delta);
			}
			const bool searching = std::any_of(g_towns.begin(), g_towns.end(), [](const TownPlan& p) { return p.state == TownPlan::State::Searching; });
			if (!searching && (g_scan -= a_delta) > 0) {
				return;
			}
			g_scan = 2.0f;
			TickTowns();
		}

		// The city's Curse is lifted: the shrine sends out a wave - a burst of light that rolls outward three times, a
		// deep toll heard across the city - and from now on it burns gold with a beam of light over it (Dress: freed pieces).
		void FreedPulse(int a_town)
		{
			auto* shrine = Placed(a_town, 0);
			if (!shrine) {
				return;
			}
			const auto at = shrine->GetPosition() + RE::NiPoint3{ 0, 0, 175.0f };
			Gun::FxBlast("lightmassive", at);
			Gun::FxBlast("shoutarea", at, 0.4f);
			Gun::FxBlast("lightmassive", at, 1.4f);
			Gun::FxBlast("turnundead", at, 2.4f);
			Forms::Play(gen::Sound::nuke);
			Forms::PlayAt(gen::Sound::shout_toll, shrine);
			g_pulse = Pulse{ shrine->CreateRefHandle(), 3.5f };
			for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
				if (gen::kLocations[i].town == a_town) {
					g_dressed[i] = false;  // dress it again now: the gold glow and the beam
				}
			}
			logger::info("shops: {}'s shrine pulses - the Curse is lifted", gen::kTowns[a_town]);
		}

		void OnActivate(RE::TESObjectREFR* a_ref)
		{
			const int obj = a_ref ? Forms::ObjectIndexOf(a_ref->GetBaseObject()) : -1;
			if (obj < 0) {
				return;
			}
			const auto& o = gen::kObjects[obj];
			logger::info("shops: activated {} ({} points)", o.id, Points::Get());
			switch (o.kind) {
			case 0:  // the shrine: take a waiting spell, else open (or close) its menu - only standing on it
				if (!OnAltarBase(a_ref) && !(g_menu && g_menuRef.get().get() == a_ref)) {
					Hud::Banner("Step up onto the altar", 1.5f);
					break;
				}
				if (TakeOffer(a_ref)) {
					break;
				}
				if (g_menu && g_menuRef.get().get() == a_ref) {
					Forms::Play(gen::Sound::ui_close);
					g_menu = false;
					g_closedAt = std::chrono::steady_clock::now();
				} else {
					g_menu = true;
					g_focus = kRowSpell, g_chooseFocus = 0;
					g_menuRef = a_ref->CreateRefHandle();
					Forms::PlayAt(gen::Sound::coffer_open, a_ref);
					PlayUi(UiSnd::kOpen, 0.6f);
				}
				break;
			case 1: BuyPerk(o.ref, a_ref); break;
			case 4:
				if (!g_menu) {
					g_menu = true, g_sel = 0, g_focus = kRowSpell, g_chooseFocus = 0;
					g_menuRef = a_ref->CreateRefHandle();
					Forms::Play(gen::Sound::coffer_open);
				} else {
					BuyPerk(g_sel, a_ref);
				}
				break;
			case 3: BuyAmmo(o); break;
			case 6:
				Curse::ClaimRelic(TownOf(a_ref));
				break;
			case 5:
				if (Gun::Reforge()) {
					Forms::PlayAt(gen::Sound::forge_upgrade, a_ref);
				}
				break;
			default: BuyWall(o); break;
			}
		}
	}
}

namespace bo3z::Shops
{
	// (Noah: every load in a running game crashed) the station icons' ExtraMapMarker records were made by hand (a game
	// marker's data copied), and Skyrim tearing the world down for a load choked on them. Before any load they are
	// lifted off their markers (not freed: the engine never touches them again) - a loaded save has its own.
	void BeforeLoad()
	{
		int n = 0;
		for (auto& h : g_markers) {
			if (auto r = h.get()) {
				if (auto* x = r->extraList.GetByType<RE::ExtraMapMarker>(); x && g_madeMarkers.contains(x)) {
					r->extraList.Remove(RE::ExtraDataType::kMapMarker, x);
					++n;
				}
			}
			h = {};
		}
		g_madeMarkers.clear();
		g_markerScanned = {};
		if (n) logger::info("shops: {} hand-made map markers lifted before the load", n);
	}
	std::vector<std::uint8_t> RollState()
	{
		RollNight();
		std::vector<std::uint8_t> out(2 + kRollTowns * 2);
		out[0] = static_cast<std::uint8_t>(g_rollNight & 0xFF), out[1] = static_cast<std::uint8_t>((g_rollNight >> 8) & 0xFF);
		for (int t = 0; t < kRollTowns; ++t) out[2 + t] = g_spellRolls[t], out[2 + kRollTowns + t] = g_bowRollsT[t];
		return out;
	}
	void SetRollState(const std::vector<std::uint8_t>& a_s)
	{
		g_spellRolls.fill(0), g_bowRollsT.fill(0), g_rollNight = -1;
		if (a_s.size() != 2 + kRollTowns * 2) return;
		g_rollNight = a_s[0] | (a_s[1] << 8);
		for (int t = 0; t < kRollTowns; ++t) g_spellRolls[t] = a_s[2 + t], g_bowRollsT[t] = a_s[2 + kRollTowns + t];
	}
	void HeardBusy(int a_town, const std::string& a_who, bool a_on)
	{
		if (a_town < 0 || a_town >= 16) return;
		if (a_on) g_busy[a_town] = { a_who, std::chrono::steady_clock::now() + std::chrono::seconds(12) };
		else g_busy[a_town].who.clear();
	}
}
