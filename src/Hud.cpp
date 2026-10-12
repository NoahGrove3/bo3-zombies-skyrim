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
		ID3D11ShaderResourceView* g_frame = nullptr;    // nine-slice panel: black field, iron and bronze border (tools/uiart.py)
		ID3D11ShaderResourceView* g_divider = nullptr;  // bronze line with a winged diamond
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
	// only the lower right (the buttons and their yes/no prompts): Skyrim's emblem, the Bethesda mark and the
	// Anniversary banner are never laid over the picture
	if (uv.x < 0.62 || uv.y < 0.58) a = 0;
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

		constexpr ImU32 kGold = IM_COL32(200, 160, 92, 255);     // aged bronze
		constexpr ImU32 kGoldDim = IM_COL32(200, 160, 92, 110);
		constexpr ImU32 kBlood = IM_COL32(178, 22, 22, 255);
		constexpr ImU32 kParch = IM_COL32(236, 226, 200, 255);
		constexpr ImU32 kGlass = IM_COL32(10, 10, 12, 170);

		ImU32 Fade(ImU32 c, float a) { return (c & 0x00FFFFFF) | (static_cast<ImU32>(((c >> 24) & 0xFF) * std::clamp(a, 0.0f, 1.0f)) << 24); }

		float EaseOut(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
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

		// Panel: a smoky black field in an iron-and-bronze frame with Nordic corner brackets (nine-slice of
		// frame.png: 80px corners of 256), or, without the art, dark glass with gold ticks.
		void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float k, float alpha = 1.0f)
		{
			if (g_frame) {
				const auto  tex = ImTextureID(reinterpret_cast<std::uintptr_t>(g_frame));
				const float c = std::min({ 40.0f * k, (b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f });
				const float u = 80.0f / 256.0f;
				const float xs[4] = { a.x, a.x + c, b.x - c, b.x }, ys[4] = { a.y, a.y + c, b.y - c, b.y };
				const float us[4] = { 0, u, 1 - u, 1 };
				const ImU32 tint = Fade(IM_COL32(255, 255, 255, 255), alpha);
				for (int i = 0; i < 3; ++i) {
					for (int j = 0; j < 3; ++j) {
						dl->AddImage(tex, { xs[i], ys[j] }, { xs[i + 1], ys[j + 1] }, { us[i], us[j] }, { us[i + 1], us[j + 1] }, tint);
					}
				}
				return;
			}
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
			if (g_divider) {
				const float h = std::max(14.0f * k, half * 48.0f / 512.0f);
				dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_divider)), { cx - half, y - h / 2 }, { cx + half, y + h / 2 }, { 0, 0 }, { 1, 1 }, Fade(IM_COL32(255, 255, 255, 255), alpha));
				return;
			}
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
							Txt(dl, g_body, 15 * k, { c0.x + 30 * k, c0.y + 15 * k }, Fade(IM_COL32(140, 136, 128, 255), al), "empty - call a spell at the shrine");
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
				TxtC(dl, g_num, std::round(44 * k), std::round(c.x), std::round(c.y - 23.5f * k), kParch, n);  // the digits' own middle on the ring's
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

			// health bars over the wounded: black iron plates with chamfered ends and bronze caps, the blood draining
			// from the right with a pale wound that follows it down; champions in gold with their names, the great in
			// violet, other foes with their names in iron (these replace Skyrim's bar under the compass)
			if (!s.perkMenu) {
				static std::unordered_map<std::uint32_t, std::pair<float, float>> wound;  // id -> (pale wound, fade in)
				static float sweep = 0;
				if ((sweep += dt) > 5.0f) {
					sweep = 0;
					std::erase_if(wound, [&](const auto& e) { return std::none_of(s.enemyBars.begin(), s.enemyBars.end(), [&](const auto& b) { return b.id == e.first; }); });
				}
				// bars that would overlap (foes standing together) are stacked: the nearer (larger) keeps its place, the
				// others step up above it, name and all
				auto bars = s.enemyBars;
				{
					std::vector<std::size_t> order(bars.size());
					for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
					std::sort(order.begin(), order.end(), [&](auto x, auto y) { return bars[x].scale > bars[y].scale; });
					std::vector<ImVec4> placed;  // x0, y0, x1, y1 in pixels
					for (auto i : order) {
						auto&       b = bars[i];
						const float w = ((b.rank == 2 ? 150 : b.rank == 1 ? 124 : 100) * k * b.scale) + 24 * k * b.scale;
						const float h = (b.rank >= 1 && b.rank <= 2 ? 9 : 7) * k * b.scale, top = h / 2 + 22 * k * b.scale, bot = h / 2 + 5 * k;
						const float cx = b.x * size.x;
						float       cy = b.y * size.y;
						for (int guard = 0; guard < 8; ++guard) {
							bool moved = false;
							for (const auto& r : placed) {
								if (cx + w / 2 > r.x && cx - w / 2 < r.z && cy + bot > r.y && cy - top < r.w) {
									cy = r.y - bot - 2 * k;
									moved = true;
								}
							}
							if (!moved) break;
						}
						placed.push_back({ cx - w / 2, cy - top, cx + w / 2, cy + bot });
						b.y = cy / size.y;
					}
				}
				for (const auto& b : bars) {
					auto& [pale, in] = wound.try_emplace(b.id, b.frac, 0.0f).first->second;
					pale = b.frac > pale ? b.frac : std::max(b.frac, pale - dt * 0.35f);
					in = std::min(1.0f, in + dt * 5.0f);
					const float sc = b.scale * (0.85f + 0.15f * EaseOut(in));
					const float w = (b.rank == 2 ? 150 : b.rank == 1 ? 124 : 100) * k * sc, h = (b.rank >= 1 && b.rank <= 2 ? 9 : 7) * k * sc;
					const ImVec2 c{ b.x * size.x, b.y * size.y };
					const ImVec2 a{ c.x - w / 2, c.y - h / 2 }, z{ c.x + w / 2, c.y + h / 2 };
					const float  al = in;
					const ImU32  col = IM_COL32((b.color >> 16) & 0xFF, (b.color >> 8) & 0xFF, b.color & 0xFF, 255);
					const ImU32  deep = IM_COL32(((b.color >> 16) & 0xFF) * 4 / 10, ((b.color >> 8) & 0xFF) * 4 / 10, (b.color & 0xFF) * 4 / 10, 255);
					const ImU32  trim = b.rank == 2 ? kGold : b.rank == 1 ? IM_COL32(150, 110, 210, 255) : IM_COL32(118, 104, 86, 255);
					const float  p = 3.0f * k * sc, e = h * 0.5f + p + 4.0f * k * sc;  // plate margin, chamfer reach
					const ImVec2 plate[6] = { { a.x - e, c.y }, { a.x - p, a.y - p }, { z.x + p, a.y - p }, { z.x + e, c.y }, { z.x + p, z.y + p }, { a.x - p, z.y + p } };
					dl->AddConvexPolyFilled(plate, 6, Fade(IM_COL32(9, 8, 10, 225), al));
					dl->AddPolyline(plate, 6, Fade(trim, 0.85f * al), ImDrawFlags_Closed, 1.2f * k);
					dl->AddRectFilled(a, z, Fade(IM_COL32(28, 8, 8, 235), al));                                       // the empty trough
					if (pale > b.frac) {
						dl->AddRectFilled({ a.x + w * b.frac, a.y }, { a.x + w * pale, z.y }, Fade(IM_COL32(222, 196, 160, 150), al));  // the fresh wound
					}
					dl->AddRectFilledMultiColor(a, { a.x + w * b.frac, z.y }, Fade(col, al), Fade(col, al), Fade(deep, al), Fade(deep, al));
					dl->AddLine({ a.x, a.y + 0.5f * k }, { a.x + w * b.frac, a.y + 0.5f * k }, Fade(IM_COL32(255, 210, 190, 120), al), 1.0f * k);  // wet sheen
					if (b.rank == 1 || b.rank == 2) {  // quarters, notched into the iron
						for (int q = 1; q < 4; ++q) {
							const float x = a.x + w * q / 4.0f;
							dl->AddLine({ x, a.y }, { x, z.y }, Fade(IM_COL32(0, 0, 0, 200), al), 1.2f * k);
						}
					}
					for (float ex : { a.x - e, z.x + e }) {  // bronze rivets on the points
						const float r = 2.6f * k * sc;
						dl->AddQuadFilled({ ex, c.y - r }, { ex + r, c.y }, { ex, c.y + r }, { ex - r, c.y }, Fade(trim, al));
					}
					if (!b.name.empty()) {
						const float fs = std::max(9.0f * k, (b.rank == 2 ? 12.5f : b.rank == 1 ? 11.5f : 11.0f) * k * sc);
						const ImU32 nc = b.rank == 2 ? IM_COL32(240, 206, 130, 255) : b.rank == 1 ? IM_COL32(214, 190, 255, 255) : IM_COL32(222, 212, 192, 255);
						std::string up = b.name;
						if (b.rank == 2 || b.rank == 1) {
							std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
						}
						ImFont*      nf = b.rank == 3 ? g_body : g_title;
						const ImVec2 ts = Measure(nf, fs, up);
						const ImVec2 tp{ std::round(c.x - ts.x * 0.5f), std::round(a.y - p - 3.0f * k - ts.y) };  // centred on the bar, just above it
						Txt(dl, nf, fs, tp, Fade(nc, al), up, 1.0f * k, IM_COL32(0, 0, 0, 255));
					}
				}
			}

			// The ritual circle: a ring of runes on the ground round the shrine. Its lit arc is the rite's progress; it
			// flashes red when the dead break it (and the timer starts over).
			if (s.ringOn && s.ringPts.size() > 8 && !s.perkMenu) {
				const float fl = std::clamp(s.ringFlash, 0.0f, 1.0f);
				const ImU32 base = s.ringHold ? IM_COL32(236, 168, 72, 255) : IM_COL32(170, 110, 255, 255);
				const ImU32 hot = IM_COL32(255, 60, 40, 255);
				const ImU32 col = fl > 0.01f ? hot : base;
				const int   n = static_cast<int>(s.ringPts.size());
				const int   lit = static_cast<int>(s.ringFrac * (n - 1));
				ImVec2      sum{ 0, 0 };
				int         front = 0;
				for (int i = 0; i + 1 < n; ++i) {
					const auto& p0 = s.ringPts[i];
					const auto& p1 = s.ringPts[i + 1];
					if (p0[2] <= 0 || p1[2] <= 0) {
						continue;  // behind the camera
					}
					const ImVec2 a{ p0[0] * size.x, p0[1] * size.y }, b{ p1[0] * size.x, p1[1] * size.y };
					if (std::abs(a.x - b.x) > size.x * 0.5f) {
						continue;
					}
					const bool on = i < lit;
					dl->AddLine(a, b, Fade(col, (on ? 0.30f : 0.12f) + 0.25f * fl), (on ? 14.0f : 9.0f) * k);   // the glow
					dl->AddLine(a, b, Fade(on ? IM_COL32(255, 236, 200, 255) : col, on ? 0.95f : 0.55f), (on ? 3.0f : 2.0f) * k);
					if (i % 6 == 0) {  // runes along the ring, pulsing in turn
						const float pr = 0.5f + 0.5f * std::sin(now * 3.0f - i * 0.3f);
						const float r = (4.0f + 2.0f * pr) * k;
						dl->AddQuadFilled({ a.x, a.y - r }, { a.x + r * 0.7f, a.y }, { a.x, a.y + r }, { a.x - r * 0.7f, a.y }, Fade(on ? IM_COL32(255, 230, 170, 255) : col, 0.5f + 0.5f * pr));
					}
					sum.x += a.x, sum.y += a.y, ++front;
				}
				if (front > 0) {
					const ImVec2 c{ sum.x / front, sum.y / front - 70 * k };
					const std::string head = s.ringHold ? (s.ringInside ? "STAND IN THE CIRCLE" : "HOLD THE SHRINE") : (s.ringInside ? "THE DEAD ARE IN THE CIRCLE" : "KEEP THE DEAD OUT");
					TxtC(dl, g_title, 20 * k, c.x, c.y, fl > 0.01f || s.ringInside ? IM_COL32(255, 120, 100, 255) : kParch, head, 2 * k, col);
					TxtC(dl, g_num, 26 * k, c.x, c.y + 26 * k, IM_COL32(236, 222, 190, 240), std::to_string(std::max(0, s.ringSeconds)) + "s", 1.5f * k, col);
				}
			}

			// The quest's way-mark: a burning diamond over the objective, or at the screen's edge pointing the way.
			if (s.objOn && !s.perkMenu) {
				const ImU32 col = IM_COL32(236, 168, 72, 255), dark = IM_COL32(20, 12, 8, 230);
				const float m = 70 * k;
				ImVec2      p{ s.objX * size.x, s.objY * size.y };
				const bool  onScreen = s.objFront && p.x > m && p.x < size.x - m && p.y > m && p.y < size.y - m;
				const int   meters = static_cast<int>(s.objDist * 0.01428f);
				const std::string dist = std::to_string(meters) + " m";
				const float pulse = 0.75f + 0.25f * std::sin(now * 3.0f);
				auto diamond = [&](ImVec2 c, float r) {
					for (int i = 3; i >= 1; --i) {
						const float g = r * (1.0f + 0.45f * i);
						dl->AddQuadFilled({ c.x, c.y - g }, { c.x + g * 0.7f, c.y }, { c.x, c.y + g }, { c.x - g * 0.7f, c.y }, Fade(col, 0.07f * pulse));
					}
					dl->AddQuadFilled({ c.x, c.y - r }, { c.x + r * 0.7f, c.y }, { c.x, c.y + r }, { c.x - r * 0.7f, c.y }, dark);
					dl->AddQuad({ c.x, c.y - r }, { c.x + r * 0.7f, c.y }, { c.x, c.y + r }, { c.x - r * 0.7f, c.y }, col, 2.0f * k);
					const float q = r * 0.42f;
					dl->AddQuadFilled({ c.x, c.y - q }, { c.x + q * 0.7f, c.y }, { c.x, c.y + q }, { c.x - q * 0.7f, c.y }, Fade(IM_COL32(255, 210, 120, 255), pulse));
				};
				if (onScreen) {
					diamond(p, 15 * k);
					TxtC(dl, g_title, 17 * k, p.x, p.y - 46 * k, IM_COL32(236, 222, 190, 235), s.objLabel, 1.5f * k, col);
					TxtC(dl, g_num, 15 * k, p.x, p.y + 20 * k, IM_COL32(220, 196, 150, 220), dist);
				} else {
					ImVec2 d{ p.x - size.x * 0.5f, p.y - size.y * 0.5f };
					if (!s.objFront) {
						d = { -d.x, -d.y };
						if (std::abs(d.y) < 1.0f) d.y = 1.0f;
						d.y = std::abs(d.y) * 0.25f + size.y * 0.2f;  // behind you: shown low on the screen, turned the right way
					}
					const float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
					const ImVec2 u{ d.x / len, d.y / len };
					const float  hx = size.x * 0.5f - m, hy = size.y * 0.5f - m;
					const float  t = std::min(std::abs(u.x) > 1e-4f ? hx / std::abs(u.x) : 1e9f, std::abs(u.y) > 1e-4f ? hy / std::abs(u.y) : 1e9f);
					const ImVec2 c{ size.x * 0.5f + u.x * t, size.y * 0.5f + u.y * t };
					diamond(c, 13 * k);
					const ImVec2 tip{ c.x + u.x * 34 * k, c.y + u.y * 34 * k }, n{ -u.y, u.x };
					const ImVec2 b0{ c.x + u.x * 20 * k + n.x * 9 * k, c.y + u.y * 20 * k + n.y * 9 * k }, b1{ c.x + u.x * 20 * k - n.x * 9 * k, c.y + u.y * 20 * k - n.y * 9 * k };
					dl->AddTriangleFilled(tip, b0, b1, Fade(col, pulse));
					TxtC(dl, g_num, 14 * k, c.x - u.x * 34 * k, c.y - u.y * 34 * k - 8 * k, IM_COL32(220, 196, 150, 220), dist);
				}
			}

			// The shrine's menu: docked at the right edge and see-through (the dead keep coming while it is open),
			// every choice one key away. Its roll is drawn here too, and a small strip under the crosshair while the
			// menu is closed.
			const auto spellCol = [](int g, float a) {
				const std::uint32_t c = g >= 0 ? gen::kGuns[g].color : 0xC8A060;
				return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, static_cast<int>(255 * std::clamp(a, 0.0f, 1.0f)));
			};
			const auto rollOrb = [&](ImVec2 c, float R) {  // the roll: a ring that fills, then the spells flicker past, then the one it chose
				const ImU32 rc = s.rollPhase >= 2 ? spellCol(s.rollGun, 1.0f) : IM_COL32(170, 110, 255, 255);
				dl->AddCircleFilled(c, R, IM_COL32(8, 6, 10, 200), 48);
				dl->AddCircle(c, R, Fade(kGold, 0.55f), 48, 1.5f * k);
				if (s.rollPhase == 1) {  // gathering: the ring fills and the light swells
					dl->PathArcTo(c, R - 4 * k, -RE::NI_PI / 2, -RE::NI_PI / 2 + 2 * RE::NI_PI * s.rollT, 48);
					dl->PathStroke(rc, 0, 4 * k);
					dl->AddCircleFilled(c, R * 0.55f * s.rollT, Fade(rc, 0.35f + 0.3f * std::sin(now * 18.0f)), 32);
				} else if (s.rollPhase >= 2) {
					const float spin = s.rollPhase == 2 ? now * (9.0f - 7.0f * s.rollT) : now * 0.8f;
					for (int q = 0; q < 6; ++q) {  // runes turning round the rim
						const float a = spin + q * RE::NI_PI / 3.0f;
						dl->AddCircleFilled({ c.x + std::cos(a) * (R - 5 * k), c.y + std::sin(a) * (R - 5 * k) }, 2.6f * k, rc, 8);
					}
					dl->AddCircleFilled(c, R * 0.82f, Fade(rc, 0.16f), 40);
					if (auto* icon = s.rollGun >= 0 ? g_weaponIcons[s.rollGun] : nullptr) {
						const float ih = R * 1.5f, iw = ih * 512.0f / 192.0f;  // the sigil end of the strip sits in the orb
						const float pop = s.rollPhase == 3 ? 1.0f + 0.05f * std::sin(now * 4.0f) : 1.0f;
						dl->PushClipRect({ c.x - R * 0.85f, c.y - R * 0.85f }, { c.x + R * 0.85f, c.y + R * 0.85f }, true);
						dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { c.x - ih * 0.5f * pop, c.y - ih * 0.5f * pop }, { c.x - ih * 0.5f * pop + iw * pop, c.y + ih * 0.5f * pop }, { 0, 0 }, { 1, 1 });
						dl->PopClipRect();
					}
					if (s.rollPhase == 3) {  // the time left to take it
						dl->PathArcTo(c, R + 4 * k, -RE::NI_PI / 2, -RE::NI_PI / 2 + 2 * RE::NI_PI * (1.0f - s.rollT), 48);
						dl->PathStroke(Fade(rc, 0.9f), 0, 3 * k);
					}
				} else {
					dl->AddCircle(c, R * 0.6f, Fade(IM_COL32(170, 110, 255, 255), 0.35f + 0.15f * std::sin(now * 2.0f)), 6, 1.5f * k);  // a sleeping hexagram
					dl->AddCircle(c, R * 0.35f, Fade(kGold, 0.35f), 32, 1.0f * k);
				}
			};
			const auto keyCap = [&](ImVec2 at, const std::string& key, bool live) {
				const ImVec2 kb{ at.x + 26 * k, at.y + 26 * k };
				dl->AddRectFilled(at, kb, live ? IM_COL32(60, 30, 18, 230) : IM_COL32(26, 22, 20, 210), 3 * k);
				dl->AddRect(at, kb, live ? kGold : kGoldDim, 3 * k, 0, 1.2f * k);
				TxtC(dl, g_num, 17 * k, at.x + 13 * k, at.y + 3 * k, live ? kParch : IM_COL32(150, 140, 125, 255), key);
			};
			if (s.perkMenu) {
				const float w = 470 * k;
				const bool  shout = s.shrineShout >= 0 && s.shrineShout < static_cast<int>(gen::kShouts.size());
				const float h = (shout ? 590 : 512) * k;
				const ImVec2 a{ size.x - w - 36 * k, std::max(24 * k, size.y * 0.5f - h * 0.5f - 30 * k) }, b{ a.x + w, a.y + h };
				Panel(dl, a, b, k, 0.92f);
				const float cx = (a.x + b.x) * 0.5f;
				TxtC(dl, g_title, 27 * k, cx, a.y + 16 * k, IM_COL32(232, 214, 176, 255), "SHRINE OF THE DIRGE", 2.5f * k, IM_COL32(150, 70, 255, 255));
				Divider(dl, cx, a.y + 54 * k, w * 0.38f, k);
				float y = a.y + 68 * k;
				const float L = a.x + 20 * k, Rr = b.x - 20 * k;
				auto price = [&](float py, int cost, bool can) {
					const std::string pr = std::to_string(cost);
					const float pw = Measure(g_num, 21 * k, pr).x;
					const ImU32 pc = can ? IM_COL32(236, 196, 112, 255) : IM_COL32(190, 70, 56, 255);
					Txt(dl, g_num, 21 * k, { Rr - pw, py }, pc, pr);
					const ImVec2 coin{ Rr - pw - 13 * k, py + 12 * k };
					dl->AddCircleFilled(coin, 7 * k, Fade(pc, 0.9f));
					dl->AddCircle(coin, 4.5f * k, IM_COL32(60, 40, 20, 200), 0, 1.3f * k);
				};
				// [1] roll a spell
				{
					const float R = 46 * k;
					rollOrb({ L + R, y + R + 4 * k }, R);
					const float tx = L + 2 * R + 16 * k;
					keyCap({ tx, y + 4 * k }, "1", s.rollPhase == 0);
					Txt(dl, g_title, 21 * k, { tx + 36 * k, y + 5 * k }, kParch, s.rollPhase == 0 ? "CALL A SPELL" : s.rollPhase == 1 ? "THE SHRINE GATHERS..." : s.rollPhase == 2 ? "THE SPELLS TURN..." : std::string(gen::kGuns[std::max(0, s.rollGun)].label));
					Txt(dl, g_body, 17 * k, { tx, y + 38 * k }, IM_COL32(196, 182, 160, 235),
						s.rollPhase == 3 ? "Activate the shrine to take it" : "Every spell you lack has an equal chance");
					if (s.rollPhase == 0) {
						price(y + 66 * k, s.rollCost, s.points >= s.rollCost);
					}
					y += 2 * R + 22 * k;
				}
				Divider(dl, cx, y, w * 0.30f, k, 0.6f);
				y += 14 * k;
				// [2] reforge the spell in hand
				{
					const int  act = s.activeSlot >= 0 && s.activeSlot < s.slotCount ? s.activeSlot : -1;
					const int  g = act >= 0 ? s.slotGun[act] : -1;
					const int  lvl = act >= 0 ? std::clamp(s.slotLevel[act], 0, gen::kForgeMax) : 0;
					const bool can = g >= 0 && lvl < gen::kForgeMax;
					keyCap({ L, y + 2 * k }, "2", can);
					Txt(dl, g_title, 21 * k, { L + 36 * k, y + 3 * k }, kParch, "REFORGE");
					Txt(dl, g_body, 17 * k, { L + 36 * k, y + 32 * k }, IM_COL32(196, 182, 160, 235),
						g < 0 ? "Hold a spell to reforge it" : std::string(gen::kGuns[g].label) + (lvl >= gen::kForgeMax ? "  -  ascended" : "  -  more power, and its signature grows"));
					for (int q = 0; q < gen::kForgeMax; ++q) {  // the forge marks
						const ImVec2 pc{ L + 150 * k + q * 18 * k, y + 14 * k };
						dl->AddQuadFilled({ pc.x, pc.y - 6 * k }, { pc.x + 5 * k, pc.y }, { pc.x, pc.y + 6 * k }, { pc.x - 5 * k, pc.y }, q < lvl ? spellCol(g, 1.0f) : IM_COL32(60, 52, 46, 220));
					}
					if (can) {
						price(y + 2 * k, gen::kForgeCost[lvl], s.points >= gen::kForgeCost[lvl]);
					}
					y += 64 * k;
				}
				Divider(dl, cx, y, w * 0.30f, k, 0.6f);
				y += 14 * k;
				// [3] brews: a row of medallions, the chosen one described under it
				{
					const int n = static_cast<int>(gen::kPerks.size());
					int owned = 0;
					for (int q = 0; q < n; ++q) {
						owned += (s.perks >> q) & 1;
					}
					const int  sel = std::clamp(s.perkSel, 0, n - 1);
					const auto& p = gen::kPerks[sel];
					const bool have = (s.perks >> sel) & 1;
					keyCap({ L, y + 2 * k }, "3", !have);
					Txt(dl, g_title, 21 * k, { L + 36 * k, y + 3 * k }, kParch, "DRINK A BREW");
					Txt(dl, g_body, 16 * k, { Rr - Measure(g_body, 16 * k, std::to_string(owned) + " of " + std::to_string(gen::kMaxPerks)).x, y + 6 * k }, IM_COL32(170, 156, 132, 220), std::to_string(owned) + " of " + std::to_string(gen::kMaxPerks));
					y += 36 * k;
					const float d = std::min(44 * k, (Rr - L) / n - 4 * k), gap = ((Rr - L) - d * n) / std::max(1, n - 1);
					for (int q = 0; q < n; ++q) {
						const ImVec2 c{ L + d * 0.5f + q * (d + gap), y + d * 0.5f };
						const bool   on = q == sel, drunk = (s.perks >> q) & 1;
						if (on) {
							dl->AddCircleFilled(c, d * 0.62f, IM_COL32(200, 80, 30, 90), 32);
							dl->AddCircle(c, d * 0.62f, kGold, 32, 2.0f * k);
						}
						if (auto* icon = g_perkIcons[q]) {
							const float r = d * 0.5f * (on ? 1.08f : 1.0f);
							dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { c.x - r, c.y - r }, { c.x + r, c.y + r }, { 0, 0 }, { 1, 1 }, drunk ? IM_COL32(120, 120, 120, 150) : IM_COL32(255, 255, 255, on ? 255 : 200));
						}
					}
					y += d + 12 * k;
					Txt(dl, g_title, 19 * k, { L, y }, have ? IM_COL32(130, 124, 112, 255) : IM_COL32(250, 236, 205, 255), std::string(p.label));
					if (have) {
						Txt(dl, g_title, 17 * k, { Rr - Measure(g_title, 17 * k, "DRUNK").x, y + 2 * k }, IM_COL32(120, 170, 110, 220), "DRUNK");
					} else {
						price(y, p.cost, s.points >= p.cost);
					}
					Txt(dl, g_body, 16 * k, { L, y + 28 * k }, IM_COL32(190, 178, 156, have ? 130 : 230), std::string(gen::kPerkDesc[sel]));
					y += 58 * k;
				}
				// [4] the freed city's Thu'um
				if (shout) {
					const auto& sh = gen::kShouts[s.shrineShout];
					const ImU32 scol = IM_COL32((sh.color >> 16) & 0xFF, (sh.color >> 8) & 0xFF, sh.color & 0xFF, 255);
					Divider(dl, cx, y, w * 0.30f, k, 0.6f);
					y += 14 * k;
					keyCap({ L, y + 2 * k }, "4", true);
					Txt(dl, g_title, 21 * k, { L + 36 * k, y + 3 * k }, scol, "LEARN THE THU'UM: " + std::string(sh.label), 1.5f * k, scol);
					Txt(dl, g_body, 17 * k, { L + 36 * k, y + 32 * k }, IM_COL32(210, 196, 170, 235), std::string(sh.w1) + "  " + std::string(sh.w2) + "  " + std::string(sh.w3) + "  -  freely given");
					y += 64 * k;
				}
				Divider(dl, cx, b.y - 58 * k, w * 0.38f, k, 0.7f);
				TxtC(dl, g_body, 16 * k, cx, b.y - 48 * k, IM_COL32(200, 186, 160, 225), "Wheel or Left / Right: choose a brew   \xC2\xB7   E or Backspace leave");
				TxtC(dl, g_body, 15 * k, cx, b.y - 27 * k, IM_COL32(160, 148, 128, 200), "Pad: X call  \xC2\xB7  Y reforge  \xC2\xB7  A drink  \xC2\xB7  D-pad choose / up Thu'um  \xC2\xB7  B leave");
			} else if (s.rollPhase > 0) {
				// the roll with the menu closed: a small strip under the crosshair
				const float R = 30 * k;
				const ImVec2 c{ size.x * 0.5f - 90 * k, size.y * 0.5f + 150 * k };
				rollOrb(c, R);
				const std::string t = s.rollPhase == 1 ? "The shrine gathers..." : s.rollPhase == 2 ? "The spells turn..." : std::string(gen::kGuns[std::max(0, s.rollGun)].label) + "  -  take it at the shrine";
				Txt(dl, g_title, 19 * k, { c.x + R + 12 * k, c.y - 12 * k }, s.rollPhase == 3 ? spellCol(s.rollGun, 1.0f) : kParch, t, 1.5f * k, IM_COL32(0, 0, 0, 255));
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
		// a: the panel's bottom-left corner (or its top-left, a_fromTop)
		void PartyPanel(ImDrawList* dl, ImVec2 size, ImVec2 a, float k, bool click, bool a_fromTop = false)
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
			if (a_fromTop) {
				a.y += h;
			}
			a.y = std::min(a.y, size.y - 12 * k);  // never below the screen
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

		// ---- the main menu (Spec 13): the Undying Nights entrance
		// Layered art (tools/menuart.py) with parallax that follows the cursor and drifts slowly, fog in two depths,
		// the rune-stone's soul-fire and the ground ring pulsing, braziers flickering, embers and ash rising, now and
		// then a cold lightning flash; the title; Skyrim's own menu entries redrawn as our buttons (Skyrim keeps
		// doing the work: mouse, keyboard and pad act on its real list, we only draw over it).
		struct MenuEntry { std::string text; int index; bool sel, disabled; };
		// Skyrim's main list as it stands (its entries and which is selected); empty if it isn't up. Its own list is
		// moved off the screen while ours shows (the keys still drive it; our buttons drive it by mouse).
		std::vector<MenuEntry> ReadMainList(RE::GFxMovieView** a_movie = nullptr)
		{
			std::vector<MenuEntry> out;
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
			RE::GFxMovieView* mv = menu ? menu->uiMovie.get() : nullptr;
			if (!mv) {
				return out;
			}
			RE::GFxValue holder;
			if (!mv->GetVariable(&holder, "_root.MenuHolder.Menu_mc.MainListHolder") || !holder.IsDisplayObject()) {
				return out;
			}
			RE::GFxValue::DisplayInfo hi;
			holder.GetDisplayInfo(&hi);
			if (!hi.GetVisible() || hi.GetAlpha() < 20.0) {
				return out;  // a sub-panel (Load, Creations, a question) has the screen
			}
			if (RE::GFxValue conf; mv->GetVariable(&conf, "_root.MenuHolder.Menu_mc.ConfirmPanel_mc") && conf.IsDisplayObject()) {
				RE::GFxValue::DisplayInfo ci;
				conf.GetDisplayInfo(&ci);
				if (ci.GetVisible() && ci.GetAlpha() > 20.0) {
					return out;  // a question is asked (Continue? New game?): it speaks alone
				}
			}
			RE::GFxValue list, entries, selV;
			if (!mv->GetVariable(&list, "_root.MenuHolder.Menu_mc.MainList") || !list.IsDisplayObject()) {
				return out;
			}
			list.GetMember("entryList", &entries);
			list.GetMember("selectedIndex", &selV);
			if (!entries.IsArray()) {
				return out;
			}
			const int sel = selV.IsNumber() ? static_cast<int>(selV.GetNumber()) : -1;
			for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i) {
				RE::GFxValue e, t, dis;
				entries.GetElement(i, &e);
				e.GetMember("text", &t);
				if (!t.IsString()) {
					continue;
				}
				e.GetMember("disabled", &dis);
				std::string txt = t.GetString();
				if (!txt.empty() && txt[0] == '$') {  // a translation key ($CONTINUE): our own words for it
					txt = txt.substr(1);
				}
				out.push_back({ txt, static_cast<int>(i), static_cast<int>(i) == sel, dis.IsBool() && dis.GetBool() });
			}
			// Skyrim's own list leaves the screen (it keeps working for keys and pad)
			RE::GFxValue::DisplayInfo li;
			list.GetDisplayInfo(&li);
			if (li.GetX() < 5000.0) {
				li.SetX(li.GetX() + 10000.0);
				list.SetDisplayInfo(li);
			}
			if (a_movie) {
				*a_movie = mv;
			}
			return out;
		}
		void PressMainList(RE::GFxMovieView* a_mv, int a_index, bool a_press)
		{
			RE::GFxValue list;
			if (!a_mv || !a_mv->GetVariable(&list, "_root.MenuHolder.Menu_mc.MainList") || !list.IsDisplayObject()) {
				return;
			}
			RE::GFxValue cur;
			list.GetMember("selectedIndex", &cur);
			if (!cur.IsNumber() || static_cast<int>(cur.GetNumber()) != a_index) {
				list.SetMember("selectedIndex", RE::GFxValue(static_cast<double>(a_index)));
			}
			if (a_press) {
				RE::GFxValue arg(0.0);
				list.Invoke("onItemPress", nullptr, &arg, 1);
			}
		}

		ID3D11ShaderResourceView* g_mPlate = nullptr;   // menu_plate.jpg: the in-game night (tools/plates.py)
		ID3D11ShaderResourceView* g_mAurora = nullptr;  // menu_aurora.png: its aurora alone, for the shimmer
		ID3D11ShaderResourceView* g_mTitle = nullptr;

		float g_pressT = -10, g_pressY = 0;  // the main menu entry last pressed (its flare)
		// Skyrim's own question box on the main menu (Continue? Start a new game?) is up
		bool MenuQuestionUp()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
			RE::GFxMovieView* mv = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue      conf;
			if (!mv || !mv->GetVariable(&conf, "_root.MenuHolder.Menu_mc.ConfirmPanel_mc") || !conf.IsDisplayObject()) {
				return false;
			}
			RE::GFxValue::DisplayInfo ci;
			conf.GetDisplayInfo(&ci);
			return ci.GetVisible() && ci.GetAlpha() > 20.0;
		}

		void DrawMenu(ImVec2 size, float now, bool click)
		{
			auto*       fg = ImGui::GetForegroundDrawList();
			const float k = size.y / 1080.0f;
			const float dt = ImGui::GetIO().DeltaTime;
			// opening: the scene fades up from black and the title settles in (a moment, never a wait)
			static float lastSeen = -10.0f, openT = 0.0f;
			if (now - lastSeen > 0.5f) {
				openT = 0.0f;
			}
			lastSeen = now;
			openT += dt;
			const float fade = std::clamp(openT / 1.1f, 0.0f, 1.0f);
			// parallax: the cursor leans the world a little, and it breathes on its own
			const ImVec2 cur = Cursor(size);
			static ImVec2 lean{ 0, 0 };
			const ImVec2 want = cur.x >= 0 ? ImVec2{ std::clamp(cur.x / size.x - 0.5f, -0.5f, 0.5f), std::clamp(cur.y / size.y - 0.5f, -0.5f, 0.5f) } : ImVec2{ 0, 0 };
			lean.x += (want.x - lean.x) * std::min(1.0f, dt * 1.5f);
			lean.y += (want.y - lean.y) * std::min(1.0f, dt * 1.5f);
			const ImVec2 drift{ std::sin(now * 0.05f) * 0.5f, std::cos(now * 0.037f) * 0.5f };
			// the art covers the screen (16:9, 6% overscan for the parallax)
			float cw = size.x, ch = size.x * 9.0f / 16.0f;
			if (ch < size.y) {
				ch = size.y, cw = size.y * 16.0f / 9.0f;
			}
			const float iw = cw * 1.06f, ih = ch * 1.06f;
			const ImU32 white = IM_COL32(255, 255, 255, static_cast<int>(255 * fade));
			fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, 255));
			// the picture: a night staged in Skyrim itself (tools/plates.py), the horde coming down from Bleak Falls
			// Barrow under the aurora. It breathes with a very slow push-in; the cursor leans it a little.
			const float push = 1.0f + 0.012f * (0.5f + 0.5f * std::sin(now * 0.035f));
			const float pw = iw * push, ph = ih * push;
			const ImVec2 po{ (size.x - pw) / 2 - (lean.x + drift.x * 0.6f) * pw * 0.02f, (size.y - ph) / 2 - (lean.y + drift.y * 0.6f) * ph * 0.012f };
			auto P = [&](float x, float y) { return ImVec2{ po.x + x / 2560.0f * pw, po.y + y / 1440.0f * ph }; };  // plate pixels -> screen
			if (g_mPlate) {
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_mPlate)), po, { po.x + pw, po.y + ph }, { 0, 0 }, { 1, 1 }, white);
			} else if (g_menuBg) {  // (the plate is missing: the old single picture)
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_menuBg)), { (size.x - cw) / 2, (size.y - ch) / 2 }, { (size.x + cw) / 2, (size.y + ch) / 2 });
			}
			// the aurora moves: its own cut-out, brightened in slow waves and slid a hair to and fro
			if (g_mAurora) {
				for (int band = 0; band < 2; ++band) {
					const float w8 = 0.5f + 0.5f * std::sin(now * (0.21f + 0.07f * band) + band * 2.1f);
					const float du = std::sin(now * (0.05f + 0.02f * band) + band) * 0.004f;
					fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_mAurora)), po, { po.x + pw, po.y + ph }, { du, 0 }, { 1 + du, 1 },
						IM_COL32(150, 255, 210, static_cast<int>((26 + 44 * w8) * fade)));
				}
			}
			// far fog over the snow, drifting
			if (g_menuFog) {
				const float u0 = now * 0.006f;
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_menuFog)), { 0, size.y * 0.42f }, { size.x, size.y * 0.70f }, { u0, 0 }, { u0 + 1.4f, 0.8f },
					IM_COL32(150, 170, 200, static_cast<int>(60 * fade)));
			}
			// the risen's eyes: cold and pale, each pair on its own breath, now and then a blink
			{
				struct Eye { float x, y, gap, ph; };
				// only the faces that look out of the plate (the Hollow Bride and the risen on the barrow steps), at their
				// eye sockets in menu_plate.jpg's own pixels
				static const Eye eyes[] = { { 651, 825.5f, 7.5f, 2.1f }, { 1158, 685, 6.7f, 1.7f }, { 1135, 704.5f, 7, 0.4f }, { 1250, 689, 6, 2.4f } };
				for (const auto& e : eyes) {
					if (std::fmod(now * 0.6f + e.ph * 3.0f, 7.0f) < 0.13f) {
						continue;
					}
					const float br = 0.75f + 0.25f * std::sin(now * 1.7f + e.ph * 2.0f);
					const ImVec2 c = P(e.x, e.y);
					const float  g = e.gap / 2560.0f * pw * 0.5f;
					for (float sx : { -g, g }) {
						fg->AddCircleFilled({ c.x + sx, c.y }, 3.6f * k, IM_COL32(110, 200, 255, static_cast<int>(34 * br * fade)), 12);
						fg->AddCircleFilled({ c.x + sx, c.y }, 1.1f * k, IM_COL32(205, 242, 255, static_cast<int>(240 * br * fade)), 8);
					}
				}
			}
			// near fog, the other way
			if (g_menuFog) {
				const float u0 = -now * 0.005f;
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_menuFog)), { 0, size.y * 0.70f }, { size.x, size.y }, { u0, 0 }, { u0 + 0.9f, 0.7f },
					IM_COL32(120, 140, 170, static_cast<int>(90 * fade)));
			}
			// snow and grave-ash drifting down on the wind, a few soul-motes rising
			{
				struct Mote { float x, y, sp, sway, ph, r; int kind; };
				static std::vector<Mote> mo;
				static std::mt19937 rng{ 7 };
				std::uniform_real_distribution<float> U(0, 1);
				if (mo.empty()) {
					for (int i = 0; i < 160; ++i) {
						mo.push_back({ U(rng), U(rng), 0.02f + 0.05f * U(rng), 0.004f + 0.012f * U(rng), U(rng) * 6.28f, 0.6f + 1.6f * U(rng), i % 9 == 0 ? 1 : 0 });
					}
				}
				for (auto& m : mo) {
					m.y += (m.kind == 1 ? -0.6f : 1.0f) * m.sp * dt;
					m.x += (m.kind == 1 ? 0.0f : 0.012f) * dt;  // the wind
					if (m.y > 1.03f) m.y = -0.03f, m.x = U(rng);
					if (m.y < -0.03f) m.y = 0.9f + 0.1f * U(rng), m.x = 0.05f + 0.5f * U(rng);
					if (m.x > 1.03f) m.x = -0.03f;
					const ImVec2 p{ (m.x + std::sin(now * 0.8f + m.ph) * m.sway) * size.x, m.y * size.y };
					const float  tw = 0.6f + 0.4f * std::sin(now * 3.0f + m.ph * 3);
					const ImU32  c = m.kind == 1 ? IM_COL32(140, 255, 210, static_cast<int>(170 * tw * fade)) : IM_COL32(220, 228, 240, static_cast<int>(120 * fade));
					if (m.kind == 1) fg->AddCircleFilled(p, m.r * k * 3.0f, Fade(c, 0.15f));
					fg->AddCircleFilled(p, m.r * k, c);
				}
			}
			// now and then, far off, a cold flash behind the barrow (rare, short, never bright)
			{
				static float next = 9.0f, life = 0;
				static std::mt19937 lr{ 11 };
				std::uniform_real_distribution<float> L(0, 1);
				if (now > next && life <= 0) {
					life = 0.42f;
					next = now + 11.0f + 12.0f * L(lr);
				}
				if (life > 0) {
					life -= dt;
					const float flick = (life > 0.30f || (life > 0.12f && life < 0.2f)) ? 1.0f : 0.25f;
					fg->AddRectFilledMultiColor({ 0, 0 }, { size.x, size.y * 0.5f }, IM_COL32(170, 190, 235, static_cast<int>(22 * flick)), IM_COL32(170, 190, 235, static_cast<int>(22 * flick)),
						IM_COL32(170, 190, 235, 0), IM_COL32(170, 190, 235, 0));
				}
			}
			// vignette
			fg->AddRectFilledMultiColor({ 0, 0 }, { size.x, size.y * 0.25f }, IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
			fg->AddRectFilledMultiColor({ 0, size.y * 0.75f }, size, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150));
			// the title, settling in
			if (g_mTitle) {
				const float tw = size.x * 0.62f, th = tw * 396.0f / 2479.0f;
				const float ty = size.y * 0.06f + (1.0f - EaseOut(std::clamp(openT / 1.6f, 0.0f, 1.0f))) * 14 * k;
				const ImVec2 a{ (size.x - tw) / 2 - lean.x * 6 * k, ty };
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_mTitle)), a, { a.x + tw, a.y + th }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * std::clamp(openT / 1.6f, 0.0f, 1.0f))));
			}
			// a question (Start a new game?) gets a dark iron plate behind its words, so they read over the rocks
			{
				static float qa = 0;
				qa = std::clamp(qa + (MenuQuestionUp() ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime * 6.0f, 0.0f, 1.0f);
				if (qa > 0) {
					const ImVec2 a{ size.x * 0.615f, size.y * 0.635f }, z{ size.x * 0.895f, size.y * 0.955f };
					fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, static_cast<int>(70 * qa)));
					fg->AddRectFilled(a, z, IM_COL32(8, 8, 11, static_cast<int>(228 * qa)), 3 * k);
					fg->AddRectFilledMultiColor(a, { z.x, a.y + (z.y - a.y) * 0.5f }, IM_COL32(40, 70, 64, static_cast<int>(40 * qa)), IM_COL32(40, 70, 64, static_cast<int>(40 * qa)), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
					fg->AddRect(a, z, IM_COL32(118, 104, 86, static_cast<int>(200 * qa)), 3 * k, 0, 1.2f * k);
					fg->AddRect({ a.x + 5 * k, a.y + 5 * k }, { z.x - 5 * k, z.y - 5 * k }, IM_COL32(118, 104, 86, static_cast<int>(70 * qa)), 2 * k, 0, 1.0f * k);
					const float dy = size.y * 0.865f;  // a rule between the question and its answers
					fg->AddLine({ a.x + 40 * k, dy }, { z.x - 40 * k, dy }, IM_COL32(110, 255, 190, static_cast<int>(70 * qa)), 1.0f * k);
				}
			}
			// Skyrim's questions and hints (lower right) laid back over the picture: only the bright words
			if (g_frameSrv && g_keyPs) {
				g_keyCursor = { -1000, -1000 };
				fg->AddCallback(KeyOn, nullptr);
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_frameSrv)), { 0, 0 }, size, { 0, 0 }, { 1, 1 });
				fg->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
			}
			// the menu: Skyrim's entries as our buttons, in a column on the right (keys and pad move Skyrim's own
			// selection, the mouse moves it by hovering ours; a click or Enter does what Skyrim would)
			{
				RE::GFxMovieView* mv = nullptr;
				const auto entries = ReadMainList(&mv);
				static float selY = -1;
				static int   hoverIdx = -1;
				const float  rowH = 58 * k;
				const float  right = size.x - 90 * k, left = right - 420 * k;
				const float  top = size.y * 0.60f - rowH * entries.size() * 0.5f;
				int hovered = -1;
				for (std::size_t i = 0; i < entries.size(); ++i) {
					const auto& e = entries[i];
					const float cy = top + rowH * (i + 0.5f);
					const float stagger = std::clamp((openT - 0.25f - 0.07f * i) / 0.4f, 0.0f, 1.0f);  // they slide in one after another
					const float dx = (1.0f - EaseOut(stagger)) * 40 * k;
					const ImVec2 pa{ left + dx, cy - rowH * 0.42f }, pb{ right + dx, cy + rowH * 0.42f };
					const bool over = cur.x >= pa.x && cur.x <= pb.x && cur.y >= pa.y && cur.y <= pb.y && !e.disabled;
					if (over) {
						hovered = static_cast<int>(i);
					}
					fg->AddRectFilledMultiColor(pa, pb, IM_COL32(4, 6, 10, 0), IM_COL32(8, 10, 14, static_cast<int>((e.sel ? 230 : 150) * stagger)), IM_COL32(8, 10, 14, static_cast<int>((e.sel ? 230 : 150) * stagger)), IM_COL32(4, 6, 10, 0));
					const ImU32 tc = e.disabled ? IM_COL32(100, 102, 108, static_cast<int>(200 * stagger)) : e.sel ? IM_COL32(245, 236, 214, static_cast<int>(255 * stagger)) : IM_COL32(170, 182, 196, static_cast<int>(225 * stagger));
					std::string up = e.text;
					std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
					const float fs = (e.sel ? 32.0f : 27.0f) * k;
					const ImVec2 ts = Measure(g_title, fs, up);
					Txt(fg, g_title, fs, { pb.x - 24 * k - ts.x, cy - ts.y * 0.5f }, tc, up, e.sel ? 2.0f * k : 0.0f, IM_COL32(90, 255, 170, 255));
					if (e.sel) {
						selY = selY < 0 ? cy : selY + (cy - selY) * std::min(1.0f, dt * 14.0f);
					}
				}
				if (!entries.empty()) {
					// the selection: a soul-fire rune that glides to the chosen entry, with a line under it
					const float pulse = 0.75f + 0.25f * std::sin(now * 4.0f);
					const float x = left + 30 * k, r = 9 * k;
					fg->AddLine({ x + 18 * k, selY + rowH * 0.36f }, { right - 10 * k, selY + rowH * 0.36f }, IM_COL32(90, 255, 170, static_cast<int>(110 * pulse)), 1.5f * k);
					fg->AddCircleFilled({ x, selY }, r * 2.6f, IM_COL32(90, 255, 170, static_cast<int>(40 * pulse)), 24);
					fg->AddQuadFilled({ x, selY - r }, { x + r * 0.7f, selY }, { x, selY + r }, { x - r * 0.7f, selY }, IM_COL32(170, 255, 210, static_cast<int>(255 * pulse)));
					// the mouse: hover moves Skyrim's selection (it plays its focus sound, ours while the menu is up), a click presses
					if (hovered >= 0 && hovered != hoverIdx && !entries[hovered].sel) {
						PressMainList(mv, entries[hovered].index, false);
					}
					hoverIdx = hovered;
					if (click && hovered >= 0) {
						PressMainList(mv, entries[hovered].index, true);
						g_pressT = now, g_pressY = selY;
					}
				} else {
					selY = -1;
				}
			}
			// a pressed entry answers: a cold flare runs along its line
			if (const float pt = now - g_pressT; pt >= 0 && pt < 0.45f) {
				const float u = pt / 0.45f, a = 1.0f - u;
				const float right = size.x - 90 * k, left = right - 420 * k;
				fg->AddRectFilledMultiColor({ left, g_pressY - 24 * k }, { right, g_pressY + 24 * k }, IM_COL32(90, 255, 170, 0), IM_COL32(170, 255, 220, static_cast<int>(90 * a)),
					IM_COL32(170, 255, 220, static_cast<int>(90 * a)), IM_COL32(90, 255, 170, 0));
				fg->AddLine({ left + (right - left) * u, g_pressY + 20 * k }, { right, g_pressY + 20 * k }, IM_COL32(220, 255, 240, static_cast<int>(220 * a)), 2.0f * k);
			}
			PartyPanel(fg, size, { 40 * k, size.y - 40 * k }, k, click);
			// the curtain that lifts as the menu opens
			if (fade < 1.0f) {
				fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, static_cast<int>(255 * (1.0f - fade))));
			}
			// our cursor: a soul-fire arrowhead in iron
			const ImVec2 c = cur;
			if (c.x >= 0) {
				const ImVec2 p1{ c.x + 5 * k, c.y + 26 * k }, p2{ c.x + 11 * k, c.y + 18 * k }, p3{ c.x + 20 * k, c.y + 17 * k };
				fg->AddQuadFilled(c, p1, p2, p3, IM_COL32(18, 20, 24, 240));
				fg->AddQuad(c, p1, p2, p3, IM_COL32(150, 170, 190, 255), 1.6f * k);
				fg->AddCircleFilled(c, 3.0f * k, IM_COL32(140, 255, 200, 255));
				fg->AddCircleFilled(c, 7.0f * k, IM_COL32(90, 255, 170, 50));
			}
		}


		// ---- The Undying Nights intro: once, when a new character first stands outside Riverwood. A short film of
		// stills captured in the game itself (Data/Interface/UndyingNights/intro_*.jpg), slow camera moves, the story in
		// a few lines, its own music (our MUSC) and stingers. The world waits (the game is paused) and it can be skipped
		// (hold Space / Enter / E / pad A). Textures load when it starts and are released when it ends.
		struct FilmScene
		{
			const char* tex;
			float       dur;
			float       z0, z1;            // zoom from, to
			float       x0, y0, x1, y1;    // pan (fraction of the spare picture, -1..1)
			const char* big;               // a title over the picture (or none)
			const char* l1;
			const char* l2;
			gen::Sound  cue;
			float       cueAt;
			bool        embers;
			gen::Sound  vo;                // the narrator's line for this scene
			float       voAt;              // when it starts (seconds into the scene)
			float       l2At;              // when the second line's words are spoken (seconds into the scene)
		};
		using GS = gen::Sound;
		const FilmScene kFilm[] = {  // timed to the narration (vo_01..vo_09: the line starts at voAt, its second sentence at l2At)
			{ nullptr, 5.3f, 1, 1, 0, 0, 0, 0, nullptr, "THE FOURTH ERA, 201", "HELGEN BURNED", GS::intro_graves, 0.3f, false, GS::vo_01, 0.9f, 2.42f },
			{ "intro_helgen", 12.1f, 1.04f, 1.14f, -0.6f, 0.2f, 0.4f, -0.2f, nullptr, "You lived.", "But as the World-Eater rose from the ash, he sang a word older than the Thu'um itself.", GS::intro_helgen, 0.4f, true, GS::vo_02, 0.8f, 2.86f },
			{ "intro_sky", 7.5f, 1.12f, 1.02f, 0.0f, -0.6f, 0.0f, 0.3f, "THE UNDYING DIRGE", nullptr, "And every grave in Skyrim heard it.", GS::intro_dirge, 0.6f, false, GS::vo_03, 0.8f, 3.38f },
			{ "intro_graves", 9.4f, 1.03f, 1.12f, 0.5f, 0.3f, -0.3f, 0.0f, nullptr, "By day, it sleeps beneath the holds.", "But when the sun goes down... the dead remember.", GS::intro_graves, 0.5f, false, GS::vo_04, 0.8f, 4.40f },
			{ "intro_horde", 11.3f, 1.14f, 1.04f, -0.3f, 0.0f, 0.3f, 0.1f, nullptr, "They come with the dusk. More each night, and harder to kill.", "Brides who run you down. Wailers who call the horde to feed.", GS::intro_horde, 0.5f, false, GS::vo_05, 0.8f, 5.42f },
			{ "intro_champions", 9.8f, 1.02f, 1.10f, 0.0f, 0.4f, 0.0f, -0.2f, nullptr, "Champions walk among them.", "The Bone Knight. The Barrow King. The Lich of the Dirge.", GS::intro_champions, 0.4f, true, GS::vo_06, 0.8f, 2.74f },
			{ "intro_wyrm", 10.2f, 1.10f, 1.02f, 0.4f, -0.2f, -0.2f, 0.2f, nullptr, "And when the fog rolls in...", "Grimmvar, the Undying Wyrm, comes hunting.", GS::intro_wyrm, 0.8f, false, GS::vo_07, 0.8f, 4.78f },
			{ "intro_shrine", 9.4f, 1.03f, 1.13f, 0.2f, 0.3f, -0.1f, -0.1f, nullptr, "Yet in every city, a Shrine of the Dirge still stands.", "Take up its spells. Hold its circle. Break the curse, city by city.", GS::intro_shrine, 0.5f, true, GS::vo_08, 0.8f, 3.54f },
			{ "menu_title", 6.9f, 1, 1, 0, 0, 0, 0, nullptr, nullptr, "Survive the night.", GS::intro_title, 0.2f, true, GS::vo_09, 0.9f, 0.90f },
		};
		constexpr int kFilmScenes = static_cast<int>(std::size(kFilm));
		constexpr float kFilmXfade = 1.2f;
		std::atomic<bool>         g_filmReq{ false };
		std::atomic<bool>         g_filmOn{ false };
		// the veil: black over everything while a new game turns into the film and the film into the game, so the
		// start prompts (answered for you) never flash and the world fades in only once it has settled
		float                                 g_veil = 0;
		bool                                  g_outro = false;
		std::chrono::steady_clock::time_point g_outroStart, g_outroBusy, g_reqSince;
		std::atomic<long long>                g_holdUntilMs{ 0 };  // a new character is on its way to Riverwood: black until the film takes over
		std::atomic<bool>         g_filmSkipHeld{ false };
		float                     g_filmT = 0, g_filmSkip = 0, g_filmEnd = -1;
		int                       g_filmCue = -1;
		int                       g_filmVo = -1;
		bool                      g_filmPaused = false;
		std::uint32_t             g_filmPauses = 0;  // pauses already in force when the film began
		ID3D11ShaderResourceView* g_filmTex[kFilmScenes]{};

		void FilmRelease()
		{
			for (auto*& t : g_filmTex) {
				if (t && t != g_mTitle) {
					t->Release();
				}
				t = nullptr;
			}
		}

		void FilmStop()
		{
			FilmRelease();
			if (g_filmPaused) {
				if (auto* ui = RE::UI::GetSingleton(); ui && ui->numPausesGame > 0) {
					--ui->numPausesGame;
				}
				g_filmPaused = false;
			}
			Audio::IntroMusic(false);
			Audio::Voice(std::nullopt);
			g_filmOn = false;
			logger::info("intro: film ended at {:.1f}s", g_filmT);
		}

		void FilmStart()
		{
			int loaded = 0;
			for (int i = 0; i < kFilmScenes; ++i) {
				if (!kFilm[i].tex) {
					continue;
				}
				if (std::string_view(kFilm[i].tex) == "menu_title") {
					g_filmTex[i] = g_mTitle;
					continue;
				}
				const std::string  n = std::string("Data\\Interface\\UndyingNights\\") + kFilm[i].tex + ".jpg";
				const std::wstring w(n.begin(), n.end());
				if (SUCCEEDED(DirectX::CreateWICTextureFromFile(g_device, w.c_str(), nullptr, &g_filmTex[i]))) {
					++loaded;
				}
			}
			if (auto* ui = RE::UI::GetSingleton()) {
				g_filmPauses = ui->numPausesGame;
				++ui->numPausesGame;  // the world waits while the film plays
				g_filmPaused = true;
			}
			Audio::IntroMusic(true);
			g_filmT = 0, g_filmSkip = 0, g_filmEnd = -1, g_filmCue = -1, g_filmVo = -1;
			g_filmOn = true;
			logger::info("intro: film starts ({} stills)", loaded);
		}

		void DrawFilm(ImVec2 size, float dt)
		{
			auto*       fg = ImGui::GetForegroundDrawList();
			const float k = size.y / 1080.0f;
			g_filmT += std::min(dt, 0.1f);
			float total = 0;
			for (const auto& sc : kFilm) total += sc.dur;
			// skip: hold for a moment (a stray key must not throw the film away)
			g_filmSkip = g_filmSkipHeld ? g_filmSkip + dt : std::max(0.0f, g_filmSkip - dt * 2);
			if (g_filmEnd < 0 && (g_filmT >= total || g_filmSkip >= 0.9f)) {
				g_filmEnd = g_filmT;
			}
			const float outFade = g_filmEnd < 0 ? 0.0f : std::clamp((g_filmT - g_filmEnd) / 1.2f, 0.0f, 1.0f);
			fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, 255));
			// which scene (and the next, while they cross)
			float start = 0;
			int   cur = kFilmScenes - 1;
			for (int i = 0; i < kFilmScenes; ++i) {
				if (g_filmT < start + kFilm[i].dur) {
					cur = i;
					break;
				}
				start += kFilm[i].dur;
			}
			const float local = g_filmT - start;
			if (g_filmEnd < 0 && cur != g_filmCue && local >= kFilm[cur].cueAt) {  // each scene's sound, once
				g_filmCue = cur;
				Audio::Ui(kFilm[cur].cue, 0.45f);  // under the narrator
			}
			if (g_filmEnd < 0 && cur != g_filmVo && local >= kFilm[cur].voAt) {  // the narrator's line, once
				g_filmVo = cur;
				Audio::Voice(kFilm[cur].vo);
			}
			float cw = size.x, ch = size.x * 9.0f / 16.0f;
			if (ch < size.y) {
				ch = size.y, cw = size.y * 16.0f / 9.0f;
			}
			auto picture = [&](int i, float t, float alpha) {
				const auto& sc = kFilm[i];
				if (!g_filmTex[i] || alpha <= 0) {
					return;
				}
				const float u = std::clamp(t / sc.dur, 0.0f, 1.0f);
				const float e = u * u * (3 - 2 * u) * 0.35f + u * 0.65f;  // a slow, steady move with soft ends
				const float z = sc.z0 + (sc.z1 - sc.z0) * e;
				const float w = cw * z, h = ch * z;
				const float px = sc.x0 + (sc.x1 - sc.x0) * e, py = sc.y0 + (sc.y1 - sc.y0) * e;
				ImVec2 a{ (size.x - w) / 2 + px * (w - cw) / 2, (size.y - h) / 2 + py * (h - ch) / 2 };
				if (std::string_view(sc.tex) == "menu_title") {  // the title card: the logo on black
					const float tw = size.x * 0.66f, th = tw * 396.0f / 2479.0f, s2 = 1.0f + 0.03f * u;
					a = { (size.x - tw * s2) / 2, size.y * 0.40f - th * s2 / 2 };
					fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_filmTex[i])), a, { a.x + tw * s2, a.y + th * s2 }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
					return;
				}
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_filmTex[i])), a, { a.x + w, a.y + h }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
			};
			const float inA = std::clamp(local / kFilmXfade, 0.0f, 1.0f);
			if (cur > 0 && local < kFilmXfade) {
				picture(cur - 1, kFilm[cur - 1].dur + local, 1.0f);
			}
			picture(cur, local, cur == 0 ? 1.0f : inA);
			// embers drifting up over the fire-lit scenes
			if (kFilm[cur].embers) {
				struct Ember { float x, y, sp, ph, r; };
				static std::vector<Ember> em;
				static std::mt19937 rng{ 3 };
				std::uniform_real_distribution<float> U(0, 1);
				if (em.empty()) {
					for (int i = 0; i < 90; ++i) em.push_back({ U(rng), U(rng), 0.03f + 0.06f * U(rng), U(rng) * 6.28f, 0.8f + 1.8f * U(rng) });
				}
				const float ea = std::clamp(local / 1.5f, 0.0f, 1.0f) * std::clamp((kFilm[cur].dur - local) / 1.0f, 0.0f, 1.0f);
				for (auto& m : em) {
					m.y -= m.sp * dt;
					if (m.y < -0.02f) m.y = 1.02f, m.x = U(rng);
					const ImVec2 p{ (m.x + 0.01f * std::sin(g_filmT * 0.8f + m.ph)) * size.x, m.y * size.y };
					const float tw = 0.5f + 0.5f * std::sin(g_filmT * 6 + m.ph * 4);
					fg->AddCircleFilled(p, m.r * k * 2.6f, IM_COL32(255, 120, 50, static_cast<int>(30 * tw * ea)));
					fg->AddCircleFilled(p, m.r * k, IM_COL32(255, 180, 90, static_cast<int>(200 * tw * ea)));
				}
			}
			// a far flash in the fog
			if (cur > 0 && std::string_view(kFilm[cur].tex ? kFilm[cur].tex : "") == "intro_wyrm") {
				const float f = std::max(0.0f, 1.0f - std::abs(local - 2.4f) * 6.0f) + 0.5f * std::max(0.0f, 1.0f - std::abs(local - 2.75f) * 9.0f);
				fg->AddRectFilled({ 0, 0 }, size, IM_COL32(170, 190, 230, static_cast<int>(40 * f)));
			}
			// vignette and the letterbox
			fg->AddRectFilledMultiColor({ 0, 0 }, { size.x * 0.22f, size.y }, IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 150));
			fg->AddRectFilledMultiColor({ size.x * 0.78f, 0 }, size, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 0));
			const float bar = size.y * 0.115f;
			fg->AddRectFilled({ 0, 0 }, { size.x, bar }, IM_COL32(0, 0, 0, 255));
			fg->AddRectFilled({ 0, size.y - bar * 1.25f }, size, IM_COL32(0, 0, 0, 255));
			fg->AddRectFilledMultiColor({ 0, bar }, { size.x, bar + 60 * k }, IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
			fg->AddRectFilledMultiColor({ 0, size.y - bar * 1.25f - 80 * k }, { size.x, size.y - bar * 1.25f }, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 220), IM_COL32(0, 0, 0, 220));
			// the words: line one, then line two; they leave before the scene does
			const auto& sc = kFilm[cur];
			auto lineA = [&](float at) { return std::clamp((local - at) / 0.9f, 0.0f, 1.0f) * std::clamp((sc.dur - 0.5f - local) / 0.7f, 0.0f, 1.0f); };
			const float ty = size.y - bar * 1.25f + 14 * k;
			const ImU32 bone = IM_COL32(232, 224, 206, 255), dim = IM_COL32(170, 176, 186, 255);
			if (cur == 0) {
				const float a = lineA(sc.voAt - 0.3f), b = lineA(sc.l2At - 0.3f);
				TxtC(fg, g_num, 26 * k, size.x / 2, size.y * 0.44f, Fade(dim, a), sc.l1);
				TxtC(fg, g_title, 54 * k, size.x / 2, size.y * 0.49f, Fade(bone, b), sc.l2, 2 * k, IM_COL32(200, 60, 40, static_cast<int>(120 * b)));
			} else {
				if (sc.big) {
					const float a = lineA(sc.voAt - 0.3f);
					TxtC(fg, g_title, std::round(92 * k), size.x / 2, size.y * 0.42f, Fade(bone, a), sc.big, 4 * k, IM_COL32(200, 40, 30, static_cast<int>(160 * a)));
				}
				if (sc.l1) {
					TxtC(fg, g_body, 34 * k, size.x / 2, ty, Fade(bone, lineA(sc.voAt - 0.3f)), sc.l1);
				}
				if (sc.l2) {
					TxtC(fg, g_body, 34 * k, size.x / 2, sc.l1 ? ty + 46 * k : ty + 20 * k, Fade(sc.l1 ? dim : bone, lineA((sc.l1 || sc.big ? sc.l2At : sc.voAt) - 0.3f)), sc.l2);
				}
				if (cur == kFilmScenes - 1) {  // the last words
					TxtC(fg, g_num, 24 * k, size.x / 2, size.y * 0.62f, Fade(IM_COL32(200, 70, 50, 255), lineA(sc.voAt + 2.2f)), "DUSK IS COMING");
				}
			}
			// skip: a quiet hint, and a ring that fills while the key is held
			{
				const float ha = std::clamp((g_filmT - 2.0f) / 1.0f, 0.0f, 1.0f) * (1.0f - outFade);
				const ImVec2 c{ size.x - 70 * k, size.y - bar * 0.6f };
				const std::string hint = "HOLD SPACE TO SKIP";
				const ImVec2 ts = Measure(g_num, 18 * k, hint);
				Txt(fg, g_num, 18 * k, { c.x - 26 * k - ts.x, c.y - ts.y / 2 }, IM_COL32(150, 156, 166, static_cast<int>(170 * ha)), hint);
				fg->AddCircle(c, 12 * k, IM_COL32(150, 156, 166, static_cast<int>(120 * ha)), 32, 1.5f * k);
				if (g_filmSkip > 0) {
					fg->PathArcTo(c, 12 * k, -1.5708f, -1.5708f + 6.2832f * std::min(1.0f, g_filmSkip / 0.9f), 32);
					fg->PathStroke(IM_COL32(110, 255, 190, 255), 0, 3 * k);
				}
			}
			// fade in from black at the start, out to the world at the end
			const float inFade = 1.0f - std::clamp(g_filmT / 1.0f, 0.0f, 1.0f);
			if (inFade > 0) fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, static_cast<int>(255 * inFade)));
			if (outFade > 0) fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, static_cast<int>(255 * outFade)));
			if (outFade >= 1.0f) {
				FilmStop();
				g_outro = true, g_veil = 1.0f;
				g_outroStart = g_outroBusy = std::chrono::steady_clock::now();
				SKSE::GetTaskInterface()->AddTask([] {  // into the game in first person
					if (auto* cam = RE::PlayerCamera::GetSingleton()) {
						cam->ForceFirstPerson();
					}
				});
			}
		}


		// ---- The new-game prompts answered for you: a new character always escaped with Ralof (Stormcloaks), and
		// survival mode stays off. Their message boxes are read the moment they open and pressed before they show.
		std::string GfxText(RE::GFxMovieView* a_mv, const char* a_path)
		{
			RE::GFxValue v;
			if (a_mv && a_mv->GetVariable(&v, a_path) && v.IsString()) {
				return v.GetString();
			}
			return {};
		}
		std::vector<std::string> GfxButtons(RE::GFxMovieView* a_mv)
		{
			std::vector<std::string> out;
			RE::GFxValue arr;
			if (!a_mv || !a_mv->GetVariable(&arr, "_root.MessageMenu.MessageButtons") || !arr.IsArray()) {
				return out;
			}
			for (std::uint32_t i = 0; i < arr.GetArraySize(); ++i) {
				RE::GFxValue b, t, s;
				std::string  label;
				if (arr.GetElement(i, &b) && b.IsObject()) {
					if (b.GetMember("ButtonText", &t) && t.IsObject() && t.GetMember("text", &s) && s.IsString()) {
						label = s.GetString();
					} else if (b.GetMember("label", &s) && s.IsString()) {
						label = s.GetString();
					}
				}
				out.push_back(label);
			}
			return out;
		}
		std::string Lower(std::string a) { std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return a; }

		void AnswerStartPrompts(RE::UI* a_ui)
		{
			static int  tries = 0;
			static bool decided = false;
			auto        m = a_ui ? a_ui->GetMenu(RE::MessageBoxMenu::MENU_NAME) : nullptr;
			if (!m || !m->uiMovie || !a_ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) {
				tries = 0, decided = false;
				return;
			}
			if (decided) {
				return;
			}
			auto* mv = m->uiMovie.get();
			const std::string text = Lower(GfxText(mv, "_root.MessageMenu.MessageText.text"));
			const auto        buttons = GfxButtons(mv);
			if (text.empty() || buttons.empty()) {
				mv->SetVisible(false);  // still filling in: unseen until we know what it asks
				if (++tries > 20) {
					mv->SetVisible(true), decided = true;
					logger::info("prompts: a message box we could not read (text '{}', {} buttons)", text, buttons.size());
				}
				return;
			}
			decided = true;
			int pick = -1;
			const bool sides = text.find("befriend") != std::string::npos || text.find("escaping from helgen") != std::string::npos;
			const bool survival = text.find("survival mode") != std::string::npos && text.find("enable") != std::string::npos;
			for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
				const std::string b = Lower(buttons[i]);
				if (sides && (b.find("ralof") != std::string::npos || b.find("stormcloak") != std::string::npos)) {
					pick = i;
				}
				if (survival && (b == "no" || b.rfind("no", 0) == 0)) {
					pick = i;
				}
			}
			if ((sides || survival) && pick >= 0 && m->fxDelegate) {
				RE::GFxValue arg(static_cast<double>(pick));
				m->fxDelegate->Callback(mv, "buttonPress", &arg, 1);
				logger::info("prompts: answered '{}' with '{}' (hidden)", sides ? "who did you befriend" : "enable survival mode", buttons[pick]);
				return;
			}
			mv->SetVisible(true);
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
			// our own type (SIL Open Font License, in Data/Interface/UndyingNights/fonts): Cinzel for titles and numbers,
			// Cormorant Garamond for the words; Windows' Georgia / Segoe if they are missing
			auto own = [&](const char* a_file) -> ImFont* {
				const std::string p = std::string("Data\\Interface\\UndyingNights\\fonts\\") + a_file;
				return ::GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES ? io.Fonts->AddFontFromFileTTF(p.c_str(), 64.0f, nullptr, kRanges) : nullptr;
			};
			g_title = own("Cinzel-Bold.ttf");
			g_num = own("Cinzel-Medium.ttf");
			g_body = own("CormorantGaramond-SemiBold.ttf");
			g_title = g_title ? g_title : font("georgiab.ttf");
			g_num = g_num ? g_num : font("seguisb.ttf");
			g_body = g_body ? g_body : font("segoeui.ttf");
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
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\UndyingNights\\menu_plate.jpg", nullptr, &g_mPlate);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\UndyingNights\\menu_aurora.png", nullptr, &g_mAurora);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\UndyingNights\\menu_title.png", nullptr, &g_mTitle);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\UndyingNights\\frame.png", nullptr, &g_frame);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\UndyingNights\\divider.png", nullptr, &g_divider);
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
			// Skyrim's cursor goes while our menu (with its own cursor) shows, and comes back for everything else
			{
				static bool hidden = false;
				if (mainMenu != hidden && uiM) {
					if (auto cm = uiM->GetMenu(RE::CursorMenu::MENU_NAME); cm && cm->uiMovie) {
						cm->uiMovie->SetVisible(!mainMenu);
						hidden = mainMenu;
					} else if (!mainMenu) {
						hidden = false;
					}
				}
			}
			// the Tab menu (Skills / Magic / Items / Map), on its own: the party panel sits in its empty top-left corner
			const bool journal = uiM && uiM->IsMenuOpen(RE::TweenMenu::MENU_NAME) && !uiM->IsMenuOpen(RE::MainMenu::MENU_NAME) &&
			                     !uiM->IsMenuOpen(RE::InventoryMenu::MENU_NAME) && !uiM->IsMenuOpen(RE::MagicMenu::MENU_NAME) &&
			                     !uiM->IsMenuOpen(RE::MapMenu::MENU_NAME) && !uiM->IsMenuOpen(RE::StatsMenu::MENU_NAME);
			const int  clicks = g_clicks.exchange(0);
			AnswerStartPrompts(uiM);
			if (g_filmReq && !g_filmOn) {  // the film waits until the world has settled (the move to Riverwood loads a cell)
				static auto settled = std::chrono::steady_clock::now();
				if (!InGame()) {
					settled = std::chrono::steady_clock::now();
				} else if (std::chrono::steady_clock::now() - settled > std::chrono::milliseconds(2500)) {
					g_filmReq = false;
					FilmStart();
				}
			}
			const bool film = g_filmOn;
			if (film) {  // another pausing menu opened over the film (the journal, a loading screen): the film yields
				auto* ui = RE::UI::GetSingleton();
				const bool loading = ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
				if (ui && (ui->numPausesGame > g_filmPauses + 1 || loading || mainMenu)) {
					logger::info("intro: film yields (pauses {} vs {}, loading {}, main menu {})", ui->numPausesGame, g_filmPauses + 1, loading, mainMenu);
					FilmStop();
					if (!mainMenu && g_filmT < 20.0f) {
						g_filmReq = true;  // a prompt (survival mode, a quest box) came up first: the film starts again once it is answered
					}
				}
			}
			{
				const auto t = std::chrono::steady_clock::now();
				auto*      ui = RE::UI::GetSingleton();
				const bool busy = ui && (ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->GameIsPaused());
				if (g_outro) {
					if (busy) {
						g_outroBusy = t;
					}
					const float since = std::chrono::duration<float>(t - g_outroStart).count(), quiet = std::chrono::duration<float>(t - g_outroBusy).count();
					if (since > 8.0f || (since > 2.6f && quiet > 0.9f)) {
						g_outro = false;
						logger::info("intro: into the game after {:.1f}s of black", since);
					}
				}
				if (!g_filmReq) {
					g_reqSince = t;
				}
				const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
			const bool want = !mainMenu && (g_filmOn || g_outro || ms < g_holdUntilMs || (g_filmReq && t - g_reqSince < std::chrono::seconds(20)));
				g_veil = mainMenu ? 0.0f : want ? std::min(1.0f, g_veil + dt * 4.0f) : std::max(0.0f, g_veil - dt / 1.8f);
			}
			if (((!InGame() && !mainMenu && !journal) || (DevTools::HudOff() && !mainMenu)) && !g_filmOn && g_veil <= 0) {
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
			if (g_filmOn) {
				DrawFilm(io.DisplaySize, dt);
			} else if (mainMenu) {
				DrawMenu(io.DisplaySize, static_cast<float>(ImGui::GetTime()), clicks > 0);
			} else if (journal) {
				const float k = io.DisplaySize.y / 1080.0f;
				PartyPanel(ImGui::GetForegroundDrawList(), io.DisplaySize, { 48 * k, 48 * k }, k, clicks > 0, true);
			} else if (InGame() && !DevTools::HudOff()) {
				Draw(s, io.DisplaySize);
				// Skyrim's enemy bar under the compass gives way to ours over each foe
				if (static int vb = 0; vb++ % 15 == 0) {
					if (auto hud = uiM ? uiM->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr; hud && hud->uiMovie) {
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.EnemyHealth_mc._visible", RE::GFxValue(false));
					}
				}
			}
			if (g_veil > 0 && !g_filmOn && !mainMenu) {
				ImGui::GetForegroundDrawList()->AddRectFilled({ 0, 0 }, io.DisplaySize, IM_COL32(0, 0, 0, static_cast<int>(255 * g_veil * g_veil * (3 - 2 * g_veil))));
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
				static RE::Setting* maxComplex = RE::INISettingCollection::GetSingleton() ? RE::INISettingCollection::GetSingleton()->GetSetting("fMaxTimeComplex:HAVOK") : nullptr;
				// smoothed: one long frame must not slow the ragdolls (a too-short step leaves bodies drifting down slowly)
				static float avg = 1.0f / 60.0f;
				if (dt > 0.0f && dt < 0.25f) {
					avg += (dt - avg) * 0.1f;
				}
				const float step = std::clamp(avg, 1.0f / 300.0f, 1.0f / 30.0f);
				if (maxTime) {
					maxTime->data.f = step;
				}
				if (maxComplex) {
					maxComplex->data.f = std::max(step, 1.0f / 60.0f);
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
		void IntroStart() { g_filmReq = true; }
		bool IntroActive() { return g_filmOn || g_filmReq; }
		void IntroVeil(float a_seconds)
		{
			g_holdUntilMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + static_cast<long long>(a_seconds * 1000);
		}
		void IntroSkipHeld(bool a_held) { g_filmSkipHeld = a_held; }

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
