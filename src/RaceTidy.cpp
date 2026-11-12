// (Noah: "I really want to go through and organize all the tabs and all of the sliders in each tab in the race menu, so
// it's a lot more organized") RaceMenu's character creator, regrouped by body area into eight tabs - RACE, HEAD, FACE,
// EYES & BROWS, HAIR, BODY, SKIN & PAINT, EXPRESSIONS - with the body sliders in sections, and the duplicates and the
// extras he chose to hide taken out ("hide them and hide the himbo morphs tab"). Nothing in RaceMenu's files changes:
// its lists are rearranged in the open menu, again whenever it rebuilds them (a race or sex change, a preset).
#include "BO3Z.h"

namespace bo3z::RaceTidy
{
	namespace
	{
		// RaceMenu's own category bits (its tabs filter on them)
		constexpr std::uint32_t kRace = 2, kBody = 4, kHead = 8, kFace = 16, kEyes = 32, kBrow = 64, kMouth = 128, kHair = 256,
								kExtra = 512, kExpr = 1024, kColors = 8192, kMakeup = 16384, kBodyPaint = 32768, kHandPaint = 65536,
								kFootPaint = 131072;
		constexpr std::uint32_t kSkin = kColors | kMakeup | kBodyPaint | kHandPaint | kFootPaint;   // (one tab for all of it)
		constexpr std::uint32_t kHidden = 1u << 24;   // (no tab has it)

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}
		bool Has(const std::string& a_low, std::initializer_list<const char*> a_any)
		{
			for (auto* w : a_any) {
				if (a_low.find(w) != std::string::npos) return true;
			}
			return false;
		}

		// where one slider goes: its tab, and its place in that tab (lower first). -1: left as it is
		struct Place
		{
			std::uint32_t flag;
			int           order;
		};
		std::optional<Place> Route(const std::string& a_text, const std::string& a_callback, std::uint32_t a_flag, int a_type)
		{
			const std::string t = Lower(a_text), cb = Lower(a_callback);
			if (a_type == 1) return Place{ kRace, 0 };   // (the races themselves)
			if (a_type == 3) return std::nullopt;        // (paint textures: RaceMenu's own grids, by their own tabs' bits)
			// the extras hidden (his answer: nipples, growth, pregnant, the Hayden feminine shapes; and the two left-over helpers)
			if (Has(t, { "nipple", "areola", "growth", "pregnan", "hayden", "legacy pecs", "belly button match" })) return Place{ kHidden, 0 };
			// RACE
			if (t == "sex") return Place{ kRace, 100 };
			if (cb == "changeheadpreset") return Place{ kRace, 101 };
			// HEAD
			if (cb == "changeheadsize" || t == "$head") return Place{ kHead, 0 };
			if (t == "complexion") return Place{ kHead, 10 };
			if (t == "scars") return Place{ kHead, 20 };
			if (Has(t, { "scars0" })) return Place{ kHead, 21 };
			if (Has(t, { "ears" })) return Place{ kHead, 30 };
			// (Noah: "the face tab is missing the face coloring on their skin, like the eyeshadow" - "Next to their feature")
			// each face color right under its feature
			if (t == "forehead color") return Place{ kFace, -1 };
			if (t == "nose color") return Place{ kFace, 3 };
			if (t == "cheek color" || t == "cheek color lower" || t == "laugh lines") return Place{ kFace, t == "cheek color" ? 11 : t == "cheek color lower" ? 12 : 13 };
			if (t == "chin color") return Place{ kFace, 31 };
			if (t == "lip color") return Place{ kFace, 42 };
			if (t == "eyeliner color" || t == "eye shadow" || t == "eye tint") return Place{ kEyes, t == "eyeliner color" ? 4 : t == "eye shadow" ? 5 : 6 };
			if (t == "neck color") return Place{ kHead, 40 };
			// FACE (nose, cheeks, jaw, chin, mouth)
			if (Has(t, { "nose type" })) return Place{ kFace, 0 };
			if (Has(t, { "nose height" })) return Place{ kFace, 1 };
			if (Has(t, { "nose length" })) return Place{ kFace, 2 };
			if (Has(t, { "cheekbone" })) return Place{ kFace, 10 };
			if (Has(t, { "jaw" })) return Place{ kFace, 20 };
			if (Has(t, { "chin width", "chin length", "chin forward" })) return Place{ kFace, 30 };
			if (Has(t, { "mouth shape" })) return Place{ kFace, 40 };
			if (Has(t, { "mouth" })) return Place{ kFace, 41 };
			// EYES & BROWS
			if (Has(t, { "eye shape" })) return Place{ kEyes, 0 };
			if (Has(t, { "eye color" })) return Place{ kEyes, 1 };
			if (Has(t, { "eye height", "eye width", "eye depth" })) return Place{ kEyes, 2 };
			if (Has(t, { "eyeshape", "_aes_" })) return Place{ kEyes, 3 };
			if (Has(t, { "brow" })) return Place{ kEyes, 10 };
			// HAIR
			if (t == "hair" || t == "facial hair") return Place{ kHair, t == "hair" ? 0 : 1 };
			if (t == "hair color") return Place{ kHair, 2 };
			// SKIN & PAINT (every color, the dirt and the war paint)
			if (t == "skin tone") return Place{ kSkin, 0 };
			if (Has(t, { "dirt" })) return Place{ kSkin, t == "dirt" ? 50 : 51 };
			if (Has(t, { "war paint" })) return Place{ kSkin, t == "war paint" ? 60 : 61 };
			if (cb == "changetintingmask" || (a_flag & kColors)) return Place{ kSkin, 10 };
			// BODY: height and weight, then the body mods' sliders by section
			if (cb == "changeheight" || t == "$height") return Place{ kBody, 0 };
			if (t == "weight") return Place{ kBody, 1 };
			if (cb.rfind("rsm_bodymorph", 0) == 0 || cb.find("morph") != std::string::npos || a_flag == 0) {
				// (Noah: "in the body slider, I only want to keep height and weight. Remove the rest") every body mod slider hidden
				return Place{ kHidden, 0 };
			}
			// what's left keeps its own tab, if that tab is still shown
			if (a_flag & (kRace | kHead | kFace | kEyes | kHair | kBody | kExpr)) return std::nullopt;
			if (a_flag & kBrow) return Place{ kEyes, 10 };
			if (a_flag & kMouth) return Place{ kFace, 41 };
			return std::nullopt;
		}

		// the tabs: what each becomes (an empty name: hidden)
		struct Tab
		{
			const char*   name;
			std::uint32_t flag;
			int           priority;
		};
		std::optional<Tab> Retab(const std::string& a_text, std::uint32_t a_flag)
		{
			const std::string t = Lower(a_text);
			if (t == "race") return Tab{ "RACE", kRace, -975 };
			if (t == "head") return Tab{ "HEAD", kHead, -950 };
			if (t == "face") return Tab{ "FACE", kFace, -925 };
			if (t == "eyes") return Tab{ "EYES & BROWS", kEyes, -900 };
			if (t == "hair") return Tab{ "HAIR", kHair, -875 };
			if (t == "body") return Tab{ "BODY", kBody, -850 };
			if (t == "$colors" || t == "colors") return Tab{ "SKIN & PAINT", kSkin, -825 };
			// ("the expressions tab has no sliders in it") hidden with the rest
			(void)a_flag;
			return Tab{ nullptr, 0, 0 };   // all else hidden: ALL, BROW, MOUTH, EXTRA, MAKEUP, the paints, BODY SCALES, the morph tabs
		}

		bool Num(const RE::GFxValue& a_o, const char* a_k, double& a_out)
		{
			RE::GFxValue v;
			if (!a_o.GetMember(a_k, &v) || !v.IsNumber()) return false;
			a_out = v.GetNumber();
			return true;
		}
		std::string Str(const RE::GFxValue& a_o, const char* a_k)
		{
			RE::GFxValue v;
			return a_o.GetMember(a_k, &v) && v.IsString() ? std::string(v.GetString()) : std::string();
		}

		// the categories: renamed, given their new bits, the rest taken out
		bool TidyTabs(RE::GFxMovieView* a_mv, RE::GFxValue& a_list)
		{
			RE::GFxValue entries;
			if (!a_list.GetMember("_entryList", &entries) || !entries.IsArray()) return false;
			const auto n = entries.GetArraySize();
			// (Noah: "it still shows himbo morphs") the body mods' tabs are added after RaceMenu's own: any tab not yet seen
			// means another pass
			bool fresh = false;
			for (std::uint32_t i = 0; i < n && !fresh; ++i) {
				RE::GFxValue e, mark;
				if (entries.GetElement(i, &e) && e.IsObject() && !(e.GetMember("caTidy", &mark) && mark.IsBool())) fresh = true;
			}
			if (!fresh) return false;
			std::vector<std::pair<int, RE::GFxValue>> keep;
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue e;
				if (!entries.GetElement(i, &e) || !e.IsObject()) continue;
				double f = 0;
				Num(e, "flag", f);
				// (Noah: "the only tabs I see are race, head, face, hair, and body") a second pass (a tab added late) read the
				// renamed tabs by their new names and hid them: each tab is judged by its first name
				std::string first = Str(e, "caText");
				if (first.empty()) {
					first = Str(e, "text");
					e.SetMember("caText", RE::GFxValue(first.c_str()));
				}
				const auto tab = Retab(first, static_cast<std::uint32_t>(f));
				if (!tab || !tab->name) continue;
				if (*tab->name) e.SetMember("text", RE::GFxValue(tab->name));
				e.SetMember("flag", RE::GFxValue(static_cast<double>(tab->flag)));
				e.SetMember("priority", RE::GFxValue(static_cast<double>(tab->priority)));
				e.SetMember("textFilter", RE::GFxValue());   // (a bit filter, not a name one)
				keep.emplace_back(tab->priority, e);
			}
			if (keep.empty()) return false;
			std::stable_sort(keep.begin(), keep.end(), [](auto& a, auto& b) { return a.first < b.first; });
			entries.SetArraySize(0);
			for (std::size_t i = 0; i < keep.size(); ++i) {
				keep[i].second.SetMember("itemIndex", RE::GFxValue(static_cast<double>(i)));
				keep[i].second.SetMember("caTidy", RE::GFxValue(true));
				entries.PushBack(keep[i].second);
			}
			(void)a_mv;
			a_list.Invoke("InvalidateData");
			return true;
		}

		// the sliders: each to its tab, in order within it
		int TidySliders(RE::GFxValue& a_list)
		{
			RE::GFxValue entries;
			if (!a_list.GetMember("_entryList", &entries) || !entries.IsArray()) return 0;
			const auto n = entries.GetArraySize();
			bool fresh = false;
			struct Row
			{
				RE::GFxValue e;
				std::uint32_t flag;
				int           order;
				std::uint32_t at;
			};
			std::vector<Row> rows;
			rows.reserve(n);
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue e;
				if (!entries.GetElement(i, &e) || !e.IsObject()) continue;
				RE::GFxValue mark;
				if (!(e.GetMember("caTidy", &mark) && mark.IsBool())) fresh = true;
				double f = 0, type = 0;
				Num(e, "filterFlag", f), Num(e, "type", type);
				// (Noah: "I don't see the slider ... where I can change the color under my eyes") RaceMenu's two eye-socket colors
				// named for what they paint; routed by their first names
				std::string text = Str(e, "caText");
				if (text.empty()) {
					text = Str(e, "text");
					e.SetMember("caText", RE::GFxValue(text.c_str()));
					if (text == "Eye Shadow") e.SetMember("text", RE::GFxValue("Upper Eye Color"));
					else if (text == "Eye Tint") e.SetMember("text", RE::GFxValue("Under-Eye Color"));
				}
				double done = 0;
				if (Num(e, "caFlag", done)) f = done;   // (moved already: from where it first was)
				const auto to = Route(text, Str(e, "callbackName"), static_cast<std::uint32_t>(f), static_cast<int>(type));
				rows.push_back({ e, to ? to->flag : static_cast<std::uint32_t>(f), to ? to->order : 1000, i });
				if (!mark.IsBool()) e.SetMember("caFlag", RE::GFxValue(f));
			}
			if (!fresh) return 0;
			std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.order != b.order ? a.order < b.order : a.at < b.at; });
			entries.SetArraySize(0);
			int moved = 0;
			for (std::size_t i = 0; i < rows.size(); ++i) {
				auto& r = rows[i];
				double was = 0;
				Num(r.e, "filterFlag", was);
				if (static_cast<std::uint32_t>(was) != r.flag) ++moved;
				r.e.SetMember("filterFlag", RE::GFxValue(static_cast<double>(r.flag)));
				r.e.SetMember("caTidy", RE::GFxValue(true));
				entries.PushBack(r.e);
			}
			a_list.Invoke("InvalidateData");
			return moved;
		}
	}

	// (Noah: "in the race menu, the player's head when creating a character should follow the mouse cursor, but the mouse
	// cursor is invisible") the game sits in its controller mode there (no cursor, so RaceMenu's own look-at never moves):
	// the mouse's own movement turns the head instead - a point before the camera, moved by the mouse, looked at
	namespace
	{
		std::atomic<float> g_lookX{ 0.0f }, g_lookY{ 0.0f };
		bool g_lookOn = false;
		void Look()
		{
			auto* ui = RE::UI::GetSingleton();
			auto* p = RE::PlayerCharacter::GetSingleton();
			const bool open = ui && p && ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) && !InputSwitch::KeyboardMouse();
			if (!open) {
				if (g_lookOn && p) p->SetGraphVariableBool("IsNPC", false);
				g_lookOn = false, g_lookX = 0.0f, g_lookY = 0.0f;
				return;
			}
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* proc = p->GetActorRuntimeData().currentProcess;
			if (!cam || !cam->cameraRoot || !proc) return;
			if (!g_lookOn) {
				p->SetGraphVariableBool("IsNPC", true);   // (the player's head turns only as an NPC's does)
				g_lookOn = true;
			}
			const auto& w = cam->cameraRoot->world;
			const RE::NiPoint3 eye = w.translate, right{ w.rotate.entry[0][0], w.rotate.entry[1][0], w.rotate.entry[2][0] },
			                   up{ w.rotate.entry[0][2], w.rotate.entry[1][2], w.rotate.entry[2][2] };
			RE::NiPoint3 at = eye + right * g_lookX.load() + up * g_lookY.load();
			proc->SetHeadtrackTarget(p, at);
		}
	}
	void Mouse(float a_dx, float a_dy)
	{
		if (!g_lookOn) return;
		// (Noah: "He's either looking all the way one way or all the way the other way") gentler: a small, slow sweep
		// about the camera, so the head turns part way and back
		g_lookX = std::clamp(g_lookX.load() + a_dx * 0.18f, -110.0f, 110.0f);
		g_lookY = std::clamp(g_lookY.load() - a_dy * 0.18f, -70.0f, 70.0f);
	}

	// (Noah: "after accepting a character name, can you make it fade black as soon as they accept? And then unfade black
	// when everything's loaded in" / "Can you make that camera movement stop?") the name taken = black at once, over
	// RaceMenu's camera swinging back behind you; the menu's close keeps it black until the game has settled
	void Named()
	{
		static std::string was;
		static bool        open = false;
		auto* ui = RE::UI::GetSingleton();
		auto* p = RE::PlayerCharacter::GetSingleton();
		const bool now = ui && p && ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
		if (!now) { open = false; return; }
		const std::string name = p->GetName() ? p->GetName() : "";
		if (!open) { open = true, was = name; return; }
		if (name != was) {
			was = name;
			Hud::IntroVeil(8.0f, true);
			logger::info("race menu: named '{}' - black at once", name);
		}
	}

	void Tick()
	{
		Look();
		Named();
		static auto last = std::chrono::steady_clock::now();
		const auto  now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(150)) return;
		last = now;
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME)) return;
		auto menu = ui->GetMenu(RE::RaceSexMenu::MENU_NAME);
		auto* mv = menu ? menu->uiMovie.get() : nullptr;
		if (!mv) return;
		static constexpr const char* kPanel = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.racePanel";
		RE::GFxValue items;
		const bool haveItems = mv->GetVariable(&items, (std::string(kPanel) + ".itemList").c_str()) && items.IsObject();
		int tabs = 0;
		for (const char* p : { ".categoryButtons.categoryList", ".slidingCategoryList.categoryList" }) {
			RE::GFxValue cats;
			if (mv->GetVariable(&cats, (std::string(kPanel) + p).c_str()) && cats.IsObject() && TidyTabs(mv, cats)) ++tabs;
		}
		const int moved = haveItems ? TidySliders(items) : 0;
		// (Noah: "when I load into a new character, the race menu shows a bunch of random sliders under the race until I move
		// to a new tab") the list kept the filter of the tab that sat at the selected place before the tabs were regrouped
		// (ALL): the selected tab's own filter put back on the list
		if ((tabs || moved) && haveItems) {
			RE::GFxValue cats, sel, entries, e, flag, chain, f0;
			if (mv->GetVariable(&cats, (std::string(kPanel) + ".categoryButtons.categoryList").c_str()) && cats.IsObject() &&
				(cats.GetMember("_selectedIndex", &sel) || cats.GetMember("selectedIndex", &sel)) && sel.IsNumber() &&
				cats.GetMember("_entryList", &entries) && entries.IsArray()) {
				const auto n = entries.GetArraySize();
				auto at = static_cast<std::int64_t>(sel.GetNumber());
				if (at < 0 || at >= static_cast<std::int64_t>(n)) at = 0;
				if (entries.GetElement(static_cast<std::uint32_t>(at), &e) && e.IsObject() && e.GetMember("flag", &flag) && flag.IsNumber()) {
					RE::GFxValue en;
					if (items.GetMember("listEnumeration", &en) && en.IsObject() && en.GetMember("_filterChain", &chain) && chain.IsArray() &&
						chain.GetArraySize() && chain.GetElement(0, &f0) && f0.IsObject()) {
						f0.SetMember("_itemFilter", flag);
						f0.SetMember("itemFilter", flag);   // (SkyUI's setter: it tells the list the filter changed)
					}
					if (at != static_cast<std::int64_t>(sel.GetNumber())) cats.SetMember("_selectedIndex", RE::GFxValue(static_cast<double>(at)));
					items.Invoke("InvalidateData");
					static int told = 0;
					if (told++ < 6) logger::info("race menu: the list filtered to the selected tab (#{}, bits {})", at, flag.GetNumber());
				}
			}
		}
		// (Noah: "on the top right, are you able to hide sliders, presets, camera, and sculpt, so that players are only able to
		// access the character creation sliders") RaceMenu's mode tabs hidden; and its overlay counter ("(4/15)" - paint layers
		// in use, read as a tab count) with them
		for (const char* p : { "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.modeSelect", "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.racePanel.tintCount" }) {
			RE::GFxValue c;
			if (mv->GetVariable(&c, p) && c.IsDisplayObject()) {
				RE::GFxValue vis;
				if (!c.GetMember("_visible", &vis) || !vis.IsBool() || vis.GetBool()) {
					c.SetMember("_visible", RE::GFxValue(false));
					c.SetMember("enabled", RE::GFxValue(false));
				}
			}
		}
		if (tabs || moved) {
			static int told = 0;
			if (told++ < 12) logger::info("race menu: tidied ({} tab lists, {} sliders moved)", tabs, moved);
		}
	}

	// (Noah: "Can you make enter on keyboard the done, which is when you would create the name for the character, and make
	// the navigation for keyboard and mouse WASD, and the light should be Q. And zoom out should be E on keyboard and mouse
	// ... We don't need the mouse cursor") in the character creator, before its menu reads them: W A S D become the arrow
	// keys; Enter, Q and E press RaceMenu's own Done, Light and Zoom. While the name is being typed, every key is the name's.
	namespace
	{
		std::atomic<bool> g_typing{ false };
		void Press(const char* a_fn)
		{
			SKSE::GetTaskInterface()->AddUITask([fn = std::string(a_fn)] {
				auto* ui = RE::UI::GetSingleton();
				auto  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
				auto* mv = menu ? menu->uiMovie.get() : nullptr;
				RE::GFxValue panels;
				if (!mv || !mv->GetVariable(&panels, "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance")) return;
				bool ok = panels.Invoke(fn.c_str());
				for (const char* sub : { "bottomBar", "racePanel", "bottomBar.buttonPanel" }) {   // (wherever RaceMenu keeps it)
					if (ok) break;
					RE::GFxValue c;
					if (mv->GetVariable(&c, (std::string("_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.") + sub).c_str()) && c.IsObject()) ok = c.Invoke(fn.c_str());
				}
				static int told = 0;
				if (told++ < 20) logger::info("race menu: {} pressed from the keyboard ({})", fn, ok ? "done" : "not found");
			});
		}
	}
	void Keys(RE::InputEvent* a_ev)
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME)) return;
		// the name box up: hands off (its letters are the name)
		{
			auto  menu = ui->GetMenu(RE::RaceSexMenu::MENU_NAME);
			auto* mv = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue te, vis;
			g_typing = mv && mv->GetVariable(&te, "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.textEntry") && te.IsDisplayObject() &&
			           te.GetMember("_visible", &vis) && vis.IsBool() && vis.GetBool();
		}
		if (g_typing) return;
		auto* ue = RE::UserEvents::GetSingleton();
		for (auto* e = a_ev; e; e = e->next) {
			auto* b = e->AsButtonEvent();
			if (!b || b->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
			const auto id = b->GetIDCode();
			switch (id) {
			case 0x11: b->SetIDCode(0xC8), b->SetUserEvent(ue->up); break;      // W
			case 0x1F: b->SetIDCode(0xD0), b->SetUserEvent(ue->down); break;    // S
			case 0x1E: b->SetIDCode(0xCB), b->SetUserEvent(ue->left); break;    // A
			case 0x20: b->SetIDCode(0xCD), b->SetUserEvent(ue->right); break;   // D
			case 0x1C: case 0x9C: case 0x10: case 0x12: {   // Enter (and the keypad's), Q, E: ours, and nothing else's
				if (b->IsDown()) Press(id == 0x10 ? "onLightClicked" : id == 0x12 ? "onZoomClicked" : "onDoneClicked");
				b->SetIDCode(0xFF), b->SetUserEvent(RE::BSFixedString(""));
				break;
			}
			default: break;
			}
		}
	}
}
