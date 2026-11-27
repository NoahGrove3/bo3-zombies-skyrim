#include "CurseOfAlduin.h"

#include <cctype>

// (Noah, 2026-11-24: "give me previews for all the weapon effects we make for my mod so we don't have to use that nexus mod" -
// the Curse Weapon Effects page, his pick of five looks for each weapon) each weapon of ours wears its own effect: an art model
// fitted to that weapon's model (tools/weaponfx.py - a living sheet along its real edges and a glow worked into its steel), hung
// on the weapon in the hand, in third person and in first, while it is drawn. Built in the weapon model's own space, it is hung
// under the weapon's own node (so the weapon's scale and turn carry it), or the hand's WEAPON node if that has none.
namespace coa::WeaponFx
{
	namespace
	{
		struct Hung
		{
			RE::NiPointer<RE::NiAVObject> obj;
			std::string                   path;
			float                         gain = -1.0f;   // (the tier strength applied to it)
			std::vector<std::pair<RE::BSEffectShaderMaterial*, RE::NiColorA>> base;   // (each effect shader's own colour, before the tier's strength)
			void Detach()
			{
				if (obj && obj->parent) obj->parent->DetachChild2(obj.get());
			}
		};
		Hung g_hung[2][2];   // [hand: 0 right, 1 left][0 third person, 1 first person]
		std::unordered_map<RE::FormID, std::string_view> g_arts;
		bool g_built = false;

		std::string ModelOf(const RE::TESForm* a_w)
		{
			auto* md = a_w ? a_w->As<RE::TESModel>() : nullptr;
			if (!md || !md->GetModel()) return {};
			std::string m = md->GetModel();
			for (auto& c : m) c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (m.starts_with("meshes\\")) m.erase(0, 7);
			return m;
		}
		std::string_view ArtFor(const RE::TESForm* a_w, bool a_left)
		{
			if (!a_w) return {};
			if (!g_built) {
				g_built = true;
				auto* dh = RE::TESDataHandler::GetSingleton();
				for (const auto& e : gen::kWeaponFx) {
					if (const auto id = dh->LookupFormID(e.weap, gen::kPlugin)) g_arts[id] = e.model;
				}
				logger::info("weaponfx: {} weapons have their own effect art", g_arts.size());
			}
			if (const auto it = g_arts.find(a_w->GetFormID()); it != g_arts.end()) return it->second;   // (in either hand: a one-handed weapon of ours in the left wears its look there too)
			// a class weapon: its model changes at every tier, and its look with the tier (and the Spellbound Rapier's with the
			// spell in the other hand) - the art fitted to the model it has now, in the look it wears now
			// (Noah, 2026-11-24: "both left and right claws ... carry the same effect") in the left hand, the claws' left twins:
			// by the model alone (key -2)
			const int key = a_left ? -2 : Classes::HandLookKey();
			if (key == -1) return {};
			const std::string m = ModelOf(a_w);
			if (m.empty()) return {};
			for (const auto& e : gen::kWeaponFxClass) {
				if (e.key == key && e.model == m) return e.art;
			}
			static std::string missed;
			if (!a_left && missed != m + std::to_string(key)) {
				missed = m + std::to_string(key);
				logger::info("weaponfx: no fitted art for {} in look {}", m, key);
			}
			return {};
		}

		RE::NiNode* Host(RE::NiAVObject* a_root, const RE::NiAVObject* a_ours, bool a_left)
		{
			auto* wn = a_root ? a_root->GetObjectByName(a_left ? "SHIELD" : "WEAPON") : nullptr;
			auto* node = wn ? wn->AsNode() : nullptr;
			if (!node) return nullptr;
			// (the weapon's own model hangs under the hand's node: ours goes under the model's own root, beside its geometry - the
			// art is built in that root's space, so a root turned in its file (the scythes') turns ours with it)
			for (auto& ch : node->GetChildren()) {
				if (!ch || ch.get() == a_ours) continue;
				if (auto* n = ch->AsNode()) return n;
			}
			return a_left ? nullptr : node;
		}
	}

	void Tick()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const bool ok = p && p->Is3DLoaded() && p->AsActorState()->IsWeaponDrawn();
		for (int hand = 0; hand < 2; ++hand) {
			auto* held = ok ? p->GetEquippedObject(hand == 1) : nullptr;
			const auto art = held && held->IsWeapon() ? ArtFor(held, hand == 1) : std::string_view{};
			for (int v = 0; v < 2; ++v) {
				auto& H = g_hung[hand][v];
				auto* root = ok && !art.empty() ? p->Get3D(v == 1) : nullptr;
				auto* host = root ? Host(root, H.obj.get(), hand == 1) : nullptr;
				if (!host) {
					H.Detach();
					continue;
				}
				if (H.path != art) {
					H.Detach();
					H.obj = Gun::LoadModel(std::string(art));
					H.path = std::string(art);
					H.gain = -1.0f, H.base.clear();
					if (H.obj) RE::BSVisit::TraverseScenegraphGeometries(H.obj.get(), [&](RE::BSGeometry* g) {
						if (auto* ep = netimmerse_cast<RE::BSEffectShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get())) {
							if (auto* m = static_cast<RE::BSEffectShaderMaterial*>(ep->material)) H.base.emplace_back(m, m->baseColor);
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
					if (H.obj) logger::info("weaponfx: {} hung on the {} {}-hand weapon", art, v ? "first-person" : "third-person", hand ? "left" : "right");
					else logger::warn("weaponfx: {} could not be loaded", art);
				}
				if (!H.obj) continue;
				// (Noah, Nov 27 workshop: "one look, stronger each tier" - his picks: I faint, II a glowing edge, III full, Legendary
				// brighter still) a class weapon's art at its tier's strength; every other weapon of ours at full
				{
					static constexpr float kTierGain[5] = { 0.45f, 0.45f, 0.7f, 1.0f, 1.35f };
					const int t = hand == 0 && Classes::HandLookKey() >= 0 ? Kit::TierOfWeapon(held) : -1;
					const float gain = t < 0 ? 1.0f : kTierGain[std::clamp(t, 0, 4)];
					if (gain != H.gain) {
						H.gain = gain;
						for (auto& [m, c] : H.base) m->baseColor = RE::NiColorA{ c.red, c.green, c.blue, std::min(1.0f, c.alpha * gain) }, m->baseColorScale = gain > 1.0f ? gain : 1.0f;
					}
				}
				if (H.obj->parent != host) {
					H.Detach();
					host->AttachChild(H.obj.get(), true);
					H.obj->local = RE::NiTransform{};
					RE::NiUpdateData ud;
					H.obj->Update(ud);
				}
			}
		}
	}
}
