// (Noah, Nov 28: "I also want to add a keybinds section for controller and keyboard and mouse in the menu settings and in game
// settings" - his picks: the Curse's own actions and Skyrim's controls, every one rebindable; in the main menu's Settings and on
// the same page in game, the Mod Configuration menu kept in step) the KEYBINDS tab: two columns, keyboard & mouse and controller.
// Skyrim's are changed in its own control map (the gameplay context) and kept in our file ([SkyrimKeys]), put back each launch;
// ours are the [Keys] / [PadKeys] the Mod Configuration menu also shows.
// (included in Hud.cpp's unnamed namespace, inside HudTallyMenu.inl, before TmSettings)

std::atomic<int> g_bindWait{ -1 };   // a key being waited for: 0 keyboard & mouse, 1 controller
std::atomic<int> g_bindGot{ -1 };    // ... what came: its code (a mouse button 0x100 + n), -2 cancelled

struct KbSky
{
	const char* label;
	const char* event;
	bool        stick;   // (moving: the left stick on a controller, not a button)
};
constexpr KbSky kKbSky[] = {
	{ "Forward", "Forward", true }, { "Back", "Back", true }, { "Strafe left", "Strafe Left", true }, { "Strafe right", "Strafe Right", true },
	{ "Jump", "Jump", false }, { "Sprint", "Sprint", false }, { "Sneak", "Sneak", false }, { "Activate", "Activate", false },
	{ "Ready weapon", "Ready Weapon", false }, { "Attack / block, left hand", "Left Attack/Block", false }, { "Attack / block, right hand", "Right Attack/Block", false },
	{ "Shout / power", "Shout", false }, { "Character menu", "Tween Menu", false }, { "Inventory", "Quick Inventory", false }, { "Magic", "Quick Magic", false },
	{ "Map", "Quick Map", false }, { "Skills", "Quick Stats", false }, { "Wait", "Wait", false }, { "Journal", "Journal", false }, { "Favorites", "Favorites", false },
	{ "First / third person", "Toggle POV", false }, { "Walk / run", "Run", false }, { "Always run", "Toggle Always Run", false }, { "Auto-move", "Auto-Move", false },
	{ "Quick save", "Quicksave", false }, { "Quick load", "Quickload", false }
};
constexpr int kKbOurs[] = { 2, 3, 4, 5, Settings::kKeyCodex, Settings::kKeyParty };
constexpr const char* kKbOursLabel[] = { "Take a perk (altar)", "Learn the Thu'um (altar)", "Leave the altar", "Bond the dragon (altar)", "Open the Codex", "Co-op party" };

std::wstring KbIni(const char* a_event, bool a_pad)
{
	std::wstring k = a_pad ? L"iPad_" : L"iKb_";
	for (const char* c = a_event; *c; ++c) if (std::isalnum(static_cast<unsigned char>(*c))) k += static_cast<wchar_t>(*c);
	return k;
}
// the event's mapping on one device, in the gameplay context (nullptr: it has none there)
RE::ControlMap::UserEventMapping* KbMapping(const char* a_event, int a_device)
{
	auto* cm = RE::ControlMap::GetSingleton();
	auto* ctx = cm ? cm->controlMap[RE::UserEvents::INPUT_CONTEXT_ID::kGameplay] : nullptr;
	if (!ctx) return nullptr;
	for (auto& m : ctx->deviceMappings[a_device]) {
		if (m.eventID.c_str() && std::strcmp(m.eventID.c_str(), a_event) == 0) return &m;
	}
	return nullptr;
}
// what Skyrim had before any of ours (for "reset")
struct KbOrig { int kbm = -1, pad = -1; };
std::unordered_map<std::string, KbOrig>& KbOriginals()
{
	static std::unordered_map<std::string, KbOrig> o;
	return o;
}
int KbSkyGet(const char* a_event, bool a_pad)
{
	if (a_pad) {
		auto* m = KbMapping(a_event, RE::INPUT_DEVICE::kGamepad);
		return m && m->inputKey != 0xFF ? m->inputKey : -1;
	}
	auto* k = KbMapping(a_event, RE::INPUT_DEVICE::kKeyboard);
	if (k && k->inputKey != 0xFF) return k->inputKey;
	auto* ms = KbMapping(a_event, RE::INPUT_DEVICE::kMouse);
	return ms && ms->inputKey != 0xFF ? 0x100 | ms->inputKey : -1;
}
void KbRemember()
{
	auto& o = KbOriginals();
	if (!o.empty() || !KbMapping("Jump", RE::INPUT_DEVICE::kKeyboard)) return;
	for (const auto& r : kKbSky) o[r.event] = { KbSkyGet(r.event, false), KbSkyGet(r.event, true) };
}
// set on Skyrim's map (the game's thread)
void KbSkyApply(const char* a_event, bool a_pad, int a_code)
{
	auto put = [&](int a_device, int a_key) {
		auto* m = KbMapping(a_event, a_device);
		if (!m && a_key == 0xFF) return;
		if (!m) {   // (it had none on this device: one made from its others)
			auto* cm = RE::ControlMap::GetSingleton();
			auto* ctx = cm ? cm->controlMap[RE::UserEvents::INPUT_CONTEXT_ID::kGameplay] : nullptr;
			RE::ControlMap::UserEventMapping* like = nullptr;
			for (int d : { 0, 1, 2 }) if (!like) like = KbMapping(a_event, d);
			if (!ctx || !like) return;
			RE::ControlMap::UserEventMapping n = *like;
			n.inputKey = static_cast<std::uint16_t>(a_key);
			ctx->deviceMappings[a_device].push_back(n);
			return;
		}
		m->inputKey = static_cast<std::uint16_t>(a_key);
	};
	if (a_pad) put(RE::INPUT_DEVICE::kGamepad, a_code < 0 ? 0xFF : a_code);
	else if (a_code < 0) put(RE::INPUT_DEVICE::kKeyboard, 0xFF), put(RE::INPUT_DEVICE::kMouse, 0xFF);
	else if (a_code >= 0x100) put(RE::INPUT_DEVICE::kMouse, a_code & 0xFF), put(RE::INPUT_DEVICE::kKeyboard, 0xFF);
	else put(RE::INPUT_DEVICE::kKeyboard, a_code), put(RE::INPUT_DEVICE::kMouse, 0xFF);
}
// our saved ones onto Skyrim's map, once it is loaded (each launch)
void KbApplySaved()
{
	static bool done = false;
	if (done || !KbMapping("Jump", RE::INPUT_DEVICE::kKeyboard)) return;
	done = true;
	KbRemember();
	int n = 0;
	for (const auto& r : kKbSky) {
		for (bool pad : { false, true }) {
			const int v = static_cast<int>(Settings::Number(L"SkyrimKeys", KbIni(r.event, pad).c_str(), -2.0f));
			if (v == -2) continue;
			const std::string ev = r.event;
			SKSE::GetTaskInterface()->AddTask([ev, pad, v] { KbSkyApply(ev.c_str(), pad, v); });
			++n;
		}
	}
	logger::info("keybinds: {} of Skyrim's controls set as you chose ({} remembered as Skyrim had them)", n, KbOriginals().size());
}
void KbSkySet(const char* a_event, bool a_pad, int a_code)
{
	KbRemember();
	Settings::SetNumber(L"SkyrimKeys", KbIni(a_event, a_pad).c_str(), static_cast<float>(a_code), true);
	const std::string ev = a_event;
	SKSE::GetTaskInterface()->AddTask([ev, a_pad, a_code] { KbSkyApply(ev.c_str(), a_pad, a_code); });
	logger::info("keybinds: Skyrim's '{}' on {} -> {}", a_event, a_pad ? "the controller" : "keyboard & mouse", Settings::CodeName(a_code, a_pad ? 2 : 0));
}

struct KRow
{
	std::string label;
	int         ours = -1;        // our action
	const KbSky* sky = nullptr;   // or Skyrim's
	int         kind = 0;         // 0 a binding, 1 a heading, 2 reset everything
};
int KbCode(const KRow& r, bool a_pad)
{
	if (r.ours >= 0) return a_pad ? Settings::PadKey(r.ours) : Settings::Key(r.ours);
	if (r.sky) return a_pad && r.sky->stick ? -3 : KbSkyGet(r.sky->event, a_pad);
	return -1;
}
void KbSet(const KRow& r, bool a_pad, int a_code)
{
	if (r.ours >= 0) {
		Settings::SetKey(r.ours, a_pad, a_code);
		logger::info("keybinds: '{}' on {} -> {}", r.label, a_pad ? "the controller" : "keyboard & mouse", Settings::CodeName(a_code, a_pad ? 2 : 0));
	} else if (r.sky && !(a_pad && r.sky->stick)) {
		KbSkySet(r.sky->event, a_pad, a_code);
	}
}

// the tab itself; true while it stays (false: back out of Settings)
bool TmKeybinds(ImDrawList* fg, ImVec2 size, float now, float dt, float a, ImVec2 cur, bool click, const TmIn& in, int& a_hint)
{
	static int   sel = 1, col = 0, capRow = -1, capCol = 0;
	static float scroll = 0, target = 0, capT = 0;
	static TmMouse mouse;
	const float W = size.x, H = size.y, u = TU(size);
	KbApplySaved();
	std::vector<KRow> rows;
	rows.push_back({ "THE CURSE OF ALDUIN", -1, nullptr, 1 });
	for (std::size_t i = 0; i < std::size(kKbOurs); ++i) rows.push_back({ kKbOursLabel[i], kKbOurs[i], nullptr, 0 });
	rows.push_back({ "SKYRIM", -1, nullptr, 1 });
	for (const auto& s : kKbSky) {
		if (KbMapping(s.event, RE::INPUT_DEVICE::kKeyboard) || KbMapping(s.event, RE::INPUT_DEVICE::kMouse) || KbMapping(s.event, RE::INPUT_DEVICE::kGamepad)) rows.push_back({ s.label, -1, &s, 0 });
	}
	rows.push_back({ "RESET ALL TO DEFAULTS", -1, nullptr, 2 });
	const int n = static_cast<int>(rows.size());
	const bool capturing = capRow >= 0;
	// a key came for the one waited on
	if (capturing) {
		capT += dt;
		const int got = g_bindGot.exchange(-1);
		if (got == -2 || capT > 8.0f) capRow = -1, g_bindWait = -1;
		else if (got >= 0) {
			KbSet(rows[capRow], capCol == 1, got);
			capRow = -1, g_bindWait = -1;
		}
		click = false;
	}
	mouse.Note(cur, in);
	const float top = 17.0f * u, bottom = H - 8.0f * u, rowH = 2.45f * u;
	const int   fits = std::max(1, static_cast<int>((bottom - top) / rowH));
	const float maxScroll = std::max(0.0f, static_cast<float>(n - fits));
	if (!capturing && in.moves) {
		for (int m = 0; m < std::abs(in.moves); ++m) {
			int nf = sel;
			do nf = std::clamp(nf + (in.moves > 0 ? 1 : -1), 0, n - 1);
			while (rows[nf].kind == 1 && nf > 0 && nf < n - 1);
			if (rows[nf].kind != 1) sel = nf;
		}
		target = std::clamp(target, static_cast<float>(sel - fits + 1), static_cast<float>(sel));
	}
	if (!capturing && in.sides) col = std::clamp(col + in.sides, 0, 1);
	if (!capturing && in.wheel) target += static_cast<float>(in.wheel) * 2.0f;
	target = std::clamp(std::round(target), 0.0f, maxScroll);
	scroll += (target - scroll) * std::min(1.0f, dt * 14.0f);
	if (std::abs(target - scroll) < 0.002f) scroll = target;
	const float colL = W * 0.5f - 34.0f * u, colR = W * 0.5f + 34.0f * u, c0 = W * 0.5f + 9.0f * u, c1 = W * 0.5f + 25.0f * u, cw = 7.5f * u;
	TShade(fg, { colL, top - 3.4f * u }, { colR, bottom + 0.4f * u }, 3.0f * u, 0.85f * a);
	TTextC(fg, HF(), 0.9f * u, c0, top - 2.3f * u, Fade(kTGold, a), "KEYBOARD & MOUSE", 0.25f);
	TTextC(fg, HF(), 0.9f * u, c1, top - 2.3f * u, Fade(kTGold, a), "CONTROLLER", 0.25f);
	// the same key twice in a column: both shown in red
	std::unordered_map<int, int> seen[2];
	for (const auto& r : rows) {
		if (r.kind != 0) continue;
		for (int c = 0; c < 2; ++c) if (const int k = KbCode(r, c == 1); k >= 0) ++seen[c][k];
	}
	int hovered = -1, hoverCol = -1, clearCell = -1;
	fg->PushClipRect({ colL, top - 0.4f * u }, { colR, bottom + 0.4f * u }, true);
	for (int i = std::max(0, static_cast<int>(scroll) - 1); i < std::min(n, static_cast<int>(scroll) + fits + 2); ++i) {
		const auto& r = rows[i];
		const float y = top + (static_cast<float>(i) - scroll) * rowH;
		if (r.kind == 1) {
			TText(fg, HF(), 0.95f * u, { colL + 1.4f * u, y + 0.55f * u }, Fade(kTAcc, a), r.label, 0.3f);
			fg->AddLine({ colL + 1.4f * u, y + rowH - 0.25f * u }, { colR - 1.4f * u, y + rowH - 0.25f * u }, IM_COL32(220, 207, 185, static_cast<int>(40 * a)), 1.0f);
			continue;
		}
		const bool over = !capturing && cur.x >= colL && cur.x <= colR && cur.y >= y && cur.y < y + rowH && cur.y >= top && cur.y <= bottom;
		if (over) {
			hovered = i;
			hoverCol = r.kind == 2 ? 0 : std::abs(cur.x - c0) < std::abs(cur.x - c1) ? 0 : 1;
		}
		const bool lit = i == sel;
		if (lit) {
			const float pl = 0.8f + 0.2f * std::sin(now * 3.0f);
			fg->AddRectFilledMultiColor({ colL, y }, { colR, y + rowH - 0.2f * u }, IM_COL32(255, 122, 60, static_cast<int>(36 * pl * a)), IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, static_cast<int>(36 * pl * a)));
			fg->AddRectFilled({ colL, y + 0.2f * u }, { colL + 0.22f * u, y + rowH - 0.4f * u }, Fade(kTAcc, a));
		}
		if (r.kind == 2) {
			TTextC(fg, HF(), 1.05f * u, W * 0.5f, y + 0.5f * u, Fade(lit ? kTAcc : kTTxt, a), r.label, 0.25f);
			continue;
		}
		TText(fg, HF(), 1.1f * u, { colL + 1.4f * u, y + 0.45f * u }, Fade(lit ? kTAcc : kTInk, a), r.label, 0.08f);
		for (int c = 0; c < 2; ++c) {
			const float cx = c == 0 ? c0 : c1;
			const int   k = KbCode(r, c == 1);
			const bool  waiting = capturing && capRow == i && capCol == c;
			std::string t = waiting ? (c == 0 ? "PRESS A KEY" : "PRESS A BUTTON") : k == -3 ? "LEFT STICK" : Settings::CodeName(k, c == 1 ? 2 : (k >= 0x100 ? 1 : 0));
			const bool  dup = k >= 0 && seen[c][k] > 1;
			const bool  on = lit && col == c;
			if (on || waiting) fg->AddRect({ cx - cw, y + 0.25f * u }, { cx + cw, y + rowH - 0.45f * u }, Fade(waiting ? kTAcc : kTGold, (waiting ? 0.6f + 0.4f * std::sin(now * 6.0f) : 0.7f) * a), 0.3f * u, 0, std::max(1.0f, 0.08f * u));
			const ImU32 tc = waiting ? kTAcc : dup ? kTRed : k < 0 && k != -3 ? kTLab : on ? kTAcc : kTTxt;
			TTextC(fg, HFB(), 1.0f * u, cx, y + 0.5f * u, Fade(tc, (k == -3 ? 0.55f : 1.0f) * a), t, 0.12f);
			// (the hints are gone: the chosen key's own small x clears it)
			if (on && !waiting && k >= 0) {
				bool hx = false;
				if (TmWord(fg, HFB(), 1.0f * u, cx + cw + 1.3f * u, y + 0.45f * u, "\xC3\x97", false, cur, click, 0.0f, 0.8f * a, &hx, kTLab)) clearCell = c, click = false;
			}
		}
	}
	fg->PopClipRect();
	if (hovered >= 0 && (mouse.on || click)) sel = hovered, col = hoverCol >= 0 ? hoverCol : col;
	if (n > fits) {
		const float th = (bottom - top) * static_cast<float>(fits) / static_cast<float>(n), ty = top + (bottom - top - th) * (scroll / std::max(1.0f, maxScroll));
		fg->AddRectFilled({ colR + 0.8f * u, top }, { colR + 0.95f * u, bottom }, IM_COL32(220, 207, 185, static_cast<int>(30 * a)));
		fg->AddRectFilled({ colR + 0.8f * u, ty }, { colR + 0.95f * u, ty + th }, Fade(kTAcc, 0.85f * a));
	}
	const bool pad = g_padLast;
	if (capturing) TTextC(fg, Inter(), 0.95f * u, W * 0.5f, H - 6.6f * u, Fade(kTGold, a), capCol == 0 ? "Press the key or mouse button for it  -  Esc cancels" : "Press the controller button for it  -  Esc cancels");
	a_hint = capturing ? -1 : TmHints(fg, size, { { pad ? Hud::PadWord("A") : std::string("ENTER"), "CHANGE" }, { pad ? Hud::PadWord("X") : std::string("DEL"), "CLEAR" },
	                                               { pad ? Hud::PadWord("LB") + " " + Hud::PadWord("RB") : std::string("Q  E"), "TAB" }, { pad ? Hud::PadWord("B") : std::string("ESC"), "BACK" } }, cur, click, a);
	if (capturing) return true;
	const auto& r = rows[std::clamp(sel, 0, n - 1)];
	const bool press = in.accepts > 0 || a_hint == 0 || (click && hovered >= 0 && a_hint < 0);
	if (press && r.kind == 2) {
		for (const int k : kKbOurs) Settings::SetKey(k, false, Settings::DefaultKey(k, false)), Settings::SetKey(k, true, Settings::DefaultKey(k, true));
		KbRemember();
		for (const auto& s : kKbSky) {
			for (bool p : { false, true }) {
				const auto it = KbOriginals().find(s.event);
				if (it == KbOriginals().end()) continue;
				const int v = p ? it->second.pad : it->second.kbm;
				Settings::SetNumber(L"SkyrimKeys", KbIni(s.event, p).c_str(), -2.0f, true);
				const std::string ev = s.event;
				SKSE::GetTaskInterface()->AddTask([ev, p, v] { KbSkyApply(ev.c_str(), p, v); });
			}
		}
		logger::info("keybinds: every key back as it was");
	} else if (press && r.kind == 0 && !(col == 1 && r.sky && r.sky->stick)) {
		capRow = sel, capCol = col, capT = 0;
		g_bindGot = -1, g_bindWait = col;
	} else if ((in.dels > 0 || a_hint == 1) && r.kind == 0) {
		KbSet(r, col == 1, -1);
	} else if (clearCell >= 0 && r.kind == 0) {
		KbSet(r, clearCell == 1, -1);
	}
	return !(a_hint == 3 || in.backs > 0 || TmBack(fg, size, cur, click, a));
}
