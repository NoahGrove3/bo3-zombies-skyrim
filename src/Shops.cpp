#include "BO3Z.h"

// Mystery Box, perk machines and wall-buys (sheets: objects, locations). Each is placed at runtime next to
// its town's map marker, snapped to the nearest walkable navmesh point, and re-used if it is already there.
// Activating one (E) spends points.
namespace bo3z
{
	namespace
	{
		std::array<RE::ObjectRefHandle, gen::kLocations.size()> g_placed{};
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
		// (Noah: "when I freed the altar, it didn't get the altar freeing animation. I wanted to use the same lightning charge
		// up effect that we used when we had the roll for weapons. And then I want the particles to fade in and the golden
		// runic symbol grow above the altar") a freed altar's moment, in three parts: the charge (the six rune stones strike
		// the heart one after another with the roll's lightning, its runes burning brighter), the burst (the curse breaks:
		// the freed look is put up), then the motes fade in and the golden rune circle grows from nothing over the altar
		struct Pulse { RE::ObjectRefHandle shrine; float t; int town; bool burst; bool started; };
		std::optional<Pulse>                     g_pulse;  // a freed shrine's moment (FreedPulse); t counts down
		constexpr float                          kPulseSeconds = 9.0f, kPulseCharge = 3.6f, kPulseGrow = 3.2f;
		// the freed look waits for the burst (the charge plays on the cursed altar)
		bool PulseHolds(int a_town) { return g_pulse && g_pulse->town == a_town && !g_pulse->burst; }
		// (dev freeloop on a city not yet freed - Noah: "it's still missing the golden rune ... and the blue particles ... those should
		// stay at the end of the echo altar's animation") the freed look shown there all the same, while the loop runs
		std::array<bool, gen::kTowns.size()> g_devFreed{};
		bool FreedLook(int a_town) { return Curse::Freed(a_town) || (a_town >= 0 && a_town < static_cast<int>(g_devFreed.size()) && g_devFreed[a_town]); }
		std::vector<Spinner>                     g_spinners;
		RE::ObjectRefHandle                      g_orbAim;  // the shrine whose orb is in the crosshair (Activate opens it)
		bool                                     g_humming = false;  // the shrine's hum is playing (a load stops every loop)

		constexpr float kOrbZ = 300.0f;   // just over the altar's heart stone (its centre at z 175, its top ~266)

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
			const bool freed = FreedLook(gen::kLocations[a_index].town) && !PulseHolds(gen::kLocations[a_index].town);
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
				// (dev freeloop: a freed city's freeing played again) its freed pieces kept - hidden through the charge, grown at the burst
				const bool replay = PulseHolds(gen::kLocations[a_index].town) && FreedLook(gen::kLocations[a_index].town);
				if (d.freed && !freed && !replay) {
					// (Noah: "when I load into a save, the altar has the particle effects before it's been freed") a freed
					// piece left from another game (or placed a moment before this save's own state was read) is taken away
					RE::TES::GetSingleton()->ForEachReferenceInRange(a_main, 700.0f, [&](RE::TESObjectREFR* r) {
						if (r && r->GetBaseObject() == base && !r->IsDeleted() && !r->IsDisabled()) {
							r->Disable();
							r->SetDelete(true);
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
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
		// a freed piece's light, k times its own (0: unseen, 1: as made) - an effect shader's colour scale, a lit shader's glow
		void Fade(RE::NiAVObject* a_root, float a_k)
		{
			static std::unordered_map<void*, float> base;
			if (!a_root) return;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* g) {
				auto* prop = g->GetGeometryRuntimeData().shaderProperty.get();
				auto* rtti = prop ? prop->GetRTTI() : nullptr;
				if (!rtti) return RE::BSVisit::BSVisitControl::kContinue;
				const std::string_view kind = rtti->GetName();
				if (kind == "BSEffectShaderProperty") {
					if (auto* m = static_cast<RE::BSEffectShaderMaterial*>(static_cast<RE::BSShaderProperty*>(prop)->material)) {
						auto [it, fresh] = base.try_emplace(m, m->baseColorScale);
						m->baseColorScale = it->second * a_k;
					}
				} else if (auto* lp = netimmerse_cast<RE::BSLightingShaderProperty*>(prop)) {
					auto [it, fresh] = base.try_emplace(lp, lp->emissiveMult);
					lp->emissiveMult = it->second * a_k;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
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

	namespace
	{

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
			if (probe && (probe->IsDeleted() || probe->GetParentCell() != a_anchor->GetParentCell())) {  // (the audit: never one left in a cell let go)
				probe->Disable();
				probe->SetDelete(true);
				probe.reset();
			}
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
			// (Noah: "the Whiterun / Markarth / Solitude / Windhelm altar is in the floor") a hand-placed spot's height is
			// where he stood: the walled cities' paving isn't seen by the floor search (it found the land far under the
			// streets), so a floor found far from his height gives way to it
			if (const auto& st = gen::kStations[a_town]; st.fixed && std::abs(plan.top - (st.z + kStandLift)) > 40.0f) {
				logger::info("shops: {} floor search said z {:.0f} - the placed height {:.0f} is used", gen::kTowns[a_town], plan.top, st.z + kStandLift);
				plan.top = st.z + kStandLift;
			}
			plan.topSet = true;
			logger::info("shops: {} stand floor at z {:.0f}", gen::kTowns[a_town], plan.top);
			ClearSite(a_town, a_anchor);
		}


		void PlaceIfMissing(std::size_t a_index)
		{
			const auto& loc = gen::kLocations[a_index];
			// (the Tablet of Mysteries stands at one freed city at a time: anywhere else it is taken away)
			if (gen::kObjects[loc.object].kind == 7 && loc.town != Tablet::Town()) {
				if (auto r = g_placed[a_index].get()) {
					r->Disable(), r->SetDelete(true);
					logger::info("shops: the tablet leaves {}", gen::kTowns[loc.town]);
				}
				g_placed[a_index] = {};
				return;
			}
			if (auto ref = g_placed[a_index].get(); ref && !ref->IsDeleted()) {
				// (Noah: "when I first open the door into a city the Echo Altar doesn't load in properly until I leave and
				// come back") a station piece whose cell is up and in view but whose body never loaded is turned off and
				// on again - Skyrim then loads it, as it does coming back in
				{
					static std::array<std::chrono::steady_clock::time_point, 64> since{};
					auto* pl = RE::PlayerCharacter::GetSingleton();
					auto* cell = ref->GetParentCell();
					const auto now = std::chrono::steady_clock::now();
					const bool missing = pl && cell && cell->IsAttached() && !ref->IsDisabled() && !ref->Is3DLoaded() &&
					                     ref->GetWorldspace() == pl->GetWorldspace() && ref->GetPosition().GetDistance(pl->GetPosition()) < 4000.0f;
					if (a_index < since.size()) {
						if (!missing) {
							since[a_index] = {};
						} else if (since[a_index] == std::chrono::steady_clock::time_point{}) {
							since[a_index] = now;
						} else if (now - since[a_index] > std::chrono::milliseconds(5000)) {
							since[a_index] = {};
							// (the audit: turned off and on while its model sat in the loader, every 1.5 s if a mesh was missing)
							// five seconds' wait, and at most twice for each piece in a session
							static std::unordered_map<RE::FormID, int> toggles;
							if (toggles.size() > 500) toggles.clear();
							if (toggles[ref->GetFormID()]++ < 2) {
								ref->Disable();
								ref->Enable(false);
							}
							logger::info("shops: {} in {} hadn't loaded - turned off and on to load it", gen::kObjects[loc.object].id, gen::kTowns[loc.town]);
						}
					}
				}
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
			// (Noah: the Tablet of Mysteries' spots placed by hand) a fixed spot stands where it was marked, turned as marked
			auto want = loc.fixed ? RE::NiPoint3{ loc.x, loc.y, loc.z } : SlotPos(plan, loc.slot);
			const float sweep = loc.fixed ? std::max<float>(gen::kPlanSweep, anchor->GetPosition().GetDistance(want) + 300.0f) : gen::kPlanSweep;
			RE::TESObjectREFR* found = nullptr;
			RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, sweep, [&](RE::TESObjectREFR* r) {
				if (r && r->GetBaseObject() == base && !r->IsDeleted() && std::hypot(r->GetPosition().x - want.x, r->GetPosition().y - want.y) < 80.0f) {
					found = r;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			// the shrine brings its own dais - (Noah: the cone under it showed, the altar rose off the floor) sunk so its
			// cobbled skirt is all below the ground and the dais floor lies flush with it
			const float floorZ = loc.fixed ? loc.z : gen::kObjects[loc.object].kind == 0 ? plan.top - kAltarSink : plan.top;
			want.z = floorZ;
			if (!found) {
				auto ref = anchor->PlaceObjectAtMe(base, false);
				if (!ref) {
					return;
				}
				ref->SetScale(gen::kObjects[loc.object].scale);
				ref->SetPosition(want);
				ref->SetAngle({ 0, 0, loc.fixed ? loc.yawDeg * RE::NI_PI / 180.0f : SlotFacing(plan, loc.slot) });
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
			if (loc.fixed) {  // (a tablet an older build stood beside the altar: taken away, the marked spot's the one)
				std::vector<RE::TESObjectREFR*> old;
				RE::TES::GetSingleton()->ForEachReferenceInRange(anchor, sweep, [&](RE::TESObjectREFR* r) {
					if (r && r != found && r->GetBaseObject() == base && !r->IsDeleted()) old.push_back(r);
					return RE::BSContainer::ForEachResult::kContinue;
				});
				for (auto* r : old) r->Disable(), r->SetDelete(true);
				if (!old.empty()) logger::info("shops: {} older tablet(s) in {} taken away", old.size(), gen::kTowns[loc.town]);
			}
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
			if (Curse::Freed(a_town)) name += " - Curse Lifted";  // (Noah: show on the map that it's freed)
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

		bool g_menu = false;
		RE::ObjectRefHandle g_menuRef;

		constexpr int kRollTowns = 16;   // (the co-save's old roll counts: their bytes kept, zero, so the Tablet's state after them reads as before)
		int TownOfAltar(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return -1;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()) && t < kRollTowns; ++t) {
				if (Shops::Placed(t, 0) == a_ref) return t;
			}
			return -1;
		}
		// (Noah) the altar's late rows open with the cities freed: the reforge at three, the Legendary roll at six
		int FreedCount()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		constexpr int kForgeAt = 3;


		int                 g_sel = 0;
		int                 g_trialSel = 0;  // the controller's pick among the altar's trials
		// (Noah: one way to drive the altar) a focus moved up and down its rows - D-pad or W/S - and A / Enter to take it
		enum Row { kRowSpell = 0, kRowBow, kRowForge, kRowPerk, kRowShout, kRowBond, kRowTrial = 10, kRowLeave = 20, kRowClass = 30, kRowOutfit };  // (the altar rework: Kit rows are 40.. your kit, 100.. the Armory)
		int g_focus = kRowClass;
		// (Noah: "different sections that show a drop down of what's in that section") the one section open (0: none)
		enum Sec { kSecKit = 210, kSecArmory = 211, kSecPerks = 212, kSecVoice = 213, kSecTrials = 214, kSecWhispers = 215, kSecGlider = 216 };
		int g_open = kSecKit;
		RE::ObjectRefHandle g_pickAltar;  // the altar whose class picker is up (no class yet): it opens when one is chosen

		void BuyPerk(int perk, RE::TESObjectREFR* a_ref)
		{
			if (perk < 0 || perk >= static_cast<int>(gen::kPerks.size())) {
				return;
			}
			if (Save::PerkStacks(perk) > 0) {  // (Noah: "the option to refund perks at 50% return") a taken brew: given back
				Perks::Refund(perk);
				return;
			}
			int held = 0;  // (Noah: "change the perk limit to 5")
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
				// (Noah: "scroll wheel and left click for keyboard and mouse - walking away or Esc") W A S D walk you off the altar now
				return code == 0x01 || code == 0x12 || code == 0xCB || code == 0xCD || code == 0xC8 || code == 0xD0 || code == 0x1C || code == 0x9C;
			case RE::INPUT_DEVICE::kMouse: return code == 0 || code == 8 || code == 9;  // (left click: select)
			case RE::INPUT_DEVICE::kGamepad: return code == 0x4000 || code == 0x8000 || code == 0x1000 || code == 0x2000 || code == 0x0001 || code == 0x0002 || code == 0x0004 || code == 0x0008 || code == 0x0100 || code == 0x0200 || code == 0x000A || code == 0x0009;
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
			const auto menuPerks = Perks::Menu();  // (Noah: each class its own perks) the shared draughts and your class's
			const int  n = std::max(1, static_cast<int>(menuPerks.size()));
			g_sel = std::clamp(g_sel, 0, n - 1);
			// (Noah: "give the special ammo, outfit change and class change their own tabs instead of keybinds") - rows now
			auto*      ref = g_menuRef.get().get();
			auto act = [&](int a) {
				switch (a) {
				case 1:
					if (const int fc = FreedCount(); fc < kForgeAt && !Gun::TestKitOn()) {  // (Noah: the reforge opens once three altars are freed)
						Hud::Banner("The altar's forge sleeps until three Echo Altars are freed (" + std::to_string(fc) + " of 3)", 3.0f);
						break;
					}
					if (Gun::Reforge() && ref) {
						Forms::PlayAt(gen::Sound::forge_upgrade, ref);
					}
					break;
				case 2: BuyPerk(Perks::MenuAt(g_sel), ref); break;
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
			const int  nav = (kb && code == 0xC8) || (pad && code == 0x0001) || (dev == RE::INPUT_DEVICE::kMouse && code == 8) ? -1
			               : (kb && code == 0xD0) || (pad && code == 0x0002) || (dev == RE::INPUT_DEVICE::kMouse && code == 9) ? 1
			                                                                                                                                 : 0;
			const int  side = (kb && code == 0xCB) || (pad && code == 0x0004) ? -1 : (kb && code == 0xCD) || (pad && code == 0x0008) ? 1 : 0;
			const bool pick = (kb && (code == 0x1C || code == 0x9C)) || (pad && code == 0x1000) || (dev == RE::INPUT_DEVICE::kMouse && code == 0);  // (Noah: Enter or left click; A / X on a pad)
			if (nav || side || pick) {
				auto* r = g_menuRef.get().get();
				const int t = r ? TownOf(r) : -1;
				// (Noah, the altar rework) the class; your kit (each item: its next upgrade); the Armory; then the brews, the
				// Thu'um, the dragon, the trials
				// (Noah: "redesign the Echo Altar UI so it's easier to navigate without having to scroll as much") sections, one
				// open at a time: the class, then the kit, the Armory, the perks, the Thu'um and dragon, the trials,
				// the whispers - each a header that opens to its rows - and leave
				std::vector<int> rows{ kRowClass };  // (Noah: the class is chosen and changed at the altar)
				const auto sectionOf = [](int id) {
					if (id >= 40 && id < 100) return static_cast<int>(kSecKit);
					if (id >= 100 && id < 200) return static_cast<int>(kSecArmory);
					if (id == kRowPerk) return static_cast<int>(kSecPerks);
					if (id == kRowShout) return static_cast<int>(kSecVoice);
					if (id == kRowBond) return static_cast<int>(kSecGlider);
					if (id >= kRowTrial && id < kRowTrial + 5) return static_cast<int>(kSecTrials);
					return 0;
				};
				if (Classes::Get() >= 0) {
					const auto kit = Kit::Rows();  // (Noah: "remove the infusion entirely")
					rows.push_back(kSecKit);
					if (g_open == kSecKit) for (int id : kit) if (id < 100) rows.push_back(id);
					if (std::any_of(kit.begin(), kit.end(), [](int id) { return id >= 100; })) {
						rows.push_back(kSecArmory);
						if (g_open == kSecArmory) for (int id : kit) if (id >= 100) rows.push_back(id);
					}
					// (Noah: the dragon glider its own section under the kit, seen once a class is chosen, locked until Whiterun is freed)
					rows.push_back(kSecGlider);
					if (g_open == kSecGlider) rows.push_back(kRowBond);
				}
				rows.push_back(kSecPerks);
				if (g_open == kSecPerks) rows.push_back(kRowPerk);
				const bool shoutRow = t >= 0 && Curse::ClaimableShout(t) >= 0;
				if (shoutRow) {
					rows.push_back(kSecVoice);
					if (g_open == kSecVoice) rows.push_back(kRowShout);
				}
				const bool trials = t >= 0 && Curse::Altar(t).on;
				if (trials) {
					rows.push_back(kSecTrials);
					if (g_open == kSecTrials) for (int q : { 0, 1, 4, 3 }) rows.push_back(kRowTrial + q);  // (the Unbroken folded into the Breath; 3 is the class trial)
				}
				if (r && !Eggs::Whispers(TownOf(r)).empty()) rows.push_back(kSecWhispers);
				rows.push_back(kRowLeave);
				if (std::find(rows.begin(), rows.end(), g_focus) == rows.end()) {  // (its section shut: back to its header)
					const int sec = sectionOf(g_focus);
					g_focus = sec && std::find(rows.begin(), rows.end(), sec) != rows.end() ? sec : kRowClass;
				}
				auto at = std::find(rows.begin(), rows.end(), g_focus);
				int  i = at == rows.end() ? 0 : static_cast<int>(at - rows.begin());
				if (nav) {
					i = (i + nav + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
					g_focus = rows[i];
					if (g_focus >= kRowTrial && g_focus < kRowTrial + 5) g_trialSel = g_focus - kRowTrial;
					PlayUi(UiSnd::kMove, 0.7f);  // (a soft stone tick)
				} else if (side && g_focus >= kSecKit && g_focus <= kSecGlider) {
					// right opens the section in focus, left shuts it
					g_open = side > 0 ? g_focus : (g_open == g_focus ? 0 : g_open);
					PlayUi(UiSnd::kMove, 0.7f);
				} else if (side) {
					if (g_focus == kRowPerk) {
						g_sel = (g_sel + side + n) % n;
					} else if (sectionOf(g_focus) && side < 0) {
						g_focus = sectionOf(g_focus);  // (left: back up to its header)
					}
					PlayUi(UiSnd::kMove, 0.7f);
				} else {
					// (Noah: sounds for the altar's menu) a deep rune chime when something is done, a dull thud when it can't be
					// (too few septims, nothing to reforge, a bond not yet open); leaving has its own soft whoosh
					const int  pts0 = Points::Get();
					const bool menu0 = g_menu;
					const int  trial0 = g_trialSel;
					bool       kitOk = false;
					if (g_focus >= kSecKit && g_focus <= kSecGlider) {  // a header: open it (shutting the one open), or shut it
						g_open = g_open == g_focus ? 0 : g_focus;
						PlayUi(UiSnd::kChoose);
						return true;
					}
					switch (g_focus) {
					case kRowClass:
						g_menu = false;
						g_closedAt = std::chrono::steady_clock::now();
						Classes::OpenPicker(Classes::Get() >= 0);  // (the first is free; a change costs)
						break;
					case kRowPerk: act(2); break;
					case kRowShout: act(3); break;
					case kRowBond: act(5); break;
					case kRowLeave: act(4); break;
					default:
						if (g_focus >= 40) {
							kitOk = Kit::Select(g_focus, ref);
							if (kitOk && g_focus >= 100) g_focus = g_focus - 100 + 40;  // (bought: the focus stays on it, now in your kit)
						} else if (g_focus >= kRowTrial && g_focus < kRowTrial + 5) {
							g_trialSel = g_focus - kRowTrial;
							act(7);
						}
					}
					if (kitOk) {
						PlayUi(UiSnd::kChoose);
						return true;
					}
					if (g_focus >= 40) {
						PlayUi(UiSnd::kFail);
						return true;
					}
					(void)trial0;
					if (g_focus == kRowLeave || (menu0 && !g_menu)) {
						PlayUi(UiSnd::kLeave);
					} else if (Points::Get() != pts0 || (g_focus >= kRowTrial && g_focus < kRowTrial + 5) || g_focus == kRowShout || g_focus == kRowOutfit) {
						PlayUi(UiSnd::kChoose);
					} else {
						PlayUi(UiSnd::kFail);
					}
				}
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kKeyboard && code == 0x01) {  // Esc: leave
				act(4);
				PlayUi(UiSnd::kLeave);
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kKeyboard) {
				for (int k = 0; k < Settings::kKeys; ++k) {  // 1 2 3 4 Backspace unless rebound (Mod Configuration > The Curse of Alduin)
					if (static_cast<int>(code) == Settings::Key(k)) {
						if (k == 0 || k == 1 || k == 6) return true;  // (the altar rework: no roll, no reforge key - the rows do it)
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
				case 0x12:                                      // E (Activate): leaves
					act(4);
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
			// the class picker opened from an altar (no class yet) closed: with a class chosen, that altar opens on its rows
			if (g_pickAltar && !Classes::Open()) {
				auto pr = g_pickAltar.get();
				g_pickAltar.reset();
				if (pr && Classes::Get() >= 0 && OnAltarBase(pr.get()) && !g_menu) {
					g_menu = true;
					g_focus = kRowClass, g_sel = 0;
					g_menuRef = pr->CreateRefHandle();
					PlayUi(UiSnd::kOpen, 0.6f);
				}
			}
			static float trim = 0;
			if ((trim -= a_delta) <= 0) {  // a character from before the two-spell rule keeps the two in hand
				trim = 3.0f;
				if (auto have = Arcana::Carried(); have.size() > Arcana::CarryCap() && !Gun::TestKitOn()) {  // (a test character keeps all 17)
					for (std::size_t i = Arcana::CarryCap(); i < have.size(); ++i) {
						Arcana::Forget(have[i]);
					}
					Hud::Banner(Arcana::CarryCap() == 3 ? "The altars let you carry three of their spells - the rest went back" : "The altars let you carry two of their spells - the rest went back", 4.0f);
				}
			}
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.perkMenu = g_menu;
				h.perkSel = g_sel;
				h.altarFocus = g_focus, h.altarOpen = g_open;
				h.freedCount = FreedCount();
				h.forgeAt = kForgeAt;
				h.pulseOn = false;
				// (Noah: "the Echo Altar did not have the city being free animation" - its Dragon Priest fell 60 m away, as the clock
				// leapt to dawn) the altar waits for you: its awakening plays once you're back within 25 m of it
				if (g_pulse && !g_pulse->started) {
					auto r = g_pulse->shrine.get();
					auto* pl = RE::PlayerCharacter::GetSingleton();
					if (r && pl && r->Is3DLoaded() && pl->GetPosition().GetDistance(r->GetPosition()) < 2500.0f) {
						g_pulse->started = true;
						Forms::PlayAt(gen::Sound::coffer_charge, r.get());
						std::string city(gen::kTowns[g_pulse->town]);
						for (auto& ch : city) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
						Hud::Banner("THE ECHO ALTAR OF " + city + " WAKES", 4.0f);
						logger::info("shops: {}'s altar wakes before you", gen::kTowns[g_pulse->town]);
					} else if (!r) {
						for (std::size_t i = 0; i < gen::kLocations.size(); ++i)
							if (gen::kLocations[i].town == g_pulse->town) g_dressed[i] = false;
						g_pulse.reset();
					}
				}
				if (g_pulse && !g_pulse->started) {
					// (still waiting for you)
				} else if (g_pulse && (g_pulse->t -= a_delta) > 0) {
					h.pulseT = g_pulse->t;
					auto r = g_pulse->shrine.get();
					const float el = kPulseSeconds - g_pulse->t;
					if (r && r->Is3DLoaded()) {
						if (el < kPulseCharge) {
							// the charge: the roll's lightning, stone after stone into the heart, its runes climbing
							const float u = el / kPulseCharge;
							// (Noah: the replayed freeing was "missing the particles and golden rune fading in") a freed city's runes
							// and motes, there from before, held at nothing through the charge - so they grow and fade in again
							RE::TES::GetSingleton()->ForEachReferenceInRange(r.get(), 500.0f, [&](RE::TESObjectREFR* x) {
								auto* b = x ? x->GetBaseObject() : nullptr;
								const bool runes = b && b == Forms::Ours(gen::kFreedRunes), motes = b && b == Forms::Ours(gen::kHeartMotes);
								if (!runes && !motes) return RE::BSContainer::ForEachResult::kContinue;
								if (auto* o = x->Get3D()) {
									o->local.scale = runes ? 0.001f : x->GetScale();
									Fade(o, 0.0f);
									RE::NiUpdateData ud;
									o->Update(ud);
								}
								return RE::BSContainer::ForEachResult::kContinue;
							});
							TickBeams(r.get(), a_delta, std::min(1.0f, u * 1.15f));
							if (const auto parts = FindAltarParts(r.get()); parts.heart) {
								HeartGlow(parts.heart, 1.0f + 6.0f * u + 2.5f * std::max(0.0f, std::sin(el * 38.0f)));
								g_glowHeart = parts.heart->CreateRefHandle();
							}
						} else if (!g_pulse->burst) {
							// the burst: the curse breaks and the freed look goes up (its motes and runes held at nothing, to grow)
							g_pulse->burst = true;
							EndBeams();
							if (auto hr = g_glowHeart.get()) HeartGlow(hr.get(), 1.0f);
							g_glowHeart = {};
							const auto at = r->GetPosition() + RE::NiPoint3{ 0, 0, 175.0f };
							Gun::FxBlast("lightmassive", at);
							Gun::FxBlast("shoutarea", at, 0.3f);
							Forms::Play(gen::Sound::nuke);
							Forms::PlayAt(gen::Sound::shout_toll, r.get());
							for (std::size_t i = 0; i < gen::kLocations.size(); ++i) {
								if (gen::kLocations[i].town == g_pulse->town) {
									g_dressed[i] = false;
									if (g_placed[i].get().get() == r.get()) Dress(i, r.get());
								}
							}
							logger::info("shops: {}'s altar bursts - the freed look rises", gen::kTowns[g_pulse->town]);
						}
						if (g_pulse->burst) {
							// the motes fade in, the golden runes grow (eased: quick at first, settling into place)
							const float g = std::clamp((el - kPulseCharge - 0.2f) / kPulseGrow, 0.0f, 1.0f);
							const float ease = 1.0f - (1.0f - g) * (1.0f - g) * (1.0f - g);
							RE::TES::GetSingleton()->ForEachReferenceInRange(r.get(), 500.0f, [&](RE::TESObjectREFR* x) {
								auto* b = x ? x->GetBaseObject() : nullptr;
								const bool runes = b && b == Forms::Ours(gen::kFreedRunes), motes = b && b == Forms::Ours(gen::kHeartMotes);
								if (!runes && !motes) return RE::BSContainer::ForEachResult::kContinue;
								if (auto* o = x->Get3D()) {
									const float full = x->GetScale();
									o->local.scale = full * (runes ? std::max(0.001f, ease) : 1.0f);
									Fade(o, runes ? std::min(1.0f, g * 2.5f) : g);
									RE::NiUpdateData ud;
									o->Update(ud);
								}
								return RE::BSContainer::ForEachResult::kContinue;
							});
						}
					}
					if (r) {
						if (auto* cam = RE::Main::WorldRootCamera()) {
							float x = 0, y = 0, z = 0;
							cam->WorldPtToScreenPt3(r->GetPosition() + RE::NiPoint3{ 0, 0, 175.0f }, x, y, z, 1e-5f);
							h.pulseOn = el >= kPulseCharge, h.pulseX = z > 0 ? x : 0.5f, h.pulseY = z > 0 ? 1.0f - y : 0.5f;
						}
					}
				} else {
					if (g_pulse) {
						EndBeams();
						if (auto hr = g_glowHeart.get()) HeartGlow(hr.get(), 1.0f);
						g_glowHeart = {};
						if (auto r = g_pulse->shrine.get()) {  // (everything at its own size and light)
							RE::TES::GetSingleton()->ForEachReferenceInRange(r.get(), 500.0f, [&](RE::TESObjectREFR* x) {
								auto* b = x ? x->GetBaseObject() : nullptr;
								if (b && (b == Forms::Ours(gen::kFreedRunes) || b == Forms::Ours(gen::kHeartMotes))) {
									if (auto* o = x->Get3D()) o->local.scale = x->GetScale(), Fade(o, 1.0f);
									// (Noah: "the particles and rune floating above the altar is missing ... if I walk away and come back
									// it spawns in") its model made again, as a return to the city would: off and straight back on
									x->Disable();
									const auto h = x->CreateRefHandle();
									Tasks::Later(0.2f, [h] { if (auto rr = h.get()) rr->Enable(false); });
								}
								return RE::BSContainer::ForEachResult::kContinue;
							});
						}
						if (!g_pulse->burst) {  // (never reached the burst - the cell unloaded: the freed look goes up anyway)
							for (std::size_t i = 0; i < gen::kLocations.size(); ++i)
								if (gen::kLocations[i].town == g_pulse->town) g_dressed[i] = false;
						}
					}
					g_pulse.reset();
				}
				h.shrineShout = -1;
				if (g_menu) Kit::Fill(h);  // (the altar rework: your kit and the Armory - only with the altar open)
				h.dragonBond = Save::DragonBond();
				h.bondLocked = Movement::BondLocked();
				h.trialsOn = false;
				h.trialLock.clear();
				h.altarFreed = false;
				h.whispers.clear();
				h.freedAltars.clear();
				h.tabletOn = false;
				if (auto* tb = Tablet::Town() >= 0 ? Placed(Tablet::Town(), 7) : nullptr) {
					const auto p = tb->GetPosition();
					h.tabletOn = true, h.tabletAt = { p.x, p.y, p.z };
				}
				for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {  // (Noah: "make the map Echo Altar icons gold when freed")
					if (!Curse::Freed(t)) continue;
					if (auto r = g_markers[t].get()) {
						const auto p = r->GetPosition();
						h.freedAltars.push_back({ p.x, p.y, p.z });
					}
				}
				if (g_menu) {
					if (auto r = g_menuRef.get()) {
						const int t = TownOf(r.get());
						h.shrineShout = Curse::ClaimableShout(t);
						h.whispers = Eggs::Whispers(t);  // (Noah: the hidden quest's whispers, a line more for each step found)
						h.altarFreed = Curse::Freed(t);
						if (const auto v = Curse::Altar(t); v.on) {  // the city still held: its trials
							h.trialsOn = true;
							h.trialDone = v.done, h.trialActive = v.active, h.trialSel = g_trialSel;
							h.trialDesc = v.desc;
							h.trialCity = std::string(gen::kTowns[t]);
							h.trialBoss = v.boss, h.trialBossLine = v.bossLine;
							if (std::string why; !Story::TrialsOpen(t, &why)) h.trialLock = why.empty() ? std::string("the altar sleeps") : why;  // (Noah: each altar locked until its city's quest is done)
						}
					}
				}
			}
			static float clock = 0;
			clock += a_delta;
			RE::NiPoint3 camPos{}, orbAt{};
			float        orbDist = 1e9f;
			RE::NiPointer<RE::TESObjectREFR> orbRef;
			if (auto* pc = RE::PlayerCamera::GetSingleton(); pc && pc->cameraRoot) {
				camPos = pc->cameraRoot->world.translate;
			}
			// (Noah: "make the floating stones gradually spin faster during the animation then gradually go back to their normal
			// rotation speed") the freeing's stones: up to six times their turn through the charge, held through the burst, then
			// easing back to their own
			static float spinBoost = 1.0f;
			static RE::NiPoint3 spinAt{};
			{
				float target = 1.0f;
				if (g_pulse && g_pulse->started) {
					if (auto pr = g_pulse->shrine.get()) spinAt = pr->GetPosition();
					const float el = kPulseSeconds - g_pulse->t;
					target = el < kPulseCharge ? 1.0f + 5.0f * (el / kPulseCharge) : el < kPulseCharge + 1.5f ? 6.0f : 1.0f;
				}
				const float rate = target > spinBoost ? 3.0f : 0.5f;   // (quick to wind up, slow to settle)
				spinBoost += (target - spinBoost) * std::min(1.0f, rate * a_delta);
			}
			for (auto& s : g_spinners) {
				if (auto r = s.ref.get(); r && r->Is3DLoaded()) {
					// the shrine's heart turns faster while it rolls (the crystal races, the shards whirl). Statics never move
					// by their angle while loaded, so their 3D node is turned (and the rocks bob) directly.
					const float boost = r->GetPosition().GetDistance(spinAt) < 900.0f ? spinBoost : 1.0f;
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
		void DevFreed(int a_town, bool a_on)
		{
			if (a_town < 0) { g_devFreed.fill(false); }
			else if (a_town < static_cast<int>(g_devFreed.size())) g_devFreed[a_town] = a_on;
			for (std::size_t i = 0; i < gen::kLocations.size(); ++i) if (a_town < 0 || gen::kLocations[i].town == a_town) g_dressed[i] = false;
		}
		void FreedPulse(int a_town)
		{
			auto* shrine = Placed(a_town, 0);
			if (!shrine) {
				for (std::size_t i = 0; i < gen::kLocations.size(); ++i)
					if (gen::kLocations[i].town == a_town) g_dressed[i] = false;
				return;
			}
			// the charge first (the burst and the freed look follow it: Tick) - once you are at the altar
			EndBeams();
			g_pulse = Pulse{ shrine->CreateRefHandle(), kPulseSeconds, a_town, false, false };
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && pl->GetPosition().GetDistance(shrine->GetPosition()) > 2500.0f) {
				Tasks::Later(6.0f, [] { if (g_pulse && !g_pulse->started) Hud::Banner("Return to the Echo Altar to see it wake", 5.0f); });
			}
			logger::info("shops: {}'s shrine gathers its light - the Curse is lifted", gen::kTowns[a_town]);
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
				if (g_menu && g_menuRef.get().get() == a_ref) {
					Forms::Play(gen::Sound::ui_close);
					g_menu = false;
					g_closedAt = std::chrono::steady_clock::now();
				} else if (Classes::Get() < 0) {
					// (Noah: "for players who haven't chosen a class yet the echo altar UI should just be the class selection") the
					// class picker alone; once a class is chosen the altar opens on that class's own rows (TickPickAltar)
					g_pickAltar = a_ref->CreateRefHandle();
					Forms::PlayAt(gen::Sound::coffer_open, a_ref);
					Classes::OpenPicker(false);
				} else {
					g_menu = true;
					g_focus = kRowClass;
					g_menuRef = a_ref->CreateRefHandle();
					Forms::PlayAt(gen::Sound::coffer_open, a_ref);
					PlayUi(UiSnd::kOpen, 0.6f);
				}
				break;
			case 1: BuyPerk(o.ref, a_ref); break;
			case 4:
				if (!g_menu) {
					g_menu = true, g_sel = 0, g_focus = kRowClass;
					g_menuRef = a_ref->CreateRefHandle();
					Forms::Play(gen::Sound::coffer_open);
				} else {
					BuyPerk(Perks::MenuAt(g_sel), a_ref);
				}
				break;
			case 7: Tablet::OnActivate(a_ref); break;  // (the Tablet of Mysteries)
			default: break;  // (the old ammo caches, wall buys, relic stands and the Atronach Reforge: gone)
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
		// (the altar no longer rolls: its old counts' bytes are written as zeros so a save reads the same) the Tablet of
		// Mysteries' state after them
		std::vector<std::uint8_t> out(2 + kRollTowns * 2, 0);
		const auto tb = Tablet::State();
		out.insert(out.end(), tb.begin(), tb.end());
		return out;
	}
	void SetRollState(const std::vector<std::uint8_t>& a_s)
	{
		constexpr std::size_t kBase = 2 + kRollTowns * 2;
		Tablet::SetState(a_s.size() > kBase ? a_s.data() + kBase : nullptr, a_s.size() > kBase ? a_s.size() - kBase : 0);
	}
}
