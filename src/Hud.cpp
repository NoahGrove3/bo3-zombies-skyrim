#include "BO3Z.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <WICTextureLoader.h>
#include <d3dcompiler.h>

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
		std::array<ID3D11ShaderResourceView*, gen::kGuns.size()>  g_weaponIcons{};
		ID3D11ShaderResourceView* g_menuBg = nullptr;   // the main menu's backdrop (tools/cover.py, MENU=1)
		ID3D11ShaderResourceView* g_menuFog = nullptr;  // a tileable fog layer that drifts over it
		std::atomic<int>          g_clicks{ 0 };        // left clicks since the last frame (menus)
		// Skyrim's main-menu buttons are kept over our picture: the frame as Skyrim drew it is copied, and in the
		// buttons' corner only its bright pixels (the words) are laid back on top (a luma key; the cursor is ours).
		ID3D11Texture2D*          g_frameCopy = nullptr;
		ID3D11ShaderResourceView* g_frameSrv = nullptr;
		UINT                      g_frameW = 0, g_frameH = 0;
		ID3D11PixelShader*        g_keyPs = nullptr;
		ID3D11Buffer*             g_keyCb = nullptr;
		ImVec2                    g_keyCursor{ -1000, -1000 };

		constexpr const char* kKeyHlsl = R"(
cbuffer K : register(b0) { float2 cursor; float2 pad; };
sampler s0 : register(s0);
Texture2D t0 : register(t0);
float4 main(float4 pos : SV_POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) : SV_Target
{
	float4 c = t0.Sample(s0, uv);
	float l = dot(c.rgb, float3(0.299, 0.587, 0.114));
	float a = smoothstep(0.20, 0.40, l);
	float2 d = pos.xy - cursor;
	if (d.x > -6 && d.x < 46 && d.y > -6 && d.y < 56) a = 0;
	return float4(c.rgb, a * col.a);
}
)";

		void MakeKeyShader()
		{
			if (g_keyPs || !g_device) {
				return;
			}
			HMODULE dc = ::LoadLibraryW(L"d3dcompiler_47.dll");
			auto*   compile = dc ? reinterpret_cast<pD3DCompile>(::GetProcAddress(dc, "D3DCompile")) : nullptr;
			if (!compile) {
				logger::warn("hud: no d3dcompiler_47; the menu keeps Skyrim's buttons uncovered");
				return;
			}
			ID3DBlob *code = nullptr, *err = nullptr;
			if (FAILED(compile(kKeyHlsl, std::strlen(kKeyHlsl), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &code, &err)) || !code) {
				logger::warn("hud: key shader: {}", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
				if (err) err->Release();
				return;
			}
			g_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_keyPs);
			code->Release();
			D3D11_BUFFER_DESC bd{};
			bd.ByteWidth = 16;
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			g_device->CreateBuffer(&bd, nullptr, &g_keyCb);
		}

		void KeyOn(const ImDrawList*, const ImDrawCmd*)
		{
			if (!g_keyPs) {
				return;
			}
			D3D11_MAPPED_SUBRESOURCE m{};
			if (g_keyCb && SUCCEEDED(g_context->Map(g_keyCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
				const float v[4]{ g_keyCursor.x, g_keyCursor.y, 0, 0 };
				std::memcpy(m.pData, v, sizeof(v));
				g_context->Unmap(g_keyCb, 0);
			}
			g_context->PSSetShader(g_keyPs, nullptr, 0);
			g_context->PSSetConstantBuffers(0, 1, &g_keyCb);
		}

		// The frame as Skyrim drew it (menus included), for the key.
		void CopyFrame(ID3D11Texture2D* a_back)
		{
			D3D11_TEXTURE2D_DESC d{};
			a_back->GetDesc(&d);
			if (d.SampleDesc.Count != 1) {
				return;
			}
			if (!g_frameCopy || d.Width != g_frameW || d.Height != g_frameH) {
				if (g_frameSrv) g_frameSrv->Release(), g_frameSrv = nullptr;
				if (g_frameCopy) g_frameCopy->Release(), g_frameCopy = nullptr;
				d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				d.Usage = D3D11_USAGE_DEFAULT;
				d.CPUAccessFlags = 0;
				d.MiscFlags = 0;
				if (FAILED(g_device->CreateTexture2D(&d, nullptr, &g_frameCopy)) || FAILED(g_device->CreateShaderResourceView(g_frameCopy, nullptr, &g_frameSrv))) {
					return;
				}
				g_frameW = d.Width, g_frameH = d.Height;
			}
			g_context->CopyResource(g_frameCopy, a_back);
		}
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

			// Event nights colour the screen: a red haze under the Blood Moon, grey murk on a Fog Night, ash, iron.
			// (Fog Night has no screen tint: its fog is real ground mist in the world, Director::TickMist.)
			if (s.nightActive && s.nightEvent >= 0 && s.nightEvent < static_cast<int>(gen::kEvents.size()) && s.nightEvent != gen::kEvent_fog) {
				const auto  t = gen::kEvents[s.nightEvent].tint;
				const int   r = (t >> 24) & 0xFF, g = (t >> 16) & 0xFF, b = (t >> 8) & 0xFF, a = t & 0xFF;
				const bool  fog = s.nightEvent == gen::kEvent_fog;
				const float pulse = 0.85f + 0.15f * std::sin(now * 0.8f);
				dl->AddRectFilled({ 0, 0 }, size, IM_COL32(r, g, b, static_cast<int>(a * (fog ? 0.9f : 0.35f) * pulse)));
				const ImU32 e = IM_COL32(r, g, b, std::min(255, static_cast<int>(a * (fog ? 2.4f : 2.0f)))), z = IM_COL32(r, g, b, 0);
				const float w = size.y * (fog ? 0.45f : 0.3f);
				dl->AddRectFilledMultiColor({ 0, 0 }, { size.x, w }, e, e, z, z);
				dl->AddRectFilledMultiColor({ 0, size.y - w }, size, z, z, e, e);
				dl->AddRectFilledMultiColor({ 0, 0 }, { w, size.y }, e, z, z, e);
				dl->AddRectFilledMultiColor({ size.x - w, 0 }, size, z, e, e, z);
			}
			if (s.nukeFlash > 0) {
				dl->AddRectFilled({ 0, 0 }, size, IM_COL32(255, 250, 235, static_cast<int>(230 * std::min(1.0f, s.nukeFlash))));
			}

			// Night counter, top-left: chalk tally marks in blood red, one per night (groups of five, struck through).
			// By day it reads NIGHTS SURVIVED; when the next night begins its tally is scratched in.
			if (s.round > 0) {
				const ImVec2 p{ 46 * k, 22 * k };
				Txt(dl, g_title, 20 * k, p, Fade(kGold, 0.85f), s.nightActive ? "NIGHT" : "NIGHTS SURVIVED");
				const float h = 70 * k, step = 17 * k, gap = 22 * k, top = p.y + 30 * k;
				const float draw = std::clamp(1.0f - (roundFlash - 1.5f), 0.0f, 1.0f);  // newest mark scratches in
				auto jit = [](int i, int j) { return static_cast<float>(((i * 73 + j * 151) % 17) - 8) / 8.0f; };
				const int  shown = std::min(s.round, 15);
				float      x = p.x + 4 * k;
				for (int i = 0; i < shown; ++i) {
					const int   inGroup = i % 5;
					const bool  newest = i == s.round - 1;
					const float t = newest ? draw : 1.0f;
					const ImU32 col = newest && roundFlash > 0 ? IM_COL32(255, 60, 40, 255) : kBlood;
					if (inGroup < 4) {
						const ImVec2 a{ x + jit(i, 1) * 2 * k, top + jit(i, 2) * 3 * k }, b{ x + jit(i, 3) * 3 * k, top + h + jit(i, 4) * 3 * k };
						const ImVec2 e{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
						dl->AddLine(a, e, Fade(col, 0.35f), 11 * k);
						dl->AddLine(a, e, col, 6 * k);
						x += step;
					} else {
						// the fifth strikes through the four
						const ImVec2 a{ x - 4 * step - 8 * k, top + h * 0.78f }, b{ x + 4 * k, top + h * 0.18f };
						const ImVec2 e{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
						dl->AddLine(a, e, Fade(col, 0.35f), 11 * k);
						dl->AddLine(a, e, col, 6 * k);
						x += gap;
					}
				}
				if (s.round > 15) {  // past three full groups: the count in numerals beside them
					Txt(dl, g_title, 60 * k, { x + 10 * k, top }, kBlood, std::to_string(s.round), 3 * k, IM_COL32(255, 40, 20, 255));
				}
				if (s.nightActive && s.nightEvent >= 0 && s.nightEvent < static_cast<int>(gen::kEvents.size())) {
					const auto  t = gen::kEvents[s.nightEvent].tint;
					const ImU32 c = IM_COL32(std::min(255, static_cast<int>(((t >> 24) & 0xFF) * 1.6f) + 60), std::min(255, static_cast<int>(((t >> 16) & 0xFF) * 1.6f) + 60), std::min(255, static_cast<int>(((t >> 8) & 0xFF) * 1.6f) + 60), 255);
					Txt(dl, g_title, 24 * k, { p.x, top + h + 14 * k }, c, std::string(gen::kEvents[s.nightEvent].label), 2 * k, c);
				}
			}

			// Points, bottom-left panel.
			{
				const ImVec2 a{ 40 * k, size.y - 196 * k }, b{ 300 * k, size.y - 132 * k };
				Panel(dl, a, b, k);
				Txt(dl, g_body, 15 * k, { a.x + 14 * k, a.y + 7 * k }, Fade(kGold, 0.9f), "SEPTIMS OF THE DEAD");
				static float shownPts = 0;
				shownPts += (s.points - shownPts) * std::min(1.0f, dt * 9.0f);
				if (std::abs(s.points - shownPts) < 1.0f) shownPts = static_cast<float>(s.points);
				const bool rising = shownPts < s.points - 0.5f;
				Txt(dl, g_num, 34 * k, { a.x + 14 * k, a.y + 22 * k }, rising ? IM_COL32(255, 228, 150, 255) : kParch, std::to_string(static_cast<int>(std::lround(shownPts))), rising ? 2 * k : 0, IM_COL32(255, 200, 80, 255));
				if (s.crew >= 0 && s.crew < static_cast<int>(gen::kCrew.size())) {
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

			// Weapon dock, bottom-right: the weapon in hand as a large card (its picture, Reforge marks, ability and
			// signature move), the other slots as small cards above it with their keys. Dimmed while sheathed.
			{
				const bool any = std::any_of(s.slotGun.begin(), s.slotGun.begin() + std::clamp(s.slotCount, 0, 3), [](int g) { return g >= 0; });
				static float shown = 0, swap = 0;
				static int   lastActive = -2;
				shown += ((s.gunOut ? 1.0f : 0.55f) - shown) * std::min(1.0f, dt * 8);
				if (s.activeSlot != lastActive) {
					swap = lastActive == -2 ? 0.0f : 1.0f, lastActive = s.activeSlot;
				}
				swap = std::max(0.0f, swap - dt * 3.5f);
				const int act = s.activeSlot >= 0 && s.activeSlot < s.slotCount ? s.activeSlot : -1;
				const int ag = act >= 0 ? s.slotGun[act] : -1;
				if (any) {
					const float al = shown;
					const ImVec2 a{ size.x - 470 * k, size.y - 218 * k + swap * 18 * k }, b{ size.x - 40 * k, size.y - 92 * k + swap * 18 * k };
					const std::uint32_t ec = ag >= 0 ? gen::kGuns[ag].color : 0xB0A890;
					const ImU32 el = IM_COL32((ec >> 16) & 0xFF, (ec >> 8) & 0xFF, ec & 0xFF, 255);
					Panel(dl, a, b, k, al);
					// the element's light washing in from the left edge
					dl->AddRectFilledMultiColor({ a.x + 1 * k, a.y + 1 * k }, { a.x + 190 * k, b.y - 1 * k }, Fade(el, 0.20f * al), Fade(el, 0.0f), Fade(el, 0.0f), Fade(el, 0.28f * al));
					dl->AddRectFilled({ a.x, a.y }, { a.x + 4 * k, b.y }, Fade(el, al));
					if (ag >= 0) {
						const auto& g = gen::kGuns[ag];
						const int   lvl = std::clamp(s.slotLevel[act], 0, 3);
						if (auto* icon = g_weaponIcons[ag]) {
							const float iw = 250 * k, ih = iw * 192.0f / 512.0f, bob = 2 * k * std::sin(now * 1.6f);
							dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { b.x - iw - 8 * k, a.y - 30 * k + bob }, { b.x - 8 * k, a.y - 30 * k + ih + bob }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * al)));
						}
						const auto  fc = gen::kForgeColor[lvl];
						const ImU32 nameCol = lvl > 0 ? IM_COL32((fc >> 24) & 0xFF, (fc >> 16) & 0xFF, (fc >> 8) & 0xFF, 255) : kParch;
						Txt(dl, g_title, 24 * k, { a.x + 18 * k, a.y + 40 * k }, Fade(nameCol, al), std::string(g.label), lvl > 0 ? 2 * k : 0, nameCol);
						// Reforge marks: three diamonds, lit by level; ASCENDANT at the top
						float dx = a.x + 22 * k;
						const float dy = a.y + 80 * k;
						for (int i = 0; i < 3; ++i) {
							const bool on = i < lvl;
							const ImVec2 c{ dx + i * 18 * k, dy };
							dl->AddQuadFilled({ c.x, c.y - 6 * k }, { c.x + 6 * k, c.y }, { c.x, c.y + 6 * k }, { c.x - 6 * k, c.y }, on ? Fade(nameCol, al) : IM_COL32(60, 56, 50, static_cast<int>(200 * al)));
							dl->AddQuad({ c.x, c.y - 6 * k }, { c.x + 6 * k, c.y }, { c.x, c.y + 6 * k }, { c.x - 6 * k, c.y }, Fade(kGoldDim, al), 1.0f * k);
						}
						Txt(dl, g_body, 14 * k, { dx + 60 * k, dy - 9 * k }, Fade(IM_COL32(200, 190, 170, 255), al), lvl >= 3 ? "ASCENDANT" : lvl > 0 ? "REFORGED " + std::string(lvl == 1 ? "I" : "II") : "UNFORGED");
						// ability and signature move
						Txt(dl, g_body, 15 * k, { a.x + 18 * k, a.y + 92 * k }, Fade(IM_COL32(205, 198, 182, 255), al), std::string(g.abilityText));
						dl->AddCircleFilled({ a.x + 24 * k, b.y - 13 * k }, 4 * k, Fade(el, al));
						Txt(dl, g_body, 15 * k, { a.x + 34 * k, b.y - 23 * k }, Fade(el, al), "HOLD  " + std::string(g.chargeName) + (lvl >= 3 ? "  -  ASCENDANT" : ""));
						// what it runs on, right
						if (s.gunKind == 0 || s.gunKind == 3 || !s.gunOut) {
							const std::string inf = "\xE2\x88\x9E";
							const auto        is = Measure(g_num, 40 * k, inf);
							Txt(dl, g_num, 40 * k, { b.x - 18 * k - is.x, b.y - 56 * k }, Fade(IM_COL32(250, 246, 236, 255), al), inf);
						} else {
							const bool        can = s.mp >= s.manaCost;
							const std::string cost = std::to_string(static_cast<int>(std::ceil(s.manaCost)));
							const auto        cs = Measure(g_num, 34 * k, cost);
							Txt(dl, g_num, 34 * k, { b.x - 64 * k - cs.x, b.y - 52 * k }, can ? IM_COL32(150, 190, 255, 255) : IM_COL32(230, 60, 50, 255), cost);
							Txt(dl, g_body, 15 * k, { b.x - 58 * k, b.y - 40 * k }, IM_COL32(150, 170, 210, 255), "mana");
						}
					} else {
						Txt(dl, g_title, 22 * k, { a.x + 18 * k, a.y + 48 * k }, Fade(IM_COL32(150, 145, 135, 255), al), "Empty hands");
					}
					Txt(dl, g_num, 15 * k, { a.x + 18 * k, a.y + 10 * k }, Fade(kGold, 0.85f * al), "SLOT " + std::to_string(act + 1));
					// the other slots, small, stacked above; G cycles
					float y = a.y - 12 * k;
					for (int i = s.slotCount - 1; i >= 0; --i) {
						if (i == act) {
							continue;
						}
						const ImVec2 c0{ b.x - 300 * k, y - 50 * k }, c1{ b.x, y };
						y -= 58 * k;
						Panel(dl, c0, c1, k, 0.7f * al);
						const int og = s.slotGun[i];
						Txt(dl, g_num, 14 * k, { c0.x + 10 * k, c0.y + 6 * k }, Fade(kGoldDim, al), std::to_string(i + 1));
						if (og >= 0) {
							if (auto* icon = g_weaponIcons[og]) {
								const float iw = 120 * k, ih = iw * 192.0f / 512.0f;
								dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { c1.x - iw - 4 * k, c0.y + (50 * k - ih) / 2 }, { c1.x - 4 * k, c0.y + (50 * k + ih) / 2 }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(200 * al)));
							}
							const int   ol = std::clamp(s.slotLevel[i], 0, 3);
							const auto  fc = gen::kForgeColor[ol];
							const ImU32 nc = ol > 0 ? IM_COL32((fc >> 24) & 0xFF, (fc >> 16) & 0xFF, (fc >> 8) & 0xFF, 255) : IM_COL32(210, 202, 186, 255);
							Txt(dl, g_title, 16 * k, { c0.x + 30 * k, c0.y + 14 * k }, Fade(nc, 0.9f * al), std::string(gen::kGuns[og].label));
						} else {
							Txt(dl, g_body, 15 * k, { c0.x + 30 * k, c0.y + 15 * k }, Fade(IM_COL32(140, 136, 128, 255), al), "empty - a staff or bow from the Coffer");
						}
					}
					if (s.slotCount > 1) {
						const std::string hint = "G  next weapon";
						Txt(dl, g_body, 13 * k, { b.x - Measure(g_body, 13 * k, hint).x, b.y + 6 * k }, Fade(IM_COL32(180, 172, 156, 200), al), hint);
					}
				}
			}
			if (s.gunOut) {
				// power-ups running (BO3 style, bottom centre): name and seconds left, blinking near the end
				{
					struct PU { const char* name; float t; ImU32 col; } pus[] = { { "INSTA-KILL", s.puInsta, IM_COL32(255, 70, 60, 255) },
						{ "DOUBLE POINTS", s.puDouble, IM_COL32(255, 210, 80, 255) }, { "FIRE SALE", s.puFireSale, IM_COL32(190, 120, 255, 255) } };
					float y = size.y - 150 * k;
					for (auto& pu : pus) {
						if (pu.t <= 0 || (pu.t < 5 && std::fmod(now, 0.5f) < 0.2f)) {
							continue;
						}
						// a dark pill in the power-up's colour, its seconds draining along the bottom
						const std::string txt = pu.name;
						const std::string sec = std::to_string(static_cast<int>(std::ceil(pu.t)));
						const auto        ts = Measure(g_title, 20 * k, txt);
						const float       w = ts.x + 90 * k, x0 = size.x * 0.5f - w * 0.5f;
						dl->AddRectFilled({ x0, y - 4 * k }, { x0 + w, y + 30 * k }, IM_COL32(8, 8, 10, 190), 15 * k);
						dl->AddRect({ x0, y - 4 * k }, { x0 + w, y + 30 * k }, Fade(pu.col, 0.8f), 15 * k, 0, 1.5f * k);
						dl->AddRectFilled({ x0 + 14 * k, y + 25 * k }, { x0 + 14 * k + (w - 28 * k) * std::clamp(pu.t / 30.0f, 0.0f, 1.0f), y + 27 * k }, pu.col);
						Txt(dl, g_title, 20 * k, { x0 + 18 * k, y }, pu.col, txt, 1.5f * k, pu.col);
						Txt(dl, g_num, 20 * k, { x0 + w - 18 * k - Measure(g_num, 20 * k, sec).x, y }, kParch, sec);
						y -= 40 * k;
					}
				}
				// signature move: a ring fills round the crosshair while you hold fire; gold and pulsing when ready
				{
					const ImVec2 cc{ size.x * 0.5f, size.y * 0.5f };
					const float  R = 30 * k;
					const ImU32  el = IM_COL32((s.gunColor >> 16) & 0xFF, (s.gunColor >> 8) & 0xFF, s.gunColor & 0xFF, 255);
					if (s.chargeCd > 0.001f) {
						dl->PathArcTo(cc, R, -3.14159265f / 2, -3.14159265f / 2 + 2 * 3.14159265f * (1.0f - s.chargeCd), 48);
						dl->PathStroke(IM_COL32(120, 120, 120, 120), 0, 2 * k);
					} else if (s.charge > 0.0f) {
						const bool ready = s.charge >= 1.0f;
						dl->PathArcTo(cc, R + (ready ? 3 * k * std::sin(now * 12) : 0), -3.14159265f / 2, -3.14159265f / 2 + 2 * 3.14159265f * s.charge, 48);
						dl->PathStroke(ready ? kGold : el, 0, (ready ? 4 : 3) * k);
						if (ready) {
							TxtC(dl, g_title, 18 * k, cc.x, cc.y + R + 10 * k, Fade(kGold, 0.9f), "RELEASE: " + s.chargeName);
						}
					}
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
				TxtC(dl, g_title, 22 * k, c.x, c.y + R + 10 * k, Fade(kGold, 0.9f), "NIGHT " + std::to_string(s.round + 1) + " APPROACHES");
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

			// The city's curse quest, top-right: city, quest title, goal and a thin progress bar.
			if (!s.questTitle.empty() || s.questNoteTime > 0) {
				const float w = 400 * k;
				const bool  note = s.questNoteTime > 0 && !s.questNote.empty();
				const ImVec2 a{ size.x - w - 40 * k, 30 * k }, b{ size.x - 40 * k, 30 * k + (note ? 128 : 104) * k };
				Panel(dl, a, b, k, 0.92f);
				Txt(dl, g_body, 14 * k, { a.x + 14 * k, a.y + 8 * k }, Fade(kGold, 0.85f), s.questCity + "  -  THE UNDYING DIRGE  " + std::to_string(s.questStep + 1) + "/4");
				Txt(dl, g_title, 22 * k, { a.x + 14 * k, a.y + 28 * k }, kParch, s.questTitle);
				Txt(dl, g_body, 16 * k, { a.x + 14 * k, a.y + 58 * k }, IM_COL32(205, 200, 185, 240), s.questGoal);
				static float qGhost = 0;
				qGhost = s.questFrac < qGhost ? s.questFrac : qGhost + (s.questFrac - qGhost) * std::min(1.0f, dt * 4);
				Bar(dl, { a.x + 14 * k, a.y + 88 * k }, w - 28 * k, 5 * k, qGhost, s.questFrac, IM_COL32(214, 178, 96, 255), k);
				if (note) {
					Txt(dl, g_body, 15 * k, { a.x + 14 * k, a.y + 100 * k }, Fade(IM_COL32(255, 200, 110, 255), std::min(1.0f, s.questNoteTime)), s.questNote);
				}
			}
			if (!s.sanctuary.empty()) {
				TxtC(dl, g_title, 24 * k, size.x * 0.5f, 104 * k, Fade(kGold, 0.9f), "SANCTUARY OF " + s.sanctuary, 2 * k, IM_COL32(255, 220, 120, 255));
				TxtC(dl, g_body, 16 * k, size.x * 0.5f, 134 * k, IM_COL32(220, 215, 200, 220), "The Dirge is broken here. The dead cannot rise.");
			}
			// Kindling a ward-fire: an ember ring round the crosshair.
			if (s.wardFrac > 0.001f) {
				const ImVec2 cc{ size.x * 0.5f, size.y * 0.5f };
				const float  R = 46 * k;
				dl->AddCircle(cc, R, IM_COL32(40, 30, 20, 160), 64, 5 * k);
				dl->PathArcTo(cc, R, -RE::NI_PI / 2, -RE::NI_PI / 2 + 2 * RE::NI_PI * s.wardFrac, 64);
				dl->PathStroke(IM_COL32(255, 150, 50, 255), 0, 5 * k);
				TxtC(dl, g_title, 20 * k, cc.x, cc.y + R + 12 * k, IM_COL32(255, 190, 110, 255), "KINDLING THE WARD-FIRE", 2 * k, IM_COL32(255, 100, 20, 255));
			}
			// Nuke: the dead regroup.
			if (s.nukeLeft > 0.05f && (s.nukeLeft > 5 || std::fmod(now, 0.5f) >= 0.2f)) {
				const std::string txt = "NUKE  -  THE DEAD REGROUP  " + std::to_string(static_cast<int>(std::ceil(s.nukeLeft)));
				TxtC(dl, g_body, 22 * k, size.x * 0.5f, size.y - 236 * k, IM_COL32(255, 160, 60, 255), txt);
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
		}

		// ---- main menu and party panel ----
		ImVec2 Cursor(ImVec2 a_size)
		{
			auto* mc = RE::MenuCursor::GetSingleton();
			if (!mc) {
				return { -1, -1 };
			}
			const auto& r = mc->GetRuntimeData();
			const float sx = r.screenWidthX > 1 ? a_size.x / r.screenWidthX : 1.0f, sy = r.screenWidthY > 1 ? a_size.y / r.screenWidthY : 1.0f;
			return { r.cursorPosX * sx, r.cursorPosY * sy };
		}

		bool Button(ImDrawList* dl, ImVec2 a, ImVec2 b, const std::string& label, ImVec2 cur, bool click, float k, ImU32 accent = kGold)
		{
			const bool hover = cur.x >= a.x && cur.x <= b.x && cur.y >= a.y && cur.y <= b.y;
			dl->AddRectFilledMultiColor(a, b, IM_COL32(20, 16, 14, hover ? 235 : 200), IM_COL32(20, 16, 14, hover ? 235 : 200), IM_COL32(8, 6, 6, 235), IM_COL32(8, 6, 6, 235));
			dl->AddRect(a, b, hover ? accent : Fade(accent, 0.45f), 2 * k, 0, (hover ? 2.0f : 1.2f) * k);
			if (hover) {
				dl->AddRectFilled({ a.x, a.y }, { a.x + 4 * k, b.y }, accent);
			}
			const auto ts = Measure(g_title, 20 * k, label);
			Txt(dl, g_title, 20 * k, { a.x + (b.x - a.x - ts.x) / 2, a.y + (b.y - a.y - ts.y) / 2 }, hover ? kParch : IM_COL32(220, 210, 190, 235), label);
			return hover && click;
		}

		// The co-op party: host a Steam party and invite, or join a friend's.
		void PartyPanel(ImDrawList* dl, ImVec2 size, ImVec2 a, float k, bool click)
		{
			if (!Net::Available()) {
				return;
			}
			const ImVec2 cur = Cursor(size);
			static std::vector<Net::FriendGame> friends;
			static float refresh = 0;
			if ((refresh -= ImGui::GetIO().DeltaTime) <= 0) {
				refresh = 2.0f;
				friends = Net::InLobby() ? std::vector<Net::FriendGame>{} : Net::FriendsInParties();
			}
			const float w = 420 * k, rowH = 46 * k;
			const int   rows = Net::InLobby() ? 2 : 1 + static_cast<int>(std::min<std::size_t>(friends.size(), 4));
			const float h = 92 * k + rows * (rowH + 8 * k) + 30 * k;
			const ImVec2 b{ a.x + w, a.y + h };
			Panel(dl, { a.x, a.y - h }, { b.x, a.y }, k, 0.95f);
			float y = a.y - h + 14 * k;
			Txt(dl, g_title, 26 * k, { a.x + 18 * k, y }, kGold, "CO-OP PARTY", 2 * k, IM_COL32(255, 160, 60, 255));
			y += 36 * k;
			Txt(dl, g_body, 16 * k, { a.x + 18 * k, y }, IM_COL32(210, 202, 186, 240), Net::Status() + (Net::InLobby() ? "   (" + std::to_string(Net::Members() + 1) + " in party)" : ""));
			y += 34 * k;
			auto row = [&](const std::string& label, ImU32 accent = kGold) {
				const bool hit = Button(dl, { a.x + 18 * k, y }, { b.x - 18 * k, y + rowH }, label, cur, click, k, accent);
				y += rowH + 8 * k;
				return hit;
			};
			if (!Net::InLobby()) {
				if (row("HOST A PARTY  -  INVITE FRIENDS")) {
					Net::HostLobby();
				}
				for (std::size_t i = 0; i < friends.size() && i < 4; ++i) {
					if (row("JOIN " + friends[i].name, IM_COL32(120, 200, 255, 255))) {
						Net::JoinLobby(friends[i].lobby);
					}
				}
			} else if (Net::IsHost()) {
				if (row("INVITE FRIENDS (STEAM)")) {
					Net::InviteFriends();
				}
				if (row("CLOSE THE PARTY", IM_COL32(220, 90, 70, 255))) {
					Net::LeaveLobby();
				}
			} else {
				if (row("LEAVE " + Net::HostName() + "'S PARTY", IM_COL32(220, 90, 70, 255))) {
					Net::LeaveLobby();
				}
				y += rowH + 8 * k;
			}
			Txt(dl, g_body, 13 * k, { a.x + 18 * k, a.y - 24 * k }, IM_COL32(170, 162, 146, 200), "Players meet in Skyrim Together; the party shares the nights.");
		}

		// The main menu: the cover's world, alive (fog drifting, embers rising, the moon breathing).
		void DrawMenu(ImVec2 size, float now, bool click)
		{
			auto*       dl = ImGui::GetBackgroundDrawList();
			auto*       fg = ImGui::GetForegroundDrawList();
			const float k = size.y / 1080.0f;
			// the picture, cropped to fill the screen (16:9 art)
			float iw = size.x, ih = size.x * 9.0f / 16.0f;
			if (ih < size.y) {
				ih = size.y, iw = size.y * 16.0f / 9.0f;
			}
			const ImVec2 o{ (size.x - iw) / 2, (size.y - ih) / 2 };
			auto at = [&](float u, float v) { return ImVec2{ o.x + u * iw, o.y + v * ih }; };
			if (g_menuBg) {
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_menuBg)), o, { o.x + iw, o.y + ih });
			}
			// the blood moon breathes
			{
				const ImVec2 m = at(0.5f, 0.333f);
				const float  pulse = 0.5f + 0.5f * std::sin(now * 0.7f);
				for (int i = 0; i < 6; ++i) {
					fg->AddCircleFilled(m, ih * (0.20f + 0.04f * i), IM_COL32(255, 70, 30, static_cast<int>((6 + 5 * pulse) * (6 - i) / 6)), 64);
				}
			}
			// the staff's gem and the lance's strike flicker
			{
				const float f = 0.6f + 0.4f * std::sin(now * 9.0f) * std::sin(now * 3.1f);
				for (auto [u, v, r] : { std::tuple{ 0.609f, 0.583f, 0.022f }, { 0.236f, 0.768f, 0.04f } }) {
					const ImVec2 c = at(u, v);
					for (int i = 0; i < 4; ++i) {
						fg->AddCircleFilled(c, ih * r * (1 + i * 0.6f), IM_COL32(90, 255, 190, static_cast<int>(28 * f * (4 - i) / 4)), 32);
					}
				}
			}
			// fog: two layers drifting across the barrow-yard at different speeds
			if (g_menuFog) {
				for (auto [speed, top, alpha, scale] : { std::tuple{ 0.012f, 0.62f, 150, 1.0f }, { -0.007f, 0.74f, 120, 1.6f } }) {
					const float u0 = now * speed;
					fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_menuFog)), at(0, top), at(1, 1.0f), { u0, 0 }, { u0 + 1.0f / scale * 2.0f, 1.0f / scale },
						IM_COL32(255, 255, 255, alpha));
				}
			}
			// embers rising from the braziers and the strike
			{
				struct Ember { float u, v, sp, sway, ph, r; bool green; };
				static std::vector<Ember> em;
				static std::mt19937 rng{ 7 };
				std::uniform_real_distribution<float> U(0, 1);
				if (em.empty()) {
					for (int i = 0; i < 160; ++i) {
						em.push_back({ U(rng), U(rng), 0.015f + 0.03f * U(rng), 0.004f + 0.01f * U(rng), U(rng) * 6.28f, 1.0f + 2.2f * U(rng), U(rng) < 0.18f });
					}
				}
				const float dt = ImGui::GetIO().DeltaTime;
				for (auto& e : em) {
					e.v -= e.sp * dt;
					if (e.v < 0.05f) {
						const float src = U(rng);
						e.u = src < 0.3f ? 0.24f + 0.02f * (U(rng) - 0.5f) : src < 0.6f ? 0.76f + 0.02f * (U(rng) - 0.5f) : U(rng);
						e.v = 0.85f + 0.12f * U(rng);
					}
					const float u = e.u + std::sin(now * 0.8f + e.ph) * e.sway;
					const float tw = 0.55f + 0.45f * std::sin(now * 6.0f + e.ph * 3);
					const ImU32 c = e.green ? IM_COL32(140, 255, 210, static_cast<int>(200 * tw)) : IM_COL32(255, 170, 80, static_cast<int>(220 * tw));
					const ImVec2 p = at(u, e.v);
					fg->AddCircleFilled(p, e.r * k * 2.2f, Fade(c, 0.18f));
					fg->AddCircleFilled(p, e.r * k, c);
				}
			}
			// now and then, a lightning bolt far off over the mountains (never twice in the same place)
			{
				static float next = 4.0f, life = 0;
				static std::vector<std::vector<ImVec2>> bolt;
				static std::mt19937 lr{ 11 };
				std::uniform_real_distribution<float> L(0, 1);
				if (now > next && life <= 0) {
					life = 0.55f;
					next = now + 5.0f + 7.0f * L(lr);
					bolt.clear();
					const float u0 = L(lr) < 0.5f ? 0.06f + 0.22f * L(lr) : 0.72f + 0.2f * L(lr);
					std::vector<ImVec2> main{ { u0, 0.0f } };
					float u = u0, v = 0.0f;
					while (v < 0.40f) {
						v += 0.018f + 0.02f * L(lr);
						u += (L(lr) - 0.5f) * 0.03f;
						main.push_back({ u, v });
						if (L(lr) < 0.18f && bolt.size() < 4) {  // a fork
							std::vector<ImVec2> br{ { u, v } };
							float bu = u, bv = v;
							const float dir = L(lr) < 0.5f ? -1.0f : 1.0f;
							for (int i = 0; i < 6; ++i) {
								bv += 0.015f + 0.012f * L(lr);
								bu += dir * (0.006f + 0.012f * L(lr));
								br.push_back({ bu, bv });
							}
							bolt.push_back(br);
						}
					}
					bolt.insert(bolt.begin(), main);
				}
				if (life > 0) {
					life -= ImGui::GetIO().DeltaTime;
					const float flick = (life > 0.42f || (life > 0.2f && life < 0.3f)) ? 1.0f : 0.35f;  // it strikes, dims, strikes again
					fg->AddRectFilledMultiColor(at(0, 0), at(1, 0.6f), IM_COL32(200, 190, 255, static_cast<int>(22 * flick)), IM_COL32(200, 190, 255, static_cast<int>(22 * flick)),
						IM_COL32(200, 190, 255, 0), IM_COL32(200, 190, 255, 0));
					for (std::size_t b = 0; b < bolt.size(); ++b) {
						std::vector<ImVec2> pts;
						for (auto& q : bolt[b]) pts.push_back(at(q.x, q.y));
						const float w = (b == 0 ? 3.0f : 1.6f) * k;
						fg->AddPolyline(pts.data(), static_cast<int>(pts.size()), IM_COL32(150, 130, 255, static_cast<int>(60 * flick)), 0, w * 6);
						fg->AddPolyline(pts.data(), static_cast<int>(pts.size()), IM_COL32(190, 180, 255, static_cast<int>(130 * flick)), 0, w * 2.5f);
						fg->AddPolyline(pts.data(), static_cast<int>(pts.size()), IM_COL32(250, 248, 255, static_cast<int>(255 * flick)), 0, w);
					}
				}
			}
			// Skyrim's buttons (bottom-right) laid back over the picture: only the words, not its old backdrop
			if (g_frameSrv && g_keyPs) {
				g_keyCursor = Cursor(size);
				const ImVec2 a0{ size.x * 0.74f, size.y * 0.58f }, a1{ size.x, size.y * 0.97f };
				fg->AddCallback(KeyOn, nullptr);
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_frameSrv)), a0, a1, { 0.74f, 0.58f }, { 1.0f, 0.97f });
				fg->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
			}
			(void)dl;
			PartyPanel(fg, size, { 40 * k, size.y - 40 * k }, k, click);
			// our cursor: a gold-edged iron arrowhead with an ember at its tip
			const ImVec2 c = Cursor(size);
			if (c.x >= 0) {
				const ImVec2 p1{ c.x + 5 * k, c.y + 26 * k }, p2{ c.x + 11 * k, c.y + 18 * k }, p3{ c.x + 20 * k, c.y + 17 * k };
				fg->AddQuadFilled(c, p1, p2, p3, IM_COL32(28, 24, 22, 240));
				fg->AddQuad(c, p1, p2, p3, kGold, 1.6f * k);
				fg->AddCircleFilled(c, 3.0f * k, IM_COL32(255, 170, 80, 255));
				fg->AddCircleFilled(c, 7.0f * k, IM_COL32(255, 120, 40, 50));
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
			static const ImWchar kRanges[] = { 0x0020, 0x00FF, 0x2190, 0x21FF, 0x2200, 0x22FF, 0 };  // Latin + arrows + math (the wand's infinity)
			auto font = [&](const char* a_file) -> ImFont* {
				const std::string p = std::string("C:\\Windows\\Fonts\\") + a_file;
				return ::GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES ? io.Fonts->AddFontFromFileTTF(p.c_str(), 64.0f, nullptr, kRanges) : nullptr;
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
			int wicons = 0;
			for (std::size_t i = 0; i < gen::kGuns.size(); ++i) {
				const auto path = L"Data\\Interface\\UndyingNights\\w_" + std::wstring(gen::kGuns[i].id.begin(), gen::kGuns[i].id.end()) + L".png";
				if (SUCCEEDED(DirectX::CreateWICTextureFromFile(g_device, path.c_str(), nullptr, &g_weaponIcons[i]))) {
					++wicons;
				}
			}
			const bool bg = SUCCEEDED(DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\UndyingNights\\menu_bg.png", nullptr, &g_menuBg));
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\UndyingNights\\menu_fog.png", nullptr, &g_menuFog);
			g_init = true;
			logger::info("hud: ready ({} perk icons, {} weapon icons, menu art {})", icons, wicons, bg);
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
				g_state.loreTime = std::max(0.0f, g_state.loreTime - dt);
				g_state.questNoteTime = std::max(0.0f, g_state.questNoteTime - dt);
				g_state.nukeFlash = std::max(0.0f, g_state.nukeFlash - dt * 1.25f);
				s = g_state;
			}
			if (!Init(a_swap)) {
				return;
			}
			auto*      uiM = RE::UI::GetSingleton();
			// our picture only while the main menu itself is up: any of Skyrim's pop-ups over it (Load, settings, a
			// question, Creations, credits) shows as Skyrim draws it
			bool popup = false;
			if (uiM) {
				static const std::array<std::string_view, 6> kQuiet{ "Main Menu", "Cursor Menu", "Fader Menu", "Mist Menu", "HUD Menu", "Kinect Menu" };
				for (auto& [name, entry] : uiM->menuMap) {
					if (entry.menu && uiM->IsMenuOpen(name) && std::find(kQuiet.begin(), kQuiet.end(), std::string_view(name.c_str())) == kQuiet.end()) {
						popup = true;
						break;
					}
				}
			}
			const bool mainMenu = uiM && uiM->IsMenuOpen(RE::MainMenu::MENU_NAME) && !popup;
			const bool journal = uiM && uiM->IsMenuOpen(RE::JournalMenu::MENU_NAME) && !uiM->IsMenuOpen(RE::MainMenu::MENU_NAME);  // in game only
			const int  clicks = g_clicks.exchange(0);
			if (!InGame() && !mainMenu && !journal) {
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
			if (mainMenu) {
				MakeKeyShader();
				CopyFrame(back);
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
			if (mainMenu) {
				DrawMenu(io.DisplaySize, static_cast<float>(ImGui::GetTime()), clicks > 0);
			} else if (journal) {
				const float k = io.DisplaySize.y / 1080.0f;
				PartyPanel(ImGui::GetForegroundDrawList(), io.DisplaySize, { 40 * k, io.DisplaySize.y - 40 * k }, k, clicks > 0);
			} else {
				Draw(s, io.DisplaySize);
			}
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
				static auto netLast = std::chrono::steady_clock::now();
				const auto  netNow = std::chrono::steady_clock::now();
				Net::Tick(std::clamp(std::chrono::duration<float>(netNow - netLast).count(), 0.0f, 0.25f));  // the co-op party (menus too)
				netLast = netNow;
			} catch (...) {
			}
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
				const auto frame = std::chrono::duration_cast<C::duration>(std::chrono::duration<double>(1.0 / std::max(1.0, static_cast<double>(gen::kFpsCap))));
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
			// Unlimited frame rate: VSync off whatever SkyrimPrefs.ini says (the launcher keeps resetting it), and
			// Havok's step follows the real frame time so physics stays at normal speed at any frame rate.
			{
				using C = std::chrono::steady_clock;
				static C::time_point last = C::now();
				const auto  now = C::now();
				const float dt = std::chrono::duration<float>(now - last).count();
				last = now;
				static RE::Setting* maxTime = RE::INISettingCollection::GetSingleton() ? RE::INISettingCollection::GetSingleton()->GetSetting("fMaxTime:HAVOK") : nullptr;
				if (maxTime && dt > 0.0f) {
					maxTime->data.f = std::clamp(dt, 1.0f / 300.0f, 1.0f / 30.0f);
				}
				static bool said = false;
				if (!said) {
					said = true;
					logger::info("hud: frame rate unlimited (VSync {} -> off, Havok step follows the frame){}", a_sync, maxTime ? "" : " [no fMaxTime setting found]");
				}
			}
			return g_present(a_swap, gen::kFpsCap > 0 ? a_sync : 0, a_flags);
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

		void OnClick() { ++g_clicks; }

		void Lore(std::string a_title, std::string a_text, float a_seconds)
		{
			std::scoped_lock l(g_lock);
			g_state.loreTitle = std::move(a_title);
			g_state.loreText = std::move(a_text);
			g_state.loreTime = g_state.loreTotal = a_seconds;
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
