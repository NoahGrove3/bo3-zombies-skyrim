// (Noah, Nov 27 - the In-game UI direction he picked for every screen: "Ember Tally") the HUD and pop-ups drawn as his approved
// workshop screens (docs/workshop/v2/uiscreens.js + ui.css .dA): Barlow Condensed for the words in spaced capitals, Inter for
// the running text; ember orange and essence gold on warm parchment; no boxes - soft shadows behind the words. Sizes in the
// workshop's own unit (1u = 1% of a 16:10 screen's width = the screen's height / 62.5), so it scales with any screen's height
// and its places follow the corners.
// (included in Hud.cpp's unnamed namespace, after its drawing helpers)

ImFont* g_hf = nullptr;    // Barlow Condensed SemiBold: the words
ImFont* g_hfb = nullptr;   // Barlow Condensed Bold: the numbers and the titles
ImFont* g_inter = nullptr; // Inter: the running text
ImFont* HF() { return g_hf ? g_hf : g_title; }
ImFont* HFB() { return g_hfb ? g_hfb : g_num; }
ImFont* Inter() { return g_inter ? g_inter : g_body; }

constexpr ImU32 kTAcc = IM_COL32(255, 122, 60, 255);    // #ff7a3c ember
constexpr ImU32 kTEmb = IM_COL32(232, 112, 63, 255);    // #e8703f the bars' ember
constexpr ImU32 kTGold = IM_COL32(241, 200, 110, 255);  // #f1c86e essence gold
constexpr ImU32 kTInk = IM_COL32(245, 226, 184, 255);   // #f5e2b8 titles
constexpr ImU32 kTTxt = IM_COL32(220, 207, 185, 255);   // #dccfb9 words
constexpr ImU32 kTLab = IM_COL32(230, 214, 187, 255);   // #e6d6bb small labels
constexpr ImU32 kTRed = IM_COL32(192, 57, 43, 255);     // #c0392b blood

float TU(ImVec2 a_size) { return a_size.y / 62.5f; }

// a line with its letters spaced (a_track: of the type's size, as CSS letter-spacing in em)
ImVec2 TMeasure(ImFont* f, float px, const std::string& s, float a_track = 0.0f)
{
	ImVec2 m = Measure(f, px, s);
	if (a_track != 0.0f && !s.empty()) {
		int n = 0;
		for (std::size_t i = 0; i < s.size(); ++i) n += (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 ? 1 : 0;
		m.x += a_track * px * static_cast<float>(std::max(0, n - 1));
	}
	return m;
}
// the text itself: a soft drop shadow under it, an optional glow round it (several faint copies), its letters spaced
void TText(ImDrawList* dl, ImFont* f, float px, ImVec2 p, ImU32 col, const std::string& s, float a_track = 0.0f, ImU32 a_glow = 0, float a_glowR = 0.0f, bool a_shadow = true)
{
	if (s.empty() || !f) return;
	const float alpha = ((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
	auto run = [&](ImVec2 at, ImU32 c) {
		if (a_track == 0.0f) { dl->AddText(f, px, at, c, s.c_str()); return; }
		float x = at.x;
		for (std::size_t i = 0; i < s.size();) {
			std::size_t j = i + 1;
			while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
			const std::string ch = s.substr(i, j - i);
			dl->AddText(f, px, { x, at.y }, c, ch.c_str());
			x += Measure(f, px, ch).x + a_track * px;
			i = j;
		}
	};
	if (a_glow && a_glowR > 0.0f) {
		const float r = a_glowR;
		for (const auto& o : { ImVec2{ -r, 0 }, ImVec2{ r, 0 }, ImVec2{ 0, -r }, ImVec2{ 0, r }, ImVec2{ -r * 0.7f, -r * 0.7f }, ImVec2{ r * 0.7f, r * 0.7f }, ImVec2{ r * 0.7f, -r * 0.7f }, ImVec2{ -r * 0.7f, r * 0.7f } }) {
			run({ p.x + o.x, p.y + o.y }, Fade(a_glow, 0.16f * alpha));
		}
	}
	if (a_shadow) run({ p.x, p.y + std::max(1.0f, px * 0.06f) }, IM_COL32(0, 0, 0, static_cast<int>(200 * alpha)));
	run(p, col);
}
void TTextC(ImDrawList* dl, ImFont* f, float px, float cx, float y, ImU32 col, const std::string& s, float a_track = 0.0f, ImU32 a_glow = 0, float a_glowR = 0.0f)
{
	TText(dl, f, px, { cx - TMeasure(f, px, s, a_track).x * 0.5f, y }, col, s, a_track, a_glow, a_glowR);
}
void TTextR(ImDrawList* dl, ImFont* f, float px, float rx, float y, ImU32 col, const std::string& s, float a_track = 0.0f, ImU32 a_glow = 0, float a_glowR = 0.0f)
{
	TText(dl, f, px, { rx - TMeasure(f, px, s, a_track).x, y }, col, s, a_track, a_glow, a_glowR);
}
// a box's soft fade (Noah: "make the transitions and visuals fade in and out smoothly"): in over a_in seconds, out over its last
// a_out of a_total
float TFade(float a_left, float a_total, float a_in = 0.35f, float a_out = 0.6f)
{
	if (a_total <= 0.0f) return std::clamp(a_left / a_out, 0.0f, 1.0f);
	const float shown = a_total - a_left;
	return std::clamp(std::min(shown / a_in, a_left / a_out), 0.0f, 1.0f);
}
// a smoked shadow fading out on every side behind a block of words (no box)
void TShade(ImDrawList* dl, ImVec2 a, ImVec2 b, float a_pad, float a_alpha)
{
	const ImU32 dark = IM_COL32(8, 5, 3, static_cast<int>(150 * a_alpha)), clear = IM_COL32(8, 5, 3, 0);
	dl->AddRectFilled(a, b, dark);
	dl->AddRectFilledMultiColor({ a.x - a_pad, a.y }, { a.x, b.y }, clear, dark, dark, clear);
	dl->AddRectFilledMultiColor({ b.x, a.y }, { b.x + a_pad, b.y }, dark, clear, clear, dark);
	dl->AddRectFilledMultiColor({ a.x, a.y - a_pad }, { b.x, a.y }, clear, clear, dark, dark);
	dl->AddRectFilledMultiColor({ a.x, b.y }, { b.x, b.y + a_pad }, dark, dark, clear, clear);
}
// a thin bar: a dark trough and its fill
void TBar(ImDrawList* dl, ImVec2 a, float w, float h, float v, ImU32 fill, ImU32 trough = IM_COL32(255, 255, 255, 41), bool a_glow = false)
{
	dl->AddRectFilled(a, { a.x + w, a.y + h }, trough);
	const float fw = w * std::clamp(v, 0.0f, 1.0f);
	if (a_glow && fw > 0) dl->AddRectFilled({ a.x, a.y - h }, { a.x + fw, a.y + 2 * h }, Fade(fill, 0.18f));
	if (fw > 0) dl->AddRectFilled(a, { a.x + fw, a.y + h }, fill);
}
// a ring that fills clockwise from the top (the hold, the Tablet weapon's uses)
void TRing(ImDrawList* dl, ImVec2 c, float r, float w, float v, ImU32 col, ImU32 track = IM_COL32(255, 255, 255, 38))
{
	dl->AddCircle(c, r, track, 64, w);
	if (v <= 0.0f) return;
	const float a0 = -1.5707963f, a1 = a0 + 6.2831853f * std::clamp(v, 0.0f, 1.0f);
	dl->PathArcTo(c, r, a0, a1, std::max(8, static_cast<int>(64 * v)));
	dl->PathStroke(col, 0, w);
}
// BO3's round tally - four strokes and a slash through them - scratched like runes, in ember (the workshop's tallySvg): its
// strokes on a 62-high grid, a_h high on screen; a_new: how far the newest is cut in (0..1)
float TallyWidth(int n, float a_h)
{
	float x = 6;
	for (int i = 0; i < n; ++i) x += i % 5 == 4 ? 15.0f : 13.0f;
	return (x + 6) * a_h / 62.0f;
}
void TallyMarks(ImDrawList* dl, ImVec2 p, float a_h, int n, float a_new, float a_alpha)
{
	const float s = a_h / 62.0f;
	const auto r = [](int i) { const double v = std::sin(i * 12.9898 + 3.1) * 43758.5453; return static_cast<float>(v - std::floor(v)); };
	struct Seg { ImVec2 a, b; };
	std::vector<Seg> segs;
	float x = 6;
	for (int i = 0; i < n; ++i) {
		if (i % 5 == 4) {
			const float x0 = x - 4 * 13 - 3;
			segs.push_back({ { x0, 44 - r(i) * 5 }, { x - 8, 17 + r(i + 3) * 4 } });
			x += 15;
			continue;
		}
		segs.push_back({ { x + r(i) * 3, 8 + r(i + 1) * 5 }, { x + 2 - r(i + 2) * 3, 54 - r(i + 5) * 5 } });
		x += 13;
	}
	for (std::size_t i = 0; i < segs.size(); ++i) {
		auto a = segs[i].a, b = segs[i].b;
		if (i + 1 == segs.size() && a_new < 1.0f) b = { a.x + (b.x - a.x) * a_new, a.y + (b.y - a.y) * a_new };   // (the newest cut in)
		const ImVec2 A{ p.x + a.x * s, p.y + a.y * s }, B{ p.x + b.x * s, p.y + b.y * s };
		// (drop-shadow .45u of rgba(232,90,40,.85): a soft ember glow round each stroke)
		dl->AddLine(A, B, IM_COL32(232, 90, 40, static_cast<int>(40 * a_alpha)), 13.0f * s);
		dl->AddLine(A, B, IM_COL32(232, 90, 40, static_cast<int>(70 * a_alpha)), 9.0f * s);
		dl->AddLine(A, B, Fade(kTAcc, a_alpha), 5.5f * s);
		dl->AddLine(A, B, IM_COL32(255, 224, 189, static_cast<int>(217 * a_alpha)), 1.6f * s);
		for (const ImVec2 c : { A, B }) dl->AddCircleFilled(c, 2.75f * s, Fade(kTAcc, a_alpha), 10);   // (round caps)
	}
}

// ---- the night: NIGHT, its tally (from night 21 its number in ember), the wave and the dead left; returns the foot of the box
float TallyNight(ImDrawList* dl, const Hud::State& s, ImVec2 size, float a_flash)
{
	const float u = TU(size);
	const ImVec2 p{ size.x * 0.024f, size.y * 0.036f };
	const bool waveOn = s.nightActive && s.waves > 0 && s.sanctuary.empty();
	const bool ev = s.nightActive && s.nightEvent >= 0 && s.nightEvent < static_cast<int>(gen::kEvents.size());
	const std::string lab = s.nightActive ? "NIGHT" : "NIGHTS SURVIVED";
	const std::string sub = waveOn && s.waveRest <= 0 ? fmt::format("WAVE {} \xC2\xB7 {} LEFT", s.wave, std::max(0, s.waveLeft)) : waveOn ? fmt::format("WAVE {} RISES", std::min(s.wave + 1, s.waves)) : std::string();
	const float th = 4.6f * u;
	const float shade = 1.0f;
	float y = p.y;
	const float wTally = s.round <= 20 ? std::min(TallyWidth(s.round, th), 22.0f * u) : TMeasure(HFB(), th * 1.05f, std::to_string(s.round)).x;
	const float w = std::max({ wTally, TMeasure(HF(), 0.62f * u, lab, 0.45f).x, TMeasure(HF(), 0.72f * u, sub, 0.14f).x }) + 1.0f * u;
	const float foot = p.y + 0.62f * u + 0.25f * u + th + (sub.empty() ? 0.0f : 0.25f * u + 0.9f * u) + (ev ? 0.9f * u : 0.0f) + (waveOn && !s.waveTheme.empty() ? 0.9f * u : 0.0f);
	dl->AddRectFilledMultiColor({ 0, 0 }, { p.x + w + 4 * u, foot + 1.2f * u }, IM_COL32(0, 0, 0, static_cast<int>(70 * shade)), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, static_cast<int>(40 * shade)));
	TText(dl, HF(), 0.62f * u, { p.x, y }, kTLab, lab, 0.45f);
	y += 0.62f * u + 0.25f * u;
	const float fl = std::clamp(a_flash, 0.0f, 1.0f);
	if (s.round <= 20) {
		// (the newest cut in as its night begins: roundFlash runs 2 .. 0)
		const float cut = std::clamp(1.0f - (a_flash - 1.0f), 0.0f, 1.0f);
		TallyMarks(dl, { p.x, y }, th, s.round, s.round > 0 ? cut : 1.0f, 1.0f);
	} else {
		TText(dl, HFB(), th * 1.05f, { p.x, y - th * 0.12f }, fl > 0 ? IM_COL32(255, 210, 170, 255) : kTAcc, std::to_string(s.round), 0.0f, IM_COL32(232, 90, 40, 255), 0.45f * u);
	}
	y += th;
	if (!sub.empty()) {
		y += 0.25f * u;
		TText(dl, HF(), 0.72f * u, { p.x, y }, kTLab, sub, 0.14f);
		y += 0.9f * u;
	}
	if (ev) {
		const auto  t = gen::kEvents[s.nightEvent].tint;
		const ImU32 c = IM_COL32(std::min(255, static_cast<int>(((t >> 24) & 0xFF) * 1.6f) + 60), std::min(255, static_cast<int>(((t >> 16) & 0xFF) * 1.6f) + 60), std::min(255, static_cast<int>(((t >> 8) & 0xFF) * 1.6f) + 60), 255);
		TText(dl, HF(), 0.7f * u, { p.x, y }, c, Upper(std::string(gen::kEvents[s.nightEvent].label)), 0.18f);
		y += 0.9f * u;
	}
	if (waveOn && !s.waveTheme.empty()) {
		TText(dl, HF(), 0.7f * u, { p.x, y }, s.waveThemeColor ? s.waveThemeColor : kTAcc, Upper(s.waveTheme), 0.18f);
		y += 0.9f * u;
	}
	return foot;
}

// ---- the quests you track, under the night (the altar's trials float over their altar instead - Noah's note)
void TallyQuests(ImDrawList* dl, const Hud::State& s, ImVec2 size, float a_top)
{
	const float u = TU(size);
	const float x = size.x * 0.024f, w = 17.0f * u;
	float y = std::max(a_top + 1.0f * u, size.y * 0.175f);
	auto wrap = [&](ImFont* f, float px, const std::string& str) {
		std::vector<std::string> rows(1);
		for (std::size_t p0 = 0, p1; p0 < str.size(); p0 = p1 + 1) {
			p1 = str.find(' ', p0);
			if (p1 == std::string::npos) p1 = str.size();
			if (p1 <= p0) continue;
			const std::string wd = str.substr(p0, p1 - p0);
			if (!rows.back().empty() && Measure(f, px, rows.back() + " " + wd).x > w && rows.size() < 3) rows.emplace_back();
			rows.back() += (rows.back().empty() ? "" : " ") + wd;
		}
		return rows;
	};
	// (the trial under way floats over its altar while its circle stands - TallyAltarQuest; a trial with none stays here)
	std::vector<Hud::State::Tracked> list;
	if (!s.questTitle.empty() && !s.ringOn) {
		std::string line = s.questGoal;
		if (s.questTimer >= 0) {
			const int t = std::max(0, static_cast<int>(std::ceil(s.questTimer)));
			line += fmt::format("   {}:{:02d}", t / 60, t % 60);
		}
		list.push_back({ s.questTitle, { line } });
	}
	for (const auto& t : s.tracked) if (s.questTitle.empty() || t.title != s.questTitle) list.push_back(t);
	for (const auto& t : list) {
		const float y0 = y;
		std::vector<std::vector<std::string>> gl;
		float h = 0.74f * u + 0.3f * u;
		for (const auto& g : t.goals) { gl.push_back(wrap(Inter(), 0.66f * u, g)); h += 0.95f * u * gl.back().size(); }
		TShade(dl, { x - 0.4f * u, y0 - 0.3f * u }, { x + w, y0 + h }, 2.0f * u, 0.45f);
		TText(dl, HF(), 0.74f * u, { x, y }, kTInk, Upper(t.title), 0.16f);
		y += 0.74f * u + 0.3f * u;
		for (const auto& rows : gl) {
			for (const auto& r : rows) {
				TText(dl, Inter(), 0.66f * u, { x, y }, kTTxt, r);
				y += 0.95f * u;
			}
		}
		y += 0.7f * u;
		if (y > size.y * 0.62f) break;
	}
}

// ---- the compass, top centre: the directions spaced along a hairline, the quest's mark in ember, the dead as red dots
void TallyCompass(ImDrawList* dl, const Hud::State& s, ImVec2 size)
{
	if (!s.compassOn) return;
	const float u = TU(size);
	const float x0 = size.x * 0.33f, x1 = size.x * 0.67f, cx = (x0 + x1) * 0.5f, y = size.y * 0.026f, half = (x1 - x0) * 0.5f;
	const float span = 1.5707963f;   // (a quarter turn each side of where you face)
	const float base = y + 1.15f * u;
	// the hairline (rgba(239,231,216,.5)), fading out at its ends
	dl->AddRectFilledMultiColor({ x0, base }, { cx, base + 0.06f * u }, IM_COL32(239, 231, 216, 0), IM_COL32(239, 231, 216, 128), IM_COL32(239, 231, 216, 128), IM_COL32(239, 231, 216, 0));
	dl->AddRectFilledMultiColor({ cx, base }, { x1, base + 0.06f * u }, IM_COL32(239, 231, 216, 128), IM_COL32(239, 231, 216, 0), IM_COL32(239, 231, 216, 0), IM_COL32(239, 231, 216, 128));
	auto place = [&](float a_ang, float& a_x, float& a_edge) {
		float d = std::remainder(a_ang - s.heading, 6.2831853f);
		a_edge = std::clamp(1.0f - (std::abs(d) - span * 0.8f) / (span * 0.2f), 0.0f, 1.0f);
		if (std::abs(d) > span) return false;
		a_x = cx + d / span * half;
		return true;
	};
	static const char* kDir[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
	for (int i = 0; i < 8; ++i) {
		float px, edge;
		if (!place(i * 0.7853982f, px, edge)) continue;
		const bool card = i % 2 == 0;
		const float fs = (card ? 0.78f : 0.58f) * u;
		TTextC(dl, HF(), fs, px, y + (card ? 0.0f : 0.15f * u), Fade(card ? IM_COL32(239, 231, 216, 255) : IM_COL32(239, 231, 216, 170), edge), kDir[i], 0.25f);
	}
	// (the marks: the quest in ember, altars and cities, the dead and bosses as red dots)
	for (const auto& m : s.compass) {
		float px, edge;
		if (!place(m.ang, px, edge)) continue;
		const float my = y + 0.42f * u;
		if (m.kind == 8 || m.kind == 9) {
			dl->AddCircleFilled({ px, my }, (m.kind == 9 ? 0.32f : 0.2f) * u, Fade(kTRed, edge), 12);
		} else if (m.kind == 7) {
			dl->AddCircleFilled({ px, my }, 0.22f * u, Fade(IM_COL32(90, 167, 255, 255), edge), 12);
		} else if (m.kind == 1 || m.kind == 2) {
			const float r = 0.32f * u;
			dl->AddQuadFilled({ px, my - r }, { px + r, my }, { px, my + r }, { px - r, my }, Fade(m.kind == 2 ? kTGold : kTEmb, edge));
		} else if (m.kind == 3 || m.kind == 4) {
			dl->AddCircle({ px, my }, 0.22f * u, Fade(IM_COL32(239, 231, 216, 200), edge), 12, 0.08f * u);
		}
	}
	if (s.objOn) {
		float px, edge;
		if (place(s.objAng, px, edge)) {
			const float my = y + 0.42f * u, r = 0.38f * u;
			dl->AddQuadFilled({ px, my - r }, { px + r, my }, { px, my + r }, { px - r, my }, Fade(kTEmb, edge));
		}
	}
}

// ---- Cursed Essence, bottom right: the small label, the number large in gold; what a kill gave rising over it
[[maybe_unused]] void TallyEssence(ImDrawList* dl, const Hud::State& s, ImVec2 size, float a_shown, bool a_rising)
{
	const float u = TU(size);
	const float rx = size.x * (1.0f - 0.026f), by = size.y * (1.0f - 0.032f);
	const std::string num = Num(static_cast<int>(std::lround(a_shown)));
	const float nh = 2.5f * u;
	const float lw = std::max(TMeasure(HFB(), nh, num).x, TMeasure(HF(), 0.55f * u, "CURSED ESSENCE", 0.4f).x);
	TShade(dl, { rx - lw - 0.5f * u, by - nh - 1.2f * u }, { rx + 0.3f * u, by }, 2.5f * u, 0.35f);
	TTextR(dl, HFB(), nh, rx, by - nh, a_rising ? IM_COL32(255, 228, 160, 255) : kTGold, num, 0.0f, IM_COL32(241, 170, 70, 255), 0.35f * u);
	TTextR(dl, HF(), 0.55f * u, rx, by - nh - 0.75f * u, kTLab, "CURSED ESSENCE", 0.4f);
	if (s.gainTime > 0 && s.lastGain != 0) {
		const std::string g = (s.lastGain > 0 ? "+" : "") + Num(s.lastGain);
		const float rise = (1.0f - std::clamp(s.gainTime, 0.0f, 1.0f)) * 1.2f * u;
		TTextR(dl, HFB(), 1.0f * u, rx, by - nh - 2.1f * u - rise, Fade(s.lastGain < 0 ? IM_COL32(230, 80, 60, 255) : IM_COL32(255, 217, 138, 255), std::min(1.0f, s.gainTime * 1.5f)), g, 0.0f, IM_COL32(241, 170, 70, 255), 0.25f * u);
	}
	if (s.styleTime > 0 && !s.styleTag.empty()) {   // (style: its tag over the essence)
		const float a = std::min(1.0f, s.styleTime / 0.4f);
		TTextR(dl, HF(), 0.8f * u, rx, by - nh - 3.3f * u, Fade(kTAcc, a), Upper(s.styleTag), 0.12f, IM_COL32(232, 90, 40, 255), 0.25f * u);
	}
}
// what each kill gave, rising where it fell (BO3's points): gold, fading as it climbs
[[maybe_unused]] void TallyKillPops(ImDrawList* dl, const Hud::State& s, ImVec2 size)
{
	const float u = TU(size);
	for (const auto& m : s.motes) {
		const float age = m[2];
		if (age > 0.75f) continue;   // (a kill's essence mote lives 0.75 s)
		const float a = std::clamp(1.0f - (age - 0.45f) / 0.3f, 0.0f, 1.0f) * std::clamp(age / 0.06f, 0.0f, 1.0f);
		const ImVec2 p{ m[0] * size.x, m[1] * size.y - age * 2.2f * u };
		TTextC(dl, HFB(), 0.95f * u, p.x, p.y, Fade(kTGold, a), "+" + std::to_string(std::max(1, s.lastGain > 0 ? std::min(s.lastGain, 100) : 10)), 0.0f, IM_COL32(241, 170, 70, 255), 0.2f * u);
	}
}

// ---- the brews you've drunk, bottom left: a row of glowing medallions, each its own icon (BO3's perk-a-colas)
void TallyBrews(ImDrawList* dl, const Hud::State& s, ImVec2 size, ID3D11ShaderResourceView* const* a_icons, std::size_t a_n)
{
	const float u = TU(size);
	const float d = 1.9f * u, gap = 0.45f * u;
	float x = size.x * 0.024f;
	const float cy = size.y * (1.0f - 0.034f) - d * 0.5f;
	for (std::size_t q = 0; q < a_n && q < s.perkStacks.size(); ++q) {
		if (s.perkStacks[q] <= 0) continue;
		const ImVec2 c{ x + d * 0.5f, cy };
		const std::uint32_t pc = q < gen::kPerkColor.size() ? gen::kPerkColor[q] : 0xC8B48CFF;   // (0xRRGGBBAA)
		const ImU32 glow = IM_COL32((pc >> 24) & 0xFF, (pc >> 16) & 0xFF, (pc >> 8) & 0xFF, 255);
		for (int g = 3; g >= 1; --g) dl->AddCircleFilled(c, d * 0.5f + g * 0.16f * u, Fade(glow, 0.09f), 32);
		dl->AddCircleFilled(c, d * 0.5f + 0.1f * u, IM_COL32(0, 0, 0, 153), 32);
		if (auto* icon = a_icons[q]) {
			const float r = d * 0.5f;
			dl->AddImageRounded(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { c.x - r, c.y - r }, { c.x + r, c.y + r }, { 0, 0 }, { 1, 1 }, IM_COL32_WHITE, r);
		} else {
			dl->AddCircleFilled(c, d * 0.5f, glow, 32);
		}
		if (s.perkStacks[q] > 1) TTextR(dl, HFB(), 0.62f * u, c.x + d * 0.55f, c.y + d * 0.1f, kTInk, std::to_string(s.perkStacks[q]));
		x += d + gap;
	}
}

// ---- the boss: its name over its long bar, top centre under the compass
void TallyBoss(ImDrawList* dl, const Hud::State& s, ImVec2 size, float a_ghost)
{
	if (s.bossName.empty()) return;
	const float u = TU(size);
	const float x0 = size.x * 0.30f, x1 = size.x * 0.70f, y = size.y * 0.065f;
	TTextC(dl, HF(), 0.8f * u, size.x * 0.5f, y, s.bossEnraged ? IM_COL32(255, 140, 90, 255) : kTInk, Upper(s.bossName), 0.22f, s.bossEnraged ? IM_COL32(232, 90, 40, 255) : 0, 0.3f * u);
	const float by = y + 0.8f * u + 0.35f * u, h = 0.35f * u, w = x1 - x0;
	dl->AddRectFilled({ x0, by }, { x1, by + h }, IM_COL32(0, 0, 0, 140));
	dl->AddRect({ x0 - 0.06f * u, by - 0.06f * u }, { x1 + 0.06f * u, by + h + 0.06f * u }, IM_COL32(232, 112, 63, 153), 0.0f, 0, 0.06f * u);
	if (a_ghost > s.bossHp) dl->AddRectFilled({ x0 + w * s.bossHp, by }, { x0 + w * a_ghost, by + h }, IM_COL32(245, 226, 184, 120));
	dl->AddRectFilledMultiColor({ x0, by }, { x0 + w * std::clamp(s.bossHp, 0.0f, 1.0f), by + h }, IM_COL32(214, 72, 46, 255), IM_COL32(232, 112, 63, 255), IM_COL32(160, 30, 20, 255), IM_COL32(140, 24, 18, 255));
}

// ---- the hold at the Tablet and the altar: a gold ring filling, and what holding does - the key in a gold-edged cap, the price
void TallyHold(ImDrawList* dl, const Hud::State& s, ImVec2 size, bool a_pad)
{
	if (s.holdFrac < 0.0f || s.holdText.empty()) return;
	const float u = TU(size);
	const ImVec2 c{ size.x * 0.5f, size.y * 0.58f + 1.7f * u };
	TRing(dl, c, 1.45f * u, 0.3f * u, s.holdFrac, kTGold, IM_COL32(255, 255, 255, 36));
	// (the line: "Hold to roll the tablet  1,500" - the price's own figure in gold if the text carries one after a dash)
	std::string text = s.holdText, cost;
	if (const auto dash = text.rfind(" - "); dash != std::string::npos) cost = text.substr(dash + 3), text = text.substr(0, dash);
	const float fs = 0.9f * u, cap = 1.4f * u;
	const float tw = TMeasure(HF(), fs, text, 0.06f).x + (cost.empty() ? 0.0f : 0.6f * u + TMeasure(HF(), fs, cost).x);
	const float x0 = size.x * 0.5f - (cap + 0.5f * u + tw) * 0.5f, y = size.y * 0.64f;
	if (a_pad) {
		PadGlyph(dl, { x0 + cap * 0.5f, y + cap * 0.5f }, cap * 0.5f, "A");
	} else {
		dl->AddRect({ x0, y }, { x0 + cap, y + cap }, kTGold, 0.2f * u, 0, 0.08f * u);
		TTextC(dl, HFB(), 0.8f * u, x0 + cap * 0.5f, y + 0.28f * u, kTInk, "E");
	}
	TText(dl, HF(), fs, { x0 + cap + 0.5f * u, y + 0.2f * u }, kTInk, text, 0.06f);
	if (!cost.empty()) TText(dl, HF(), fs, { x0 + cap + 0.5f * u + TMeasure(HF(), fs, text, 0.06f).x + 0.6f * u, y + 0.2f * u }, kTGold, cost);
}

// ---- before the night: THE NIGHT BEGINS IN, the seconds large in ember
void TallyCountdown(ImDrawList* dl, const Hud::State& s, ImVec2 size)
{
	if (s.countdown <= 0.05f) return;
	const float u = TU(size);
	const int n = static_cast<int>(std::ceil(s.countdown));
	const float f = s.countdown - std::floor(s.countdown);   // (1 just ticked .. 0)
	const float pop = 1.0f + 0.12f * f * f;
	const float a = std::clamp((gen::kRounds.breakSeconds - s.countdown) / 0.5f, 0.0f, 1.0f);   // (it fades in)
	const float cy = size.y * 0.5f;
	TTextC(dl, HF(), 0.7f * u, size.x * 0.5f, cy - 3.4f * u, Fade(kTLab, a), "THE NIGHT BEGINS IN", 0.4f);
	TTextC(dl, HFB(), 6.0f * u * pop, size.x * 0.5f, cy - 2.4f * u - (pop - 1.0f) * 3.0f * u, Fade(kTAcc, a), std::to_string(n), 0.0f, IM_COL32(232, 90, 40, 255), 0.6f * u);
}

// ---- the Tablet weapon's uses: a small ring in its rarity's colour beside the stamina bar, "USES 66"
void TallyUses(ImDrawList* dl, const Hud::State& s, ImVec2 size)
{
	if (s.tabletWear < 0.0f) return;
	const float u = TU(size);
	static constexpr ImU32 kRare[3] = { IM_COL32(61, 140, 255, 255), IM_COL32(168, 77, 255, 255), IM_COL32(255, 179, 41, 255) };
	const ImU32 col = kRare[std::clamp(s.tabletRarity, 0, 2)];
	const float d = 2.2f * u;
	const ImVec2 c{ size.x * 0.855f + d * 0.5f, size.y * (1.0f - 0.016f) - 0.62f * u - 0.3f * u - d * 0.5f };
	const float f = std::clamp(s.tabletWear, 0.0f, 1.0f);
	const float blink = f < 0.1f ? 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 9.0f) : 1.0f;
	dl->AddCircleFilled(c, d * 0.5f, IM_COL32(0, 0, 0, 90), 40);
	TRing(dl, c, d * 0.5f - 0.22f * u, 0.44f * u, f, Fade(col, blink), IM_COL32(255, 255, 255, 38));
	TTextC(dl, HF(), 0.62f * u, c.x, c.y + d * 0.5f + 0.3f * u, IM_COL32(233, 220, 196, 255), fmt::format("USES {}", s.tabletUses), 0.12f);
}

// ---- a card in the middle-top: a small ember line, the name large, a gold line (a Dragon Priest slain, the Tablet's win)
void TallyCard(ImDrawList* dl, ImVec2 size, const std::string& a_small, const std::string& a_big, const std::string& a_sub, float a_alpha, ImU32 a_smallCol = kTAcc)
{
	const float u = TU(size);
	const float cx = size.x * 0.5f, y = size.y * 0.16f;
	const float w = std::max({ TMeasure(HFB(), 2.2f * u, a_big).x, TMeasure(HF(), 0.62f * u, a_small, 0.4f).x, TMeasure(HF(), 0.8f * u, a_sub, 0.08f).x }) + 6.0f * u;
	// (a radial smoke behind it)
	for (int i = 6; i >= 1; --i) {
		const float t = i / 6.0f;
		dl->AddRectFilled({ cx - w * 0.5f * t, y - 0.6f * u * t }, { cx + w * 0.5f * t, y + 5.2f * u - 0.6f * u * (1 - t) + 0.6f * u * t }, IM_COL32(10, 6, 4, static_cast<int>(26 * a_alpha)), 3.0f * u * t);
	}
	TTextC(dl, HF(), 0.62f * u, cx, y + 0.4f * u, Fade(a_smallCol, a_alpha), a_small, 0.4f);
	TTextC(dl, HFB(), 2.2f * u, cx, y + 1.2f * u, Fade(kTInk, a_alpha), a_big, 0.0f, IM_COL32(232, 90, 40, 255), 0.4f * u * a_alpha);
	if (!a_sub.empty()) TTextC(dl, HF(), 0.8f * u, cx, y + 3.7f * u, Fade(kTGold, a_alpha), a_sub, 0.08f);
}

// ---- a message from the mod (the banner): spaced capitals in the middle-top, fading in and out
void TallyBanner(ImDrawList* dl, ImVec2 size, const std::string& a_text, float a_alpha)
{
	if (a_text.empty() || a_alpha <= 0.0f) return;
	const float u = TU(size);
	const float cx = size.x * 0.5f, y = size.y * 0.2f, fs = 1.0f * u;
	// (long lines broken in two at the dash)
	std::vector<std::string> rows{ a_text };
	if (TMeasure(HF(), fs, a_text, 0.08f).x > size.x * 0.6f) {
		if (const auto at = a_text.find("  -  "); at != std::string::npos) rows = { a_text.substr(0, at), a_text.substr(at + 5) };
	}
	float w = 0;
	for (const auto& r : rows) w = std::max(w, TMeasure(HF(), fs, Upper(r), 0.08f).x);
	TShade(dl, { cx - w * 0.5f - 0.6f * u, y - 0.3f * u }, { cx + w * 0.5f + 0.6f * u, y + rows.size() * 1.4f * u }, 3.0f * u, 0.45f * a_alpha);
	for (std::size_t i = 0; i < rows.size(); ++i) {
		TTextC(dl, HF(), i == 0 ? fs : fs * 0.82f, cx, y + i * 1.4f * u, Fade(i == 0 ? kTInk : kTTxt, a_alpha), Upper(rows[i]), 0.08f);
	}
}

// ---- falling: the world drains grey (drawn by the caller), YOU FELL in red, the night and the dead, the bar to rising
void TallyDeath(ImDrawList* dl, const Hud::State& s, ImVec2 size, float a_alpha)
{
	const float u = TU(size);
	const float cx = size.x * 0.5f, cy = size.y * 0.5f;
	const float rise = (1.0f - a_alpha) * 1.0f * u;
	TTextC(dl, HFB(), 4.2f * u, cx, cy - 4.2f * u + rise, Fade(IM_COL32(255, 90, 54, 255), a_alpha), "YOU FELL", 0.12f, IM_COL32(200, 40, 20, 255), 0.7f * u * a_alpha);
	TTextC(dl, HF(), 0.9f * u, cx, cy + 0.6f * u + rise, Fade(IM_COL32(239, 231, 216, 255), a_alpha), fmt::format("Night {} \xC2\xB7 {} of the dead", s.deathNight, s.deathKills), 0.1f);
	const float bw = 16.0f * u;
	TBar(dl, { cx - bw * 0.5f, cy + 2.3f * u + rise }, bw, 0.25f * u, s.deathRise, Fade(kTAcc, a_alpha), IM_COL32(255, 255, 255, static_cast<int>(38 * a_alpha)), true);
	TTextC(dl, HF(), 0.65f * u, cx, cy + 3.1f * u + rise, Fade(kTLab, a_alpha), s.deathLoading ? "RISING" : s.deathReady ? "PRESS TO RISE" : "HOLD TO RISE", 0.4f);
}

// ---- the Echo Altar's trial, over the altar in the world while its circle stands (Noah: "for the echo altar quests it should only
// show above the echo altar"): its name, what to do (the count or the clock), a thin ember bar
void TallyAltarQuest(ImDrawList* dl, const Hud::State& s, ImVec2 size)
{
	if (!s.ringOn || !s.ringLabelOn) return;
	const float u = TU(size);
	const ImVec2 c{ s.ringLabelX * size.x, s.ringLabelY * size.y };
	const bool warn = s.ringFlash > 0.01f || s.questWarn || (!s.ringHold && s.ringInside);
	std::string head = !s.questTitle.empty() ? s.questTitle : s.ringHead;
	if (head.empty()) head = s.ringHold ? (s.ringInside ? "STAND IN THE CIRCLE" : "HOLD THE CIRCLE") : (s.ringInside ? "THE DEAD ARE IN THE CIRCLE" : "KEEP THE DEAD OUT");
	std::string line = !s.ringSub.empty() ? s.ringSub : !s.questGoal.empty() ? s.questGoal : std::to_string(std::max(0, s.ringSeconds)) + "s";
	if (s.questTimer >= 0) {
		const int t = std::max(0, static_cast<int>(std::ceil(s.questTimer)));
		line += fmt::format("   {}:{:02d}", t / 60, t % 60);
	}
	const float w = std::max({ TMeasure(HF(), 0.8f * u, Upper(head), 0.22f).x, TMeasure(Inter(), 0.66f * u, line).x, 12.0f * u }) + 2.8f * u;
	TShade(dl, { c.x - w * 0.5f, c.y - 1.6f * u }, { c.x + w * 0.5f, c.y + 1.4f * u }, 2.5f * u, 0.55f);
	TTextC(dl, HF(), 0.8f * u, c.x, c.y - 1.25f * u, warn ? IM_COL32(255, 120, 100, 255) : kTInk, Upper(head), 0.22f);
	TTextC(dl, Inter(), 0.66f * u, c.x, c.y - 0.1f * u, kTTxt, line);
	const float frac = s.questFrac > 0 ? s.questFrac : s.ringFrac;
	TBar(dl, { c.x - 6.0f * u, c.y + 0.85f * u }, 12.0f * u, 0.18f * u, frac, kTAcc, IM_COL32(255, 255, 255, 46), true);
}

// ---- the Echo Altar (Noah, Nov 27: the new altar menu - "the class painting, the tab icons along the top, every brew with its own
// icon"): a wide panel over a dimmed world, its tabs along the top (Q / E or the shoulder buttons step between them), the open tab's
// rows under them; the row in focus lit with an ember bar at its left (the altar is driven by Shops' focus rows, as before)
void TallyTabIcon(ImDrawList* dl, int a_sec, ImVec2 c, float r, ImU32 col)
{
	const float w = std::max(1.0f, r * 0.16f);
	switch (a_sec) {
	case 210: {   // Your Kit: a shield
		const ImVec2 p[6] = { { c.x, c.y - r }, { c.x + r * 0.75f, c.y - r * 0.6f }, { c.x + r * 0.7f, c.y }, { c.x, c.y + r }, { c.x - r * 0.7f, c.y }, { c.x - r * 0.75f, c.y - r * 0.6f } };
		dl->AddPolyline(p, 6, col, ImDrawFlags_Closed, w);
		dl->AddLine({ c.x, c.y - r * 0.55f }, { c.x, c.y + r * 0.55f }, col, w);
		dl->AddLine({ c.x - r * 0.4f, c.y - r * 0.15f }, { c.x + r * 0.4f, c.y - r * 0.15f }, col, w);
		break;
	}
	case 211:   // the Armory: crossed blades
		dl->AddLine({ c.x - r, c.y + r }, { c.x + r * 0.9f, c.y - r * 0.9f }, col, w);
		dl->AddLine({ c.x + r, c.y + r }, { c.x - r * 0.9f, c.y - r * 0.9f }, col, w);
		dl->AddLine({ c.x - r * 0.55f, c.y + r * 0.15f }, { c.x - r * 0.15f, c.y + r * 0.55f }, col, w);
		dl->AddLine({ c.x + r * 0.55f, c.y + r * 0.15f }, { c.x + r * 0.15f, c.y + r * 0.55f }, col, w);
		break;
	case 212: {   // Brews: a flask
		const ImVec2 p[6] = { { c.x - r * 0.25f, c.y - r }, { c.x - r * 0.25f, c.y - r * 0.3f }, { c.x - r * 0.8f, c.y + r * 0.75f }, { c.x + r * 0.8f, c.y + r * 0.75f }, { c.x + r * 0.25f, c.y - r * 0.3f }, { c.x + r * 0.25f, c.y - r } };
		dl->AddPolyline(p, 6, col, 0, w);
		dl->AddLine({ c.x - r * 0.55f, c.y + r * 0.3f }, { c.x + r * 0.55f, c.y + r * 0.3f }, col, w);
		dl->AddLine({ c.x - r * 0.45f, c.y - r }, { c.x + r * 0.45f, c.y - r }, col, w);
		break;
	}
	case 213:   // Thu'um: the voice's waves
		for (int q = 0; q < 3; ++q) {
			const float yy = c.y - r * 0.6f + q * r * 0.6f;
			ImVec2 p[9];
			for (int i = 0; i < 9; ++i) p[i] = { c.x - r + i * r * 0.25f, yy + std::sin(i * 0.8f + q) * r * 0.22f };
			dl->AddPolyline(p, 9, col, 0, w);
		}
		break;
	case 214:   // Trials: a dial
		dl->AddCircle(c, r * 0.85f, col, 24, w);
		dl->AddLine(c, { c.x, c.y - r * 0.5f }, col, w);
		dl->AddLine(c, { c.x + r * 0.35f, c.y + r * 0.3f }, col, w);
		break;
	case 217: {   // Talk: a word-box
		const ImVec2 p[5] = { { c.x - r, c.y - r * 0.75f }, { c.x + r, c.y - r * 0.75f }, { c.x + r, c.y + r * 0.35f }, { c.x - r * 0.25f, c.y + r * 0.35f }, { c.x - r * 0.75f, c.y + r * 0.85f } };
		dl->AddPolyline(p, 5, col, 0, w);
		dl->AddLine({ c.x - r * 0.75f, c.y + r * 0.85f }, { c.x - r * 0.75f, c.y - r * 0.75f }, col, w);
		dl->AddLine({ c.x - r * 0.45f, c.y - r * 0.3f }, { c.x + r * 0.45f, c.y - r * 0.3f }, col, w);
		break;
	}
	default:   // Whispers: a spiral of three
		for (int q = 0; q < 3; ++q) dl->AddCircleFilled({ c.x + std::cos(q * 2.094f) * r * 0.55f, c.y + std::sin(q * 2.094f) * r * 0.55f }, r * 0.2f, col, 10);
		dl->AddCircle(c, r * 0.95f, Fade(col, 0.5f), 24, w * 0.7f);
		break;
	}
}
const char* TallyTabName(int a_sec)
{
	switch (a_sec) {
	case 210: return "YOUR KIT";
	case 211: return "ARMORY";
	case 212: return "BREWS";
	case 213: return "THU'UM";
	case 214: return "TRIALS";
	case 217: return "TALK";
	default: return "WHISPERS";
	}
}
// the class paintings (Lore-Based Loading Screens, from the player's own install, never shipped)
ID3D11ShaderResourceView* ClassPainting(int a_cls)
{
	static constexpr const wchar_t* kPic[4] = { L"Data\\Textures\\loreloadscreens\\MagicConjurer.dds", L"Data\\Textures\\loreloadscreens\\MagicSchoolDestruction.dds",
		L"Data\\Textures\\loreloadscreens\\SkillAlchemyAlchemist.dds", L"Data\\Textures\\loreloadscreens\\MagicBattlemage.dds" };
	static ID3D11ShaderResourceView* pic[4]{};
	static bool tried[4]{};
	if (a_cls < 0 || a_cls > 3) return nullptr;
	if (!tried[a_cls] && g_device) {
		tried[a_cls] = true;
		if (FAILED(DirectX::CreateDDSTextureFromFile(g_device, kPic[a_cls], nullptr, &pic[a_cls]))) pic[a_cls] = nullptr;
	}
	return pic[a_cls];
}
void TallyAltar(ImDrawList* dl, const Hud::State& s, ImVec2 size, float now)
{
	const float u = TU(size);
	const float dt = ImGui::GetIO().DeltaTime;
	static float openT = 0.0f, lastSeen = -10.0f;
	if (now - lastSeen > 0.5f) openT = 0.0f;
	lastSeen = now, openT += dt;
	const float fade = std::clamp(openT / 0.35f, 0.0f, 1.0f);
	// the world dimmed behind it (radial: darker at the edges)
	dl->AddRectFilled({ 0, 0 }, size, IM_COL32(6, 5, 4, static_cast<int>(140 * fade)));
	const float e = size.y * 0.3f;
	const ImU32 edge = IM_COL32(6, 5, 4, static_cast<int>(70 * fade)), clr = IM_COL32(6, 5, 4, 0);
	dl->AddRectFilledMultiColor({ 0, 0 }, { size.x, e }, edge, edge, clr, clr);
	dl->AddRectFilledMultiColor({ 0, size.y - e }, size, clr, clr, edge, edge);
	const int cls = Classes::Get();
	// its tabs (as Shops orders them)
	std::vector<int> tabs;
	if (cls >= 0) {
		tabs.push_back(210);
		if (!s.armoryLines.empty()) tabs.push_back(211);
	}
	tabs.push_back(212);
	tabs.push_back(213);
	if (s.trialsOn) tabs.push_back(214);
	if (s.talkOn) tabs.push_back(217);
	if (!s.whispers.empty()) tabs.push_back(215);
	const int open = std::find(tabs.begin(), tabs.end(), s.altarOpen) != tabs.end() ? s.altarOpen : tabs.front();
	static int lastOpen = -1;
	static float tabT = 1.0f;
	if (open != lastOpen) lastOpen = open, tabT = 0.0f;
	tabT = std::min(1.0f, tabT + dt / 0.22f);
	const float bodyA = fade * (0.25f + 0.75f * tabT);
	const ImVec2 A{ size.x * 0.08f, size.y * 0.12f }, B{ size.x * 0.92f, size.y * 0.84f };
	// over the panel: THE ECHO ALTAR, your path, CURSE LIFTED
	{
		std::string head = "THE ECHO ALTAR";
		if (!s.trialCity.empty()) head += "  \xC2\xB7  " + Upper(s.trialCity);
		TText(dl, HF(), 1.0f * u, { A.x, A.y - 1.7f * u }, Fade(kTInk, fade), head, 0.26f);
		std::string right = cls >= 0 ? Upper(std::string(Classes::Name(cls))) + "'S ALTAR" : std::string("CHOOSE YOUR PATH");
		if (s.altarFreed) right = "CURSE LIFTED  \xC2\xB7  " + right;
		TTextR(dl, HF(), 0.7f * u, B.x, A.y - 1.4f * u, Fade(s.altarFreed ? kTGold : kTLab, fade), right, 0.3f);
	}
	dl->AddRectFilled({ A.x, A.y + 2 * u }, { B.x, B.y + 2 * u }, IM_COL32(0, 0, 0, static_cast<int>(90 * fade)));   // (its shadow)
	dl->AddRectFilled(A, B, IM_COL32(12, 7, 4, static_cast<int>(184 * fade)));
	dl->AddRect(A, B, IM_COL32(232, 112, 63, static_cast<int>(89 * fade)), 0.0f, 0, 0.08f * u);
	// the tabs
	const float tabH = 2.9f * u;
	{
		float x = A.x + 1.0f * u;
		// (the keys that step through them, at both ends)
		auto key = [&](float kx, const char* a_kb, const char* a_pad) {
			if (g_padLast) PadGlyph(dl, { kx + 0.7f * u, A.y + tabH * 0.5f }, 0.6f * u, a_pad, fade);
			else {
				dl->AddRect({ kx, A.y + tabH * 0.5f - 0.65f * u }, { kx + 1.3f * u, A.y + tabH * 0.5f + 0.65f * u }, Fade(kTGold, 0.8f * fade), 0.2f * u, 0, 0.07f * u);
				TTextC(dl, HFB(), 0.75f * u, kx + 0.65f * u, A.y + tabH * 0.5f - 0.45f * u, Fade(kTInk, fade), a_kb);
			}
		};
		key(x, "Q", "LB");
		x += 2.0f * u;
		for (int t : tabs) {
			const bool on = t == open;
			const std::string nm = TallyTabName(t);
			const float tw = 1.3f * u + 0.5f * u + TMeasure(HF(), 0.78f * u, nm, 0.2f).x + 2.4f * u;
			if (on) {
				dl->AddRectFilledMultiColor({ x, A.y }, { x + tw, A.y + tabH }, IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, 0), IM_COL32(255, 122, 60, static_cast<int>(52 * fade)), IM_COL32(255, 122, 60, static_cast<int>(52 * fade)));
				dl->AddRectFilled({ x, A.y + tabH - 0.22f * u }, { x + tw, A.y + tabH }, Fade(kTAcc, fade));
			}
			const ImVec2 ic{ x + 1.2f * u + 0.65f * u, A.y + tabH * 0.5f };
			if (on) dl->AddCircleFilled(ic, 1.0f * u, IM_COL32(255, 122, 60, static_cast<int>(40 * fade)), 20);
			TallyTabIcon(dl, t, ic, 0.62f * u, Fade(on ? kTAcc : kTTxt, fade));
			TText(dl, HF(), 0.78f * u, { x + 1.2f * u + 1.3f * u + 0.5f * u, A.y + tabH * 0.5f - 0.5f * u }, Fade(on ? kTInk : kTTxt, fade), nm, 0.2f);
			x += tw + 0.2f * u;
		}
		key(B.x - 2.3f * u, "E", "RB");
		dl->AddRectFilled({ A.x, A.y + tabH }, { B.x, A.y + tabH + 0.08f * u }, IM_COL32(232, 112, 63, static_cast<int>(89 * fade)));
	}
	// the body
	const float L0 = A.x + 2.0f * u, R0 = B.x - 2.0f * u, top = A.y + tabH + 1.6f * u, foot = B.y - 1.4f * u;
	static float scroll = 0.0f, scrollGoal = 0.0f;
	if (tabT < 0.05f) scroll = scrollGoal = 0.0f;
	scroll += (scrollGoal - scroll) * std::min(1.0f, dt * 10.0f);
	dl->PushClipRect({ A.x, A.y + tabH + 0.1f * u }, { B.x, foot - 2.4f * u }, true);
	const float slide = (1.0f - EaseOut(tabT)) * 0.8f * u;
	float focusY0 = -1, focusY1 = -1;
	const auto rowBg = [&](int a_row, float x0, float y0, float x1, float y1, bool a_lock = false) {
		const bool on = s.altarFocus == a_row;
		if (on) {
			focusY0 = y0, focusY1 = y1;
			dl->AddRectFilledMultiColor({ x0, y0 }, { x1, y1 }, IM_COL32(255, 122, 60, static_cast<int>(77 * bodyA)), IM_COL32(0, 0, 0, static_cast<int>(51 * bodyA)), IM_COL32(0, 0, 0, static_cast<int>(51 * bodyA)), IM_COL32(255, 122, 60, static_cast<int>(77 * bodyA)));
			dl->AddRectFilled({ x0, y0 }, { x0 + 0.22f * u, y1 }, Fade(kTAcc, bodyA));
		} else {
			dl->AddRectFilled({ x0, y0 }, { x1, y1 }, IM_COL32(0, 0, 0, static_cast<int>((a_lock ? 40 : 71) * bodyA)));
		}
		return on;
	};
	const auto pips = [&](float x, float y, int n, int of, ImU32 col) {   // its tier as diamonds
		for (int q = 0; q < of; ++q) {
			const ImVec2 c{ x + q * 1.0f * u + 0.35f * u, y + 0.35f * u };
			const float r = 0.35f * u;
			if (q < n) dl->AddQuadFilled({ c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y }, Fade(col, bodyA));
			dl->AddQuad({ c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y }, Fade(kTGold, bodyA), 0.08f * u);
		}
	};
	auto rgb = [](std::uint32_t c) { return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255); };
	float y = top - scroll + slide;
	auto price = [&](float rx, float py, int cost, bool can) {
		TTextR(dl, HF(), 0.9f * u, rx, py, Fade(can ? kTGold : IM_COL32(200, 90, 70, 255), bodyA), Num(cost));
	};
	auto line = [&](int a_row, float x0, float x1, ImU32 a_dot, const std::string& a_name, const std::string& a_sub, const std::string& a_right, ImU32 a_rightCol, int a_tier = -1, bool a_lock = false) {
		const float h = (a_sub.empty() ? 2.2f : 3.0f) * u;
		rowBg(a_row, x0, y, x1, y + h, a_lock);
		const float tx = x0 + 1.0f * u + (a_dot ? 1.6f * u : 0.0f);
		if (a_dot) {
			const ImVec2 c{ x0 + 1.6f * u, y + h * 0.5f };
			dl->AddCircleFilled(c, 0.75f * u, Fade(a_dot, 0.25f * bodyA), 20);
			const float r = 0.45f * u;
			dl->AddQuadFilled({ c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y }, Fade(a_dot, bodyA));
		}
		const float al = a_lock ? 0.45f : 1.0f;
		TText(dl, HF(), 1.0f * u, { tx, y + (a_sub.empty() ? 0.55f : 0.35f) * u }, Fade(kTInk, bodyA * al), a_name, 0.03f);
		if (a_tier >= 0) pips(tx + TMeasure(HF(), 1.0f * u, a_name, 0.03f).x + 0.8f * u, y + 0.55f * u, a_tier, 3, a_dot ? a_dot : kTAcc);
		if (!a_sub.empty()) TText(dl, Inter(), 0.62f * u, { tx, y + 1.65f * u }, Fade(kTTxt, bodyA * al), a_sub);
		if (!a_right.empty()) TTextR(dl, HF(), 0.9f * u, x1 - 1.0f * u, y + h * 0.5f - 0.6f * u, Fade(a_rightCol, bodyA * al), a_right);
		y += h + 0.5f * u;
	};
	if (open == 210 || cls < 0) {
		// Your Kit: the class's painting at the left, your class and kit at the right
		const float pw = 20.0f * u, px0 = L0, py0 = y, py1 = std::min(foot - 3.0f * u, py0 + 26.0f * u);
		if (cls >= 0) {
			if (auto* pic = ClassPainting(cls)) {
				// (cover: cropped to the panel's tall shape, its figure kept)
				static constexpr float kFocusX[4] = { 0.44f, 0.50f, 0.50f, 0.42f };
				const float ar = (pw / (py1 - py0)) / (16.0f / 9.0f);
				const float u0 = std::clamp(kFocusX[cls] - ar * 0.5f, 0.0f, 1.0f - ar);
				dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(pic)), { px0, py0 }, { px0 + pw, py1 }, { u0, 0 }, { u0 + ar, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * bodyA)));
			} else {
				dl->AddRectFilled({ px0, py0 }, { px0 + pw, py1 }, IM_COL32(30, 20, 14, static_cast<int>(220 * bodyA)));
			}
			const float sh = 8.0f * u;
			dl->AddRectFilledMultiColor({ px0, py1 - sh }, { px0 + pw, py1 }, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, static_cast<int>(217 * bodyA)), IM_COL32(0, 0, 0, static_cast<int>(217 * bodyA)));
			dl->AddRect({ px0, py0 }, { px0 + pw, py1 }, IM_COL32(232, 112, 63, static_cast<int>(89 * bodyA)), 0.0f, 0, 0.08f * u);
			TText(dl, HFB(), 1.5f * u, { px0 + 1.2f * u, py1 - 3.4f * u }, Fade(kTInk, bodyA), Upper(std::string(Classes::Name(cls))), 0.2f);
			int tier = 0;
			for (const auto& l : s.kitLines) tier = std::max(tier, l.tier);
			TText(dl, Inter(), 0.7f * u, { px0 + 1.2f * u, py1 - 1.5f * u }, Fade(kTTxt, bodyA), fmt::format("Tier {}", tier <= 0 ? std::string("I") : tier == 1 ? std::string("I") : tier == 2 ? std::string("II") : std::string("III")));
		}
		const float lx0 = cls >= 0 ? px0 + pw + 2.0f * u : L0, lx1 = R0;
		line(30, lx0, lx1, 0, cls < 0 ? std::string("Choose your class") : "Class: " + std::string(Classes::Name(cls)), cls < 0 ? std::string("Summoner, Destruction, Alchemist or Spellsword - free, your first path") : std::string("Change class - your gear and spells stay with you"),
			cls >= 0 ? Num(Classes::SwitchCost()) : std::string("Choose"), cls >= 0 && s.points >= Classes::SwitchCost() ? kTGold : cls < 0 ? kTAcc : IM_COL32(200, 90, 70, 255));
		for (const auto& l : s.kitLines) {
			const bool all = l.tier < 0;
			const std::string right = !l.carried ? std::string(all ? "Claim all" : "Claim") : l.cost > 0 ? (l.cost > 0 ? Num(l.cost) : std::string()) : std::string();
			line(l.id, lx0, lx1, all ? 0 : rgb(l.color), l.name, l.info, right, !l.carried ? kTAcc : l.can ? kTGold : IM_COL32(200, 90, 70, 255), all ? -1 : std::clamp(l.tier, 0, 3));
		}
		if (cls >= 0) {   // the dragon glider (its bonds)
			static constexpr const char* kBond[4] = { "", "Wind-Rider", "Talon Strike", "Alduin's Thrall" };
			const int have = std::clamp(s.dragonBond, 0, 3);
			const bool maxed = have >= 3;
			const std::string nm = s.bondLocked ? std::string("Dragon Glider - locked") : maxed ? std::string("Dragon Bond: every bond made") : "Dragon Bond " + std::string(have == 0 ? "I" : have == 1 ? "II" : "III") + ": " + kBond[have + 1];
			const std::string sub = s.bondLocked ? fmt::format("Free Whiterun's Echo Altar to unlock it - bond {} of 3", have) : maxed ? std::string("It is yours to the end") : fmt::format("Bond {} of 3", have);
			const bool can = !maxed && !s.bondLocked && s.points >= Movement::kBondCost[std::min(have, 2)];
			line(5, lx0, lx1, IM_COL32(120, 190, 255, 255), nm, sub, maxed || s.bondLocked ? std::string() : Num(Movement::kBondCost[have]), can ? kTGold : IM_COL32(200, 90, 70, 255), have, s.bondLocked);
		}
		if (cls >= 0) {   // your brews, as their icons
			float bx = lx0;
			bool any = false;
			for (std::size_t q = 0; q < gen::kPerks.size() && q < s.perkStacks.size(); ++q) {
				if (s.perkStacks[q] <= 0) continue;
				any = true;
				if (auto* icon = g_perkIcons[q]) {
					dl->AddCircleFilled({ bx + 1.2f * u, y + 1.2f * u }, 1.45f * u, IM_COL32(255, 122, 60, static_cast<int>(40 * bodyA)), 24);
					dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { bx, y }, { bx + 2.4f * u, y + 2.4f * u }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * bodyA)));
				}
				bx += 3.0f * u;
			}
			if (any) TText(dl, Inter(), 0.62f * u, { bx + 0.4f * u, y + 0.8f * u }, Fade(kTTxt, bodyA), "Your brews");
			y += 3.2f * u;
		}
	} else if (open == 211) {   // the Armory: each with its colour, its tier as pips, the cost; locked ones faint
		for (const auto& l : s.armoryLines) {
			const bool lock = l.cost <= 0;
			line(l.id, L0, R0, rgb(l.color), l.name, l.info, lock ? std::string("Locked") : Num(l.cost), lock ? kTTxt : l.can ? kTGold : IM_COL32(200, 90, 70, 255), std::clamp(l.tier, 0, 3), lock);
		}
	} else if (open == 212) {   // Brews: every brew with its own icon, what it does, its price; the ones you have glow
		const auto menu = Perks::Menu();
		int owned = 0;
		for (std::size_t q = 0; q < gen::kPerks.size() && q < s.perkStacks.size(); ++q) owned += s.perkStacks[q] > 0 ? 1 : 0;
		const std::string rule = owned >= s.perkSlots ? (s.perkSlots < gen::kMaxPerks ? "All open slots full - more open with the nights" : "All slots full - refund one to take another") : fmt::format("{} of {} held - kept through death; select one you have to refund half", owned, s.perkSlots);
		TText(dl, Inter(), 0.7f * u, { L0, y }, Fade(kTTxt, bodyA), rule);
		y += 1.5f * u;
		const bool rowOn = s.altarFocus == 3;
		if (rowOn) focusY0 = y, focusY1 = y + 12 * u;
		const int cols = 5;
		const float gap = 0.8f * u, cw = (R0 - L0 - gap * (cols - 1)) / cols, ch = 9.6f * u;
		const int sel = menu.empty() ? -1 : menu[std::clamp(s.perkSel, 0, static_cast<int>(menu.size()) - 1)];
		for (int j = 0; j < static_cast<int>(menu.size()); ++j) {
			const int q = menu[j];
			const float x0 = L0 + (j % cols) * (cw + gap), y0 = y + (j / cols) * (ch + gap);
			const bool have = s.perkStacks[q] > 0, on = q == sel;
			if (on && rowOn) focusY0 = y0, focusY1 = y0 + ch;
			dl->AddRectFilled({ x0, y0 }, { x0 + cw, y0 + ch }, IM_COL32(0, 0, 0, static_cast<int>(77 * bodyA)));
			dl->AddRect({ x0, y0 }, { x0 + cw, y0 + ch }, on ? Fade(kTAcc, bodyA) : IM_COL32(232, 112, 63, static_cast<int>(89 * bodyA)), 0.0f, 0, (on ? 0.14f : 0.06f) * u);
			if (on) dl->AddRect({ x0 - 0.3f * u, y0 - 0.3f * u }, { x0 + cw + 0.3f * u, y0 + ch + 0.3f * u }, IM_COL32(255, 122, 60, static_cast<int>(60 * bodyA)), 0.0f, 0, 0.4f * u);
			const ImVec2 ic{ x0 + cw * 0.5f, y0 + 2.6f * u };
			if (have) for (int g = 3; g >= 1; --g) dl->AddCircleFilled(ic, 1.8f * u + g * 0.2f * u, IM_COL32(255, 122, 60, static_cast<int>(26 * bodyA)), 28);
			if (auto* icon = g_perkIcons[q]) dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { ic.x - 1.8f * u, ic.y - 1.8f * u }, { ic.x + 1.8f * u, ic.y + 1.8f * u }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>((have || on ? 255 : 190) * bodyA)));
			TTextC(dl, HFB(), 0.82f * u, ic.x, y0 + 4.8f * u, Fade(kTInk, bodyA), std::string(gen::kPerks[q].label));
			{   // what it does: two short lines at most
				std::vector<std::string> rows(1);
				std::istringstream in{ std::string(gen::kPerkDesc[q]) };
				std::string wd;
				while (in >> wd) {
					if (!rows.back().empty() && Measure(Inter(), 0.58f * u, rows.back() + " " + wd).x > cw - 1.0f * u) { if (rows.size() >= 2) { rows.back() += "..."; break; } rows.emplace_back(); }
					rows.back() += (rows.back().empty() ? "" : " ") + wd;
				}
				for (std::size_t r = 0; r < rows.size(); ++r) TTextC(dl, Inter(), 0.58f * u, ic.x, y0 + 6.0f * u + r * 0.8f * u, Fade(kTTxt, bodyA), rows[r]);
			}
			TTextC(dl, HFB(), 0.8f * u, ic.x, y0 + ch - 1.3f * u, Fade(have ? kTAcc : s.points >= gen::kPerks[q].cost ? kTGold : IM_COL32(200, 90, 70, 255), bodyA), have ? (on ? "Drunk - refund +" + Num(gen::kPerks[q].cost / 2) : std::string("Drunk")) : Num(gen::kPerks[q].cost));
		}
		y += ((static_cast<int>(menu.size()) + cols - 1) / cols) * (ch + gap);
	} else if (open == 213) {   // Thu'um (Noah: "only for the dragon glider shout and the shout that makes everyone tiny")
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* up = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESShout>(gen::kUpdraftShout, gen::kPlugin);
		auto* tiny = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESShout>(gen::kDanceShout, gen::kPlugin);
		struct Sh { const char* name; const char* city; const char* w[3]; const char* how; bool known; };
		const Sh sh[2] = { { "DRAGON'S UPDRAFT", "Whiterun", { "VEN", "GAAR", "NOS" }, "Free Whiterun's Echo Altar: the dragon glider's shout - a whirlwind lifts you, then glide.", p && up && p->HasShout(up) },
			{ "TINY TERRORS", "Solitude", { "MAL", "DIIL", "KROH" }, "Free Solitude: its bards teach it - the dead shrink to a toddler's size and fall to one blow.", p && tiny && p->HasShout(tiny) } };
		const float cw = (R0 - L0 - 2.0f * u) * 0.5f;
		for (int i = 0; i < 2; ++i) {
			const float cx = L0 + cw * 0.5f + i * (cw + 2.0f * u), y0 = y + 1.0f * u;
			dl->AddRectFilled({ cx - cw * 0.5f, y0 }, { cx + cw * 0.5f, y0 + 14.0f * u }, IM_COL32(0, 0, 0, static_cast<int>(60 * bodyA)));
			TTextC(dl, HFB(), 1.8f * u, cx, y0 + 1.6f * u, Fade(kTInk, bodyA), sh[i].name, 0.2f);
			TTextC(dl, Inter(), 0.75f * u, cx, y0 + 4.0f * u, Fade(kTTxt, bodyA), std::string(sh[i].city) + (sh[i].known ? "  \xC2\xB7  learned" : "  \xC2\xB7  not yet learned"));
			for (int w = 0; w < 3; ++w) {
				const float wx = cx + (w - 1) * 5.5f * u;
				const ImU32 wc = sh[i].known ? kTAcc : IM_COL32(120, 106, 90, 255);
				TTextC(dl, HFB(), 2.0f * u, wx, y0 + 6.2f * u, Fade(wc, bodyA), sh[i].w[w], 0.1f, sh[i].known ? IM_COL32(232, 90, 40, 255) : 0, 0.4f * u);
			}
			std::vector<std::string> rows(1);
			std::istringstream in{ std::string(sh[i].how) };
			std::string wd;
			while (in >> wd) {
				if (!rows.back().empty() && Measure(Inter(), 0.8f * u, rows.back() + " " + wd).x > cw - 3.0f * u) rows.emplace_back();
				rows.back() += (rows.back().empty() ? "" : " ") + wd;
			}
			for (std::size_t r = 0; r < rows.size(); ++r) TTextC(dl, Inter(), 0.8f * u, cx, y0 + 9.6f * u + r * 1.2f * u, Fade(kTTxt, bodyA), rows[r]);
		}
		y += 16.0f * u;
	} else if (open == 214) {   // Trials: the four as a path, the one to take next lit; the Dragon Priest after them
		int done = 0;
		for (int q = 0; q < 4; ++q) done += (s.trialDone >> q) & 1;
		const bool full = done >= 4 || s.trialActive == 6;
		TTextC(dl, Inter(), 0.75f * u, (L0 + R0) * 0.5f, y, Fade(s.trialLock.empty() ? kTTxt : IM_COL32(200, 120, 100, 255), bodyA),
			!s.trialLock.empty() ? "Locked - " + s.trialLock : std::string(Party::FriendCount() > 0 ? "Four trials, shared with your party, one after another, at night" : "Four trials, one after another, at night"));
		y += 2.0f * u;
		static constexpr const char* kRoman[4] = { "I", "II", "III", "IV" };
		const float sw = 10.0f * u, gap = 3.0f * u, total = sw * 4 + gap * 3, x0 = (L0 + R0) * 0.5f - total * 0.5f;
		for (int q = 0; q < 4; ++q) {
			const float cx = x0 + sw * 0.5f + q * (sw + gap);
			const bool dn = (s.trialDone >> q) & 1, active = s.trialActive == q + 1, next = !dn && !active && !full && q == s.trialNext;
			const bool on = s.altarFocus == 10 + q;
			if (q < 3) dl->AddRectFilled({ cx + 2.2f * u, y + 1.65f * u }, { cx + sw * 0.5f + gap + sw * 0.5f - 2.2f * u, y + 1.75f * u }, IM_COL32(232, 112, 63, static_cast<int>(89 * bodyA)));
			const ImVec2 c{ cx, y + 1.7f * u };
			if (on) { focusY0 = y - 0.5f * u, focusY1 = y + 9.0f * u; dl->AddRectFilled({ cx - sw * 0.5f, y - 0.5f * u }, { cx + sw * 0.5f, y + 9.0f * u }, IM_COL32(255, 122, 60, static_cast<int>(40 * bodyA))); }
			if (dn) dl->AddCircleFilled(c, 1.7f * u, IM_COL32(241, 200, 110, static_cast<int>(89 * bodyA)), 32);
			if (next || active) { dl->AddCircle(c, 1.7f * u + 0.1f * u, Fade(kTAcc, bodyA), 32, 0.16f * u); dl->AddCircleFilled(c, 2.6f * u, IM_COL32(255, 122, 60, static_cast<int>(30 * bodyA)), 32); }
			else if (!dn) dl->AddCircle(c, 1.7f * u, IM_COL32(220, 207, 185, static_cast<int>(90 * bodyA)), 32, 0.08f * u);
			const float al = dn || next || active ? 1.0f : 0.45f;
			TTextC(dl, g_title, 1.0f * u, c.x, c.y - 0.6f * u, Fade(next || active ? kTAcc : kTInk, bodyA * al), kRoman[q]);
			TTextC(dl, HFB(), 0.78f * u, cx, y + 4.0f * u, Fade(kTInk, bodyA * al), Curse::TrialTitle(q));
			TTextC(dl, Inter(), 0.58f * u, cx, y + 5.1f * u, Fade(kTTxt, bodyA * al), dn ? std::string("Done") : active ? std::string("Under way") : next ? std::string("Next - tonight") : std::string("Locked"));
			std::vector<std::string> rows(1);
			std::istringstream in{ s.trialDesc[q] };
			std::string wd;
			while (in >> wd) {
				if (!rows.back().empty() && Measure(Inter(), 0.56f * u, rows.back() + " " + wd).x > sw) { if (rows.size() >= 3) { rows.back() += "..."; break; } rows.emplace_back(); }
				rows.back() += (rows.back().empty() ? "" : " ") + wd;
			}
			for (std::size_t r = 0; r < rows.size(); ++r) TTextC(dl, Inter(), 0.56f * u, cx, y + 6.2f * u + r * 0.8f * u, Fade(kTTxt, bodyA * al * 0.9f), rows[r]);
		}
		y += 10.0f * u;
		const bool bossUp = s.trialActive == 6;
		TTextC(dl, HFB(), 0.95f * u, (L0 + R0) * 0.5f, y, Fade(bossUp ? IM_COL32(255, 110, 80, 255) : kTInk, bodyA), "THE DRAGON PRIEST: " + Upper(s.trialBoss), 0.16f);
		TTextC(dl, Inter(), 0.7f * u, (L0 + R0) * 0.5f, y + 1.4f * u, Fade(kTTxt, bodyA), s.trialBossLine);
		y += 3.0f * u;
	} else if (open == 217) {   // Talk: Felldir's topics, each with how many you haven't asked; what you can ask under the open one
		static constexpr const char* kTopic[5] = { "", "The story so far", "This city", "Your path and wonder", "Who the voice is" };
		for (int g = 1; g <= 4; ++g) {
			const bool any = s.talkAny[g], op = s.talkTopic == g && any;
			const std::string nm = g == 4 && s.whisperNamed ? std::string("Who Felldir is") : std::string(kTopic[g]);
			line(500 + g, L0, R0, 0, (op ? "- " : "+ ") + nm, std::string(), !any ? std::string("nothing yet") : s.talkNew[g] ? fmt::format("{} new", s.talkNew[g]) : std::string("all asked"), s.talkNew[g] ? kTAcc : kTTxt, -1, !any);
			if (!op) continue;
			for (int q = 0; q < static_cast<int>(s.talkRows.size()); ++q) {
				const auto& tr = s.talkRows[q];
				const bool saying = s.talkSaying == tr.prompt;
				line(600 + q, L0 + 2.0f * u, R0, 0, "\"" + tr.ask + "\"", std::string(), saying ? std::string(s.whisperNamed ? "Felldir speaks..." : "The voice speaks...") : tr.heard ? std::string() : std::string("new"), saying ? IM_COL32(170, 200, 245, 255) : kTAcc);
			}
		}
	} else {   // Whispers: the voice, then the four wonders, each opening to its cards
		for (int g = 0; g < 5; ++g) {
			int n = 0;
			for (const auto& w : s.whispers) n += w.group == g ? 1 : 0;
			const bool op = s.whisperGroup == g && n > 0;
			line(400 + g, L0, R0, 0, (op ? "- " : "+ ") + Eggs::GroupName(g), std::string(), n == 0 ? std::string("not yet heard") : fmt::format("{} heard", n), kTTxt, -1, n == 0);
			if (!op) continue;
			for (int wi = 0; wi < static_cast<int>(s.whispers.size()); ++wi) {
				const auto& w = s.whispers[wi];
				if (w.group != g) continue;
				const bool saying = s.whisperSpeaking == w.id;
				line(300 + wi, L0 + 2.0f * u, R0, 0, w.title, saying ? std::string(s.whisperNamed ? "Felldir speaks..." : "The voice speaks...") : w.info, saying ? std::string("speaking") : std::string(), IM_COL32(170, 200, 245, 255));
			}
		}
	}
	const float contentH = y + scroll - slide - top;
	dl->PopClipRect();
	// keep the row in focus in view
	const float viewTop = top, viewBot = foot - 3.0f * u;
	if (focusY1 > viewBot) scrollGoal += focusY1 - viewBot;
	if (focusY0 >= 0 && focusY0 < viewTop) scrollGoal -= viewTop - focusY0;
	scrollGoal = std::clamp(scrollGoal, 0.0f, std::max(0.0f, contentH - (viewBot - viewTop)));
	// leave
	{
		const float ly = foot - 2.2f * u;
		const bool on = s.altarFocus == 20;
		if (on) {
			dl->AddRectFilledMultiColor({ L0, ly - 0.3f * u }, { L0 + 16.0f * u, ly + 1.9f * u }, IM_COL32(255, 122, 60, static_cast<int>(77 * fade)), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(255, 122, 60, static_cast<int>(77 * fade)));
			dl->AddRectFilled({ L0, ly - 0.3f * u }, { L0 + 0.22f * u, ly + 1.9f * u }, Fade(kTAcc, fade));
		}
		TText(dl, HF(), 0.95f * u, { L0 + 0.8f * u, ly }, Fade(on ? kTInk : kTTxt, fade), "LEAVE THE ALTAR", 0.2f);
		TTextR(dl, HFB(), 1.0f * u, R0, ly - 0.1f * u, Fade(kTGold, fade), Num(s.points) + "  CURSED ESSENCE", 0.04f);
	}
}
