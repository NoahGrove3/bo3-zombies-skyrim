// (Noah, Nov 27 - Skyrim's menus, ours: "Inventory: remade", "Magic: remade", "Tab menu: remade", with his notes: "I want the
// inventory models as well for the items and the flasks would be in the weapons tab not its own. I also want the model cards like
// how it looks in my game right now") Skyrim's Inventory and Magic menus keep working as they do - the item's 3D model and its
// card (Untarnished UI's) stay; only their category bar and list are hidden, and ours drawn in their place, in Ember Tally:
// the categories as tabs along the top, the list cleaner (a rarity mark, the name, what it is, its count / value), the one
// selected lit. They read the menu's own lists every frame (the keys, the mouse wheel and the pad still drive Skyrim's), so the
// two can never disagree; a click on one of our rows selects it there, a second click uses it. If the menu's lists can't be
// found (another UI mod), nothing is hidden and nothing drawn - its paths are logged once.
// The Tab menu: four medallions round the middle (Skyrim's own buttons, hidden, still take the keys), the night and your
// essence over them, your class under them.
// (included in Hud.cpp's unnamed namespace, after HudTally.inl)

struct MenuLists
{
	std::string items, cats, holder;   // their paths in the movie ("" not found)
	bool searched = false, logged = false;
};
MenuLists g_invLists, g_magLists;

// a list in the movie: an object with an entry array and a selected index (SkyUI's and Skyrim's lists alike)
void FindLists(RE::GFxMovieView* mv, RE::GFxValue a_obj, const std::string& a_path, int a_depth, std::vector<std::pair<std::string, int>>& a_out)
{
	if (a_depth > 7 || !a_obj.IsObject()) return;
	RE::GFxValue entries, sel;
	if (a_obj.GetMember("entryList", &entries) && entries.IsArray() && a_obj.GetMember("selectedIndex", &sel) && sel.IsNumber()) {
		a_out.emplace_back(a_path, static_cast<int>(entries.GetArraySize()));
		return;
	}
	a_obj.VisitMembers([&](const char* a_name, const RE::GFxValue& a_v) {
		if (a_name && a_v.IsDisplayObject() && a_out.size() < 12) FindLists(mv, a_v, a_path + "." + a_name, a_depth + 1, a_out);
	});
}
bool ResolveLists(RE::GFxMovieView* mv, MenuLists& a_l, const char* a_menu)
{
	if (a_l.searched) return !a_l.items.empty();
	a_l.searched = true;
	// (SkyUI and the UIs built on it: _root.Menu_mc.inventoryLists.itemList / categoryList)
	for (const char* holder : { "_root.Menu_mc.inventoryLists", "_root.Menu_mc.InventoryLists_mc", "_root.InventoryLists_mc" }) {
		RE::GFxValue h, il, cl;
		if (!mv->GetVariable(&h, holder) || !h.IsObject()) continue;
		if (h.GetMember("itemList", &il) && il.IsObject()) a_l.items = std::string(holder) + ".itemList";
		else if (h.GetMember("ItemsList", &il) && il.IsObject()) a_l.items = std::string(holder) + ".ItemsList";
		if (h.GetMember("categoryList", &cl) && cl.IsObject()) a_l.cats = std::string(holder) + ".categoryList";
		else if (h.GetMember("CategoriesList", &cl) && cl.IsObject()) a_l.cats = std::string(holder) + ".CategoriesList";
		if (!a_l.items.empty()) { a_l.holder = holder; break; }
	}
	if (a_l.items.empty()) {   // (searched for)
		RE::GFxValue root;
		std::vector<std::pair<std::string, int>> found;
		if (mv->GetVariable(&root, "_root")) FindLists(mv, root, "_root", 0, found);
		for (const auto& [p, n] : found) logger::info("menus: {} - a list at {} ({} entries)", a_menu, p, n);
		std::string lower;
		for (const auto& [p, n] : found) {
			std::string q = p;
			std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (q.find("categor") != std::string::npos) a_l.cats = p;
			else if (a_l.items.empty() || q.find("item") != std::string::npos) a_l.items = p;
		}
		if (const auto dot = a_l.items.rfind('.'); dot != std::string::npos) a_l.holder = a_l.items.substr(0, dot);
	}
	logger::info("menus: {} - items {}, categories {}, holder {}", a_menu, a_l.items.empty() ? "not found" : a_l.items, a_l.cats.empty() ? "none" : a_l.cats, a_l.holder);
	return !a_l.items.empty();
}
struct MenuRow { std::string text, sub, right; int index = -1; bool equipped = false, selected = false; RE::FormID form = 0; };
// the entries the list shows now (its filter applied: SkyUI's listEnumeration, else every entry), and which is selected
std::vector<MenuRow> ReadList(RE::GFxMovieView* mv, const std::string& a_path, int& a_sel, bool a_cats)
{
	std::vector<MenuRow> out;
	a_sel = -1;
	RE::GFxValue list;
	if (!mv->GetVariable(&list, a_path.c_str()) || !list.IsObject()) return out;
	RE::GFxValue sel, entries, en, n;
	const int selIdx = list.GetMember("selectedIndex", &sel) && sel.IsNumber() ? static_cast<int>(sel.GetNumber()) : -1;
	auto readEntry = [&](const RE::GFxValue& e, int idx) {
		MenuRow r;
		RE::GFxValue t, c, eq, ii, fid, v, w, d, a, flag;
		if (e.GetMember("text", &t) && t.IsString()) r.text = t.GetString();
		const int count = e.GetMember("count", &c) && c.IsNumber() ? static_cast<int>(c.GetNumber()) : 1;
		if (e.GetMember("equipState", &eq) && eq.IsNumber()) r.equipped = eq.GetNumber() > 0.5;
		r.index = e.GetMember("itemIndex", &ii) && ii.IsNumber() ? static_cast<int>(ii.GetNumber()) : idx;
		if (e.GetMember("formId", &fid) && fid.IsNumber()) r.form = static_cast<RE::FormID>(fid.GetNumber());
		if (!a_cats) {
			if (count > 1) r.sub = fmt::format("x{}", count);
			if (e.GetMember("infoDamage", &d) && d.IsNumber() && d.GetNumber() > 0) r.right = fmt::format("{:.0f}", d.GetNumber());
			else if (e.GetMember("infoArmor", &a) && a.IsNumber() && a.GetNumber() > 0) r.right = fmt::format("{:.0f}", a.GetNumber());
			else if (e.GetMember("infoValue", &v) && v.IsNumber() && v.GetNumber() > 0) r.right = fmt::format("{:.0f}", v.GetNumber());
		}
		r.selected = r.index == selIdx || idx == selIdx;
		return r;
	};
	RE::GFxValue en2;
	if (list.GetMember("listEnumeration", &en2) && en2.IsObject() && en2.Invoke("size", &n) && n.IsNumber()) {
		const int sz = static_cast<int>(n.GetNumber());
		for (int i = 0; i < sz && i < 600; ++i) {
			RE::GFxValue e, arg(static_cast<double>(i));
			if (!en2.Invoke("at", &e, &arg, 1) || !e.IsObject()) continue;
			out.push_back(readEntry(e, i));
			if (out.back().selected) a_sel = static_cast<int>(out.size()) - 1;
		}
	} else if (list.GetMember("entryList", &entries) && entries.IsArray()) {
		for (std::uint32_t i = 0; i < entries.GetArraySize() && i < 600; ++i) {
			RE::GFxValue e;
			if (!entries.GetElement(i, &e) || !e.IsObject()) continue;
			RE::GFxValue filt;
			if (!a_cats && e.GetMember("filterFlag", &filt) && filt.IsNumber() && filt.GetNumber() == 0) continue;
			out.push_back(readEntry(e, static_cast<int>(i)));
			if (out.back().selected) a_sel = static_cast<int>(out.size()) - 1;
		}
	}
	return out;
}
// the selected entry set (a click on our row), and on a second click used
void ListSelect(RE::GFxMovieView* mv, const std::string& a_path, int a_index, bool a_press)
{
	RE::GFxValue list;
	if (!mv->GetVariable(&list, a_path.c_str()) || !list.IsObject()) return;
	if (a_press) {
		if (list.HasMember("onItemPress")) { RE::GFxValue arg(0.0); list.Invoke("onItemPress", nullptr, &arg, 1); }
		return;
	}
	if (list.HasMember("doSetSelectedIndex")) {
		std::array<RE::GFxValue, 2> args{ RE::GFxValue(static_cast<double>(a_index)), RE::GFxValue(1.0) };
		list.Invoke("doSetSelectedIndex", args);
	} else {
		list.SetMember("selectedIndex", RE::GFxValue(static_cast<double>(a_index)));
	}
}
// a Tablet or class weapon's rarity colour, from its form (0: none)
ImU32 RowRarity(RE::FormID a_form)
{
	if (!a_form) return 0;
	for (const auto& w : gen::kTabletWeapons) {
		if (auto* f = Forms::Ours(w.form); f && f->GetFormID() == a_form) {
			static constexpr ImU32 kRare[3] = { IM_COL32(61, 140, 255, 255), IM_COL32(168, 77, 255, 255), IM_COL32(255, 179, 41, 255) };
			return kRare[std::clamp(w.rarity, 0, 2)];
		}
	}
	return 0;
}
void TallyItemMenu(RE::UI* a_ui, ImVec2 size, int a_clicks, bool a_magic)
{
	auto menu = a_ui->GetMenu(a_magic ? RE::MagicMenu::MENU_NAME : RE::InventoryMenu::MENU_NAME);
	auto* mv = menu ? menu->uiMovie.get() : nullptr;
	auto& L = a_magic ? g_magLists : g_invLists;
	if (!mv || !ResolveLists(mv, L, a_magic ? "Magic" : "Inventory")) return;
	int sel = -1, csel = -1;
	auto rows = ReadList(mv, L.items, sel, false);
	auto cats = L.cats.empty() ? std::vector<MenuRow>{} : ReadList(mv, L.cats, csel, true);
	// Skyrim's own bar and list out of sight (they still take the keys); ours over where they were
	if (!L.holder.empty()) mv->SetVariable((L.holder + "._alpha").c_str(), RE::GFxValue(0.0));
	auto* dl = ImGui::GetForegroundDrawList();
	const float u = TU(size);
	const ImVec2 A{ size.x * 0.03f, size.y * 0.1f };
	const float W = std::min(size.x * 0.36f, 30.0f * u);
	// (a smoke behind it, fading to the right)
	dl->AddRectFilledMultiColor({ 0, 0 }, { A.x + W + 6 * u, size.y }, IM_COL32(8, 5, 3, 200), IM_COL32(8, 5, 3, 0), IM_COL32(8, 5, 3, 0), IM_COL32(8, 5, 3, 200));
	TText(dl, HF(), 0.7f * u, { A.x, A.y - 2.6f * u }, kTLab, a_magic ? "MAGIC" : "INVENTORY", 0.45f);
	// the categories as tabs (Skyrim's own, in its order: the flasks are weapons, so they stand under Weapons)
	float tx = A.x, ty = A.y - 1.4f * u;
	for (int i = 0; i < static_cast<int>(cats.size()); ++i) {
		const auto& c = cats[i];
		if (c.text.empty()) continue;
		std::string nm = Upper(c.text);
		const float tw = TMeasure(HF(), 0.78f * u, nm, 0.2f).x;
		if (tx + tw > A.x + W + 4 * u) { tx = A.x, ty += 1.5f * u; }
		const bool on = i == csel;
		TText(dl, HF(), 0.78f * u, { tx, ty }, on ? kTInk : kTTxt, nm, 0.2f);
		if (on) dl->AddRectFilled({ tx, ty + 1.15f * u }, { tx + tw, ty + 1.3f * u }, kTAcc);
		tx += tw + 1.4f * u;
	}
	const float listTop = ty + 2.2f * u;
	dl->AddRectFilled({ A.x, listTop - 0.6f * u }, { A.x + W, listTop - 0.54f * u }, IM_COL32(232, 112, 63, 89));
	// the rows: the selected one kept in view
	const float rh = 2.1f * u;
	const int vis = std::max(4, static_cast<int>((size.y * 0.9f - listTop) / rh));
	static int first = 0;
	if (sel >= 0) {
		if (sel < first) first = sel;
		if (sel >= first + vis) first = sel - vis + 1;
	}
	first = std::clamp(first, 0, std::max(0, static_cast<int>(rows.size()) - vis));
	const ImVec2 m = Cursor(size);
	static int lastClickRow = -1;
	for (int i = first; i < static_cast<int>(rows.size()) && i < first + vis; ++i) {
		const auto& r = rows[i];
		const float y = listTop + (i - first) * rh;
		const bool on = i == sel;
		const bool hover = m.x >= A.x && m.x <= A.x + W && m.y >= y && m.y < y + rh - 0.2f * u;
		if (on) {
			dl->AddRectFilledMultiColor({ A.x, y }, { A.x + W, y + rh - 0.2f * u }, IM_COL32(255, 122, 60, 82), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(255, 122, 60, 82));
			dl->AddRectFilled({ A.x, y }, { A.x + 0.22f * u, y + rh - 0.2f * u }, kTAcc);
		} else if (hover) {
			dl->AddRectFilled({ A.x, y }, { A.x + W, y + rh - 0.2f * u }, IM_COL32(255, 255, 255, 14));
		}
		const ImU32 rc = RowRarity(r.form);
		if (rc) dl->AddQuadFilled({ A.x + 1.0f * u, y + rh * 0.5f - 0.35f * u }, { A.x + 1.35f * u, y + rh * 0.5f }, { A.x + 1.0f * u, y + rh * 0.5f + 0.35f * u }, { A.x + 0.65f * u, y + rh * 0.5f }, rc);
		if (r.equipped) dl->AddCircleFilled({ A.x + 1.0f * u, y + rh * 0.5f }, 0.2f * u, kTGold, 10);
		std::string nm = r.text;
		const float maxW = W - 6.0f * u;
		while (nm.size() > 4 && Measure(HF(), 1.0f * u, nm).x > maxW) nm = nm.substr(0, nm.size() - 4) + "...";
		TText(dl, HF(), 1.0f * u, { A.x + 2.0f * u, y + 0.35f * u }, on ? kTInk : kTTxt, nm, 0.02f);
		if (!r.sub.empty()) TText(dl, Inter(), 0.62f * u, { A.x + 2.0f * u + Measure(HF(), 1.0f * u, nm).x + 0.6f * u, y + 0.65f * u }, kTTxt, r.sub);
		if (!r.right.empty()) TTextR(dl, HF(), 0.9f * u, A.x + W - 0.8f * u, y + 0.45f * u, on ? kTGold : Fade(kTGold, 0.8f), r.right);
		if (hover && a_clicks > 0) {   // (a click selects it; a click on the one selected uses it)
			ListSelect(mv, L.items, r.index, on && lastClickRow == i);
			lastClickRow = i;
		}
	}
	if (rows.size() > static_cast<std::size_t>(vis)) {   // (how far down)
		const float track = vis * rh, f = static_cast<float>(first) / std::max(1, static_cast<int>(rows.size()) - vis);
		const float bh = std::max(2.0f * u, track * vis / rows.size());
		dl->AddRectFilled({ A.x + W + 0.5f * u, listTop + (track - bh) * f }, { A.x + W + 0.7f * u, listTop + (track - bh) * f + bh }, IM_COL32(232, 112, 63, 150));
	}
	if (rows.empty()) TText(dl, Inter(), 0.8f * u, { A.x + 1.0f * u, listTop }, kTTxt, "Nothing here");
	if (!L.logged) {
		L.logged = true;
		logger::info("menus: {} drawn ours - {} rows, {} categories", a_magic ? "Magic" : "Inventory", rows.size(), cats.size());
	}
}
// the Tab menu: Skyrim's own (hidden) keeps its keys; four medallions round the middle in their places, the night and essence
// over them, your class under them
void TallyTween(RE::UI* a_ui, ImVec2 size, const Hud::State& s, float now)
{
	auto menu = a_ui->GetMenu(RE::TweenMenu::MENU_NAME);
	auto* mv = menu ? menu->uiMovie.get() : nullptr;
	if (!mv) return;
	static bool logged = false;
	if (!logged) {   // (what the Tab menu holds - for the next fit if a mod changed it)
		logged = true;
		RE::GFxValue root;
		if (mv->GetVariable(&root, "_root")) root.VisitMembers([](const char* a_name, const RE::GFxValue& a_v) { if (a_name && a_v.IsDisplayObject()) logger::info("menus: Tab menu holds {}", a_name); });
	}
	mv->SetVisible(false);
	auto* dl = ImGui::GetForegroundDrawList();
	const float u = TU(size);
	const ImVec2 c{ size.x * 0.5f, size.y * 0.5f };
	dl->AddRectFilled({ 0, 0 }, size, IM_COL32(6, 5, 4, 110));
	const float R = 9.0f * u, mr = 3.2f * u;
	struct Med { const char* name; ImVec2 at; int icon; };
	const Med meds[4] = { { "SKILLS", { c.x, c.y - R }, 0 }, { "MAGIC", { c.x - R * 1.35f, c.y }, 1 }, { "ITEMS", { c.x + R * 1.35f, c.y }, 2 }, { "MAP", { c.x, c.y + R }, 3 } };
	const ImVec2 m = Cursor(size);
	for (const auto& md : meds) {
		const bool hover = std::hypot(m.x - md.at.x, m.y - md.at.y) < mr * 1.2f;
		const float br = hover ? 1.0f : 0.75f + 0.1f * std::sin(now * 1.5f + md.icon);
		for (int g = 3; g >= 1; --g) dl->AddCircleFilled(md.at, mr + g * 0.25f * u, IM_COL32(255, 122, 60, static_cast<int>(20 * br)), 40);
		dl->AddCircleFilled(md.at, mr, IM_COL32(18, 11, 7, 235), 40);
		dl->AddCircle(md.at, mr, Fade(hover ? kTAcc : kTGold, br), 40, 0.16f * u);
		dl->AddCircle(md.at, mr - 0.45f * u, Fade(kTEmb, 0.5f * br), 40, 0.06f * u);
		const float r = 1.0f * u;
		const ImU32 ic = Fade(hover ? kTAcc : kTInk, br);
		switch (md.icon) {
		case 0: for (int q = 0; q < 3; ++q) dl->AddLine({ md.at.x - r + q * r, md.at.y + r * 0.8f }, { md.at.x - r + q * r, md.at.y + r * 0.8f - (q + 1) * r * 0.55f }, ic, 0.2f * u); break;   // (rising bars)
		case 1: { const ImVec2 p[5] = { { md.at.x, md.at.y - r * 1.1f }, { md.at.x + r * 0.7f, md.at.y + r * 0.1f }, { md.at.x, md.at.y + r * 0.9f }, { md.at.x - r * 0.7f, md.at.y + r * 0.1f }, { md.at.x, md.at.y - r * 1.1f } }; dl->AddPolyline(p, 5, ic, 0, 0.16f * u); break; }   // (a flame)
		case 2: { const ImVec2 p[4] = { { md.at.x - r * 0.8f, md.at.y - r * 0.3f }, { md.at.x + r * 0.8f, md.at.y - r * 0.3f }, { md.at.x + r * 0.6f, md.at.y + r * 0.9f }, { md.at.x - r * 0.6f, md.at.y + r * 0.9f } }; dl->AddPolyline(p, 4, ic, ImDrawFlags_Closed, 0.16f * u); dl->PathArcTo({ md.at.x, md.at.y - r * 0.3f }, r * 0.45f, 3.1416f, 6.2832f, 12); dl->PathStroke(ic, 0, 0.16f * u); break; }   // (a pack)
		default: { const ImVec2 p[8] = { { md.at.x - r, md.at.y - r * 0.6f }, { md.at.x - r * 0.33f, md.at.y - r * 0.9f }, { md.at.x + r * 0.33f, md.at.y - r * 0.6f }, { md.at.x + r, md.at.y - r * 0.9f }, { md.at.x + r, md.at.y + r * 0.6f }, { md.at.x + r * 0.33f, md.at.y + r * 0.9f }, { md.at.x - r * 0.33f, md.at.y + r * 0.6f }, { md.at.x - r, md.at.y + r * 0.9f } }; dl->AddPolyline(p, 8, ic, ImDrawFlags_Closed, 0.16f * u); break; }   // (a folded map)
		}
		TTextC(dl, HFB(), 0.9f * u, md.at.x, md.at.y + mr + 0.5f * u, Fade(hover ? kTInk : kTTxt, br), md.name, 0.3f);
	}
	// the night and your essence over them; your class under
	{
		const std::string night = s.round > 0 ? fmt::format("NIGHT {}", s.round) : std::string("THE CURSE OF ALDUIN");
		TTextC(dl, HFB(), 2.0f * u, c.x, c.y - R - mr - 5.2f * u, kTInk, night, 0.2f, IM_COL32(232, 90, 40, 255), 0.3f * u);
		TTextC(dl, HF(), 0.85f * u, c.x, c.y - R - mr - 2.6f * u, kTGold, Num(s.points) + "  CURSED ESSENCE", 0.2f);
		const int cls = Classes::Get();
		auto* p = RE::PlayerCharacter::GetSingleton();
		const int lvl = p ? p->GetLevel() : 0;
		const std::string foot = (cls >= 0 ? Upper(std::string(Classes::Name(cls))) + "  \xC2\xB7  " : std::string()) + fmt::format("LEVEL {}", lvl);
		const float fy = c.y + R + mr + 2.6f * u;
		TTextC(dl, HF(), 0.85f * u, c.x, fy, kTLab, foot, 0.3f);
		float xp = 0.0f;
		if (p) if (auto* sk = p->GetInfoRuntimeData().skills; sk && sk->data && sk->data->levelThreshold > 0) xp = std::clamp(sk->data->xp / sk->data->levelThreshold, 0.0f, 1.0f);
		TBar(dl, { c.x - 6.0f * u, fy + 1.4f * u }, 12.0f * u, 0.16f * u, xp, kTAcc, IM_COL32(255, 255, 255, 40), true);
	}
}
