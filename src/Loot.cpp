#include "BO3Z.h"

// Quick loot, The Curse of Alduin's own (QuickLoot RE does not run on Skyrim 1.7.104): look at a container or a body and its
// things are listed beside the crosshair in the game's style (src/Hud.cpp DrawLoot) - no menu, the dead keep coming.
//   Keyboard: E takes the chosen thing, R takes everything, mouse wheel / Up / Down choose, G opens Skyrim's own
//   container menu. Gamepad: A take, X take all, D-pad up / down choose.
// Owned things (stealing) and locked chests are not taken here: they say so, and G (or Activate) opens them as usual.
namespace bo3z::Loot
{
	namespace
	{
		RE::ObjectRefHandle g_ref;
		std::vector<RE::TESBoundObject*> g_objs;   // the listed things, in the order shown
		int   g_sel = 0;
		float g_scan = 0;
		bool  g_on = false, g_takeable = false;

		int KindOf(RE::TESBoundObject* a_obj)
		{
			if (a_obj->GetFormID() == 0xF) {
				return 1;  // gold
			}
			switch (a_obj->GetFormType()) {
			case RE::FormType::Weapon: return 2;
			case RE::FormType::Armor: return 3;
			case RE::FormType::AlchemyItem: return 4;
			case RE::FormType::Ingredient: return 5;
			case RE::FormType::Book: return 6;
			case RE::FormType::Scroll: return 7;
			case RE::FormType::Ammo: return 8;
			case RE::FormType::SoulGem: return 9;
			case RE::FormType::KeyMaster: return 10;
			default: return 0;
			}
		}

		// an ash pile (what the burned and the turned-to-ash leave): the body it stands for - the dead actor whose ash pile
		// it is (Noah: our loot on everything, the ash piles too)
		RE::TESObjectREFR* AshOwner(RE::TESObjectREFR* a_ref)
		{
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || base->GetFormType() != RE::FormType::Activator) {
				return nullptr;
			}
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return nullptr;
			}
			const auto      me = a_ref->CreateRefHandle();
			RE::TESObjectREFR* found = nullptr;
			auto look = [&](RE::BSTArray<RE::ActorHandle>& a_list) {
				for (auto& h : a_list) {
					auto a = h.get();
					if (!a || found || !a->IsDead()) continue;
					if (auto* x = a->extraList.GetByType<RE::ExtraAshPileRef>(); x && x->ashPileRef == me) {
						found = a.get();
					}
				}
			};
			look(lists->highActorHandles);
			look(lists->middleHighActorHandles);
			look(lists->middleLowActorHandles);
			look(lists->lowActorHandles);
			return found;
		}

		bool Lootable(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted()) {
				return false;
			}
			auto* base = a_ref->GetBaseObject();
			if (!base) {
				return false;
			}
			if (base->GetFormType() == RE::FormType::Container) {
				return true;
			}
			if (auto* a = a_ref->As<RE::Actor>(); a && a->IsDead() && a != RE::PlayerCharacter::GetSingleton()) {
				return true;
			}
			return false;
		}

		void Publish(RE::TESObjectREFR* a_ref)
		{
			std::vector<Hud::State::LootItem> items;
			g_objs.clear();
			std::string title, note;
			g_takeable = false;
			if (a_ref) {
				title = a_ref->GetDisplayFullName() ? a_ref->GetDisplayFullName() : "";
				const bool locked = a_ref->IsLocked();
				const bool owned = a_ref->IsOffLimits();
				if (locked) {
					note = "Locked";
				} else {
					auto inv = a_ref->GetInventory([](RE::TESBoundObject& o) { return o.GetPlayable() && o.GetName() && *o.GetName(); });
					struct Row { RE::TESBoundObject* obj; int count; int kind; };
					std::vector<Row> rows;
					for (auto& [obj, e] : inv) {
						if (obj && e.first > 0) {
							rows.push_back({ obj, e.first, KindOf(obj) });
						}
					}
					// gold first, then the most valuable
					std::sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) {
						if ((x.kind == 1) != (y.kind == 1)) return x.kind == 1;
						return x.obj->GetGoldValue() > y.obj->GetGoldValue();
					});
					for (const auto& r : rows) {
						g_objs.push_back(r.obj);
						items.push_back({ r.obj->GetName(), r.count, r.obj->GetGoldValue(), r.obj->GetWeight(), r.kind });
					}
					if (items.empty()) {
						note = "Empty";
					} else if (owned) {
						note = "Owned - G or E to steal";
					} else {
						g_takeable = true;
					}
				}
			}
			g_sel = std::clamp(g_sel, 0, std::max(0, static_cast<int>(items.size()) - 1));
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.lootOn = a_ref != nullptr;
			h.lootTitle = std::move(title);
			h.lootNote = std::move(note);
			h.loot = std::move(items);
			h.lootSel = g_sel;
		}

		void Take(int a_index)
		{
			auto ref = g_ref.get();
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (!ref || !pl || !g_takeable || a_index < 0 || a_index >= static_cast<int>(g_objs.size())) {
				return;
			}
			auto* obj = g_objs[a_index];
			auto  inv = ref->GetInventory([obj](RE::TESBoundObject& o) { return &o == obj; });
			const auto it = inv.find(obj);
			if (it == inv.end() || it->second.first <= 0) {
				return;
			}
			ref->RemoveItem(obj, it->second.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, pl);
			pl->PlayPickUpSound(obj, true, false);
		}
	}

	void Tick(float a_delta)
	{
		auto* pl = RE::PlayerCharacter::GetSingleton();
		auto* ui = RE::UI::GetSingleton();
		RE::TESObjectREFR* target = nullptr;
		if (pl && ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) {
			if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
				auto t = pick->target.get();
				if (t && t->GetPosition().GetDistance(pl->GetPosition()) < 260.0f) {
					if (Lootable(t.get())) {
						target = t.get();
					} else if (auto* owner = AshOwner(t.get())) {
						target = owner;
					}
				}
			}
		}
		const bool changed = (target ? target->CreateRefHandle() : RE::ObjectRefHandle{}) != g_ref;
		if (changed) {
			g_ref = target ? target->CreateRefHandle() : RE::ObjectRefHandle{};
			g_sel = 0;
			g_scan = 0;
		}
		g_on = target != nullptr;
		if ((g_scan -= a_delta) <= 0 || changed) {
			g_scan = 0.25f;
			Publish(target);
		}
	}

	bool Eats(RE::InputEvent* a_event)
	{
		auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
		if (!g_on || !b) {
			return false;
		}
		auto*       ue = RE::UserEvents::GetSingleton();
		const auto& q = b->QUserEvent();
		const auto  dev = b->GetDevice();
		const auto  code = b->GetIDCode();
		if ((dev == RE::INPUT_DEVICE::kKeyboard && code == 0x22) || (dev == RE::INPUT_DEVICE::kGamepad && code == 0x8000)) {  // G / pad Y: Skyrim's own menu (ours opens it)
			return true;
		}
		if (!g_takeable) {
			return false;  // owned, locked or empty: Skyrim's own Activate as usual
		}
		if (q == ue->activate || q == ue->readyWeapon) {
			return true;
		}
		if (dev == RE::INPUT_DEVICE::kMouse && (code == 8 || code == 9)) {
			return true;  // (the wheel chooses, it doesn't zoom the camera)
		}
		if (dev == RE::INPUT_DEVICE::kKeyboard && (code == 0xC8 || code == 0xD0)) {
			return true;
		}
		if (dev == RE::INPUT_DEVICE::kGamepad && (code == 0x0001 || code == 0x0002)) {
			return true;
		}
		return false;
	}

	bool OnButton(const RE::ButtonEvent* a_event)
	{
		if (!g_on || !a_event || !a_event->IsDown()) {
			return false;
		}
		auto*       ue = RE::UserEvents::GetSingleton();
		const auto& q = a_event->QUserEvent();
		const auto  dev = a_event->GetDevice();
		const auto  code = a_event->GetIDCode();
		const int   n = static_cast<int>(g_objs.size());
		if ((dev == RE::INPUT_DEVICE::kKeyboard && code == 0x22) || (dev == RE::INPUT_DEVICE::kGamepad && code == 0x8000)) {
			if (auto ref = g_ref.get()) {
				ref->ActivateRef(RE::PlayerCharacter::GetSingleton(), 0, nullptr, 0, false);
			}
			return true;
		}
		if (!g_takeable) {
			return false;
		}
		bool acted = false;
		if (q == ue->activate) {
			Take(g_sel);
			acted = true;
		} else if (q == ue->readyWeapon) {
			for (int i = n - 1; i >= 0; --i) {
				Take(i);
			}
			acted = true;
		} else if ((dev == RE::INPUT_DEVICE::kMouse && code == 8) || (dev == RE::INPUT_DEVICE::kKeyboard && code == 0xC8) || (dev == RE::INPUT_DEVICE::kGamepad && code == 0x0001)) {
			g_sel = std::max(0, g_sel - 1);
			acted = true;
		} else if ((dev == RE::INPUT_DEVICE::kMouse && code == 9) || (dev == RE::INPUT_DEVICE::kKeyboard && code == 0xD0) || (dev == RE::INPUT_DEVICE::kGamepad && code == 0x0002)) {
			g_sel = std::min(std::max(0, n - 1), g_sel + 1);
			acted = true;
		}
		if (acted) {
			Publish(g_ref.get().get());
		}
		return acted;
	}
}
