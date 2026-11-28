// (Noah, Nov 27 test of 2b534e1, the main menu: "the load page and party page should be updated, the text looks layered and blurry
// and the party UI needs to match our new UI Setup. My mouse cursor also stays behind the party UI instead of over it. The
// confirmation buttons and text need to match the new UI. I also want to add a settings tab in the menu for graphics, resolution,
// Fps cap, all the gameplay settings and general ones too like audio") - his picks: Settings as tabs on one page; what needs a
// restart saved now and applied on the next launch; Load with the save's picture, Delete and a filter by character.
// The Ember Tally main menu is ours entirely now: Skyrim's (Edge UI's) own menu is hidden and takes no keys (it was laid over our
// painting through a key - the layered, blurry words); its list still knows CONTINUE / NEW / QUIT and does them when we press
// them, and its questions (Start a new game?) are answered by our own Yes / No. Load, Settings and the party are our own pages.
// (included in Hud.cpp's unnamed namespace, after TallyMenuWords)

struct TmIn
{
	int moves = 0, accepts = 0, backs = 0, sides = 0, wheel = 0, tabs = 0, dels = 0;
};

// (Noah, Nov 28: "For controller the binds should be the dpad or left stick direction and X or A") the left stick as the d-pad:
// a push moves one; held, it repeats
void TmStick(TmIn& a_in, float a_dt)
{
	static float ty = -1.0f, tx = -1.0f;
	auto step = [&](float v, float& t, int& out, int sign) {
		if (std::abs(v) < 0.55f) { t = -1.0f; return; }
		const int d = (v > 0 ? 1 : -1) * sign;
		if (t < 0.0f) { out += d, t = 0.0f; return; }
		t += a_dt;
		if (t > 0.38f) out += d, t -= 0.12f;
	};
	const float x = g_lsX.load(), y = g_lsY.load();
	if (std::abs(y) >= std::abs(x)) step(y, ty, a_in.moves, -1), tx = -1.0f;
	else step(x, tx, a_in.sides, 1), ty = -1.0f;
}

// the mouse counts for "which one" only once it moves; a key or the stick hands the choice back to the keys
struct TmMouse
{
	ImVec2 last{ -1, -1 };
	bool   on = true;
	void   Note(ImVec2 a_cur, const TmIn& a_in)
	{
		if (last.x >= 0 && std::abs(a_cur.x - last.x) + std::abs(a_cur.y - last.y) > 3.0f) on = true;
		last = a_cur;
		if (a_in.moves || a_in.sides) on = false;
	}
};

std::string TmUpper(std::string a_s)
{
	if (!a_s.empty() && a_s[0] == '$') a_s = a_s.substr(1);
	std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
	return a_s;
}

// a word in the menu's style: parchment, ember orange (with a soft halo) when chosen or under the mouse; true when clicked
bool TmWord(ImDrawList* fg, ImFont* f, float px, float cx, float y, const std::string& s, bool lit, ImVec2 cur, bool click, float track, float a, bool* a_hover = nullptr, ImU32 a_col = kTTxt)
{
	const float tw = TMeasure(f, px, s, track).x;
	const bool  over = cur.x >= cx - tw * 0.5f - px * 0.6f && cur.x <= cx + tw * 0.5f + px * 0.6f && cur.y >= y - px * 0.3f && cur.y <= y + px * 1.3f;
	if (a_hover) *a_hover = over;
	const bool on = lit || over;
	TTextC(fg, f, px, cx, y, Fade(on ? kTAcc : a_col, a), s, track, on ? IM_COL32(232, 90, 40, static_cast<int>(150 * a)) : 0, on ? 0.06f * px : 0.0f);
	return over && click;
}

// a page's heading: spaced capitals, the thin orange line under them (clear of the letters)
void TmHeading(ImDrawList* fg, ImVec2 size, float y, const std::string& s, float a)
{
	const float W = size.x, u = TU(size);
	TTextC(fg, HFB(), 2.6f * u, W * 0.5f, y, Fade(kTInk, a), s, 0.3f, IM_COL32(232, 90, 40, static_cast<int>(100 * a)), 0.08f * u);
	const float ly = y + 3.0f * u, hw = 8.0f * u;
	fg->AddRectFilledMultiColor({ W * 0.5f - hw, ly }, { W * 0.5f, ly + 0.1f * u + 1.0f }, IM_COL32(255, 122, 60, 0), Fade(kTAcc, a), Fade(kTAcc, a), IM_COL32(255, 122, 60, 0));
	fg->AddRectFilledMultiColor({ W * 0.5f, ly }, { W * 0.5f + hw, ly + 0.1f * u + 1.0f }, Fade(kTAcc, a), IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, 0), Fade(kTAcc, a));
}

// the keys along the foot, both ways (pad or keyboard, whichever was touched last): a key in a small gold frame, then what it
// does; each is clickable. Returns the one clicked, or -1.
// (Noah, Nov 28: "I want to remove the control prompts that show up on the bottom of the screen in the menu") no longer drawn - the
// pages have their own words to click (BACK, LOAD, DELETE...) and the keys and pad still work as before
constexpr bool kTmHints = false;
int TmHints(ImDrawList* fg, ImVec2 size, const std::vector<std::pair<std::string, std::string>>& a_items, ImVec2 cur, bool click, float a)
{
	if (!kTmHints) return -1;
	const float u = TU(size), fs = 0.95f * u, ks = 0.8f * u, gap = 2.4f * u, pad = 0.45f * u;
	float total = 0;
	std::vector<float> ws;
	for (const auto& [key, what] : a_items) {
		const float w = TMeasure(HFB(), ks, key, 0.08f).x + pad * 2 + 0.6f * u + TMeasure(HF(), fs, what, 0.18f).x;
		ws.push_back(w), total += w;
	}
	total += gap * static_cast<float>(std::max<std::size_t>(1, a_items.size()) - 1);
	float x = size.x * 0.5f - total * 0.5f;
	const float y = size.y - 4.2f * u;
	int hit = -1;
	for (std::size_t i = 0; i < a_items.size(); ++i) {
		const auto& [key, what] = a_items[i];
		const bool over = cur.x >= x - 0.4f * u && cur.x <= x + ws[i] + 0.4f * u && cur.y >= y - 0.5f * u && cur.y <= y + 1.6f * u;
		const float kw = TMeasure(HFB(), ks, key, 0.08f).x + pad * 2;
		fg->AddRect({ x, y - 0.12f * u }, { x + kw, y + 1.32f * u }, Fade(over ? kTAcc : kTGold, 0.85f * a), 0.25f * u, 0, std::max(1.0f, 0.08f * u));
		TText(fg, HFB(), ks, { x + pad, y + 0.1f * u }, Fade(over ? kTAcc : kTGold, a), key, 0.08f);
		TText(fg, HF(), fs, { x + kw + 0.6f * u, y }, Fade(over ? kTAcc : kTTxt, a), what, 0.18f);
		if (over && click) hit = static_cast<int>(i);
		x += ws[i] + gap;
	}
	return hit;
}

// a page's way back, top left: "BACK" in the menu's words (the hints are gone); true when clicked
bool TmBack(ImDrawList* fg, ImVec2 size, ImVec2 cur, bool click, float a)
{
	// (Noah's pick, Nov 28: "Right-click backs out" - no BACK word; a right click, Esc or B goes back)
	constexpr bool kBackWord = false;
	if (!kBackWord) return false;
	const float u = TU(size);
	bool hv = false;
	const float px = 1.0f * u, x = 4.0f * u, y = 3.2f * u;
	const std::string t = "\xE2\x80\xB9  BACK";
	const float tw = TMeasure(HF(), px, t, 0.3f).x;
	return TmWord(fg, HF(), px, x + tw * 0.5f, y, t, false, cur, click, 0.3f, 0.85f * a, &hv);
}

// a question in our style: the scene dimmed, the words in the middle, YES and NO under them. Returns 1 yes, 0 no, -1 asking.
int TmAsk(ImDrawList* fg, ImVec2 size, float a, const std::string& a_text, const std::string& a_sub, ImVec2 cur, bool click, const TmIn& in, int& a_focus)
{
	const float W = size.x, H = size.y, u = TU(size);
	fg->AddRectFilled({ 0, 0 }, size, IM_COL32(4, 2, 1, static_cast<int>(170 * a)));
	const float tw = std::max(TMeasure(HFB(), 2.0f * u, a_text, 0.12f).x, TMeasure(Inter(), 1.05f * u, a_sub).x);
	TShade(fg, { W * 0.5f - tw * 0.5f - 3.0f * u, H * 0.40f - 1.5f * u }, { W * 0.5f + tw * 0.5f + 3.0f * u, H * 0.58f }, 4.0f * u, a);
	TTextC(fg, HFB(), 2.0f * u, W * 0.5f, H * 0.40f, Fade(kTInk, a), a_text, 0.12f, IM_COL32(232, 90, 40, static_cast<int>(90 * a)), 0.06f * u);
	if (!a_sub.empty()) TTextC(fg, Inter(), 1.05f * u, W * 0.5f, H * 0.40f + 3.0f * u, Fade(kTTxt, 0.85f * a), a_sub);
	a_focus = std::clamp(a_focus + in.sides + in.moves, 0, 1);
	bool hy = false, hn = false;
	const float y = H * 0.50f;
	const bool  cy = TmWord(fg, HFB(), 1.6f * u, W * 0.5f - 6.0f * u, y, "YES", a_focus == 0, cur, click, 0.35f, a, &hy);
	const bool  cn = TmWord(fg, HFB(), 1.6f * u, W * 0.5f + 6.0f * u, y, "NO", a_focus == 1, cur, click, 0.35f, a, &hn);
	if (hy) a_focus = 0;
	if (hn) a_focus = 1;
	const int  h = TmHints(fg, size, { { g_padLast ? Hud::PadWord("A") : std::string("ENTER"), "CHOOSE" }, { g_padLast ? Hud::PadWord("B") : std::string("ESC"), "NO" } }, cur, click, a);
	if (cy || (in.accepts > 0 && a_focus == 0) || (h == 0 && a_focus == 0)) return 1;
	if (cn || in.backs > 0 || (in.accepts > 0 && a_focus == 1) || h == 1 || (h == 0 && a_focus == 1)) return 0;
	return -1;
}

// ---------------------------------------------------------------- Skyrim's own question (Start a new game? Quit?), in our words
std::string MenuQuestionText(RE::GFxMovieView* a_mv)
{
	RE::GFxValue conf;
	if (!a_mv || !a_mv->GetVariable(&conf, "_root.MenuHolder.Menu_mc.ConfirmPanel_mc") || !conf.IsDisplayObject()) return {};
	std::string best;
	std::function<void(RE::GFxValue&, int)> walk = [&](RE::GFxValue& a_o, int a_depth) {
		a_o.VisitMembers([&](const char*, const RE::GFxValue& a_v) {
			if (!a_v.IsDisplayObject()) return;
			auto& v = const_cast<RE::GFxValue&>(a_v);
			RE::GFxValue::DisplayInfo di;
			v.GetDisplayInfo(&di);
			if (!di.GetVisible()) return;
			RE::GFxValue t;
			if (v.GetMember("text", &t) && t.IsString() && t.GetString()) {
				std::string s = t.GetString();
				if (!s.empty() && s[0] == '$') s = s.substr(1);
				if (s.size() > best.size()) best = s;
			}
			if (a_depth < 2) walk(v, a_depth + 1);
		});
	};
	walk(conf, 0);
	return best;
}
void MenuAnswer(RE::GFxMovieView* a_mv, bool a_yes)
{
	RE::GFxValue menu;
	if (!a_mv || !a_mv->GetVariable(&menu, "_root.MenuHolder.Menu_mc")) return;
	for (const char* fn : a_yes ? std::array<const char*, 2>{ "onAcceptPress", "onAcceptMousePress" } : std::array<const char*, 2>{ "onCancelPress", "onCancelMousePress" }) {
		if (menu.Invoke(fn, nullptr, nullptr, 0)) {
			logger::info("menu: Skyrim's question answered {} ({})", a_yes ? "yes" : "no", fn);
			return;
		}
	}
	logger::warn("menu: Skyrim's question could not be answered {} - its menu has no answer to call", a_yes ? "yes" : "no");
}

// ---------------------------------------------------------------- the save's own picture (in its file, before the game itself)
ID3D11ShaderResourceView* SaveShot(const std::string& a_file, int& a_w, int& a_h)
{
	struct Shot
	{
		std::string file;
		ID3D11ShaderResourceView* srv = nullptr;
		int w = 0, h = 0;
	};
	static Shot cur;
	if (cur.file == a_file) { a_w = cur.w, a_h = cur.h; return cur.srv; }
	if (cur.srv) cur.srv->Release();
	cur = {};
	cur.file = a_file;
	a_w = a_h = 0;
	auto dir = SKSE::log::log_directory();
	if (!dir || !g_device || a_file.empty()) return nullptr;
	const auto saves = dir->parent_path() / "Saves";
	std::error_code ec;
	auto p = saves / (a_file + ".ess");
	if (!std::filesystem::exists(p, ec)) p = saves / a_file;
	std::ifstream f(p, std::ios::binary);
	if (!f) return nullptr;
	char magic[13]{};
	std::uint32_t hs = 0;
	f.read(magic, 13), f.read(reinterpret_cast<char*>(&hs), 4);
	if (!f || std::memcmp(magic, "TESV_SAVEGAME", 13) != 0 || hs < 16 || hs > 4096) return nullptr;
	std::vector<char> hdr(hs);
	f.read(hdr.data(), hs);
	if (!f) return nullptr;
	std::size_t o = 0;
	auto u32 = [&]() { std::uint32_t v = 0; if (o + 4 <= hdr.size()) std::memcpy(&v, hdr.data() + o, 4); o += 4; return v; };
	auto u16 = [&]() { std::uint16_t v = 0; if (o + 2 <= hdr.size()) std::memcpy(&v, hdr.data() + o, 2); o += 2; return v; };
	auto wstr = [&]() { o += u16(); };
	const std::uint32_t ver = u32();
	u32(), wstr(), u32(), wstr(), wstr(), wstr(), u16(), u32(), u32();
	o += 8;   // (the file time)
	const std::uint32_t sw = u32(), sh = u32();
	if (o > hdr.size() || sw == 0 || sh == 0 || sw > 4096 || sh > 4096) return nullptr;
	const std::size_t bpp = ver >= 12 ? 4 : 3;
	std::vector<std::uint8_t> raw(static_cast<std::size_t>(sw) * sh * bpp);
	f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
	if (!f) return nullptr;
	std::vector<std::uint8_t> rgba(static_cast<std::size_t>(sw) * sh * 4);
	for (std::size_t i = 0; i < static_cast<std::size_t>(sw) * sh; ++i) {
		rgba[i * 4 + 0] = raw[i * bpp + 0], rgba[i * 4 + 1] = raw[i * bpp + 1], rgba[i * 4 + 2] = raw[i * bpp + 2], rgba[i * 4 + 3] = 255;
	}
	D3D11_TEXTURE2D_DESC td{};
	td.Width = sw, td.Height = sh, td.MipLevels = 1, td.ArraySize = 1, td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1, td.Usage = D3D11_USAGE_IMMUTABLE, td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd{ rgba.data(), sw * 4, 0 };
	ID3D11Texture2D* tex = nullptr;
	if (FAILED(g_device->CreateTexture2D(&td, &sd, &tex)) || !tex) return nullptr;
	g_device->CreateShaderResourceView(tex, nullptr, &cur.srv);
	tex->Release();
	cur.w = static_cast<int>(sw), cur.h = static_cast<int>(sh);
	a_w = cur.w, a_h = cur.h;
	return cur.srv;
}
// (Noah's pick: "Delete a save") put aside, not destroyed: the save and its SKSE co-save are moved into Saves\Deleted, where
// Skyrim no longer lists them (and from where they can be brought back)
bool DeleteSave(const std::string& a_file)
{
	auto dir = SKSE::log::log_directory();
	if (!dir || a_file.empty()) return false;
	const auto saves = dir->parent_path() / "Saves", aside = saves / "Deleted";
	std::error_code ec;
	std::filesystem::create_directories(aside, ec);
	bool moved = false;
	for (const char* ext : { ".ess", ".skse" }) {
		const auto from = saves / (a_file + ext);
		if (!std::filesystem::exists(from, ec)) continue;
		std::filesystem::rename(from, aside / (a_file + ext), ec);
		if (ec) logger::warn("menu: '{}{}' could not be put aside ({})", a_file, ext, ec.message());
		else moved = true;
	}
	logger::info("menu: save '{}' {} Saves\\Deleted", a_file, moved ? "put aside in" : "NOT moved to");
	return moved;
}

// ---------------------------------------------------------------- LOAD: every save, its picture, by character
bool TmLoad(ImDrawList* fg, ImVec2 size, float now, float dt, float a, ImVec2 cur, bool click, const TmIn& in)
{
	// (Noah, Nov 28: "In the load menu left click on a save or X/A should select the save and pressing again should load it, that way
	// players can select a save and choose to either load that save or delete the selected save") a click, A / Cross or Enter
	// chooses a save; the same again loads it; once chosen its LOAD and DELETE stand under its picture (left / right between them)
	static int   sel = 0, filter = 0, askFocus = 1, chosen = -1, btn = 0;
	static float scroll = 0, target = 0;
	static bool  loading = false, asking = false;
	static TmMouse mouse;
	const float W = size.x, H = size.y, u = TU(size);
	TmHeading(fg, size, 6.0f * u, "LOAD", a);
	std::vector<SaveRow> all;
	{
		std::scoped_lock l(g_saveLock);
		all = g_saves;
	}
	// the characters, most recently played first
	std::vector<std::string> chars;
	for (const auto& r : all) {
		const std::string nm = r.name.empty() ? std::string("Unknown") : r.name;
		if (std::find(chars.begin(), chars.end(), nm) == chars.end()) chars.push_back(nm);
	}
	const int nf = static_cast<int>(chars.size()) + 1;
	const int filter0 = filter;
	if (!asking) filter = (filter + in.tabs + nf * 8) % nf;
	// the filter: < ALL CHARACTERS >
	{
		const std::string who = filter == 0 ? std::string("ALL CHARACTERS") : TmUpper(chars[std::min(filter, nf - 1) - 1]);
		const float y = 10.6f * u;
		bool hl = false, hr = false;
		const float ww = TMeasure(HF(), 1.2f * u, who, 0.25f).x;
		TTextC(fg, HF(), 1.2f * u, W * 0.5f, y, Fade(kTGold, a), who, 0.25f);
		if (TmWord(fg, HFB(), 1.3f * u, W * 0.5f - ww * 0.5f - 2.4f * u, y - 0.05f * u, "\xE2\x80\xB9", false, cur, click && !asking, 0.0f, a, &hl)) filter = (filter + nf - 1) % nf, click = false;
		if (TmWord(fg, HFB(), 1.3f * u, W * 0.5f + ww * 0.5f + 2.4f * u, y - 0.05f * u, "\xE2\x80\xBA", false, cur, click && !asking, 0.0f, a, &hr)) filter = (filter + 1) % nf, click = false;
	}
	if (filter != filter0) chosen = -1, sel = 0, target = 0;
	std::vector<SaveRow> saves;
	for (const auto& r : all) {
		if (filter == 0 || (r.name.empty() ? std::string("Unknown") : r.name) == chars[std::min(filter, nf - 1) - 1]) saves.push_back(r);
	}
	const int n = g_savesReady ? static_cast<int>(saves.size()) : 0;
	if (!g_savesReady) TTextC(fg, Inter(), 1.1f * u, W * 0.5f, H * 0.45f, Fade(kTTxt, a), "Reading your saves...");
	else if (n == 0) TTextC(fg, Inter(), 1.1f * u, W * 0.5f, H * 0.45f, Fade(kTTxt, a), "No saves yet - start a New game");
	// the list on the left, the chosen save's picture and its details on the right
	const float listL = W * 0.5f - 44.0f * u, listR = W * 0.5f + 2.0f * u, top = 14.0f * u, bottom = H - 6.0f * u;
	const float rowH = 4.4f * u;
	const int   fits = std::max(1, static_cast<int>((bottom - top) / rowH));
	mouse.Note(cur, in);
	const float maxScroll = std::max(0.0f, static_cast<float>(n - fits));
	if (n > 0 && !asking && in.moves) {
		sel = std::clamp(sel + in.moves, 0, n - 1);
		if (chosen != sel) chosen = -1;
		target = std::clamp(target, static_cast<float>(sel - fits + 1), static_cast<float>(sel));
	}
	if (!asking && in.wheel) target += static_cast<float>(in.wheel) * 2.0f;
	target = std::clamp(std::round(target), 0.0f, maxScroll);
	if (n > 0) sel = std::clamp(sel, 0, n - 1);
	if (chosen >= n) chosen = -1;
	if (!asking && chosen >= 0 && in.sides) btn = std::clamp(btn + in.sides, 0, 1);
	scroll += (target - scroll) * std::min(1.0f, dt * 14.0f);
	if (std::abs(target - scroll) < 0.002f) scroll = target;
	int hovered = -1;
	fg->PushClipRect({ listL - 2.0f * u, top - 0.6f * u }, { listR + 2.0f * u, bottom + 0.2f * u }, true);
	for (int i = std::max(0, static_cast<int>(scroll) - 1); i < std::min(n, static_cast<int>(scroll) + fits + 2); ++i) {
		const auto& r = saves[i];
		const float y = top + (static_cast<float>(i) - scroll) * rowH;
		const bool  over = !asking && cur.x >= listL && cur.x <= listR && cur.y >= y && cur.y < y + rowH && cur.y >= top && cur.y <= bottom;
		if (over) hovered = i;
		const bool on = i == chosen, lit = i == sel || on;
		if (lit) {
			const float pl = 0.8f + 0.2f * std::sin(now * 3.0f);
			const int   al = static_cast<int>((on ? 52 : 22) * pl * a);
			fg->AddRectFilledMultiColor({ listL, y }, { listR, y + rowH - 0.3f * u }, IM_COL32(255, 122, 60, al), IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, al));
			if (on) fg->AddRectFilled({ listL, y + 0.4f * u }, { listL + 0.22f * u, y + rowH - 0.7f * u }, Fade(kTAcc, a));
		} else {
			fg->AddLine({ listL + 1.4f * u, y + rowH - 0.15f * u }, { listR - 1.4f * u, y + rowH - 0.15f * u }, IM_COL32(220, 207, 185, static_cast<int>(26 * a)), 1.0f);
		}
		const std::string nm = TmUpper(r.name.empty() ? std::string("Unknown") : r.name);
		TText(fg, HF(), 1.45f * u, { listL + 1.4f * u, y + 0.45f * u }, Fade(on ? kTAcc : i == sel ? IM_COL32(255, 190, 140, 255) : kTInk, a), nm, 0.14f);
		const std::string line = "Level " + std::to_string(r.level) + (r.place.empty() ? "" : "  \xc2\xb7  " + r.place);
		TText(fg, Inter(), 0.95f * u, { listL + 1.4f * u, y + 2.35f * u }, Fade(kTTxt, 0.85f * a), line);
		TTextR(fg, Inter(), 0.9f * u, listR - 1.2f * u, y + 0.65f * u, Fade(kTLab, 0.8f * a), r.date);
		TTextR(fg, HF(), 0.85f * u, listR - 1.2f * u, y + 2.4f * u, Fade(r.autosave ? IM_COL32(150, 170, 200, 255) : kTGold, 0.8f * a), r.autosave ? "AUTOSAVE" : "SAVE", 0.25f);
	}
	fg->PopClipRect();
	if (hovered >= 0 && mouse.on) sel = hovered;
	if (n > fits) {   // (where in the list: a thin ember thread down its right)
		const float th = (bottom - top) * static_cast<float>(fits) / static_cast<float>(n), ty = top + (bottom - top - th) * (scroll / std::max(1.0f, maxScroll));
		fg->AddRectFilled({ listR + 0.8f * u, top }, { listR + 0.95f * u, bottom }, IM_COL32(220, 207, 185, static_cast<int>(30 * a)));
		fg->AddRectFilled({ listR + 0.8f * u, ty }, { listR + 0.95f * u, ty + th }, Fade(kTAcc, 0.85f * a));
	}
	// the chosen one (or the one under the mouse / the keys): its picture, the rest of it, and LOAD / DELETE once chosen
	int act = -1;   // 0 load, 1 delete
	if (n > 0) {
		const int show = chosen >= 0 ? chosen : sel;
		const auto& r = saves[show];
		const float px0 = W * 0.5f + 6.0f * u, px1 = W * 0.5f + 44.0f * u;
		int sw = 0, sh = 0;
		ID3D11ShaderResourceView* shot = SaveShot(r.file, sw, sh);
		const float pw = px1 - px0, ph = sw > 0 && sh > 0 ? pw * static_cast<float>(sh) / static_cast<float>(sw) : pw * 0.6f;
		if (shot) fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(shot)), { px0, top }, { px1, top + ph }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
		else TTextC(fg, Inter(), 0.95f * u, (px0 + px1) * 0.5f, top + ph * 0.45f, Fade(kTLab, 0.6f * a), "No picture in this save");
		fg->AddRect({ px0, top }, { px1, top + ph }, Fade(kTGold, 0.55f * a), 0, 0, std::max(1.0f, 0.08f * u));
		float y = top + ph + 1.2f * u;
		TText(fg, HFB(), 1.7f * u, { px0, y }, Fade(kTInk, a), TmUpper(r.name.empty() ? std::string("Unknown") : r.name), 0.16f);
		y += 2.6f * u;
		TText(fg, Inter(), 1.0f * u, { px0, y }, Fade(kTTxt, a), "Level " + std::to_string(r.level) + (r.race.empty() ? "" : "  \xc2\xb7  " + r.race));
		y += 1.7f * u;
		if (!r.place.empty()) TText(fg, Inter(), 1.0f * u, { px0, y }, Fade(kTTxt, a), r.place), y += 1.7f * u;
		TText(fg, Inter(), 0.95f * u, { px0, y }, Fade(kTLab, 0.85f * a), r.date + (r.played.empty() ? "" : "   \xc2\xb7   Played " + r.played));
		y += 2.6f * u;
		if (chosen >= 0 && !asking && !loading) {
			bool h0 = false, h1 = false;
			const float bx = px0 + 4.0f * u;
			if (TmWord(fg, HF(), 1.45f * u, bx, y, "LOAD", btn == 0, cur, click, 0.35f, a, &h0)) act = 0;
			if (TmWord(fg, HF(), 1.45f * u, bx + 12.0f * u, y, "DELETE", btn == 1, cur, click, 0.35f, a, &h1, IM_COL32(230, 120, 100, 255))) act = 1;
			if (h0 && mouse.on) btn = 0;
			if (h1 && mouse.on) btn = 1;
			if (act >= 0) click = false;
		} else if (!asking && !loading) {
			TText(fg, Inter(), 0.9f * u, { px0, y }, Fade(kTLab, 0.7f * a), "Choose it to load or delete it");
		}
	}
	const bool back = TmBack(fg, size, cur, click && !asking, a);
	if (back) click = false;
	// Delete: asked first (Delete / Square / X on the keyboard: straight to the question)
	if (!asking && !loading && n > 0 && (in.dels > 0 || act == 1)) {
		if (act != 1) chosen = sel;
		asking = true, askFocus = 1;
		logger::info("menu: delete '{}'? asked", saves[std::clamp(chosen, 0, n - 1)].file);
		return true;
	}
	if (asking) {
		const auto& r = saves[std::clamp(chosen, 0, std::max(0, n - 1))];
		const int ans = n > 0 ? TmAsk(fg, size, a, "DELETE THIS SAVE?", (r.name.empty() ? std::string("Unknown") : r.name) + "  \xc2\xb7  Level " + std::to_string(r.level) + "  \xc2\xb7  " + r.date, cur, click, in, askFocus) : 0;
		if (ans >= 0) {
			asking = false;
			logger::info("menu: delete '{}' - {}", r.file, ans == 1 ? "yes" : "no");
			if (ans == 1 && DeleteSave(r.file)) RequestSaves(), chosen = -1;
		}
		return true;
	}
	if (back || in.backs > 0) {
		if (chosen >= 0 && !back) { chosen = -1; return true; }   // (Esc / B: first the choice let go, then the page)
		loading = false, chosen = -1;
		return false;
	}
	if (!loading && n > 0) {
		bool load = act == 0;
		if (click && hovered >= 0) {
			if (hovered == chosen) load = true;
			else chosen = hovered, sel = hovered, btn = 0;
		} else if (in.accepts > 0) {
			if (chosen == sel && chosen >= 0) {
				if (btn == 0) load = true;
				else {
					asking = true, askFocus = 1;
					return true;
				}
			} else chosen = sel, btn = 0;
		}
		if (load && chosen >= 0) {
			loading = true;
			g_menuNewAt = g_menuNewPressed = {};
			LoadSave(saves[chosen].file);
		}
	}
	if (loading) TTextC(fg, HF(), 1.3f * u, W * 0.5f, H - 4.6f * u, Fade(kTAcc, a), "LOADING...", 0.3f);
	return true;
}

// ---------------------------------------------------------------- SETTINGS: five tabs on one page
RE::Setting* TPref(const char* a_name)
{
	auto* c = RE::INIPrefSettingCollection::GetSingleton();
	return c ? c->GetSetting(a_name) : nullptr;
}
float TPrefGet(const char* a_name, float a_def)
{
	auto* s = TPref(a_name);
	if (!s) return a_def;
	switch (a_name[0]) {
	case 'b': return s->data.b ? 1.0f : 0.0f;
	case 'i': return static_cast<float>(s->data.i);
	case 'u': return static_cast<float>(s->data.u);
	case 'f': return s->data.f;
	default: return a_def;
	}
}
bool g_tmPrefsDirty = false, g_tmRestartChanged = false;
void TPrefSet(const char* a_name, float a_v)
{
	auto* s = TPref(a_name);
	if (!s) { logger::warn("settings: Skyrim has no setting {}", a_name); return; }
	switch (a_name[0]) {
	case 'b': s->data.b = a_v != 0.0f; break;
	case 'i': s->data.i = static_cast<std::int32_t>(std::lround(a_v)); break;
	case 'u': s->data.u = static_cast<std::uint32_t>(std::lround(a_v)); break;
	case 'f': s->data.f = a_v; break;
	default: return;
	}
	g_tmPrefsDirty = true;
}
struct TOpt
{
	std::string label;
	int kind = 0;   // 0 on / off, 1 a choice of names, 2 a slider
	std::vector<std::string> names;
	float mn = 0, mx = 1, step = 0.05f;
	bool restart = false;
	std::function<float()> get;
	std::function<void(float)> set;
	std::function<std::string(float)> show;
};
std::vector<std::pair<int, int>> ScreenModes()
{
	std::vector<std::pair<int, int>> out;
	DEVMODEW dm{};
	dm.dmSize = sizeof(dm);
	for (DWORD i = 0; ::EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
		const std::pair<int, int> m{ static_cast<int>(dm.dmPelsWidth), static_cast<int>(dm.dmPelsHeight) };
		if (m.first >= 1024 && m.second >= 600 && std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
	}
	std::sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.first * x.second > y.first * y.second; });
	return out;
}
std::vector<std::vector<TOpt>>& SettingsTabs()
{
	static std::vector<std::vector<TOpt>> tabs;
	if (!tabs.empty()) return tabs;
	auto onoff = [](std::string l, const char* pref, bool restart = false) {
		TOpt o;
		o.label = std::move(l), o.kind = 0, o.restart = restart;
		o.get = [pref] { return TPrefGet(pref, 0.0f); };
		o.set = [pref, restart](float v) { TPrefSet(pref, v); if (restart) g_tmRestartChanged = true; };
		return o;
	};
	auto ours = [](std::string l, int kind, const wchar_t* sec, const wchar_t* key, float def, float mn, float mx, float step, bool isInt, std::function<std::string(float)> show = nullptr) {
		TOpt o;
		o.label = std::move(l), o.kind = kind, o.mn = mn, o.mx = mx, o.step = step, o.show = std::move(show);
		o.get = [sec, key, def] { return Settings::Number(sec, key, def); };
		o.set = [sec, key, isInt](float v) { Settings::SetNumber(sec, key, v, isInt); };
		return o;
	};
	auto pct = [](float v) { return std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%"; };
	tabs.resize(6);   // (the sixth, KEYBINDS, is drawn by TmKeybinds)
	// GRAPHICS
	{
		auto& t = tabs[0];
		{
			TOpt o;
			o.label = "Brightness", o.kind = 2, o.mn = Settings::kBrightMin, o.mx = Settings::kBrightMax, o.step = 0.05f;
			o.get = [] { return Settings::Brightness(); };
			o.set = [](float v) { Settings::SetBrightness(v); };
			o.show = [](float v) { char b[16]; std::snprintf(b, sizeof(b), "%.2f", v); return std::string(b); };
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "Shadow quality", o.kind = 1, o.names = { "LOW", "MEDIUM", "HIGH", "ULTRA" }, o.restart = true;
			o.get = [] {
				const float r = TPrefGet("iShadowMapResolution:Display", 2048), d = TPrefGet("fShadowDistance:Display", 8000);
				return r <= 1024 ? 0.0f : r <= 2048 ? 1.0f : d < 9000 ? 2.0f : 3.0f;
			};
			o.set = [](float v) {
				static constexpr float kRes[4] = { 1024, 2048, 4096, 4096 }, kDist[4] = { 4000, 6000, 8000, 10000 };
				const int i = std::clamp(static_cast<int>(v), 0, 3);
				TPrefSet("iShadowMapResolution:Display", kRes[i]), TPrefSet("fShadowDistance:Display", kDist[i]);
				g_tmRestartChanged = true;
			};
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "Object fade distance", o.kind = 2, o.mn = 5, o.mx = 30, o.step = 1, o.restart = true;
			o.get = [] { return TPrefGet("fLODFadeOutMultObjects:LOD", 15); };
			o.set = [](float v) { TPrefSet("fLODFadeOutMultObjects:LOD", v); g_tmRestartChanged = true; };
			o.show = [](float v) { return std::to_string(static_cast<int>(v)); };
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "Grass distance", o.kind = 2, o.mn = 2000, o.mx = 7000, o.step = 500, o.restart = true;
			o.get = [] { return TPrefGet("fGrassStartFadeDistance:Grass", 7000); };
			o.set = [](float v) { TPrefSet("fGrassStartFadeDistance:Grass", v); g_tmRestartChanged = true; };
			o.show = [](float v) { return std::to_string(static_cast<int>(v)); };
			t.push_back(o);
		}
		t.push_back(onoff("Antialiasing (TAA)", "bUseTAA:Display", true));
		t.push_back(onoff("Ambient occlusion", "bSAOEnable:Display", true));
		t.push_back(onoff("Volumetric lighting", "bVolumetricLightingEnable:Display", true));
		t.push_back(onoff("Screen-space reflections", "bScreenSpaceReflectionEnabled:Display", true));
		t.push_back(onoff("Depth of field", "bDoDepthOfField:Imagespace", true));
		t.push_back(ours("The night's fog", 2, L"Effects", L"fFogStrength", 1.0f, 0.0f, 1.5f, 0.1f, false, pct));
		t.push_back(ours("Screen shake", 0, L"Effects", L"bScreenShake", 1.0f, 0, 1, 1, true));
		t.push_back(ours("Softer flashes", 0, L"Effects", L"bReduceFlashes", 0.0f, 0, 1, 1, true));
	}
	// DISPLAY
	{
		auto& t = tabs[1];
		{
			TOpt o;
			o.label = "Resolution", o.kind = 1, o.restart = true;
			static const auto modes = ScreenModes();
			for (const auto& [w, h] : modes) o.names.push_back(std::to_string(w) + " x " + std::to_string(h));
			o.get = [] {
				const int w = static_cast<int>(TPrefGet("iSize W:Display", 1920)), h = static_cast<int>(TPrefGet("iSize H:Display", 1080));
				for (std::size_t i = 0; i < modes.size(); ++i) if (modes[i].first == w && modes[i].second == h) return static_cast<float>(i);
				return 0.0f;
			};
			o.set = [](float v) {
				if (modes.empty()) return;
				const auto& m = modes[std::clamp(static_cast<std::size_t>(v), std::size_t{ 0 }, modes.size() - 1)];
				TPrefSet("iSize W:Display", static_cast<float>(m.first)), TPrefSet("iSize H:Display", static_cast<float>(m.second));
				g_tmRestartChanged = true;
			};
			if (o.names.empty()) o.names = { "AS NOW" };
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "Display mode", o.kind = 1, o.names = { "FULLSCREEN", "BORDERLESS", "WINDOWED" }, o.restart = true;
			o.get = [] { return TPrefGet("bFull Screen:Display", 0) != 0 ? 0.0f : TPrefGet("bBorderless:Display", 1) != 0 ? 1.0f : 2.0f; };
			o.set = [](float v) {
				const int i = std::clamp(static_cast<int>(v), 0, 2);
				TPrefSet("bFull Screen:Display", i == 0 ? 1.0f : 0.0f), TPrefSet("bBorderless:Display", i == 1 ? 1.0f : 0.0f);
				g_tmRestartChanged = true;
			};
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "Frame rate cap", o.kind = 1, o.names = { "UNLIMITED", "30", "60", "75", "90", "120", "144", "165", "240" };
			static constexpr float kCaps[9] = { 0, 30, 60, 75, 90, 120, 144, 165, 240 };
			o.get = [] {
				const float c = Settings::FpsCap();
				for (int i = 0; i < 9; ++i) if (std::abs(kCaps[i] - c) < 0.5f) return static_cast<float>(i);
				return 0.0f;
			};
			o.set = [](float v) { Settings::SetNumber(L"Display", L"fFpsCap", kCaps[std::clamp(static_cast<int>(v), 0, 8)], false); };
			t.push_back(o);
		}
		t.push_back(ours("VSync", 0, L"Display", L"bVSync", 0.0f, 0, 1, 1, true));
		{
			TOpt o;
			o.label = "Field of view - first person", o.kind = 2, o.mn = Settings::kFovMin, o.mx = Settings::kFovMax, o.step = 5;
			o.get = [] { return Settings::FovFirst(); };
			o.set = [](float v) { Settings::SetFov(true, v); };
			o.show = [](float v) { return std::to_string(static_cast<int>(v)); };
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "Field of view - third person", o.kind = 2, o.mn = Settings::kFovMin, o.mx = Settings::kFovMax, o.step = 5;
			o.get = [] { return Settings::FovThird(); };
			o.set = [](float v) { Settings::SetFov(false, v); };
			o.show = [](float v) { return std::to_string(static_cast<int>(v)); };
			t.push_back(o);
		}
	}
	// GAMEPLAY
	{
		auto& t = tabs[2];
		{
			TOpt o;
			o.label = "Difficulty", o.kind = 1, o.names = { "NOVICE", "APPRENTICE", "ADEPT", "EXPERT", "MASTER", "LEGENDARY" };
			o.get = [] { return TPrefGet("iDifficulty:GamePlay", 2); };
			o.set = [](float v) { TPrefSet("iDifficulty:GamePlay", std::clamp(v, 0.0f, 5.0f)); };
			t.push_back(o);
		}
		t.push_back(ours("The dead follow the difficulty", 0, L"Difficulty", L"bFollowSkyrim", 1.0f, 0, 1, 1, true));
		t.push_back(ours("Melee swing speed", 2, L"General", L"iMeleeSpeedStep", 3.0f, 0, 10, 1, true, [](float v) { char b[16]; std::snprintf(b, sizeof(b), "%.1fx", 1.0f + 0.1f * v); return std::string(b); }));
		t.push_back(ours("Bow draw speed", 2, L"General", L"iDrawSpeedStep", 0.0f, 0, 10, 1, true, [](float v) { char b[16]; std::snprintf(b, sizeof(b), "%.1fx", 1.0f + 0.1f * v); return std::string(b); }));
		t.push_back(ours("Bodies stay", 2, L"General", L"fCorpseSeconds", 10.0f, 3, 60, 1, false, [](float v) { return std::to_string(static_cast<int>(v)) + " s"; }));
		t.push_back(onoff("Compass", "bShowCompass:Interface"));
		t.push_back(onoff("Crosshair", "bCrosshairEnabled:MAIN"));
		t.push_back(onoff("Quest markers", "bShowFloatingQuestMarkers:GamePlay"));
		t.push_back(onoff("Dialogue subtitles", "bDialogueSubtitles:Interface"));
		t.push_back(onoff("General subtitles", "bGeneralSubtitles:Interface"));
		t.push_back(ours("Altar and character subtitles", 0, L"Subtitles", L"bSubtitles", 1.0f, 0, 1, 1, true));
		t.push_back(onoff("Save on rest", "bSaveOnRest:MAIN"));
		t.push_back(onoff("Save on wait", "bSaveOnWait:MAIN"));
		t.push_back(onoff("Save on travel", "bSaveOnTravel:MAIN"));
	}
	// CONTROLS
	{
		auto& t = tabs[3];
		{
			TOpt o;
			o.label = "Mouse sensitivity", o.kind = 2, o.mn = 0.005f, o.mx = 0.08f, o.step = 0.0025f;
			o.get = [] { return TPrefGet("fMouseHeadingSensitivity:Controls", 0.028f); };
			o.set = [](float v) { TPrefSet("fMouseHeadingSensitivity:Controls", v); };
			o.show = [](float v) { char b[16]; std::snprintf(b, sizeof(b), "%.1f", v * 100.0f); return std::string(b); };
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "Controller look sensitivity", o.kind = 2, o.mn = 0.5f, o.mx = 3.0f, o.step = 0.1f;
			o.get = [] { return TPrefGet("fGamepadHeadingSensitivity:Controls", 1.5f); };
			o.set = [](float v) { TPrefSet("fGamepadHeadingSensitivity:Controls", v); };
			o.show = [](float v) { char b[16]; std::snprintf(b, sizeof(b), "%.1f", v); return std::string(b); };
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "Aiming sensitivity", o.kind = 2, o.mn = Settings::kAdsSensMin, o.mx = Settings::kAdsSensMax, o.step = 0.05f;
			o.get = [] { return Settings::AdsSensitivity(); };
			o.set = [](float v) { Settings::SetAdsSensitivity(v); };
			o.show = [](float v) { char b[16]; std::snprintf(b, sizeof(b), "%.2fx", v); return std::string(b); };
			t.push_back(o);
		}
		t.push_back(onoff("Invert look", "bInvertYValues:Controls"));
		t.push_back(onoff("Controller vibration", "bGamePadRumble:Controls"));
		t.push_back(ours("PlayStation button icons", 0, L"Controls", L"bPlayStationIcons", 0.0f, 0, 1, 1, true));
	}
	// AUDIO: the master, then every sound category Skyrim's own audio menu lists (by its name), then the altar's voice
	{
		auto& t = tabs[4];
		auto* dh = RE::TESDataHandler::GetSingleton();
		RE::BGSSoundCategory* master = nullptr;
		struct Cat { int slot; RE::BGSSoundCategory* cat; };
		std::vector<Cat> cats;
		for (int i = 0; i < 8; ++i) {
			const std::string idn = "uID" + std::to_string(i) + ":AudioMenu";
			const auto id = static_cast<RE::FormID>(TPrefGet(idn.c_str(), 0));
			auto* c = id ? RE::TESForm::LookupByID<RE::BGSSoundCategory>(id) : nullptr;
			if (!c) continue;
			cats.push_back({ i, c });
			if (!master) for (auto* p = c->parentCategory; p; p = p->parentCategory) master = p;
		}
		(void)dh;
		{
			TOpt o;
			o.label = "Master volume", o.kind = 2, o.mn = 0, o.mx = 1, o.step = 0.05f, o.show = pct;
			o.get = [] { return TPrefGet("fAudioMasterVolume:AudioMenu", 1.0f); };
			o.set = [master](float v) { TPrefSet("fAudioMasterVolume:AudioMenu", v); if (master) master->SetCategoryVolume(v); };
			t.push_back(o);
		}
		for (const auto& ct : cats) {
			const int slot = ct.slot;
			RE::BGSSoundCategory* cat = ct.cat;
			TOpt o;
			const char* nm = cat->GetName();
			o.label = nm && *nm ? nm : "Sound " + std::to_string(slot + 1);
			o.kind = 2, o.mn = 0, o.mx = 1, o.step = 0.05f, o.show = pct;
			const std::string val = "fVal" + std::to_string(slot) + ":AudioMenu";
			o.get = [val] { return TPrefGet(val.c_str(), 1.0f); };
			o.set = [val, cat](float v) { TPrefSet(val.c_str(), v); cat->SetCategoryVolume(v); };
			t.push_back(o);
		}
		{
			TOpt o;
			o.label = "The Echo Altar's voice", o.kind = 2, o.mn = 0, o.mx = 100, o.step = 5;
			o.get = [] { return Settings::Number(L"Voices", L"fAltarVoiceVolume", 100.0f); };
			o.set = [](float v) { Settings::SetNumber(L"Voices", L"fAltarVoiceVolume", v, false); };
			o.show = [](float v) { return std::to_string(static_cast<int>(v)) + "%"; };
			t.push_back(o);
		}
		logger::info("settings: the main menu's page - {} sound categories, {} screen sizes", cats.size(), ScreenModes().size());
	}
	return tabs;
}
#include "HudKeybinds.inl"

void TmStep(TOpt& o, int a_dir)
{
	const float v = o.get();
	if (o.kind == 0) o.set(v != 0.0f ? 0.0f : 1.0f);
	else if (o.kind == 1) {
		const int n = std::max<int>(1, static_cast<int>(o.names.size()));
		o.set(static_cast<float>((static_cast<int>(v) + a_dir + n) % n));
	} else o.set(std::clamp(std::round((v + o.step * static_cast<float>(a_dir)) / o.step) * o.step, o.mn, o.mx));
}
void TmSettingsLeave()
{
	if (g_tmPrefsDirty) {
		if (auto* c = RE::INIPrefSettingCollection::GetSingleton()) c->WriteAllSettings();
		logger::info("settings: SkyrimPrefs.ini written from our Settings");
		g_tmPrefsDirty = false;
	}
	g_bindWait = -1;
}
bool TmSettings(ImDrawList* fg, ImVec2 size, float now, float a, ImVec2 cur, bool click, const TmIn& in, float dt = 1.0f / 60.0f)
{
	static int tab = 0, sel = 0, dragRow = -1, dragTab = -1;
	static TmMouse mouse;
	if (!g_mouseHeld) dragRow = -1;
	const float W = size.x, H = size.y, u = TU(size);
	auto& tabs = SettingsTabs();
	TmHeading(fg, size, 6.0f * u, "SETTINGS", a);
	static const char* kTabs[6] = { "GRAPHICS", "DISPLAY", "GAMEPLAY", "CONTROLS", "KEYBINDS", "AUDIO" };
	static constexpr int kTabOf[6] = { 0, 1, 2, 3, 5, 4 };   // (the tab shown -> its options; KEYBINDS is drawn on its own)
	if (in.tabs && g_bindWait.load() < 0) tab = (tab + in.tabs + 6 * 8) % 6, sel = 0;
	// the tabs
	{
		const float fs = 1.25f * u, gap = 3.2f * u;
		float total = 0;
		for (const char* t : kTabs) total += TMeasure(HF(), fs, t, 0.3f).x;
		total += gap * 5;
		float x = W * 0.5f - total * 0.5f;
		const float y = 10.6f * u;
		for (int i = 0; i < 6; ++i) {
			const float tw = TMeasure(HF(), fs, kTabs[i], 0.3f).x;
			bool hv = false;
			if (TmWord(fg, HF(), fs, x + tw * 0.5f, y, kTabs[i], i == tab, cur, click, 0.3f, a, &hv) && i != tab) tab = i, sel = 0, click = false;
			if (i == tab) fg->AddRectFilled({ x, y + 1.75f * u }, { x + tw, y + 1.75f * u + std::max(1.5f, 0.1f * u) }, Fade(kTAcc, a));
			x += tw + gap;
		}
	}
	if (kTabOf[tab] == 5) {   // KEYBINDS
		int h = -1;
		const bool stay = TmKeybinds(fg, size, now, dt, a, cur, click, in, h);
		if (h == 2) tab = (tab + 1) % 6, sel = 0;
		if (!stay) { TmSettingsLeave(); return false; }
		return true;
	}
	auto& opts = tabs[kTabOf[tab]];
	const int n = static_cast<int>(opts.size());
	mouse.Note(cur, in);
	const bool mouseOn = mouse.on || click;
	sel = n > 0 ? std::clamp(sel + in.moves, 0, n - 1) : 0;
	const float colL = W * 0.5f - 30.0f * u, colR = W * 0.5f + 30.0f * u, valC = W * 0.5f + 19.0f * u;
	const float rowH = 2.75f * u, top = 15.0f * u;
	TShade(fg, { colL, top - 0.8f * u }, { colR, top + rowH * static_cast<float>(n) + 0.4f * u }, 3.0f * u, 0.85f * a);
	for (int i = 0; i < n; ++i) {
		auto& o = opts[i];
		const float y = top + rowH * static_cast<float>(i);
		const bool  over = mouseOn && cur.x >= colL && cur.x <= colR && cur.y >= y - 0.4f * u && cur.y < y + rowH - 0.4f * u;
		if (over) sel = i;
		const bool lit = i == sel;
		if (lit) {
			const float pl = 0.8f + 0.2f * std::sin(now * 3.0f);
			fg->AddRectFilledMultiColor({ colL, y - 0.4f * u }, { colR, y + rowH - 0.5f * u }, IM_COL32(255, 122, 60, static_cast<int>(40 * pl * a)), IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, static_cast<int>(40 * pl * a)));
			fg->AddRectFilled({ colL, y - 0.1f * u }, { colL + 0.22f * u, y + rowH - 0.8f * u }, Fade(kTAcc, a));
		}
		TText(fg, HF(), 1.2f * u, { colL + 1.4f * u, y + 0.1f * u }, Fade(lit ? kTAcc : kTInk, a), TmUpper(o.label), 0.16f);
		if (o.restart) {
			const float lw = TMeasure(HF(), 1.2f * u, TmUpper(o.label), 0.16f).x;
			TText(fg, HF(), 0.75f * u, { colL + 2.4f * u + lw, y + 0.42f * u }, Fade(kTGold, 0.7f * a), "RESTART", 0.25f);
		}
		// the value: < NAME > for a choice, ON / OFF, or a slider with its number
		const float v = o.get();
		if (o.kind == 2) {
			const float bx0 = valC - 7.5f * u, bx1 = valC + 4.5f * u, by = y + 0.85f * u, t = (v - o.mn) / std::max(1e-6f, o.mx - o.mn);
			fg->AddRectFilled({ bx0, by - 0.12f * u }, { bx1, by + 0.12f * u }, IM_COL32(255, 255, 255, static_cast<int>(40 * a)));
			fg->AddRectFilled({ bx0, by - 0.12f * u }, { bx0 + (bx1 - bx0) * std::clamp(t, 0.0f, 1.0f), by + 0.12f * u }, Fade(kTEmb, a));
			fg->AddCircleFilled({ bx0 + (bx1 - bx0) * std::clamp(t, 0.0f, 1.0f), by }, 0.42f * u, Fade(lit ? kTAcc : kTInk, a), 16);
			const std::string sv = o.show ? o.show(v) : std::to_string(static_cast<int>(v));
			TText(fg, HFB(), 1.1f * u, { bx1 + 1.2f * u, y + 0.15f * u }, Fade(lit ? kTAcc : kTTxt, a), sv, 0.05f);
			// (Noah, Nov 28: "make using the sliders with the cursor smooth by holding left click to drag the sliders") pressed on its bar,
			// the slider follows the cursor for as long as the button is held
			if (click && cur.x >= bx0 - 0.6f * u && cur.x <= bx1 + 0.6f * u && cur.y >= y - 0.3f * u && cur.y <= y + rowH - 0.6f * u) dragRow = i, dragTab = tab;
			if (dragRow == i && dragTab == tab && g_mouseHeld) {
				sel = i;
				const float nv = o.mn + (o.mx - o.mn) * std::clamp((cur.x - bx0) / (bx1 - bx0), 0.0f, 1.0f);
				const float sv = std::clamp(std::round(nv / o.step) * o.step, o.mn, o.mx);
				if (std::abs(sv - v) > o.step * 0.25f) o.set(sv);
			}
		} else {
			const std::string sv = o.kind == 0 ? std::string(v != 0.0f ? "ON" : "OFF") : (o.names.empty() ? std::string() : o.names[std::clamp(static_cast<std::size_t>(std::max(0.0f, v)), std::size_t{ 0 }, o.names.size() - 1)]);
			TTextC(fg, HFB(), 1.15f * u, valC, y + 0.12f * u, Fade(lit ? kTAcc : (o.kind == 0 && v == 0.0f ? kTLab : kTTxt), a), sv, 0.18f);
			const float sw = TMeasure(HFB(), 1.15f * u, sv, 0.18f).x;
			bool hl = false, hr = false;
			if (TmWord(fg, HFB(), 1.2f * u, valC - sw * 0.5f - 2.2f * u, y + 0.05f * u, "<", false, cur, click, 0.0f, lit ? a : 0.5f * a, &hl)) TmStep(o, -1);
			else if (TmWord(fg, HFB(), 1.2f * u, valC + sw * 0.5f + 2.2f * u, y + 0.05f * u, ">", false, cur, click, 0.0f, lit ? a : 0.5f * a, &hr)) TmStep(o, 1);
			else if (click && over && std::abs(cur.x - valC) < sw * 0.5f + 1.0f * u) TmStep(o, 1);
		}
	}
	if (n > 0 && in.sides) TmStep(opts[sel], in.sides > 0 ? 1 : -1);
	if (n > 0 && in.accepts > 0 && opts[sel].kind != 2) TmStep(opts[sel], 1);
	if (g_tmRestartChanged) TTextC(fg, Inter(), 0.95f * u, W * 0.5f, H - 7.0f * u, Fade(kTGold, a), "Settings marked RESTART take effect the next time you start Skyrim.");
	const bool pad = g_padLast;
	const int h = TmHints(fg, size, { { pad ? Hud::PadWord("LB") + " " + Hud::PadWord("RB") : std::string("Q  E"), "TAB" }, { pad ? std::string("D-PAD") : std::string("ARROWS"), "CHANGE" }, { pad ? Hud::PadWord("B") : std::string("ESC"), "BACK" } }, cur, click, a);
	if (h == 0) tab = (tab + 1) % 6, sel = 0;
	if (h == 2 || in.backs > 0 || TmBack(fg, size, cur, click, a)) {
		TmSettingsLeave();
		return false;
	}
	return true;
}

// ---------------------------------------------------------------- the co-op party, in the menu's style (the Esc journal too)
void TallyPartyPanel(ImDrawList* dl, ImVec2 size, bool click, bool a_dim = true)
{
	// (Noah, Nov 28: "I want the stats part to look better and cleaner to match the UI in the Party Menu") the party as its own page,
	// laid out as the workshop's Co-op party preview: no box, the heading and line, your party's words in the middle, then the night's
	// table in even columns under a thin rule, last night and your records - the menu's words and title gone from behind it
	const ImVec2 cur = Cursor(size);
	const bool   net = Net::Available();
	click = click && (!net || !Net::OverlayBusy());
	static std::vector<Net::FriendGame> friends;
	static float refresh = 0;
	if (net && (refresh -= ImGui::GetIO().DeltaTime) <= 0) {
		refresh = 2.0f;
		friends = Net::InLobby() ? std::vector<Net::FriendGame>{} : Net::FriendsInParties();
	}
	const auto  me = Stats::Tonight();
	const auto  last = Stats::LastNight();
	const auto  rec = Stats::Lifetime();
	const auto  party = Stats::Party();
	const float W = size.x, H = size.y, u = TU(size);
	if (a_dim) dl->AddRectFilled({ 0, 0 }, size, IM_COL32(4, 2, 1, 200));
	TmHeading(dl, size, 6.0f * u, "CO-OP PARTY", 1.0f);
	const std::string st = !net ? "Solo - Steam isn't running" : Net::Status() + (Net::InLobby() ? "   \xc2\xb7   " + std::to_string(Net::Members() + 1) + " in the party" : "");
	TTextC(dl, Inter(), 1.0f * u, W * 0.5f, 10.6f * u, Fade(kTTxt, 0.9f), st);
	// the party's words: up / down (or the mouse) to choose, A / Enter or a click to press
	struct Act { std::string text; ImU32 col; int what; int arg; };
	std::vector<Act> acts;
	if (net) {
		if (!Net::InLobby()) {
			acts.push_back({ "HOST A PARTY", kTTxt, 0, 0 });
			for (std::size_t i = 0; i < friends.size() && i < 3; ++i) acts.push_back({ "JOIN " + TmUpper(friends[i].name), IM_COL32(150, 200, 255, 255), 1, static_cast<int>(i) });
		} else if (Net::IsHost()) {
			acts.push_back({ "INVITE FRIENDS", kTTxt, 2, 0 });
			acts.push_back({ "CLOSE THE PARTY", IM_COL32(230, 110, 90, 255), 3, 0 });
		} else {
			acts.push_back({ "LEAVE " + TmUpper(Net::HostName()) + "'S PARTY", IM_COL32(230, 110, 90, 255), 3, 0 });
		}
	}
	const int focus = acts.empty() ? 0 : std::clamp(g_partyFocus.load(), 0, static_cast<int>(acts.size()) - 1);
	g_partyFocus = focus;
	const bool press = g_partyPress.exchange(0) > 0;
	float y = H * 0.215f;
	for (int i = 0; i < static_cast<int>(acts.size()); ++i) {
		bool hv = false;
		const bool hit = TmWord(dl, HF(), 1.4f * u, W * 0.5f, y, acts[i].text, i == focus, cur, click, 0.3f, 1.0f, &hv, acts[i].col);
		if (hv) g_partyFocus = i;
		if (hit || (press && i == focus)) {
			if (acts[i].what == 0) Net::HostLobby();
			else if (acts[i].what == 1) Net::JoinLobby(friends[acts[i].arg].lobby);
			else if (acts[i].what == 2) Net::InviteFriends();
			else Net::LeaveLobby();
		}
		y += 2.4f * u;
	}
	// tonight: the table, in the preview's columns (16.5 : 6 : 6 : 6 : 10 : 6 of 51u)
	const float tL = W * 0.5f - 25.5f * u, tW = 51.0f * u;
	static constexpr float kFr[6] = { 16.5f, 6.0f, 6.0f, 6.0f, 10.0f, 6.0f };
	float cols[6];
	{
		float x = tL;
		for (int c = 0; c < 6; ++c) cols[c] = x, x += tW * kFr[c] / 50.5f;
	}
	y = std::max(y + 1.6f * u, H * 0.33f);
	const char* heads[6] = { me.night > 0 ? "TONIGHT" : "PLAYER", "KILLS", "HEADS", "STREAK", "ESSENCE", "REVIVES" };
	for (int c = 0; c < 6; ++c) TText(dl, HF(), 0.85f * u, { cols[c], y }, Fade(kTAcc, 0.95f), heads[c], 0.25f);
	y += 1.35f * u;
	dl->AddLine({ tL, y }, { tL + tW, y }, IM_COL32(220, 207, 185, 42), 1.0f);
	y += 0.55f * u;
	auto line = [&](const std::string& who, const Stats::Line& v, ImU32 col) {
		const std::string vals[6] = { who, Num(v.kills), Num(v.heads), Num(v.best), Num(v.septims), Num(v.revives) };
		for (int c = 0; c < 6; ++c) TText(dl, Inter(), 1.0f * u, { cols[c], y }, col, vals[c]);
		y += 1.75f * u;
	};
	auto* pl = RE::PlayerCharacter::GetSingleton();
	line(pl && pl->GetName() && *pl->GetName() ? std::string(pl->GetName()) + " (you)" : "You", me, kTInk);
	for (const auto& f : party) line(f.name, f.line, kTTxt);
	// last night, then your records
	y = std::max(y + 1.4f * u, H * 0.48f);
	TText(dl, HF(), 1.0f * u, { tL, y }, Fade(kTAcc, 0.95f), last.night > 0 ? fmt::format("LAST NIGHT  \xc2\xb7  NIGHT {}", last.night) : std::string("LAST NIGHT"), 0.25f);
	y += 1.75f * u;
	if (last.night > 0) {
		TText(dl, Inter(), 0.95f * u, { tL, y }, kTTxt, fmt::format("{} kills   \xc2\xb7   {} headshots   \xc2\xb7   best streak {}   \xc2\xb7   {} Cursed Essence", Num(last.kills), Num(last.heads), Num(last.best), Num(last.septims)));
		y += 1.55f * u;
		std::string mvp = pl && pl->GetName() ? pl->GetName() : "You";
		int best = last.kills * 2 + last.heads + last.style / 50;
		for (const auto& f : party) {
			const int sc = f.line.kills * 2 + f.line.heads + f.line.style / 50;
			if (sc > best) best = sc, mvp = f.name;
		}
		TText(dl, Inter(), 0.95f * u, { tL, y }, kTTxt, fmt::format("style {}   \xc2\xb7   waves {}   \xc2\xb7   mini-bosses {}{}", Num(last.style), last.waves, last.minis, party.empty() ? std::string() : "   \xc2\xb7   MVP " + mvp));
		y += 1.55f * u;
	} else {
		TText(dl, Inter(), 0.95f * u, { tL, y }, Fade(kTLab, 0.8f), "Survive a night and its score shows here.");
		y += 1.55f * u;
	}
	y += 1.2f * u;
	TText(dl, HF(), 1.0f * u, { tL, y }, Fade(kTAcc, 0.95f), "RECORDS", 0.25f);
	y += 1.75f * u;
	TText(dl, Inter(), 0.95f * u, { tL, y }, kTTxt, fmt::format("best night {}   \xc2\xb7   nights survived {}   \xc2\xb7   kills {}   \xc2\xb7   headshots {}", rec.bestNight, rec.nights, Num(rec.kills), Num(rec.heads)));
	y += 1.55f * u;
	TText(dl, Inter(), 0.95f * u, { tL, y }, kTTxt, fmt::format("best streak {}   \xc2\xb7   mini-bosses {}   \xc2\xb7   wonders won {}   \xc2\xb7   path secrets {}", rec.bestStreak, rec.minis, rec.wonders, rec.quests));
	if (TmBack(dl, size, cur, click, 1.0f)) g_partyOpen = false;
}

// ---------------------------------------------------------------- the menu itself: the words, and the pages over them
void TallyMenu(ImDrawList* fg, ImVec2 size, float now, float dt, float openT, ImVec2 cur, bool click)
{
	auto* ui = RE::UI::GetSingleton();
	auto  menu = ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
	RE::GFxMovieView* mv = menu ? menu->uiMovie.get() : nullptr;
	static int page = 0, sel = -1, askFocus = 0;
	static float pageA = 0, askA = 0;
	static auto lastList = std::chrono::steady_clock::time_point{};
	// a message box of Skyrim's own over the menu (a missing-content warning): it keeps the keys, and shows through
	const bool otherMenu = ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME);
	// Skyrim's (Edge UI's) menu: hidden - the mouse can't reach its buttons under our words
	if (mv) {
		RE::GFxValue m;
		if (mv->GetVariable(&m, "_root.MenuHolder.Menu_mc") && m.IsDisplayObject()) {
			RE::GFxValue::DisplayInfo di;
			m.GetDisplayInfo(&di);
			if (di.GetVisible()) {
				di.SetVisible(false);
				m.SetDisplayInfo(di);
				static bool said = false;
				if (!said) said = true, logger::info("menu: Skyrim's own main menu hidden - the Ember Tally menu is the whole of it");
			}
		}
	}
	const bool question = MenuQuestionUp();
	RE::GFxMovieView* lmv = nullptr;
	auto entries = ReadMainList(&lmv);
	const auto tnow = std::chrono::steady_clock::now();
	if (!entries.empty()) lastList = tnow;
	const bool listOk = !entries.empty() || (tnow - lastList) < std::chrono::seconds(3);
	g_menuOwn = !otherMenu && (listOk || question || page != 0);
	g_tmSettings = page == 2 && !g_partyOpen;
	TmIn in;
	{
		const int mv0 = g_padMove.exchange(0), ac = g_padAccept.exchange(0), bk = g_padBack.exchange(0), sd = g_padSide.exchange(0), wh = g_wheel.exchange(0), tb = g_padTab.exchange(0), de = g_padDel.exchange(0);
		if (!g_partyOpen && !otherMenu) in = { mv0, ac, bk, sd, wh, tb, de };
		if (!otherMenu) {
			TmIn st;
			TmStick(st, dt);
			if (g_partyOpen) g_partyFocus = g_partyFocus + st.moves;
			else in.moves += st.moves, in.sides += st.sides;
		}
	}
	pageA = std::clamp(pageA + (page != 0 || g_partyOpen ? 1.0f : -1.0f) * dt * 5.0f, 0.0f, 1.0f);   // (the party too is a page now: the words and title give way to it)
	g_tmPageA = pageA;
	if (otherMenu) {   // (its box shows through, as before)
		if (g_frameSrv && g_keyPs) {
			fg->AddCallback(KeyOn, nullptr);
			fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_frameSrv)), { 0, 0 }, size, { 0, 0 }, { 1, 1 });
			fg->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		}
		return;
	}
	if (pageA > 0.0f) fg->AddRectFilled({ 0, 0 }, size, IM_COL32(4, 2, 1, static_cast<int>(150 * pageA)));
	// the pages
	if (page == 1) {
		if (!TmLoad(fg, size, now, dt, pageA, cur, click && !g_partyOpen, in)) page = 0;
		in = {}, click = false;
	} else if (page == 2) {
		if (!TmSettings(fg, size, now, pageA, cur, click && !g_partyOpen, in, dt)) page = 0;
		in = {}, click = false;
	}
	// the words: Skyrim's own (CO-OP PARTY among them) and our SETTINGS before QUIT
	struct Row { std::string text; int index; bool disabled; int kind; };   // kind: 0 Skyrim's, 1 LOAD, 2 CO-OP PARTY, 3 SETTINGS
	std::vector<Row> rows;
	for (const auto& e : entries) {
		const std::string up = TmUpper(e.text);
		if (up == "QUIT") rows.push_back({ "SETTINGS", -1, false, 3 });
		rows.push_back({ up, e.index, e.disabled, up == "LOAD" ? 1 : up == "CO-OP PARTY" ? 2 : 0 });
	}
	if (!rows.empty() && std::none_of(rows.begin(), rows.end(), [](const Row& r) { return r.kind == 3; })) rows.push_back({ "SETTINGS", -1, false, 3 });
	const int n = static_cast<int>(rows.size());
	if (n > 0 && sel < 0) {
		sel = 0;
		for (std::size_t i = 0; i < entries.size(); ++i) if (entries[i].sel) for (int r = 0; r < n; ++r) if (rows[r].index == entries[i].index) sel = r;
	}
	if (n > 0) sel = std::clamp(sel, 0, n - 1);
	const float wordsA = 1.0f - pageA;
	if (n > 0 && wordsA > 0.01f) {
		const float W = size.x, H = size.y, u = TU(size);
		const float fs = 1.3f * u, track = 0.42f, rowH = 2.15f * u;
		const float top = H * 0.30f + kTallyTitleGap * u + 1.5f * u;
		static std::vector<float> glow;
		glow.resize(rows.size(), 0.0f);
		const bool live = page == 0 && !question && !g_partyOpen;
		static TmMouse mouse;
		if (live) mouse.Note(cur, in);
		// keys: up / down, skipping what can't be chosen
		if (live && in.moves) {
			for (int m = 0; m < std::abs(in.moves); ++m) {
				int nf = sel;
				do nf = (nf + (in.moves > 0 ? 1 : -1) + n) % n;
				while (rows[nf].disabled && nf != sel);
				sel = nf;
			}
		}
		int hovered = -1;
		for (int i = 0; i < n; ++i) {
			const auto& r = rows[i];
			const float y = top + rowH * static_cast<float>(i);
			const float tw = TMeasure(HF(), fs, r.text, track).x;
			const float hw = std::max(tw * 0.5f + 1.4f * u, 7.0f * u);
			if (live && !r.disabled && cur.x >= W * 0.5f - hw && cur.x <= W * 0.5f + hw && cur.y >= y - 0.3f * u && cur.y < y + rowH - 0.3f * u) hovered = i;
			glow[i] += ((i == sel && live ? 1.0f : 0.0f) - glow[i]) * std::min(1.0f, dt * 10.0f);
			const float in0 = std::clamp((openT - 0.6f - 0.08f * static_cast<float>(i)) / 0.45f, 0.0f, 1.0f);
			const float ease = 1.0f - (1.0f - in0) * (1.0f - in0);
			const ImU32 base = r.disabled ? IM_COL32(150, 140, 125, 150) : kTTxt;
			auto mix = [&](int sh) { const float x0 = static_cast<float>((base >> sh) & 0xFF), x1 = static_cast<float>((kTAcc >> sh) & 0xFF); return static_cast<ImU32>(x0 + (x1 - x0) * glow[i]) << sh; };
			const ImU32 col = Fade(mix(IM_COL32_R_SHIFT) | mix(IM_COL32_G_SHIFT) | mix(IM_COL32_B_SHIFT) | mix(IM_COL32_A_SHIFT), in0 * wordsA * (question ? 0.35f : 1.0f));
			const float br = 0.8f + 0.2f * std::sin(now * 3.0f);
			TTextC(fg, HF(), fs, W * 0.5f, y + (1.0f - ease) * 0.6f * u, col, r.text, track, glow[i] > 0.02f ? IM_COL32(232, 90, 40, static_cast<int>(170 * glow[i] * br * in0 * wordsA)) : 0, 0.07f * fs * glow[i]);
		}
		// (Noah, Nov 28: "The menu UI isnt working when I try to navigate") the word under a still mouse no longer takes the choice
		// back from the keys every frame: it is the choice when the mouse moves onto it, or is clicked
		if (hovered >= 0 && (mouse.on || click)) sel = hovered;
		// Skyrim's own choice follows ours (the party's own press looks at it)
		if (lmv && rows[sel].index >= 0) {
			RE::GFxValue list, cv;
			if (lmv->GetVariable(&list, "_root.MenuHolder.Menu_mc.MainList") && list.IsDisplayObject() && list.GetMember("selectedIndex", &cv) && (!cv.IsNumber() || static_cast<int>(cv.GetNumber()) != rows[sel].index)) {
				list.SetMember("selectedIndex", RE::GFxValue(static_cast<double>(rows[sel].index)));
			}
		}
		if (live && ((click && hovered >= 0) || in.accepts > 0) && !rows[sel].disabled && !Net::OverlayBusy()) {
			const auto& r = rows[sel];
			logger::info("menu: '{}' pressed (ours)", r.text);
			if (r.kind == 1) page = 1, RequestSaves(), g_menuNewAt = g_menuNewPressed = {};
			else if (r.kind == 3) page = 2;
			else if (r.kind == 2) g_partyOpen = true, g_partyFocus = 0;
			else PressMainList(lmv, r.index, true);
			click = false;
		}
		// the keys at the foot
		if (live) {
			const bool pad = g_padLast;
			const int h = TmHints(fg, size, { { pad ? std::string("D-PAD") : std::string("ARROWS"), "CHOOSE" }, { pad ? Hud::PadWord("A") : std::string("ENTER"), "SELECT" } }, cur, click, wordsA * std::clamp(openT - 1.0f, 0.0f, 1.0f));
			if (h == 1 && !rows[sel].disabled) {
				const auto& r = rows[sel];
				if (r.kind == 1) page = 1, RequestSaves();
				else if (r.kind == 3) page = 2;
				else if (r.kind == 2) g_partyOpen = true, g_partyFocus = 0;
				else PressMainList(lmv, r.index, true);
			}
		}
	}
	// Skyrim's question, in our words
	askA = std::clamp(askA + (question ? 1.0f : -1.0f) * dt * 6.0f, 0.0f, 1.0f);
	static std::string qtext;
	static auto asked = std::chrono::steady_clock::time_point{};
	if (question) {
		if (asked == std::chrono::steady_clock::time_point{}) {
			asked = tnow, askFocus = 0;
			qtext = MenuQuestionText(mv);
			logger::info("menu: Skyrim asks '{}' - in our words", qtext);
		}
		const std::string shown = qtext.empty() ? std::string("ARE YOU SURE?") : TmUpper(qtext);
		const int ans = TmAsk(fg, size, askA, shown, "", cur, click && !g_partyOpen, in, askFocus);
		if (ans >= 0 && tnow - asked > std::chrono::milliseconds(250)) MenuAnswer(mv, ans == 1), asked = tnow + std::chrono::hours(1);
	} else {
		asked = {};   // (the next question asks afresh)
	}
	// the party, over everything (the cursor is drawn after it)
	if (g_partyOpen && Net::Available()) TallyPartyPanel(fg, size, click, false);
}
