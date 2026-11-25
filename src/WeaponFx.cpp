#include "CurseOfAlduin.h"

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
			void Detach()
			{
				if (obj && obj->parent) obj->parent->DetachChild2(obj.get());
			}
		};
		Hung g_hung[2];   // [0] third person, [1] first person
		std::unordered_map<RE::FormID, std::string_view> g_arts;
		bool g_built = false;

		std::string_view ArtFor(const RE::TESForm* a_w)
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
			const auto it = g_arts.find(a_w->GetFormID());
			return it == g_arts.end() ? std::string_view{} : it->second;
		}

		RE::NiNode* Host(RE::NiAVObject* a_root, const RE::NiAVObject* a_ours)
		{
			auto* wn = a_root ? a_root->GetObjectByName("WEAPON") : nullptr;
			auto* node = wn ? wn->AsNode() : nullptr;
			if (!node) return nullptr;
			// (the weapon's own model hangs under the hand's WEAPON node: ours goes beside its geometry, under it)
			for (auto& ch : node->GetChildren()) {
				if (!ch || ch.get() == a_ours) continue;
				if (auto* n = ch->AsNode()) return n;
			}
			return node;
		}
	}

	void Tick()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const bool ok = p && p->Is3DLoaded() && p->AsActorState()->IsWeaponDrawn();
		const auto art = ok ? ArtFor(p->GetEquippedObject(false)) : std::string_view{};
		for (int v = 0; v < 2; ++v) {
			auto& H = g_hung[v];
			auto* root = ok && !art.empty() ? p->Get3D(v == 1) : nullptr;
			auto* host = root ? Host(root, H.obj.get()) : nullptr;
			if (!host) {
				H.Detach();
				continue;
			}
			if (H.path != art) {
				H.Detach();
				H.obj = Gun::LoadModel(std::string(art));
				H.path = std::string(art);
				if (H.obj) logger::info("weaponfx: {} hung on the {} weapon", art, v ? "first-person" : "third-person");
				else logger::warn("weaponfx: {} could not be loaded", art);
			}
			if (!H.obj) continue;
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
