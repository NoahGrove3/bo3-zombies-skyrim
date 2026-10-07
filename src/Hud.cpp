#include "BO3Z.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <WICTextureLoader.h>

// BO3-style HUD (sheet: hud), drawn with Dear ImGui over Skyrim's frame from a Present hook
// (the hook itself follows SkyCraft's Overlay.cpp, MIT, chasmlol).
namespace bo3z
{
	namespace
	{
		using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
		PresentFn            g_present = nullptr;
		std::mutex           g_lock;
		Hud::State           g_state;
		bool                 g_init = false, g_failed = false;
		ID3D11Device*        g_device = nullptr;
		ID3D11DeviceContext* g_context = nullptr;
		ImFont*              g_font = nullptr;
		std::array<ID3D11ShaderResourceView*, gen::kPerks.size()> g_perkIcons{};
		auto                 g_last = std::chrono::steady_clock::now();

		ImU32 Col(std::uint32_t a_rgba, float a_alpha = 1.0f)
		{
			const auto a = static_cast<std::uint32_t>(((a_rgba & 0xFF) / 255.0f) * std::clamp(a_alpha, 0.0f, 1.0f) * 255.0f);
			return IM_COL32((a_rgba >> 24) & 0xFF, (a_rgba >> 16) & 0xFF, (a_rgba >> 8) & 0xFF, a);
		}

		ImVec2 Anchor(const gen::HudItem& a_item, ImVec2 a_size, float a_scale)
		{
			const float ox = a_item.ox * a_scale, oy = a_item.oy * a_scale;
			switch (a_item.anchor) {
			case 0: return { ox, a_size.y + oy };                   // bottom_left
			case 1: return { a_size.x + ox, a_size.y + oy };        // bottom_right
			case 2: return { a_size.x * 0.5f + ox, a_size.y * 0.5f + oy };  // center
			case 4: return { a_size.x * 0.5f + ox, a_size.y + oy };  // bottom_center
			default: return { a_size.x * 0.5f + ox, oy };           // top_center
			}
		}

		void Text(ImDrawList* dl, const gen::HudItem& it, ImVec2 size, float scale, const std::string& s, float align, float alpha = 1.0f, std::uint32_t rgba = 0)
		{
			const float px = it.fontPx * scale;
			const auto  ts = g_font ? g_font->CalcTextSizeA(px, FLT_MAX, 0.0f, s.c_str()) : ImGui::CalcTextSize(s.c_str());
			auto        p = Anchor(it, size, scale);
			p.x -= ts.x * align;
			p.y -= ts.y * 0.5f;
			dl->AddText(g_font, px, { p.x + 2 * scale, p.y + 2 * scale }, IM_COL32(0, 0, 0, static_cast<int>(160 * alpha)), s.c_str());
			dl->AddText(g_font, px, p, Col(rgba ? rgba : it.rgba, alpha), s.c_str());
		}

		// ---- Undying Nights HUD: dark glass panels, gold trim, serif titles, soft glows ----
		ImFont* g_title = nullptr;  // Georgia Bold
		ImFont* g_num = nullptr;    // Segoe UI Semibold
		ImFont* g_body = nullptr;   // Segoe UI

		constexpr ImU32 kGold = IM_COL32(214, 178, 96, 255);
		constexpr ImU32 kGoldDim = IM_COL32(214, 178, 96, 110);
		constexpr ImU32 kBlood = IM_COL32(178, 22, 22, 255);
		constexpr ImU32 kParch = IM_COL32(236, 226, 200, 255);
		constexpr ImU32 kGlass = IM_COL32(10, 10, 12, 170);

		ImU32 Fade(ImU32 c, float a) { return (c & 0x00FFFFFF) | (static_cast<ImU32>(((c >> 24) & 0xFF) * std::clamp(a, 0.0f, 1.0f)) << 24); }

		ImVec2 Measure(ImFont* f, float px, const std::string& s) { return f ? f->CalcTextSizeA(px, FLT_MAX, 0, s.c_str()) : ImVec2{ px * s.size() * 0.5f, px }; }

		// Text with a soft dark shadow; glow > 0 adds a coloured halo.
		void Txt(ImDrawList* dl, ImFont* f, float px, ImVec2 p, ImU32 col, const std::string& s, float glow = 0, ImU32 glowCol = 0)
		{
			if (glow > 0) {
				for (int i = 0; i < 8; ++i) {
					const float a = i * 0.785f;
					dl->AddText(f, px, { p.x + std::cos(a) * glow, p.y + std::sin(a) * glow }, Fade(glowCol, 0.18f), s.c_str());
				}
			}
			dl->AddText(f, px, { p.x + px * 0.04f, p.y + px * 0.05f }, IM_COL32(0, 0, 0, static_cast<int>(((col >> 24) & 0xFF) * 0.7f)), s.c_str());
			dl->AddText(f, px, p, col, s.c_str());
		}
		void TxtC(ImDrawList* dl, ImFont* f, float px, float cx, float y, ImU32 col, const std::string& s, float glow = 0, ImU32 glowCol = 0)
		{
			Txt(dl, f, px, { cx - Measure(f, px, s).x * 0.5f, y }, col, s, glow, glowCol);
		}

		// Panel: translucent dark glass with a thin gold frame and corner ticks.
		void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float k, float alpha = 1.0f)
		{
			dl->AddRectFilledMultiColor(a, b, Fade(kGlass, alpha), Fade(kGlass, alpha), Fade(IM_COL32(4, 4, 6, 210), alpha), Fade(IM_COL32(4, 4, 6, 210), alpha));
			dl->AddRect(a, b, Fade(kGoldDim, alpha), 0, 0, 1.2f * k);
			const float t = 10 * k;
			for (auto [x, y, sx, sy] : { std::tuple{ a.x, a.y, 1.f, 1.f }, { b.x, a.y, -1.f, 1.f }, { a.x, b.y, 1.f, -1.f }, { b.x, b.y, -1.f, -1.f } }) {
				dl->AddLine({ x, y }, { x + sx * t, y }, Fade(kGold, alpha), 2 * k);
				dl->AddLine({ x, y }, { x, y + sy * t }, Fade(kGold, alpha), 2 * k);
			}
		}

		// Ornamental divider: line - diamond - line.
		void Divider(ImDrawList* dl, float cx, float y, float half, float k, float alpha = 1.0f)
		{
			dl->AddLine({ cx - half, y }, { cx - 8 * k, y }, Fade(kGoldDim, alpha), 1.2f * k);
			dl->AddLine({ cx + 8 * k, y }, { cx + half, y }, Fade(kGoldDim, alpha), 1.2f * k);
			dl->AddQuadFilled({ cx, y - 5 * k }, { cx + 5 * k, y }, { cx, y + 5 * k }, { cx - 5 * k, y }, Fade(kGold, alpha));
		}

		// Slim stat bar with a lagging "ghost" that shows recent loss.
		void Bar(ImDrawList* dl, ImVec2 a, float w, float h, float frac, float ghost, ImU32 col, float k)
		{
			const ImVec2 b{ a.x + w, a.y + h };
			dl->AddRectFilled({ a.x - 2 * k, a.y - 2 * k }, { b.x + 2 * k, b.y + 2 * k }, IM_COL32(0, 0, 0, 150), 3 * k);
			dl->AddRectFilled(a, { a.x + w * std::clamp(ghost, 0.0f, 1.0f), b.y }, IM_COL32(255, 240, 200, 90), 2 * k);
			const ImU32 top = Fade(col, 1.0f), bot = Fade(IM_COL32((col & 0xFF) * 6 / 10, ((col >> 8) & 0xFF) * 6 / 10, ((col >> 16) & 0xFF) * 6 / 10, 255), 1.0f);
			dl->AddRectFilledMultiColor(a, { a.x + w * std::clamp(frac, 0.0f, 1.0f), b.y }, top, top, bot, bot);
			dl->AddRect({ a.x - 2 * k, a.y - 2 * k }, { b.x + 2 * k, b.y + 2 * k }, kGoldDim, 3 * k, 0, 1.0f * k);
		}

		std::string Roman(int n)
		{
			if (n <= 0 || n > 39) {
				return std::to_string(n);
			}
			static const char* t[] = { "", "X", "XX", "XXX" };
			static const char* o[] = { "", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX" };
			return std::string(t[n / 10]) + o[n % 10];
		}

		void Draw(const Hud::State& s, ImVec2 size)
		{
			auto*       dl = ImGui::GetForegroundDrawList();
			const float k = size.y / 1080.0f;
			const float now = static_cast<float>(ImGui::GetTime());
			static int   shownRound = 0;
			static float roundFlash = 0;
			static float hpGhost = 1, mpGhost = 1, spGhost = 1;
			if (s.round != shownRound) {
				shownRound = s.round, roundFlash = 2.5f;
			}
			const float dt = ImGui::GetIO().DeltaTime;
			roundFlash = std::max(0.0f, roundFlash - dt);

			// Damage vignette: red edges when hurt, stronger when low.
			const float hpFrac = s.hpMax > 0 ? s.hp / s.hpMax : 1.0f;
			const float hurt = std::max(std::clamp(s.hurtTime, 0.0f, 1.0f) * 0.8f, std::clamp((0.4f - hpFrac) / 0.4f, 0.0f, 1.0f) * (0.6f + 0.25f * std::sin(now * 5)));
			if (hurt > 0.01f) {
				const ImU32 r = IM_COL32(150, 0, 0, static_cast<int>(200 * hurt)), z = IM_COL32(150, 0, 0, 0);
				const float e = size.y * 0.28f;
				dl->AddRectFilledMultiColor({ 0, 0 }, { size.x, e }, r, r, z, z);
				dl->AddRectFilledMultiColor({ 0, size.y - e }, size, z, z, r, r);
				dl->AddRectFilledMultiColor({ 0, 0 }, { e, size.y }, r, z, z, r);
				dl->AddRectFilledMultiColor({ size.x - e, 0 }, size, z, r, r, z);
			}

			// Night counter, top-left: Roman numeral in blood red with a pulse when a night begins.
			if (s.round > 0) {
				const float px = (78 + 26 * std::min(1.0f, roundFlash)) * k;
				const ImVec2 p{ 46 * k, 26 * k };
				Txt(dl, g_title, 20 * k, p, Fade(kGold, 0.85f), "NIGHT");
				Txt(dl, g_title, px, { p.x - 2 * k, p.y + 18 * k }, kBlood, Roman(s.round), 3 * k + 6 * k * std::min(1.0f, roundFlash), IM_COL32(255, 40, 20, 255));
			}

			// Points, bottom-left panel.
			{
				const ImVec2 a{ 40 * k, size.y - 196 * k }, b{ 300 * k, size.y - 132 * k };
				Panel(dl, a, b, k);
				Txt(dl, g_body, 15 * k, { a.x + 14 * k, a.y + 7 * k }, Fade(kGold, 0.9f), "SEPTIMS OF THE DEAD");
				Txt(dl, g_num, 34 * k, { a.x + 14 * k, a.y + 22 * k }, kParch, std::to_string(s.points));
				if (s.crew >= 0) {
					const std::string c(gen::kCrew[s.crew].label);
					Txt(dl, g_body, 16 * k, { b.x - Measure(g_body, 16 * k, c).x - 14 * k, a.y + 34 * k }, IM_COL32(190, 190, 190, 220), c);
				}
				if (s.gainTime > 0) {
					const std::string g = (s.lastGain > 0 ? "+" : "") + std::to_string(s.lastGain);
					const float       rise = (1.0f - s.gainTime) * 26 * k;
					Txt(dl, g_num, 26 * k, { b.x + 12 * k, a.y + 18 * k - rise }, Fade(s.lastGain < 0 ? IM_COL32(230, 60, 50, 255) : IM_COL32(240, 205, 90, 255), s.gainTime), g);
				}
			}

			// Health / magicka / stamina: our own bars, bottom centre, perks above them.
			if (s.hpMax > 0) {
				auto ease = [&](float& g, float v) { g = v > g ? v : std::max(v, g - dt * 0.35f); };
				ease(hpGhost, hpFrac);
				const float mpF = s.mpMax > 0 ? s.mp / s.mpMax : 0, spF = s.spMax > 0 ? s.sp / s.spMax : 0;
				ease(mpGhost, mpF), ease(spGhost, spF);
				const float cx = size.x * 0.5f, y = size.y - 62 * k, hw = 300 * k, sw = 210 * k, h = 11 * k;
				Bar(dl, { cx - hw / 2, y }, hw, h, hpFrac, hpGhost, IM_COL32(196, 28, 28, 255), k);
				Bar(dl, { cx - hw / 2 - 24 * k - sw, y + 2 * k }, sw, 8 * k, mpF, mpGhost, IM_COL32(52, 110, 220, 255), k);
				Bar(dl, { cx + hw / 2 + 24 * k, y + 2 * k }, sw, 8 * k, spF, spGhost, IM_COL32(70, 170, 70, 255), k);
				// perk row
				int n = 0;
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					n += (s.perks >> i) & 1;
				}
				const float r = 19 * k, gap = 46 * k;
				float       x = cx - (n - 1) * gap * 0.5f;
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					if (!((s.perks >> i) & 1)) {
						continue;
					}
					const float py = y - 34 * k;
					const auto  c = gen::kPerkColor[i];
					dl->AddCircleFilled({ x, py }, r + 6 * k, IM_COL32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, static_cast<int>(50 + 30 * std::sin(now * 2 + i))));
					if (auto* icon = g_perkIcons[i]) {
						dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { x - r, py - r }, { x + r, py + r });
					} else {
						dl->AddCircleFilled({ x, py }, r, Col(c));
					}
					x += gap;
				}
			}

			// Weapon panel, bottom-right.
			if (s.gunOut) {
				const ImVec2 a{ size.x - 360 * k, size.y - 196 * k }, b{ size.x - 40 * k, size.y - 120 * k };
				Panel(dl, a, b, k);
				Txt(dl, g_title, 20 * k, { a.x + 14 * k, a.y + 8 * k }, kParch, s.gunName);
				const std::string clip = std::to_string(s.clip), res = std::to_string(s.reserve);
				const auto        cs = Measure(g_num, 40 * k, clip), rs = Measure(g_num, 22 * k, res);
				const ImU32       cc = s.clip == 0 ? IM_COL32(230, 60, 50, 255) : IM_COL32(250, 250, 250, 255);
				Txt(dl, g_num, 22 * k, { b.x - 14 * k - rs.x, b.y - 36 * k }, IM_COL32(180, 180, 180, 255), res);
				Txt(dl, g_num, 40 * k, { b.x - 24 * k - rs.x - cs.x, b.y - 52 * k }, cc, clip);
				if (s.reloading) {
					const float p = std::fmod(now * 1.6f, 1.0f);
					dl->AddRectFilled({ a.x + 14 * k, b.y - 12 * k }, { a.x + 14 * k + (b.x - a.x - 28 * k) * p, b.y - 8 * k }, kGold);
					Txt(dl, g_body, 16 * k, { a.x + 14 * k, b.y - 34 * k }, Fade(kGold, 0.6f + 0.4f * std::sin(now * 8)), "RECHARGING");
				}
				// crosshair: four ticks that tighten when aiming, a dot at full aim
				const ImVec2 c{ size.x * 0.5f, size.y * 0.5f };
				if (s.ads < 0.95f) {
					const float g = 10 * k * (1.0f - s.ads * 0.6f), l = 8 * k;
					const ImU32 col = IM_COL32(255, 245, 225, static_cast<int>(220 * (1.0f - s.ads)));
					dl->AddLine({ c.x - g - l, c.y }, { c.x - g, c.y }, col, 2 * k);
					dl->AddLine({ c.x + g, c.y }, { c.x + g + l, c.y }, col, 2 * k);
					dl->AddLine({ c.x, c.y - g - l }, { c.x, c.y - g }, col, 2 * k);
					dl->AddLine({ c.x, c.y + g }, { c.x, c.y + g + l }, col, 2 * k);
				} else {
					dl->AddCircleFilled(c, 2.5f * k, IM_COL32(255, 80, 60, 230));
				}
			}
			if (s.hitTime > 0) {
				const ImVec2 c{ size.x * 0.5f, size.y * 0.5f };
				const float  a = 6 * k, b = (14 + 6 * (0.2f - s.hitTime) * 5) * k;
				const ImU32  col = s.hitKill ? IM_COL32(235, 50, 40, 255) : IM_COL32(255, 245, 225, 240);
				for (int sx : { -1, 1 }) {
					for (int sy : { -1, 1 }) {
						dl->AddLine({ c.x + sx * a, c.y + sy * a }, { c.x + sx * b, c.y + sy * b }, col, 2.5f * k);
					}
				}
			}

			// Countdown between nights: numeral inside a draining gold ring.
			if (s.countdown > 0.05f) {
				const ImVec2 c{ size.x * 0.5f, size.y * 0.2f };
				const float  R = 46 * k, frac = std::clamp(s.countdown / gen::kRounds.breakSeconds, 0.0f, 1.0f);
				dl->AddCircleFilled(c, R, IM_COL32(8, 8, 10, 170), 64);
				dl->PathArcTo(c, R, -RE::NI_PI / 2, -RE::NI_PI / 2 + 2 * RE::NI_PI * frac, 64);
				dl->PathStroke(kGold, 0, 4 * k);
				const std::string n = std::to_string(static_cast<int>(std::ceil(s.countdown)));
				TxtC(dl, g_num, 44 * k, c.x, c.y - 28 * k, kParch, n);
				TxtC(dl, g_title, 22 * k, c.x, c.y + R + 10 * k, Fade(kGold, 0.9f), "NIGHT " + Roman(s.round + 1) + " APPROACHES");
			}

			// Boss bar: name over a long ornate bar at the top.
			if (!s.bossName.empty()) {
				const float w = size.x * 0.42f, h = 14 * k, x = size.x * 0.5f - w / 2, y = 64 * k;
				TxtC(dl, g_title, 26 * k, size.x * 0.5f, y - 36 * k, s.bossEnraged ? IM_COL32(255, 120, 60, 255) : kParch, s.bossName, s.bossEnraged ? 3 * k : 0, IM_COL32(255, 60, 20, 255));
				static float bossGhost = 1;
				bossGhost = s.bossHp > bossGhost ? s.bossHp : std::max(s.bossHp, bossGhost - dt * 0.25f);
				Bar(dl, { x, y }, w, h, s.bossHp, bossGhost, s.bossEnraged ? IM_COL32(230, 90, 20, 255) : IM_COL32(150, 20, 20, 255), k);
				Divider(dl, size.x * 0.5f, y + h + 12 * k, w * 0.3f, k);
			}

			// Banner: centred line between ornaments, fades and drifts in.
			if (s.bannerTime > 0 && !s.banner.empty()) {
				const float a = std::min(1.0f, s.bannerTime * 2), y = size.y * 0.11f + (1.0f - a) * 10 * k;
				const auto  ts = Measure(g_title, 32 * k, s.banner);
				TxtC(dl, g_title, 32 * k, size.x * 0.5f, y, Fade(kParch, a), s.banner, 2 * k, IM_COL32(0, 0, 0, 255));
				Divider(dl, size.x * 0.5f, y + ts.y + 10 * k, std::min(size.x * 0.4f, ts.x * 0.6f + 40 * k), k, a);
			}

			// Brew machine menu.
			if (s.perkMenu) {
				const float w = 560 * k, rowH = 58 * k, h = 140 * k + rowH * gen::kPerks.size();
				const ImVec2 a{ size.x * 0.5f - w / 2, size.y * 0.5f - h / 2 }, b{ a.x + w, a.y + h };
				dl->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, 90));
				Panel(dl, a, b, k);
				TxtC(dl, g_title, 34 * k, size.x * 0.5f, a.y + 16 * k, kGold, "BREW MACHINE", 2 * k, IM_COL32(255, 180, 60, 255));
				Divider(dl, size.x * 0.5f, a.y + 62 * k, w * 0.38f, k);
				int owned = 0;
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					owned += (s.perks >> i) & 1;
				}
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					const auto& p = gen::kPerks[i];
					const float y = a.y + 78 * k + i * rowH;
					const bool  sel = i == s.perkSel, have = (s.perks >> i) & 1;
					const bool  afford = s.points >= p.cost;
					if (sel) {
						dl->AddRectFilledMultiColor({ a.x + 10 * k, y }, { b.x - 10 * k, y + rowH - 6 * k }, IM_COL32(214, 178, 96, 70), IM_COL32(214, 178, 96, 10), IM_COL32(214, 178, 96, 10), IM_COL32(214, 178, 96, 70));
						dl->AddRect({ a.x + 10 * k, y }, { b.x - 10 * k, y + rowH - 6 * k }, kGoldDim, 0, 0, 1.2f * k);
					}
					const float r = 20 * k;
					if (auto* icon = g_perkIcons[i]) {
						dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { a.x + 22 * k, y + 6 * k }, { a.x + 22 * k + 2 * r, y + 6 * k + 2 * r }, { 0, 0 }, { 1, 1 }, have ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 255));
					}
					const ImU32 nameCol = have ? IM_COL32(150, 150, 150, 255) : kParch;
					Txt(dl, g_title, 22 * k, { a.x + 78 * k, y + 4 * k }, nameCol, std::to_string(i + 1) + "   " + std::string(p.label));
					Txt(dl, g_body, 15 * k, { a.x + 78 * k, y + 30 * k }, IM_COL32(200, 195, 180, have ? 140 : 230), std::string(gen::kPerkDesc[i]));
					const std::string price = have ? "OWNED" : std::to_string(p.cost);
					Txt(dl, g_num, 22 * k, { b.x - 24 * k - Measure(g_num, 22 * k, price).x, y + 12 * k }, have ? IM_COL32(120, 200, 120, 255) : (afford ? kGold : IM_COL32(200, 70, 60, 255)), price);
				}
				const std::string foot = "1-9 or Up/Down + Activate to drink   |   Tab to close   |   " + std::to_string(owned) + "/" + std::to_string(gen::kMaxPerks) + " brews";
				TxtC(dl, g_body, 16 * k, size.x * 0.5f, b.y - 34 * k, IM_COL32(200, 195, 180, 220), foot);
			}

			if (s.crewScreen) {
				dl->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, 175));
				TxtC(dl, g_title, 54 * k, size.x * 0.5f, size.y * 0.15f, kGold, "CHOOSE YOUR HERO", 3 * k, IM_COL32(255, 160, 40, 255));
				Divider(dl, size.x * 0.5f, size.y * 0.15f + 72 * k, size.x * 0.2f, k);
				const float w = size.x * 0.19f, gap = size.x * 0.02f, x0 = (size.x - (4 * w + 3 * gap)) / 2, y0 = size.y * 0.30f, h = size.y * 0.42f;
				for (int i = 0; i < 4; ++i) {
					const auto& c = gen::kCrew[i];
					const float x = x0 + i * (w + gap);
					const bool  on = i == s.crewHighlight;
					Panel(dl, { x, y0 }, { x + w, y0 + h }, k, on ? 1.0f : 0.7f);
					if (on) {
						dl->AddRect({ x - 3 * k, y0 - 3 * k }, { x + w + 3 * k, y0 + h + 3 * k }, kGold, 0, 0, 2.5f * k);
					}
					Txt(dl, g_num, 26 * k, { x + 16 * k, y0 + 14 * k }, Fade(kGold, 0.8f), std::to_string(i + 1));
					Txt(dl, g_title, 30 * k, { x + 16 * k, y0 + 56 * k }, kParch, std::string(c.label));
					dl->AddText(g_body, 19 * k, { x + 16 * k, y0 + 108 * k }, IM_COL32(205, 200, 185, 255), std::string(c.quote).c_str(), nullptr, w - 32 * k);
					Txt(dl, g_body, 19 * k, { x + 16 * k, y0 + h - 52 * k }, kGold, c.passiveText.data());
				}
				TxtC(dl, g_body, 24 * k, size.x * 0.5f, y0 + h + 36 * k, IM_COL32(220, 215, 200, 255), "Press 1-4 to choose, Enter to confirm");
			}
		}

		bool Init(IDXGISwapChain* a_swap)
		{
			if (g_init || g_failed) {
				return g_init;
			}
			if (FAILED(a_swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device)))) {
				g_failed = true;
				return false;
			}
			g_device->GetImmediateContext(&g_context);
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			auto font = [&](const char* a_file) -> ImFont* {
				const std::string p = std::string("C:\\Windows\\Fonts\\") + a_file;
				return ::GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES ? io.Fonts->AddFontFromFileTTF(p.c_str(), 64.0f) : nullptr;
			};
			g_title = font("georgiab.ttf");
			g_num = font("seguisb.ttf");
			g_body = font("segoeui.ttf");
			g_font = g_title ? g_title : font("impact.ttf");
			if (!g_font) {
				g_font = io.Fonts->AddFontDefault();
			}
			g_title = g_title ? g_title : g_font;
			g_num = g_num ? g_num : g_font;
			g_body = g_body ? g_body : g_font;
			if (!ImGui_ImplDX11_Init(g_device, g_context)) {
				g_failed = true;
				logger::error("hud: ImGui DX11 init failed");
				return false;
			}
			int icons = 0;
			for (std::size_t i = 0; i < gen::kPerks.size(); ++i) {
				const auto path = L"Data\\Interface\\UndyingNights\\" + std::wstring(gen::kPerks[i].id.begin(), gen::kPerks[i].id.end()) + L".png";
				if (SUCCEEDED(DirectX::CreateWICTextureFromFile(g_device, path.c_str(), nullptr, &g_perkIcons[i]))) {
					++icons;
				}
			}
			g_init = true;
			logger::info("hud: ready ({} perk icons)", icons);
			return true;
		}

		bool InGame()
		{
			auto* ui = RE::UI::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			return ui && player && player->Is3DLoaded() && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::MainMenu::MENU_NAME) && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		}

		void Frame(IDXGISwapChain* a_swap)
		{
			const auto  now = std::chrono::steady_clock::now();
			const float dt = std::clamp(std::chrono::duration<float>(now - g_last).count(), 0.0001f, 0.25f);
			g_last = now;

			DevTools::PollFromRender();
			DevTools::RunQueuedOnMain();  // Present runs on the main thread, also while menus pause the game
			Hud::State s;
			{
				std::scoped_lock l(g_lock);
				g_state.bannerTime = std::max(0.0f, g_state.bannerTime - dt);
				g_state.gainTime = std::max(0.0f, g_state.gainTime - dt);
				g_state.hitTime = std::max(0.0f, g_state.hitTime - dt);
				g_state.hurtTime = std::max(0.0f, g_state.hurtTime - dt);
				s = g_state;
			}
			if (!Init(a_swap)) {
				return;
			}
			if (!InGame()) {
				if (static int m = 0; m++ % 1200 == 0) {
					auto* ui = RE::UI::GetSingleton();
					auto* pl = RE::PlayerCharacter::GetSingleton();
					logger::info("hud: hidden (paused={} mainMenu={} loading={} player3D={})", ui && ui->GameIsPaused(), ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME),
						ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME), pl && pl->Is3DLoaded());
				}
				DevTools::CaptureIfRequested(g_context, a_swap);  // menus and loading screens too
				return;
			}
			DXGI_SWAP_CHAIN_DESC desc{};
			a_swap->GetDesc(&desc);
			ID3D11Texture2D* back = nullptr;
			if (FAILED(a_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
				return;
			}
			ID3D11RenderTargetView* rtv = nullptr;
			g_device->CreateRenderTargetView(back, nullptr, &rtv);
			back->Release();
			if (!rtv) {
				return;
			}
			auto& io = ImGui::GetIO();
			io.DisplaySize = { static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height) };
			io.DeltaTime = dt;

			ImGui_ImplDX11_NewFrame();
			ImGui::NewFrame();
			Draw(s, io.DisplaySize);
			ImGui::Render();
			if (static int n = 0; n++ % 600 == 0) {
				auto* dd = ImGui::GetDrawData();
				D3D11_VIEWPORT vp[4]{};
				UINT           nvp = 4;
				g_context->RSGetViewports(&nvp, vp);
				UINT          sw = 0, sh = 0;
				IDXGISwapChain2* sc2 = nullptr;
				if (SUCCEEDED(a_swap->QueryInterface(__uuidof(IDXGISwapChain2), reinterpret_cast<void**>(&sc2))) && sc2) {
					sc2->GetSourceSize(&sw, &sh);
					sc2->Release();
				}
				auto* win = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
				auto* st = RE::BSGraphics::State::GetSingleton();
				logger::info("hud: sizes window {}x{} at {},{} | state screen {}x{} fbvp {}x{} dynres {:.2f}x{:.2f} | viewport[{}] {:.0f}x{:.0f}+{:.0f},{:.0f} | sourceSize {}x{}",
					win ? win->windowWidth : -1, win ? win->windowHeight : -1, win ? win->windowX : -1, win ? win->windowY : -1, st ? st->screenWidth : 0, st ? st->screenHeight : 0,
					st ? st->frameBufferViewport[0] : 0, st ? st->frameBufferViewport[1] : 0, st ? st->GetRuntimeData().dynamicResolutionWidthRatio : 0.0f,
					st ? st->GetRuntimeData().dynamicResolutionHeightRatio : 0.0f, nvp, vp[0].Width, vp[0].Height, vp[0].TopLeftX, vp[0].TopLeftY, sw, sh);
				logger::info("hud: frame {} display {}x{} fmt {} lists {} verts {} font {} imgui {}", n, desc.BufferDesc.Width, desc.BufferDesc.Height,
					static_cast<int>(desc.BufferDesc.Format), dd ? dd->CmdListsCount : -1, dd ? dd->TotalVtxCount : -1, g_font != nullptr, IMGUI_VERSION);
			}

			ID3D11RenderTargetView* oldRtv = nullptr;
			ID3D11DepthStencilView* oldDsv = nullptr;
			g_context->OMGetRenderTargets(1, &oldRtv, &oldDsv);
			g_context->OMSetRenderTargets(1, &rtv, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			g_context->OMSetRenderTargets(1, &oldRtv, oldDsv);
			if (oldRtv) oldRtv->Release();
			if (oldDsv) oldDsv->Release();
			rtv->Release();
			DevTools::CaptureIfRequested(g_context, a_swap);
		}

		HRESULT WINAPI PresentHook(IDXGISwapChain* a_swap, UINT a_sync, UINT a_flags)
		{
			try {
				Frame(a_swap);
			} catch (...) {
			}
			// Frame cap (sheet: movement.fps_cap): sleep most of the gap, spin the last bit for an even pace.
			if (gen::kFpsCap > 0) {
				using C = std::chrono::steady_clock;
				static C::time_point next = C::now();
				static const bool fineTimer = (::timeBeginPeriod(1) == TIMERR_NOERROR);  // 1 ms sleeps
				(void)fineTimer;
				const auto frame = std::chrono::duration_cast<C::duration>(std::chrono::duration<double>(1.0 / gen::kFpsCap));
				auto now = C::now();
				if (next > now) {
					if (next - now > 2ms) {
						std::this_thread::sleep_for(next - now - 1500us);
					}
					while (C::now() < next) {
						_mm_pause();
					}
				}
				now = C::now();
				next = (now - next > frame * 4) ? now + frame : next + frame;
			}
			return g_present(a_swap, a_sync, a_flags);
		}
	}

	namespace Hud
	{
		State&      Edit() { return g_state; }
		std::mutex& Lock() { return g_lock; }

		void Banner(std::string a_text, float a_seconds)
		{
			std::scoped_lock l(g_lock);
			g_state.banner = std::move(a_text);
			g_state.bannerTime = a_seconds;
		}

		void Install()
		{
			auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
			auto* swap = window ? reinterpret_cast<IDXGISwapChain*>(window->swapChain) : nullptr;
			if (!swap) {
				logger::error("hud: no swap chain yet");
				return;
			}
			auto** vtable = *reinterpret_cast<void***>(swap);
			DWORD  old = 0;
			::VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
			g_present = reinterpret_cast<PresentFn>(vtable[8]);
			vtable[8] = reinterpret_cast<void*>(&PresentHook);
			::VirtualProtect(&vtable[8], sizeof(void*), old, &old);
			logger::info("hud: Present hooked");
		}
	}
}
