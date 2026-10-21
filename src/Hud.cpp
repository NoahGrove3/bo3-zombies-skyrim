#include "BO3Z.h"
#include "Codex.h"
#include <sstream>

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
		std::array<ID3D11ShaderResourceView*, 8>                  g_elementIcons{};  // the Coffer spells' element emblems (el_<element>.png)
		std::array<std::array<ID3D11ShaderResourceView*, 8>, 5>   g_schoolIcons{};
		std::array<ID3D11ShaderResourceView*, 16>                 g_bowIcons{};      // the altar's bows: their archery emblem in the element's colour (bw_<element>.png)   // ...their school's sigil in the element's colour (sc_<school>_<element>.png)
		ID3D11ShaderResourceView* g_menuBg = nullptr;   // the main menu's backdrop (tools/cover.py, MENU=1)
		ID3D11ShaderResourceView* g_menuFog = nullptr;  // a tileable fog layer that drifts over it
		ID3D11ShaderResourceView* g_mapIcons = nullptr;  // the world map's marker icons, an 8x8 atlas (tools/mapicons.py)
		ID3D11ShaderResourceView* g_fire = nullptr;     // the shrine orb's purple magic fire: 16x8 flipbook (tools/magicfire.py)
		ID3D11ShaderResourceView* g_flame = nullptr;    // the aura round a spell emblem: 4x4 atlas of a looping white flame ring (tools/flameaura.py)
		ID3D11ShaderResourceView* g_frame = nullptr;
		ID3D11ShaderResourceView* g_cursorTex = nullptr;  // Noah's cursor: Moonlit Spirit, 8 frames of 128 px side by side (cursor_moonlit.png)    // nine-slice panel: black field, iron and bronze border (tools/uiart.py)
		ID3D11ShaderResourceView* g_divider = nullptr;  // bronze line with a winged diamond
		std::atomic<int>          g_clicks{ 0 };        // left clicks since the last frame (menus)
		std::atomic<int>          g_cxScroll{ 0 }, g_cxChapter{ 0 }, g_cxBook{ 0 };  // the Codex: wheel / keys since the last frame
		std::atomic<bool>         g_padLast{ false };   // the last thing touched was a controller: show its buttons
		std::atomic<int>          g_padMove{ 0 }, g_padAccept{ 0 }, g_padBack{ 0 }, g_padSide{ 0 }, g_wheel{ 0 };  // main menu: pad / arrow keys since the last frame
		std::string               g_devPick;            // dev: a main-menu word to press as a click would (menupick)
		std::mutex                g_devPickLock;
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

		// ---- The Curse of Alduin HUD: dark glass panels, gold trim, serif titles, soft glows ----
		ImFont* g_title = nullptr;  // Georgia Bold
		ImFont* g_num = nullptr;    // Segoe UI Semibold
		ImFont* g_body = nullptr;   // Segoe UI

		// (Noah: our UI redone in Nordic UI's style) Nordic UI's look: black glass, warm-white lettering and thin silver
		// lines, no colour but what means something - septims (kCoin), Legendary gold (TxtGold), blood, the elements
		constexpr ImU32 kGold = IM_COL32(232, 228, 218, 255);    // Nordic white (the old bronze's name kept: every accent follows)
		constexpr ImU32 kGoldDim = IM_COL32(232, 228, 218, 100);
		constexpr ImU32 kCoin = IM_COL32(220, 175, 90, 255);     // septims and the rare
		constexpr ImU32 kBlood = IM_COL32(178, 22, 22, 255);
		constexpr ImU32 kParch = IM_COL32(214, 210, 200, 255);
		constexpr ImU32 kGlass = IM_COL32(0, 0, 0, 165);
		constexpr ImU32 kRune = IM_COL32(196, 200, 206, 255);    // silver lines and borders
		constexpr ImU32 kRuneDim = IM_COL32(196, 200, 206, 90);
		constexpr ImU32 kIce = IM_COL32(245, 245, 242, 255);
		constexpr ImU32 kNavy = IM_COL32(0, 0, 0, 205);  // (the panels' black - the name kept)

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

		// words wrapped to a width (at most a_maxLines lines); returns the lines drawn
		int TxtWrap(ImDrawList* dl, ImFont* f, float px, ImVec2 at, ImU32 col, const std::string& s, float width, int a_maxLines = 3)
		{
			std::vector<std::string> lines;
			std::string              line, word;
			std::istringstream       in(s);
			while (in >> word) {
				const std::string tryLine = line.empty() ? word : line + " " + word;
				if (!line.empty() && Measure(f, px, tryLine).x > width) {
					lines.push_back(line), line = word;
				} else {
					line = tryLine;
				}
			}
			if (!line.empty()) {
				lines.push_back(line);
			}
			const int n = std::min(static_cast<int>(lines.size()), a_maxLines);
			for (int i = 0; i < n; ++i) {
				Txt(dl, f, px, { at.x, at.y + i * px * 1.25f }, col, lines[i]);
			}
			return n;
		}
		std::string Upper(std::string a_s)
		{
			for (auto& c : a_s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return a_s;
		}
		void TxtC(ImDrawList* dl, ImFont* f, float px, float cx, float y, ImU32 col, const std::string& s, float glow = 0, ImU32 glowCol = 0)
		{
			Txt(dl, f, px, { cx - Measure(f, px, s).x * 0.5f, y }, col, s, glow, glowCol);
		}
		// (Noah: Legendary spells' lettering glowing gold) a warm halo that breathes, a bright gold face and a pale
		// highlight across its top - on the altar's roll, its offer, and the rune banner bottom right
		void TxtGold(ImDrawList* dl, ImFont* f, float px, ImVec2 p, const std::string& s, float al = 1.0f)
		{
			const float t = static_cast<float>(ImGui::GetTime());
			const float breathe = 0.75f + 0.25f * std::sin(t * 2.4f);
			const float glow = px * 0.16f;
			for (int ring = 0; ring < 2; ++ring) {
				const float rr = glow * (ring ? 1.0f : 0.55f);
				for (int i = 0; i < 12; ++i) {
					const float a = i * 0.5236f;
					dl->AddText(f, px, { p.x + std::cos(a) * rr, p.y + std::sin(a) * rr }, IM_COL32(255, 170, 40, static_cast<int>((ring ? 26 : 44) * breathe * al)), s.c_str());
				}
			}
			dl->AddText(f, px, { p.x + px * 0.04f, p.y + px * 0.05f }, IM_COL32(40, 20, 0, static_cast<int>(200 * al)), s.c_str());
			dl->AddText(f, px, p, IM_COL32(255, 206, 92, static_cast<int>(255 * al)), s.c_str());
			// the highlight: the face again, paler, clipped to its upper half
			const ImVec2 sz = Measure(f, px, s);
			dl->PushClipRect({ p.x - 2, p.y }, { p.x + sz.x + 2, p.y + sz.y * 0.48f }, true);
			dl->AddText(f, px, p, IM_COL32(255, 244, 196, static_cast<int>(190 * al)), s.c_str());
			dl->PopClipRect();
		}
		void TxtGoldC(ImDrawList* dl, ImFont* f, float px, float cx, float y, const std::string& s, float al = 1.0f)
		{
			TxtGold(dl, f, px, { cx - Measure(f, px, s).x * 0.5f, y }, s, al);
		}
		bool Legendary(int id) { return id >= Arcana::kIdBase && id < Bows::kIdBase && Arcana::TierOf(id - Arcana::kIdBase) >= 3; }

		// Panel: a smoky black field in an iron-and-bronze frame with Nordic corner brackets (nine-slice of
		// frame.png: 80px corners of 256), or, without the art, dark glass with gold ticks.
		// A spell's emblem (256x256 medallion, rim at 104/128 of its half-width) wrapped in its living aura: the flame
		// ring (rim at 0.62 of the frame's half-width) tinted with the spell's colour, a hotter inner pass in its accent,
		// 16 frames a second; then the medallion on top. R is the medallion's rim radius on screen.
		// what a HUD id names: a weapon (0..), or one of the Coffer's spells (Arcana::kIdBase + its index)
		std::string ArmsName(int id) { return id >= Bows::kIdBase ? std::string(Bows::Name(id - Bows::kIdBase)) : id >= Arcana::kIdBase ? std::string(Arcana::Name(id - Arcana::kIdBase)) : id >= 0 && id < static_cast<int>(gen::kGuns.size()) ? std::string(gen::kGuns[id].label) : std::string(); }
		// what kind it is, under its name on the roll: "Fire  -  Destruction" for a spell, "Fire Bow" for a bow
		std::string ArmsType(int id)
		{
			const auto cap = [](std::string_view v) { std::string o(v); if (!o.empty()) o[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(o[0]))); return o; };
			if (id >= Bows::kIdBase) {
				return cap(Bows::Element(id - Bows::kIdBase)) + " Bow";
			}
			if (id >= Arcana::kIdBase) {
				static constexpr std::array<std::string_view, 5> kSchool{ "Destruction", "Conjuration", "Restoration", "Alteration", "Illusion" };
				const int i = id - Arcana::kIdBase;
				return cap(Arcana::ElementName(Arcana::ElementOf(i))) + "  -  " + std::string(kSchool[std::clamp(Arcana::SchoolOf(i), 0, 4)]) + (Arcana::TierOf(i) >= 3 ? "  -  Legendary" : "");
			}
			return {};
		}
		std::uint32_t ArmsColor(int id) { return id >= Bows::kIdBase ? Bows::Color(id - Bows::kIdBase) : id >= Arcana::kIdBase ? Arcana::ElementColor(Arcana::ElementOf(id - Arcana::kIdBase)) : id >= 0 && id < static_cast<int>(gen::kGuns.size()) ? gen::kGuns[id].color : 0xC8A060; }

		struct OrbTex { ID3D11ShaderResourceView* crystal[4]; ID3D11ShaderResourceView* soft; };
		OrbTex MenuOrbTextures();  // (the menu's heart stone renders and soft light, defined with them below)
		void DrawEmblem(ImDrawList* dl, int gun, ImVec2 c, float R, float alpha, float now, float flame = 1.0f)
		{
			ID3D11ShaderResourceView* iconTex = nullptr;
			std::uint32_t             col = 0, acc = 0xFFFFFF;
			if (gun >= Bows::kIdBase) {  // one of the altar's bows: the archery emblem in its element's colour
				const int b = gun - Bows::kIdBase;
				col = Bows::Color(b);
				acc = ((std::min(255u, ((col >> 16) & 0xFF) + 90)) << 16) | ((std::min(255u, ((col >> 8) & 0xFF) + 90)) << 8) | std::min(255u, (col & 0xFF) + 90);
				iconTex = b >= 0 && b < static_cast<int>(g_bowIcons.size()) ? g_bowIcons[b] : nullptr;
			} else if (gun >= Arcana::kIdBase) {  // a Coffer spell: its element's emblem
				const int el = Arcana::ElementOf(gun - Arcana::kIdBase);
				col = Arcana::ElementColor(el);
				acc = ((std::min(255u, ((col >> 16) & 0xFF) + 90)) << 16) | ((std::min(255u, ((col >> 8) & 0xFF) + 90)) << 8) | std::min(255u, (col & 0xFF) + 90);
				const int sc = std::clamp(Arcana::SchoolOf(gun - Arcana::kIdBase), 0, 4);
				iconTex = el >= 0 && el < 8 ? (g_schoolIcons[sc][el] ? g_schoolIcons[sc][el] : g_elementIcons[el]) : nullptr;  // (Noah: the school, not the element)
			} else {
				if (gun < 0 || gun >= static_cast<int>(gen::kGuns.size())) {
					return;
				}
				col = gen::kGuns[gun].color;
				for (const auto& sc : gen::kSpellcraft) {
					if (sc.id == gen::kGuns[gun].id) {
						col = sc.color, acc = sc.accent;
					}
				}
				iconTex = g_weaponIcons[gun];
			}
			const auto rgb = [](std::uint32_t v, float a) { return IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, static_cast<int>(255 * std::clamp(a, 0.0f, 1.0f))); };
			// A Coffer spell's emblem shows how far it is reforged: unforged, the bare medallion (no flame, no glow); once, its
			// flame; twice, a brighter, faster flame in a glow; three times (the last), all of that with a ring of runes
			// turning round it and sparks circling, the whole emblem breathing.
			int forge = 1;
			if (gun >= Bows::kIdBase) {
				forge = std::clamp(Bows::Level(gun - Bows::kIdBase), 0, 3);
				if (forge == 0) {
					flame = 0.0f;
				}
			} else if (gun >= Arcana::kIdBase) {
				forge = std::clamp(Arcana::Level(gun - Arcana::kIdBase), 0, 3);
				if (forge == 0) {
					flame = 0.0f;
				}
			}
			if (forge == 3) {
				R *= 1.0f + 0.035f * std::sin(now * 3.2f);
			}
			if (forge >= 2) {  // the glow behind it
				dl->AddCircleFilled(c, R * (forge == 3 ? 1.75f : 1.5f), rgb(col, (forge == 3 ? 0.26f : 0.18f) * alpha * (0.85f + 0.15f * std::sin(now * 2.4f))), 40);
				dl->AddCircleFilled(c, R * 1.18f, rgb(acc, 0.16f * alpha), 40);
			}
			if (g_flame && flame > 0.0f) {
				const auto tex = ImTextureID(reinterpret_cast<std::uintptr_t>(g_flame));
				const float fps = forge >= 3 ? 26.0f : forge == 2 ? 21.0f : 16.0f;
				const auto frame = [&](float t, float h, ImU32 tint) {
					const int   f = static_cast<int>(t * fps) % 16;
					const float u = (f % 4) * 0.25f, v = (f / 4) * 0.25f;
					dl->AddImage(tex, { c.x - h, c.y - h }, { c.x + h, c.y + h }, { u, v }, { u + 0.25f, v + 0.25f }, tint);
				};
				const float h = R / 0.62f, hot = forge >= 2 ? 1.0f : 0.95f;
				if (forge >= 3) {
					frame(now + 0.25f, h * 1.2f, rgb(col, 0.6f * alpha * flame));   // a taller outer blaze
				}
				frame(now, h * (forge >= 2 ? 1.1f : 1.04f), rgb(col, hot * alpha * flame));         // the flame, the spell's colour
				frame(now + 0.5f, h * 0.93f, rgb(acc, (forge >= 2 ? 0.8f : 0.55f) * alpha * flame));  // a hotter tongue inside
				if (forge >= 2) {
					frame(now + 0.75f, h * 0.82f, IM_COL32(255, 248, 230, static_cast<int>(110 * alpha * flame)));  // a white-hot heart
				}
			}
			if (forge == 3) {  // the ring of runes and the circling sparks
				const float rr = R * 1.32f;
				for (int q = 0; q < 12; ++q) {
					const float a0 = q * 0.5236f + now * 0.6f;
					dl->PathArcTo(c, rr, a0, a0 + 0.3f, 6);
					dl->PathStroke(rgb(acc, 0.85f * alpha), 0, 2.2f);
				}
				for (int q = 0; q < 6; ++q) {
					const float a0 = q * 1.0472f - now * 1.8f, sr = R * (1.45f + 0.06f * std::sin(now * 4.0f + q));
					const ImVec2 sp{ c.x + std::cos(a0) * sr, c.y + std::sin(a0) * sr };
					dl->AddCircleFilled(sp, 3.2f, IM_COL32(255, 244, 210, static_cast<int>(230 * alpha)), 10);
					dl->AddCircleFilled(sp, 7.0f, rgb(col, 0.35f * alpha), 12);
				}
			}
			if (auto* icon = iconTex) {
				const float h = R * 128.0f / 104.0f;
				dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { c.x - h, c.y - h }, { c.x + h, c.y + h }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * std::clamp(alpha, 0.0f, 1.0f))));
			}
		}

		// A controller button as the pad shows it: the face buttons as coloured discs with their letter (A green, B red,
		// X blue, Y yellow), the d-pad as a cross with the pressed arm lit, the bumpers and triggers as pills.
		// a_name: A B X Y | DU DD DL DR | LB RB LT RT | START BACK. c is the centre, r the radius.
		void PadGlyph(ImDrawList* dl, ImVec2 c, float r, std::string_view a_name, float alpha = 1.0f)
		{
			const auto F = [&](ImU32 col) { return Fade(col, alpha); };
			if (a_name.size() == 1 && Settings::PlayStationIcons()) {  // Cross, Circle, Square, Triangle in their own colours
				const char  ch = a_name[0];
				const ImU32 col = ch == 'A' ? IM_COL32(124, 178, 232, 255) : ch == 'B' ? IM_COL32(255, 102, 102, 255) : ch == 'X' ? IM_COL32(232, 154, 216, 255) : IM_COL32(64, 224, 176, 255);
				dl->AddCircleFilled({ c.x, c.y + r * 0.08f }, r, F(IM_COL32(0, 0, 0, 150)), 28);
				dl->AddCircleFilled(c, r, F(IM_COL32(24, 24, 28, 245)), 28);
				dl->AddCircle(c, r, F(IM_COL32(255, 255, 255, 40)), 28, 1.0f);
				const float s = r * 0.46f, t = r * 0.15f;
				if (ch == 'A') {
					dl->AddLine({ c.x - s, c.y - s }, { c.x + s, c.y + s }, F(col), t);
					dl->AddLine({ c.x - s, c.y + s }, { c.x + s, c.y - s }, F(col), t);
				} else if (ch == 'B') {
					dl->AddCircle(c, s * 1.05f, F(col), 24, t);
				} else if (ch == 'X') {
					dl->AddRect({ c.x - s * 0.9f, c.y - s * 0.9f }, { c.x + s * 0.9f, c.y + s * 0.9f }, F(col), 0, 0, t);
				} else {
					dl->AddTriangle({ c.x, c.y - s * 1.05f }, { c.x + s, c.y + s * 0.75f }, { c.x - s, c.y + s * 0.75f }, F(col), t);
				}
				return;
			}
			if (a_name.size() == 1) {
				const char  ch = a_name[0];
				const ImU32 col = ch == 'A' ? IM_COL32(92, 186, 60, 255) : ch == 'B' ? IM_COL32(214, 58, 52, 255) : ch == 'X' ? IM_COL32(52, 120, 220, 255) : IM_COL32(236, 186, 40, 255);
				dl->AddCircleFilled({ c.x, c.y + r * 0.08f }, r, F(IM_COL32(0, 0, 0, 150)), 28);              // its shadow
				dl->AddCircleFilled(c, r, F(IM_COL32(24, 24, 28, 245)), 28);
				dl->AddCircle(c, r * 0.86f, F(col), 28, r * 0.16f);
				dl->AddCircle(c, r, F(IM_COL32(255, 255, 255, 40)), 28, 1.0f);
				TxtC(dl, g_num, r * 1.25f, c.x, c.y - r * 0.66f, F(col), std::string(1, ch));
				return;
			}
			if (a_name.size() == 2 && a_name[0] == 'D') {   // the d-pad
				const float w = r * 0.42f, L = r;
				dl->AddRectFilled({ c.x - w, c.y - L }, { c.x + w, c.y + L }, F(IM_COL32(30, 30, 34, 245)), r * 0.15f);
				dl->AddRectFilled({ c.x - L, c.y - w }, { c.x + L, c.y + w }, F(IM_COL32(30, 30, 34, 245)), r * 0.15f);
				const ImU32 lit = F(IM_COL32(240, 226, 196, 255));
				const char  d = a_name[1];
				if (d == 'U') dl->AddRectFilled({ c.x - w * 0.7f, c.y - L * 0.92f }, { c.x + w * 0.7f, c.y - w }, lit, r * 0.1f);
				if (d == 'D') dl->AddRectFilled({ c.x - w * 0.7f, c.y + w }, { c.x + w * 0.7f, c.y + L * 0.92f }, lit, r * 0.1f);
				if (d == 'L') dl->AddRectFilled({ c.x - L * 0.92f, c.y - w * 0.7f }, { c.x - w, c.y + w * 0.7f }, lit, r * 0.1f);
				if (d == 'R') dl->AddRectFilled({ c.x + w, c.y - w * 0.7f }, { c.x + L * 0.92f, c.y + w * 0.7f }, lit, r * 0.1f);
				dl->AddRect({ c.x - w, c.y - L }, { c.x + w, c.y + L }, F(IM_COL32(255, 255, 255, 50)), r * 0.15f, 0, 1.0f);
				dl->AddRect({ c.x - L, c.y - w }, { c.x + L, c.y + w }, F(IM_COL32(255, 255, 255, 50)), r * 0.15f, 0, 1.0f);
				return;
			}
			// pills: bumpers, triggers, start / back
			const float  hw = r * (a_name.size() > 2 ? 1.6f : 1.25f), hh = r * 0.68f;
			const bool   trig = a_name == "LT" || a_name == "RT";
			if (Settings::PlayStationIcons()) {  // L1 / R1 / L2 / R2, Options / Create
				a_name = a_name == "LB" ? "L1" : a_name == "RB" ? "R1" : a_name == "LT" ? "L2" : a_name == "RT" ? "R2" : a_name == "Start" ? "Options" : a_name == "Back" ? "Create" : a_name;
			}
			dl->AddRectFilled({ c.x - hw, c.y - hh }, { c.x + hw, c.y + hh }, F(IM_COL32(24, 24, 28, 245)), trig ? r * 0.25f : hh);
			dl->AddRect({ c.x - hw, c.y - hh }, { c.x + hw, c.y + hh }, F(IM_COL32(220, 210, 190, 200)), trig ? r * 0.25f : hh, 0, 1.4f);
			TxtC(dl, g_num, r * 0.95f, c.x, c.y - r * 0.5f, F(IM_COL32(240, 230, 210, 255)), std::string(a_name));
		}

		void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float k, float alpha = 1.0f)
		{
			{  // the modpack's look (Noah: black and a blue accent): black glass, a rune-blue border with a soft glow, a rune mark at each corner
				dl->AddRectFilledMultiColor(a, b, Fade(IM_COL32(10, 10, 12, 235), alpha), Fade(IM_COL32(10, 10, 12, 235), alpha), Fade(IM_COL32(2, 2, 3, 242), alpha), Fade(IM_COL32(2, 2, 3, 242), alpha));
				for (int g = 3; g >= 1; --g) {
					dl->AddRect({ a.x - g * k, a.y - g * k }, { b.x + g * k, b.y + g * k }, Fade(IM_COL32(220, 220, 214, 22), alpha), 3 * k, 0, 2.0f * k);
				}
				dl->AddRect(a, b, Fade(IM_COL32(200, 202, 206, 210), alpha), 2 * k, 0, 1.8f * k);
				const float t = 12 * k;
				for (auto [x, y, sx, sy] : { std::tuple{ a.x, a.y, 1.f, 1.f }, { b.x, a.y, -1.f, 1.f }, { a.x, b.y, 1.f, -1.f }, { b.x, b.y, -1.f, -1.f } }) {
					dl->AddLine({ x, y }, { x + sx * t, y }, Fade(IM_COL32(170, 215, 255, 255), alpha), 2 * k);
					dl->AddLine({ x, y }, { x, y + sy * t }, Fade(IM_COL32(170, 215, 255, 255), alpha), 2 * k);
					dl->AddLine({ x + sx * 4 * k, y + sy * 4 * k }, { x + sx * 8 * k, y + sy * 8 * k }, Fade(IM_COL32(170, 215, 255, 200), alpha), 1.5f * k);  // (a rune's stroke)
				}
				return;
			}
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
		// A red fire licking round a health bar's border: tier 1 (the specials) a low smoulder, 2 (elites) a steady
		// blaze, 3 (champions) tall tongues and embers, 4 (bosses) a roaring crown. a..z is the plate's box.
		void FlameBorder(ImDrawList* dl, ImVec2 a, ImVec2 z, int tier, float t, float alpha, float k)
		{
			if (tier <= 0 || alpha <= 0.01f) {
				return;
			}
			const float T = static_cast<float>(tier);
			const float w = z.x - a.x, hgt = z.y - a.y;
			const float reach = (3.0f + 2.6f * T) * k;          // how tall the tongues stand
			const float fps = 1.0f + 0.35f * T;
			const auto  nz = [](float x) { return 0.5f + 0.5f * std::sin(x) * std::cos(x * 0.73f + 1.3f); };
			const auto  col = [&](int r, int g, int b, float a0) { return IM_COL32(r, g, b, static_cast<int>(std::clamp(a0 * alpha, 0.0f, 1.0f) * 255)); };
			// the heat haze behind the plate
			for (int q = 3; q >= 1; --q) {
				const float g = q * (1.4f + 0.9f * T) * k;
				dl->AddRectFilled({ a.x - g, a.y - g * 1.3f }, { z.x + g, z.y + g * 0.6f }, col(200, 20, 10, (0.05f + 0.025f * T) * (1.0f + 0.25f * std::sin(t * 3.0f + q))), g + hgt * 0.5f);
			}
			// tongues along the top edge (outer dark red, inner orange, a hot yellow heart from the blaze up)
			const float step = std::max(3.0f * k, (7.5f - 0.9f * T) * k);
			const int   n = std::max(4, static_cast<int>(w / step));
			for (int layer = 0; layer < (tier >= 2 ? 3 : 2); ++layer) {
				const float lh = layer == 0 ? 1.0f : layer == 1 ? 0.62f : 0.34f;
				const ImU32 c = layer == 0 ? col(150, 12, 8, 0.55f + 0.08f * T) : layer == 1 ? col(235, 60, 18, 0.6f + 0.08f * T) : col(255, 190, 90, 0.55f + 0.1f * T);
				for (int i = 0; i <= n; ++i) {
					const float x = a.x + w * i / n + (layer == 1 ? step * 0.5f : 0.0f);
					if (x > z.x + 1) continue;
					const float ph = i * 1.917f + layer * 2.3f;
					const float fl = nz(ph + t * fps * 5.0f) * 0.75f + 0.25f * nz(ph * 1.7f - t * fps * 8.0f);
					const float h = reach * lh * (0.35f + 0.9f * fl);
					const float half = step * (0.55f + 0.1f * layer);
					const float sway = std::sin(t * 4.0f + ph) * step * 0.45f;
					dl->AddTriangleFilled({ x - half, a.y + 1.0f * k }, { x + sway, a.y - h }, { x + half, a.y + 1.0f * k }, c);
				}
			}
			// licks up the two ends, and a faint fringe under the bar
			for (int sd = 0; sd < 2; ++sd) {
				const float ex = sd == 0 ? a.x : z.x, dir = sd == 0 ? -1.0f : 1.0f;
				for (int j = 0; j < 2 + tier; ++j) {
					const float yy = z.y - hgt * (j + 0.5f) / (2 + tier);
					const float fl = nz(j * 2.1f + sd * 4.0f + t * fps * 6.0f);
					const float L = reach * (0.45f + 0.6f * fl);
					dl->AddTriangleFilled({ ex, yy + 2.0f * k }, { ex + dir * L * 0.55f, yy - L * 0.85f }, { ex, yy - 2.0f * k }, sd == 0 ? col(220, 40, 14, 0.55f) : col(220, 40, 14, 0.55f));
				}
			}
			dl->AddLine({ a.x, z.y + 0.5f * k }, { z.x, z.y + 0.5f * k }, col(255, 70, 30, 0.25f + 0.08f * T), 1.2f * k);
			// embers rising (champions and bosses)
			if (tier >= 3) {
				const int m = tier == 3 ? 7 : 14;
				for (int i = 0; i < m; ++i) {
					const float life = std::fmod(t * (0.7f + 0.13f * (i % 5)) + i * 0.618f, 1.0f);
					const float x = a.x + w * std::fmod(i * 0.381f + 0.13f * std::sin(t + i), 1.0f);
					const float y = a.y - reach * 0.4f - life * reach * 2.2f;
					dl->AddCircleFilled({ x + std::sin(t * 3.0f + i) * 3.0f * k, y }, (1.0f + 0.5f * (1 - life)) * k, col(255, 150 - static_cast<int>(90 * life), 60, (1.0f - life) * 0.9f), 6);
				}
			}
			// the border itself glows hot
			dl->AddRect({ a.x - 1.0f * k, a.y - 1.0f * k }, { z.x + 1.0f * k, z.y + 1.0f * k }, col(255, 80 + 20 * tier, 30, 0.35f + 0.1f * T * (0.8f + 0.2f * std::sin(t * 6.0f))), 2.0f * k, 0, (0.8f + 0.3f * T) * k);
		}

		void Bar(ImDrawList* dl, ImVec2 a, float w, float h, float frac, float ghost, ImU32 col, float k)
		{
			const ImVec2 b{ a.x + w, a.y + h };
			dl->AddRectFilled({ a.x - 2 * k, a.y - 2 * k }, { b.x + 2 * k, b.y + 2 * k }, IM_COL32(0, 0, 0, 150), 3 * k);
			dl->AddRectFilled(a, { a.x + w * std::clamp(ghost, 0.0f, 1.0f), b.y }, IM_COL32(255, 240, 200, 90), 2 * k);
			const ImU32 top = Fade(col, 1.0f), bot = Fade(IM_COL32((col & 0xFF) * 6 / 10, ((col >> 8) & 0xFF) * 6 / 10, ((col >> 16) & 0xFF) * 6 / 10, 255), 1.0f);
			dl->AddRectFilledMultiColor(a, { a.x + w * std::clamp(frac, 0.0f, 1.0f), b.y }, top, top, bot, bot);
			dl->AddRect({ a.x - 2 * k, a.y - 2 * k }, { b.x + 2 * k, b.y + 2 * k }, kGoldDim, 3 * k, 0, 1.0f * k);
		}

		// The compass, in the mod's iron and bronze: a chamfered plate across the top, rune ticks every 15 degrees,
		// the four winds (north in blood), and the marks of what lies around you - your rite (a burning diamond), the
		// shrines (a blue orb), cities, towns and dungeons, friends, and at night the dead (red, nearer = bigger).
		// The nearest mark at the middle shows its name and distance under the plate.
		void DrawCompass(ImDrawList* dl, const Hud::State& s, ImVec2 size, float k, float now)
		{
			const float W = 640 * k, H = 30 * k, cx = size.x * 0.5f, top = 38 * k, cy = top + H * 0.5f;
			const float span = 1.40f;  // radians each side of the middle
			const ImVec2 a{ cx - W / 2, top }, b{ cx + W / 2, top + H };
			const float e = H * 0.5f + 8 * k;
			const ImVec2 plate[6] = { { a.x - e, cy }, { a.x, a.y }, { b.x, a.y }, { b.x + e, cy }, { b.x, b.y }, { a.x, b.y } };
			dl->AddConvexPolyFilled(plate, 6, IM_COL32(4, 4, 6, 225));
			dl->AddRectFilledMultiColor(a, { b.x, cy }, IM_COL32(40, 80, 140, 30), IM_COL32(40, 80, 140, 30), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
			dl->AddPolyline(plate, 6, Fade(kGold, 0.9f), ImDrawFlags_Closed, 1.4f * k);
			auto rel = [&](float ang) {
				float d = ang - s.heading;
				while (d > RE::NI_PI) d -= 2 * RE::NI_PI;
				while (d < -RE::NI_PI) d += 2 * RE::NI_PI;
				return d;
			};
			auto xAt = [&](float d) { return cx + d / span * (W / 2 - 14 * k); };
			auto edgeFade = [&](float d) { return std::clamp((span - std::abs(d)) / 0.25f, 0.0f, 1.0f); };
			dl->PushClipRect({ a.x, a.y - 30 * k }, { b.x, b.y + 4 * k }, true);
			// ticks and winds
			for (int i = 0; i < 24; ++i) {
				const float ang = i * RE::NI_PI / 12, d = rel(ang);
				if (std::abs(d) > span) continue;
				const float x = xAt(d), f = edgeFade(d);
				if (i % 6 == 0) {
					static const char* kW[4] = { "N", "E", "S", "W" };
					const ImU32 c = i == 0 ? IM_COL32(220, 50, 40, 255) : kParch;
					TxtC(dl, g_title, 20 * k, x, cy - 11 * k, Fade(c, f), kW[i / 6], i == 0 ? 2 * k : 0, IM_COL32(255, 40, 20, 255));
				} else if (i % 3 == 0) {
					static const char* kI[4] = { "NE", "SE", "SW", "NW" };
					TxtC(dl, g_body, 12 * k, x, cy - 7 * k, Fade(kGold, 0.75f * f), kI[i / 6]);
				} else {  // a rune tick
					dl->AddLine({ x, b.y - 8 * k }, { x, b.y - 3 * k }, Fade(kGold, 0.6f * f), 1.2f * k);
					dl->AddLine({ x - 2 * k, b.y - 6 * k }, { x + 2 * k, b.y - 6 * k }, Fade(kGold, 0.4f * f), 1.0f * k);
				}
			}
			// the marks: places first (under), then the living things, the rite on top
			std::vector<const Hud::State::CompassMark*> order;
			for (const auto& m : s.compass) order.push_back(&m);
			std::sort(order.begin(), order.end(), [](auto x, auto y) {
				auto rank = [](int kd) { return kd >= 3 && kd <= 6 ? 0 : kd == 8 || kd == 9 ? 1 : kd == 7 ? 2 : kd == 2 ? 3 : 4; };
				return rank(x->kind) != rank(y->kind) ? rank(x->kind) < rank(y->kind) : x->dist > y->dist;
			});
			const Hud::State::CompassMark* centre = nullptr;
			float best = 0.09f;
			for (const auto* m : order) {
				const float d = rel(m->ang);
				if (std::abs(d) > span) continue;
				const float  x = xAt(d), f = edgeFade(d);
				const ImVec2 c{ x, cy + 1 * k };
				const float  nr = std::clamp(1.0f - m->dist / 6000.0f, 0.0f, 1.0f);
				// the map's places: the world map's own icon for each (Noah: the compass should match the map)
				if (m->kind >= 3 && m->kind <= 6 && g_mapIcons && m->icon > 0 && m->icon < 64) {
					const float  r = (m->kind == 3 ? 12.0f : m->kind == 4 ? 10.0f : 8.5f) * k;
					const ImVec2 uv0{ (m->icon % 8) / 8.0f, (m->icon / 8) / 8.0f }, uv1{ uv0.x + 0.125f, uv0.y + 0.125f };
					const int    a8 = static_cast<int>(255 * f * (m->known ? 1.0f : 0.55f));
					dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_mapIcons)), { c.x - r + 1 * k, c.y - r + 1 * k }, { c.x + r + 1 * k, c.y + r + 1 * k }, uv0, uv1, IM_COL32(0, 0, 0, a8 * 3 / 4));
					dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_mapIcons)), { c.x - r, c.y - r }, { c.x + r, c.y + r }, uv0, uv1, m->kind == 3 ? IM_COL32(255, 236, 196, a8) : IM_COL32(236, 232, 224, a8));
				} else switch (m->kind) {
				case 1: {  // the rite: a burning diamond
					const float r = 9 * k, pu = 0.75f + 0.25f * std::sin(now * 3.0f);
					for (int i = 3; i >= 1; --i) {
						const float g = r * (1.0f + 0.4f * i);
						dl->AddQuadFilled({ c.x, c.y - g }, { c.x + g * 0.7f, c.y }, { c.x, c.y + g }, { c.x - g * 0.7f, c.y }, Fade(IM_COL32(236, 168, 72, 255), 0.08f * pu * f));
					}
					dl->AddQuadFilled({ c.x, c.y - r }, { c.x + r * 0.7f, c.y }, { c.x, c.y + r }, { c.x - r * 0.7f, c.y }, Fade(IM_COL32(20, 12, 8, 240), f));
					dl->AddQuad({ c.x, c.y - r }, { c.x + r * 0.7f, c.y }, { c.x, c.y + r }, { c.x - r * 0.7f, c.y }, Fade(IM_COL32(236, 168, 72, 255), f), 1.8f * k);
					const float q = r * 0.42f;
					dl->AddQuadFilled({ c.x, c.y - q }, { c.x + q * 0.7f, c.y }, { c.x, c.y + q }, { c.x - q * 0.7f, c.y }, Fade(IM_COL32(255, 214, 130, 255), pu * f));
					break;
				}
				case 2: {  // a shrine: the blue orb in its ring
					const float r = 6.5f * k, pu = 0.5f + 0.5f * std::sin(now * 1.6f);
					dl->AddCircleFilled(c, r * 2.0f, IM_COL32(80, 150, 255, static_cast<int>((30 + 30 * pu) * f)), 20);
					dl->AddCircle(c, r * 1.45f, Fade(IM_COL32(140, 200, 255, 255), 0.8f * f), 20, 1.2f * k);
					dl->AddCircleFilled(c, r, Fade(IM_COL32(150, 210, 255, 255), f), 20);
					dl->AddCircleFilled({ c.x - r * 0.3f, c.y - r * 0.3f }, r * 0.35f, Fade(IM_COL32(240, 250, 255, 255), f), 10);
					break;
				}
				case 3: {  // a city: a bronze keep
					const float w = 7 * k, h = 9 * k;
					dl->AddRectFilled({ c.x - w, c.y - h * 0.4f }, { c.x + w, c.y + h }, Fade(IM_COL32(20, 16, 12, 230), f));
					dl->AddRect({ c.x - w, c.y - h * 0.4f }, { c.x + w, c.y + h }, Fade(kGold, f), 0, 0, 1.2f * k);
					for (int t = -1; t <= 1; ++t) {
						dl->AddRectFilled({ c.x + t * w * 0.7f - w * 0.22f, c.y - h }, { c.x + t * w * 0.7f + w * 0.22f, c.y - h * 0.4f }, Fade(kGold, f));
					}
					break;
				}
				case 4: {  // a town: a roof
					const float w = 6 * k;
					dl->AddTriangleFilled({ c.x - w, c.y }, { c.x, c.y - w }, { c.x + w, c.y }, Fade(kGold, 0.9f * f));
					dl->AddRectFilled({ c.x - w * 0.7f, c.y }, { c.x + w * 0.7f, c.y + w * 0.8f }, Fade(IM_COL32(150, 120, 80, 255), 0.9f * f));
					break;
				}
				case 5: {  // a dungeon: a dark arch
					const float w = 6 * k;
					dl->PathArcTo({ c.x, c.y }, w, RE::NI_PI, 2 * RE::NI_PI, 10);
					dl->PathLineTo({ c.x + w, c.y + w });
					dl->PathLineTo({ c.x - w, c.y + w });
					dl->PathFillConvex(Fade(IM_COL32(30, 24, 22, 230), f));
					dl->PathArcTo({ c.x, c.y }, w, RE::NI_PI, 2 * RE::NI_PI, 10);
					dl->PathStroke(Fade(IM_COL32(180, 150, 110, 255), f), 0, 1.3f * k);
					break;
				}
				case 6:  // any other place: a small rune
					dl->AddQuadFilled({ c.x, c.y - 4 * k }, { c.x + 3 * k, c.y }, { c.x, c.y + 4 * k }, { c.x - 3 * k, c.y }, Fade(kGoldDim, f));
					break;
				case 7: {  // a friend: a green shield
					const float w = 6 * k;
					const ImVec2 sh[5] = { { c.x - w, c.y - w }, { c.x + w, c.y - w }, { c.x + w, c.y + w * 0.2f }, { c.x, c.y + w * 1.2f }, { c.x - w, c.y + w * 0.2f } };
					dl->AddConvexPolyFilled(sh, 5, Fade(IM_COL32(90, 200, 110, 255), f));
					dl->AddPolyline(sh, 5, Fade(IM_COL32(220, 255, 220, 255), f), ImDrawFlags_Closed, 1.2f * k);
					break;
				}
				case 8:  // the dead: red, nearer = bigger and brighter
					dl->AddCircleFilled(c, (2.0f + 3.0f * nr) * k, Fade(IM_COL32(220, 40, 30, 255), (0.45f + 0.55f * nr) * f), 10);
					break;
				case 9: {  // a boss or champion: a red skull
					const float r = 7 * k, pu = 0.6f + 0.4f * std::sin(now * 5.0f);
					dl->AddCircleFilled(c, r * 1.8f, IM_COL32(255, 40, 20, static_cast<int>(50 * pu * f)), 16);
					dl->AddCircleFilled({ c.x, c.y - r * 0.2f }, r, Fade(IM_COL32(230, 60, 40, 255), f), 16);
					dl->AddRectFilled({ c.x - r * 0.55f, c.y + r * 0.4f }, { c.x + r * 0.55f, c.y + r }, Fade(IM_COL32(230, 60, 40, 255), f));
					dl->AddCircleFilled({ c.x - r * 0.38f, c.y - r * 0.25f }, r * 0.25f, Fade(IM_COL32(10, 4, 4, 255), f), 8);
					dl->AddCircleFilled({ c.x + r * 0.38f, c.y - r * 0.25f }, r * 0.25f, Fade(IM_COL32(10, 4, 4, 255), f), 8);
					break;
				}
				}
				if (std::abs(d) < best && m->kind != 8 && !m->label.empty()) {
					best = std::abs(d), centre = m;
				}
			}
			dl->PopClipRect();
			if (centre) {
				const int m = static_cast<int>(centre->dist * 0.01428f);
				const std::string t = centre->label + "   " + std::to_string(m) + " m";
				TxtC(dl, g_body, 15 * k, cx, b.y + 6 * k, IM_COL32(236, 222, 190, 235), t, 1.5f * k, IM_COL32(0, 0, 0, 255));
			}
			// (Noah: a 12-hour clock, easier to read) the time of day at the compass's right end
			if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->gameHour) {
				const float h = std::fmod(cal->gameHour->value, 24.0f);
				int         hh = static_cast<int>(h), mm = static_cast<int>((h - hh) * 60.0f);
				const bool  pm = hh >= 12;
				hh %= 12;
				if (hh == 0) hh = 12;
				const std::string t = fmt::format("{}:{:02} {}", hh, mm, pm ? "PM" : "AM");
				Txt(dl, g_title, 17 * k, { b.x + e + 10 * k, cy - 10 * k }, IM_COL32(236, 222, 190, 230), t, 1.5f * k, IM_COL32(0, 0, 0, 255));
			}
		}

		// (the classes overhaul) the class picker: four cards across the screen, the chosen one lit in its colour
		void DrawClassPicker(ImDrawList* dl, ImVec2 size, float k, float now)
		{
			struct Card { const char* name; ImU32 col; const char* tag; const char* arms; const char* grow; const char* kit; };
			static const Card kCards[4] = {
				{ "SUMMONER", IM_COL32(170, 120, 255, 255), "An army that grows family by family", "Summons - beasts, the dead, Daedra, Dwemer, the wild",
				  "Bind Pact: raise each family's summon at the altar", "Conjure Skeleton, Conjure Wolf, summoner's robes (the Wandering Summoner's for women, hooded conjurer's robes for men)" },
				{ "DESTRUCTION", IM_COL32(255, 120, 60, 255), "Master the elements one at a time", "Two-handed casting: fire, frost, shock, sun, poison, blood",
				  "Reforge: climb each element's ladder to its Legendary spell", "Scorch, Hoarfrost, the Wild Witch's hat and gown" },
				{ "RANGER", IM_COL32(110, 210, 120, 255), "Bows and crossbows - and every arrow counts", "The six elemental bows, crossbows and their bolts",
				  "Mastery: a bow's kills raise its tier; buy ammo at the altars", "An elemental bow, the Dawnguard Crossbow, 30 bolts, the Drifter Outfit" },
				{ "SPELLSWORD", IM_COL32(120, 180, 255, 255), "Steel in one hand, a spell in the other", "Spell-weapons, spellshields, touch spells, The Wizard Warrior",
				  "Imbue: raise each element's touch spell; power attacks cast your spell", "Steel Spellsword, Steel Spellshield, Flame Touch, the Practical Pirate outfit" } };
			dl->AddRectFilled({ 0, 0 }, size, IM_COL32(4, 4, 8, 215));
			TxtC(dl, g_title, 46 * k, size.x * 0.5f, size.y * 0.12f, kParch, Classes::Switching() ? "CHANGE YOUR PATH" : "CHOOSE YOUR PATH", 3 * k, Fade(kGold, 0.6f));
			const std::string sub = Classes::Switching() ? "Switching costs " + std::to_string(Classes::SwitchCost()) + " septims. Each class keeps its own spells and progress."
			                                             : "Each player picks their own path. Duplicates are allowed.";
			TxtC(dl, g_body, 20 * k, size.x * 0.5f, size.y * 0.12f + 58 * k, IM_COL32(210, 200, 180, 230), sub);
			const float cw = 330 * k, ch = 430 * k, gap = 26 * k;
			const float x0 = size.x * 0.5f - (cw * 4 + gap * 3) * 0.5f, y0 = size.y * 0.5f - ch * 0.5f + 20 * k;
			const int   sel = Classes::Selected(), cur = Classes::Get();
			for (int i = 0; i < 4; ++i) {
				const Card& c = kCards[i];
				const bool  on = i == sel;
				const float lift = on ? (6 + 2 * std::sin(now * 3.0f)) * k : 0.0f;
				const ImVec2 a{ x0 + i * (cw + gap), y0 - lift }, b{ a.x + cw, a.y + ch };
				Panel(dl, a, b, k, on ? 1.0f : 0.75f);
				if (on) {
					dl->AddRect({ a.x - 3 * k, a.y - 3 * k }, { b.x + 3 * k, b.y + 3 * k }, Fade(c.col, 0.9f), 10 * k, 0, 2.5f * k);
					dl->AddRectFilledMultiColor(a, { b.x, a.y + 90 * k }, Fade(c.col, 0.28f), Fade(c.col, 0.28f), Fade(c.col, 0.0f), Fade(c.col, 0.0f));
				}
				const float cx = (a.x + b.x) * 0.5f;
				TxtC(dl, g_body, 16 * k, cx, a.y + 18 * k, Fade(c.col, 0.9f), std::to_string(i + 1));
				TxtC(dl, g_title, 30 * k, cx, a.y + 38 * k, on ? IM_COL32(255, 248, 232, 255) : kParch, c.name, on ? 2.5f * k : 0, Fade(c.col, 0.8f));
				TxtWrap(dl, g_body, 18 * k, { a.x + 22 * k, a.y + 92 * k }, IM_COL32(236, 226, 206, 245), c.tag, cw - 44 * k, 2);
				float y = a.y + 150 * k;
				const auto sec = [&](const char* h, const char* t) {
					Txt(dl, g_body, 14 * k, { a.x + 22 * k, y }, Fade(c.col, 0.95f), h);
					const int n = TxtWrap(dl, g_body, 16 * k, { a.x + 22 * k, y + 20 * k }, IM_COL32(205, 196, 178, 235), t, cw - 44 * k, 3);
					y += 28 * k + n * 20 * k;
				};
				sec("FIGHTS WITH", c.arms);
				sec("GROWS BY", c.grow);
				sec("STARTS WITH", c.kit);
				if (i == cur) TxtC(dl, g_body, 15 * k, cx, b.y - 30 * k, IM_COL32(255, 210, 110, 255), "YOUR CLASS");
			}
			const std::string hint = g_padLast ? (Classes::Switching() ? "D-pad  choose      A  take this path      B  keep yours" : "D-pad  choose      A  take this path")
			                                   : (Classes::Switching() ? "1-4 or Left / Right  choose      Enter  take this path      Backspace  keep yours" : "1-4 or Left / Right  choose      Enter  take this path");
			TxtC(dl, g_body, 19 * k, size.x * 0.5f, y0 + ch + 40 * k, IM_COL32(230, 220, 200, 240), hint);
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
			if (Classes::Open()) {  // (the class picker takes the whole screen)
				DrawClassPicker(dl, size, k, now);
				return;
			}

			// The shrine's orb in the crosshair: what Activate does (its aura of cold flame is 3D now, in crystal.nif)
			if (s.orbOn && s.orbAim && s.orbFade > 0.01f && !s.perkMenu) {
				if (g_padLast) {  // the controller's button beside the name
					const float w = Measure(g_title, 20 * k, "Echo Altar").x;
					PadGlyph(dl, { size.x * 0.5f - w * 0.5f - 6 * k, size.y * 0.5f + 52 * k }, 12 * k, "A");
					TxtC(dl, g_title, 20 * k, size.x * 0.5f + 12 * k, size.y * 0.5f + 40 * k, kParch, "Echo Altar", 2 * k, IM_COL32(60, 120, 255, 255));
				} else {
					TxtC(dl, g_title, 20 * k, size.x * 0.5f, size.y * 0.5f + 40 * k, kParch, "E   Echo Altar", 2 * k, IM_COL32(60, 120, 255, 255));
				}
			}

			if (s.compassOn && !s.perkMenu && s.bossName.empty()) {  // (a boss's bar takes the top of the screen while it lives)
				DrawCompass(dl, s, size, k, now);
			}

			// The crosshair: a small white dot (Skyrim's own is hidden), and while you sneak a small clean eye just over
			// it - a thin almond that opens as the most watchful one grows suspicious, white while hidden, amber when
			// they wonder, red when you are seen
			if (!s.perkMenu && !s.lootOn) {
				const ImVec2 cc{ size.x * 0.5f, size.y * 0.5f };
				dl->AddCircleFilled(cc, 3.2f * k, IM_COL32(0, 0, 0, 90), 16);
				dl->AddCircleFilled(cc, 2.2f * k, IM_COL32(255, 255, 255, 235), 16);
			}
			constexpr bool kOwnSneakEye = false;  // (Noah: back to Skyrim's own sneak symbol)
			if (kOwnSneakEye && s.sneaking && !s.perkMenu) {
				static float open = 0.15f;
				open += (std::max(0.15f, s.aware) - open) * std::min(1.0f, dt * 8);
				const ImVec2 c{ size.x * 0.5f, size.y * 0.5f - 26 * k };
				const float  w = 15 * k, h = 7 * k;
				const ImU32  col = s.seen ? IM_COL32(240, 70, 55, 255) : s.aware > 0.15f ? IM_COL32(240, 180, 80, 255) : IM_COL32(240, 240, 240, 255);
				ImVec2 up[17], lo[17];
				for (int q = 0; q <= 16; ++q) {
					const float t = q / 8.0f - 1.0f, cv = 1 - t * t;
					up[q] = { c.x + t * w, c.y - h * cv };
					lo[q] = { c.x + t * w, c.y + h * cv };
				}
				ImVec2 lens[32];
				int    m = 0;
				for (int q = 0; q <= 16; ++q) { const float t = q / 8.0f - 1.0f; lens[m++] = { c.x + t * w, c.y - h * (1 - t * t) * open }; }
				for (int q = 15; q >= 1; --q) { const float t = q / 8.0f - 1.0f; lens[m++] = { c.x + t * w, c.y + h * (1 - t * t) * open }; }
				dl->AddConcavePolyFilled(lens, m, Fade(col, 0.30f));
				dl->AddPolyline(up, 17, Fade(col, 0.9f), 0, 1.4f * k);
				dl->AddPolyline(lo, 17, Fade(col, 0.9f), 0, 1.4f * k);
				dl->AddCircleFilled(c, std::max(1.2f * k, h * 0.55f * open), Fade(col, 0.95f), 16);
			}

			// The quests chosen in the journal, top left (under Skyrim's notices): name in gold, open goals beneath
			if (!s.tracked.empty() && !s.perkMenu) {
				float y = size.y * 0.24f;
				const float x = 34 * k;
				for (const auto& t : s.tracked) {
					float w = Measure(g_title, 20 * k, t.title.c_str()).x;
					for (const auto& g : t.goals) {
						w = std::max(w, Measure(g_body, 16 * k, g.c_str()).x + 18 * k);
					}
					w = std::min(w, size.x * 0.28f);
					const float h = 30 * k + t.goals.size() * 22 * k;
					dl->AddRectFilledMultiColor({ x - 14 * k, y - 6 * k }, { x + w + 40 * k, y + h }, IM_COL32(8, 7, 10, 150), IM_COL32(8, 7, 10, 0), IM_COL32(8, 7, 10, 0), IM_COL32(8, 7, 10, 150));
					dl->AddLine({ x - 14 * k, y - 6 * k }, { x - 14 * k, y + h }, Fade(kGold, 0.8f), 2 * k);
					Txt(dl, g_title, 20 * k, { x, y }, Fade(kGold, 0.95f), t.title, 1.5f * k, IM_COL32(0, 0, 0, 200));
					float gy = y + 28 * k;
					for (const auto& g : t.goals) {
						const ImVec2 c{ x + 5 * k, gy + 9 * k };
						const ImVec2 d[4] = { { c.x, c.y - 4 * k }, { c.x + 4 * k, c.y }, { c.x, c.y + 4 * k }, { c.x - 4 * k, c.y } };
						dl->AddConvexPolyFilled(d, 4, Fade(kGold, 0.8f));
						Txt(dl, g_body, 16 * k, { x + 18 * k, gy }, IM_COL32(222, 214, 196, 240), g, 1.2f * k, IM_COL32(0, 0, 0, 200));
						gy += 22 * k;
					}
					y += h + 14 * k;
				}
			}

			// Quick loot (src/Loot.cpp): what a body or a chest holds, beside the crosshair, in the shrine menu's style
			if (s.lootOn && !s.perkMenu) {
				const float w = 400 * k, rowH = 34 * k;
				const bool  listed = s.lootNote.empty() || s.lootNote.rfind("Owned", 0) == 0;  // (owned: its things are shown, in red)
				const bool  owned = !s.lootNote.empty() && listed;
				const int   rows = listed ? static_cast<int>(s.loot.size()) : 0, shown = std::min(rows, 7);
				const float h = 64 * k + std::max(1, shown) * rowH + (owned ? rowH : 0.0f) + 46 * k;
				const ImVec2 a{ size.x * 0.5f + 90 * k, size.y * 0.5f - h * 0.45f }, b{ a.x + w, a.y + h };
				Panel(dl, a, b, k, 0.90f);
				const float cx = (a.x + b.x) * 0.5f;
				TxtC(dl, g_title, 22 * k, cx, a.y + 12 * k, IM_COL32(232, 214, 176, 255), s.lootTitle.empty() ? std::string("Remains") : s.lootTitle, 2.0f * k, IM_COL32(150, 70, 255, 255));
				Divider(dl, cx, a.y + 46 * k, w * 0.36f, k);
				float y = a.y + 58 * k;
				if (!s.lootNote.empty()) {
					TxtC(dl, g_body, 17 * k, cx, y + 6 * k, s.lootNote == "Empty" ? IM_COL32(170, 160, 140, 230) : IM_COL32(230, 110, 90, 240), s.lootNote);
					if (owned) {
						y += rowH;
					}
				}
				const int first = std::clamp(s.lootSel - 3, 0, std::max(0, rows - shown));
				auto icon = [&](int kind, ImVec2 c, float r, ImU32 col) {  // a small sigil per kind of thing
					switch (kind) {
					case 1: dl->AddCircleFilled(c, r * 0.8f, IM_COL32(230, 180, 70, 255), 16); dl->AddCircle(c, r * 0.55f, IM_COL32(120, 80, 20, 255), 16, 1.2f * k); break;   // gold
					case 2: dl->AddLine({ c.x - r, c.y + r }, { c.x + r, c.y - r }, col, 2.4f * k); dl->AddLine({ c.x - r * 0.5f, c.y + r * 0.1f }, { c.x - r * 0.1f, c.y + r * 0.5f }, col, 2.4f * k); break;  // a blade
					case 3: dl->AddQuadFilled({ c.x - r, c.y - r * 0.8f }, { c.x + r, c.y - r * 0.8f }, { c.x + r * 0.7f, c.y + r * 0.4f }, { c.x - r * 0.7f, c.y + r * 0.4f }, Fade(col, 0.8f)); dl->AddTriangleFilled({ c.x - r * 0.7f, c.y + r * 0.4f }, { c.x + r * 0.7f, c.y + r * 0.4f }, { c.x, c.y + r }, Fade(col, 0.8f)); break;  // a shield
					case 4: dl->AddCircleFilled({ c.x, c.y + r * 0.25f }, r * 0.65f, IM_COL32(200, 60, 80, 255), 12); dl->AddRectFilled({ c.x - r * 0.2f, c.y - r }, { c.x + r * 0.2f, c.y - r * 0.3f }, col); break;  // a flask
					case 5: dl->AddCircleFilled(c, r * 0.6f, IM_COL32(110, 170, 80, 255), 8); break;  // a herb
					case 6: case 7: dl->AddRect({ c.x - r * 0.8f, c.y - r }, { c.x + r * 0.8f, c.y + r }, col, 1.5f * k, 0, 1.6f * k); dl->AddLine({ c.x, c.y - r }, { c.x, c.y + r }, col, 1.2f * k); break;  // a book
					case 8: dl->AddLine({ c.x - r, c.y + r }, { c.x + r, c.y - r }, col, 1.6f * k); dl->AddTriangleFilled({ c.x + r, c.y - r }, { c.x + r * 0.2f, c.y - r * 0.6f }, { c.x + r * 0.6f, c.y - r * 0.2f }, col); break;  // an arrow
					case 9: dl->AddQuadFilled({ c.x, c.y - r }, { c.x + r * 0.7f, c.y }, { c.x, c.y + r }, { c.x - r * 0.7f, c.y }, IM_COL32(170, 110, 255, 255)); break;  // a soul gem
					case 10: dl->AddCircle({ c.x - r * 0.4f, c.y }, r * 0.45f, col, 12, 1.6f * k); dl->AddLine({ c.x, c.y }, { c.x + r, c.y }, col, 1.6f * k); break;  // a key
					default: dl->AddCircle(c, r * 0.6f, col, 6, 1.4f * k); break;
					}
				};
				for (int i = first; i < first + shown; ++i) {
					const auto& it = s.loot[i];
					const bool  sel = i == s.lootSel;
					const ImVec2 r0{ a.x + 12 * k, y }, r1{ b.x - 12 * k, y + rowH - 4 * k };
					if (sel) {
						const float pl = 0.75f + 0.25f * std::sin(now * 4.0f);
						dl->AddRectFilledMultiColor(r0, r1, IM_COL32(120, 60, 220, static_cast<int>(120 * pl)), IM_COL32(60, 30, 120, 30), IM_COL32(60, 30, 120, 30), IM_COL32(120, 60, 220, static_cast<int>(120 * pl)));
						dl->AddRect(r0, r1, Fade(kGold, 0.7f), 2 * k, 0, 1.2f * k);
					}
					icon(it.kind, { r0.x + 16 * k, y + rowH * 0.45f }, 9 * k, sel ? kParch : IM_COL32(200, 186, 160, 230));
					std::string nm = it.name;
					if (nm.size() > 30) nm = nm.substr(0, 28) + "..";
					if (it.count > 1) nm += "  (" + std::to_string(it.count) + ")";
					Txt(dl, g_body, 18 * k, { r0.x + 36 * k, y + 5 * k }, owned ? IM_COL32(230, 120, 100, 235) : sel ? kParch : IM_COL32(214, 204, 182, 235), nm);
					if (it.kind != 1) {
						const std::string val = std::to_string(it.value);
						Txt(dl, g_num, 15 * k, { r1.x - 96 * k, y + 7 * k }, IM_COL32(220, 180, 90, 220), val);
						char wt[16];
						std::snprintf(wt, sizeof(wt), "%.1f", it.weight);
						Txt(dl, g_num, 15 * k, { r1.x - 46 * k, y + 7 * k }, IM_COL32(170, 162, 145, 210), wt);
					}
					y += rowH;
				}
				if (rows > shown) {  // more below / above
					TxtC(dl, g_num, 13 * k, b.x - 26 * k, a.y + 52 * k, kGoldDim, std::to_string(s.lootSel + 1) + "/" + std::to_string(rows));
				}
				// the keys
				const float fy = b.y - 34 * k;
				auto cap = [&](float x, const std::string& key, const std::string& what) {
					if (g_padLast && key.size() == 1) {  // a controller in hand: its button
						PadGlyph(dl, { x + 12 * k, fy + 12 * k }, 12 * k, key);
						Txt(dl, g_body, 15 * k, { x + 30 * k, fy + 3 * k }, IM_COL32(214, 204, 182, 230), what);
						return;
					}
					dl->AddRectFilled({ x, fy }, { x + 24 * k, fy + 24 * k }, IM_COL32(60, 30, 18, 230), 3 * k);
					dl->AddRect({ x, fy }, { x + 24 * k, fy + 24 * k }, kGold, 3 * k, 0, 1.2f * k);
					TxtC(dl, g_num, 15 * k, x + 12 * k, fy + 3 * k, kParch, key);
					Txt(dl, g_body, 15 * k, { x + 30 * k, fy + 3 * k }, IM_COL32(214, 204, 182, 230), what);
				};
				if (s.lootNote.empty() || owned) {
					cap(a.x + 16 * k, g_padLast ? "A" : "E", owned ? "Steal" : "Take");
					cap(a.x + 126 * k, g_padLast ? "X" : "R", owned ? "Steal all" : "Take all");
				} else if (s.lootNote == "Locked") {
					cap(a.x + 16 * k, g_padLast ? "A" : "E", "Pick the lock");
				}
			}

			// The quests' targets in the world (Skyrim's floating quest markers, which are turned off, in our style): a
			// small burning diamond over each, its distance under it, the quest's name when you look right at it
			if (!s.perkMenu) {
				const ImU32 col = IM_COL32(236, 168, 72, 255), dark = IM_COL32(20, 12, 8, 230);
				const float pulse = 0.75f + 0.25f * std::sin(now * 3.0f);
				for (const auto& q : s.qmarks) {
					const ImVec2 p{ q.x * size.x, q.y * size.y };
					if (!q.front || p.x < 40 * k || p.x > size.x - 40 * k || p.y < 60 * k || p.y > size.y - 40 * k) {
						continue;
					}
					const float r = std::clamp(15.0f * 2500.0f / std::max(800.0f, q.dist), 9.0f, 15.0f) * k;
					for (int i = 3; i >= 1; --i) {
						const float g = r * (1.0f + 0.45f * i);
						dl->AddQuadFilled({ p.x, p.y - g }, { p.x + g * 0.7f, p.y }, { p.x, p.y + g }, { p.x - g * 0.7f, p.y }, Fade(col, 0.07f * pulse));
					}
					dl->AddQuadFilled({ p.x, p.y - r }, { p.x + r * 0.7f, p.y }, { p.x, p.y + r }, { p.x - r * 0.7f, p.y }, dark);
					dl->AddQuad({ p.x, p.y - r }, { p.x + r * 0.7f, p.y }, { p.x, p.y + r }, { p.x - r * 0.7f, p.y }, col, 2.0f * k);
					const float c = r * 0.42f;
					dl->AddQuadFilled({ p.x, p.y - c }, { p.x + c * 0.7f, p.y }, { p.x, p.y + c }, { p.x - c * 0.7f, p.y }, Fade(IM_COL32(255, 210, 120, 255), pulse));
					TxtC(dl, g_num, 14 * k, p.x, p.y + r + 6 * k, IM_COL32(220, 196, 150, 210), std::to_string(static_cast<int>(q.dist * 0.01428f)) + " m");
					if (std::abs(q.x - 0.5f) < 0.10f && std::abs(q.y - 0.5f) < 0.12f && !q.label.empty()) {
						TxtC(dl, g_title, 17 * k, p.x, p.y - r - 30 * k, IM_COL32(236, 222, 190, 235), q.label, 1.5f * k, col);
					}
				}
			}

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

			// Co-op: down (a bleed-out ring and the seconds left), dead (whom you watch), a friend to revive, friends down
			if (Net::InLobby()) {
				const auto pv = Party::Now();
				// where each friend is: a marker over them (their name, the distance), or at the screen's edge
				for (const auto& m : pv.marks) {
					const ImU32 col = m.life == Party::Life::Down ? IM_COL32(240, 70, 60, 255) : m.life == Party::Life::Dead ? IM_COL32(150, 150, 150, 220) : IM_COL32(110, 220, 255, 255);
					ImVec2      p{ m.x * size.x, m.y * size.y };
					const bool  on = m.front && p.x > 40 * k && p.x < size.x - 40 * k && p.y > 60 * k && p.y < size.y - 60 * k;
					if (!on) {  // off screen or behind: pinned to the edge, pointing the way
						ImVec2 d{ p.x - size.x / 2, p.y - size.y / 2 };
						if (!m.front) d = { -d.x, -d.y };
						const float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
						d = { d.x / len, d.y / len };
						const float sx = (size.x / 2 - 50 * k) / std::max(0.001f, std::abs(d.x)), sy = (size.y / 2 - 70 * k) / std::max(0.001f, std::abs(d.y));
						const float t = std::min(sx, sy);
						p = { size.x / 2 + d.x * t, size.y / 2 + d.y * t };
					}
					// a player icon: a round badge with a figure in it, a soft pulse round it, the name and distance under it
					const float r = 17 * k, pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
					const ImU32 colA = (col & 0x00FFFFFF) | (static_cast<ImU32>(70 + 60 * pulse) << 24);
					dl->AddCircleFilled(p, r + 7 * k * pulse, (col & 0x00FFFFFF) | (static_cast<ImU32>(40 * (1 - pulse)) << 24), 32);
					dl->AddCircleFilled(p, r, IM_COL32(12, 12, 18, 225), 32);
					dl->AddCircle(p, r, col, 32, 2.5f * k);
					dl->AddCircle(p, r + 3 * k, colA, 32, 1.5f * k);
					dl->AddCircleFilled({ p.x, p.y - r * 0.32f }, r * 0.26f, col, 16);  // head
					dl->PathArcTo({ p.x, p.y + r * 0.62f }, r * 0.52f, 3.1416f, 6.2832f, 16);  // shoulders
					dl->PathFillConvex(col);
					const std::string label = m.name + "   " + std::to_string(static_cast<int>(m.dist / 70.0f)) + " m";
					TxtC(dl, g_title, 18 * k, p.x, p.y + r + 6 * k, col, label);
				}
				if (pv.life == Party::Life::Down) {
					dl->AddRectFilled({ 0, 0 }, size, IM_COL32(60, 0, 0, 90));
					const ImVec2 c{ size.x / 2, size.y * 0.42f };
					const float  f = std::clamp(pv.left / 30.0f, 0.0f, 1.0f);
					dl->AddCircle(c, 46 * k, IM_COL32(40, 10, 10, 200), 64, 8 * k);
					dl->PathArcTo(c, 46 * k, -1.5708f, -1.5708f + 6.2832f * f, 64);
					dl->PathStroke(IM_COL32(230, 50, 40, 255), 0, 8 * k);
					TxtC(dl, g_title, 30 * k, c.x, c.y - 17 * k, IM_COL32(240, 220, 200, 255), std::to_string(static_cast<int>(std::ceil(pv.left))));
					TxtC(dl, g_title, 40 * k, c.x, c.y + 64 * k, IM_COL32(230, 60, 50, 255), "YOU ARE DOWN", 3 * k, IM_COL32(120, 0, 0, 255));
					TxtC(dl, g_body, 22 * k, c.x, c.y + 114 * k, IM_COL32(220, 210, 196, 230), "A friend can revive you: they stand by you and hold E (A on a controller)");
				} else if (pv.life == Party::Life::Dead) {
					TxtC(dl, g_title, 34 * k, size.x / 2, size.y * 0.10f, IM_COL32(230, 60, 50, 255), "YOU DIED", 3 * k, IM_COL32(120, 0, 0, 255));
					TxtC(dl, g_body, 24 * k, size.x / 2, size.y * 0.10f + 46 * k, IM_COL32(230, 220, 205, 240),
						pv.watching.empty() ? std::string("No one left standing...") : "Watching " + pv.watching + "   \xE2\x97\x80 \xE2\x96\xB6 to switch");
					TxtC(dl, g_body, 20 * k, size.x / 2, size.y * 0.10f + 80 * k, IM_COL32(190, 180, 165, 220), "You rise again when a trial is complete, or at dawn");
				}
				// the party, left side under the night: each friend's badge, name, health and state
				{
					float py = (s.round > 0 ? (s.nightActive && s.nightEvent >= 0 ? 200.0f : 172.0f) : 60.0f) * k;  // (under the night's plaque)
					for (const auto& m : pv.marks) {
						const bool  down = m.life == Party::Life::Down, dead = m.life == Party::Life::Dead;
						const ImU32 col = down ? IM_COL32(240, 70, 60, 255) : dead ? IM_COL32(140, 140, 140, 230) : IM_COL32(110, 220, 255, 255);
						const ImVec2 a{ 34 * k, py }, b{ 300 * k, py + 44 * k };
						Panel(dl, a, b, k * 0.6f, 0.85f);
						const ImVec2 bc{ a.x + 22 * k, py + 22 * k };
						const float  br = 14 * k;
						dl->AddCircleFilled(bc, br, IM_COL32(12, 12, 18, 235), 28);
						dl->AddCircle(bc, br, col, 28, 2.0f * k);
						dl->AddCircleFilled({ bc.x, bc.y - br * 0.32f }, br * 0.27f, col, 14);
						dl->PathArcTo({ bc.x, bc.y + br * 0.64f }, br * 0.52f, 3.1416f, 6.2832f, 14);
						dl->PathFillConvex(col);
						Txt(dl, g_title, 17 * k, { a.x + 44 * k, py + 4 * k }, kParch, m.name);
						const std::string st = down ? "DOWN" : dead ? "FALLEN" : std::to_string(static_cast<int>(m.dist / 70.0f)) + " m";
						Txt(dl, g_num, 13 * k, { b.x - 12 * k - Measure(g_num, 13 * k, st).x, py + 6 * k }, col, st);
						const float bx0 = a.x + 44 * k, bx1 = b.x - 12 * k, by = py + 28 * k;
						dl->AddRectFilled({ bx0, by }, { bx1, by + 6 * k }, IM_COL32(30, 10, 10, 230), 1 * k);
						const float f = dead ? 0.0f : down ? 0.08f : std::clamp(m.hp, 0.0f, 1.0f);
						dl->AddRectFilledMultiColor({ bx0, by }, { bx0 + (bx1 - bx0) * f, by + 6 * k }, IM_COL32(220, 42, 36, 255), IM_COL32(220, 42, 36, 255), IM_COL32(110, 8, 10, 255), IM_COL32(110, 8, 10, 255));
						dl->AddRect({ bx0 - 1 * k, by - 1 * k }, { bx1 + 1 * k, by + 7 * k }, Fade(kGoldDim, 0.8f), 1 * k, 0, 1.0f * k);
						py += 52 * k;
					}
				}
				if (pv.life == Party::Life::Alive) {
					// a friend down: a blood-red alert under the compass, the seconds they have left draining, where they are
					float y = size.y * 0.14f;
					for (const auto& f : pv.fallen) {
						const float pulse = 0.5f + 0.5f * std::sin(now * 6.0f);
						const std::string head = Upper(f.name) + " IS DOWN";
						const float w = std::max(360 * k, Measure(g_title, 30 * k, head).x + 120 * k);
						const ImVec2 a{ size.x / 2 - w / 2, y }, b{ size.x / 2 + w / 2, y + 76 * k };
						dl->AddRectFilled({ a.x - 6 * k, a.y - 6 * k }, { b.x + 6 * k, b.y + 6 * k }, IM_COL32(200, 20, 20, static_cast<int>(40 + 60 * pulse)), 10 * k);
						Panel(dl, a, b, k);
						dl->AddRectFilled(a, { a.x + 5 * k, b.y }, IM_COL32(230, 50, 40, 255));
						// a skull
						const ImVec2 sc{ a.x + 38 * k, y + 38 * k };
						dl->AddCircleFilled({ sc.x, sc.y - 4 * k }, 15 * k, IM_COL32(236, 220, 200, 255), 24);
						dl->AddRectFilled({ sc.x - 8 * k, sc.y + 6 * k }, { sc.x + 8 * k, sc.y + 15 * k }, IM_COL32(236, 220, 200, 255), 2 * k);
						dl->AddCircleFilled({ sc.x - 5.5f * k, sc.y - 4 * k }, 4 * k, IM_COL32(120, 10, 10, 255), 12);
						dl->AddCircleFilled({ sc.x + 5.5f * k, sc.y - 4 * k }, 4 * k, IM_COL32(120, 10, 10, 255), 12);
						TxtC(dl, g_title, 30 * k, size.x / 2 + 20 * k, y + 8 * k, IM_COL32(245, 90, 70, 255), head, 2 * k, IM_COL32(100, 0, 0, 255));
						const float fl = std::clamp(f.left / 30.0f, 0.0f, 1.0f);
						const float bx0 = a.x + 70 * k, bx1 = b.x - 20 * k, by = y + 50 * k;
						dl->AddRectFilled({ bx0, by }, { bx1, by + 6 * k }, IM_COL32(40, 10, 10, 230));
						dl->AddRectFilled({ bx0, by }, { bx0 + (bx1 - bx0) * fl, by + 6 * k }, IM_COL32(230, 60, 40, 255));
						const std::string sub = std::to_string(static_cast<int>(std::ceil(f.left))) + "s to save them   -   " + std::to_string(static_cast<int>(f.dist / 70.0f)) + " m";
						TxtC(dl, g_body, 15 * k, size.x / 2 + 20 * k, by + 8 * k, IM_COL32(230, 214, 196, 230), sub);
						y += 92 * k;
					}
					if (pv.reviveFrac >= 0 && !pv.reviveName.empty()) {
						const ImVec2 c{ size.x / 2, size.y * 0.62f };
						dl->AddCircle(c, 30 * k, IM_COL32(20, 30, 26, 200), 48, 6 * k);
						if (pv.reviveFrac > 0) {
							dl->PathArcTo(c, 30 * k, -1.5708f, -1.5708f + 6.2832f * pv.reviveFrac, 48);
							dl->PathStroke(IM_COL32(110, 255, 190, 255), 0, 6 * k);
						}
						const std::string tail = "to revive " + pv.reviveName;
						const float       tw = Measure(g_title, 24 * k, tail).x, gw = 34 * k;
						const float       x0 = c.x - (tw + gw + 60 * k) / 2;
						TxtC(dl, g_title, 24 * k, x0 + 22 * k, c.y + 42 * k, IM_COL32(230, 240, 230, 255), "Hold");
						if (g_padLast) {
							PadGlyph(dl, { x0 + 52 * k + gw / 2, c.y + 56 * k }, 14 * k, "A");
						} else {
							dl->AddRect({ x0 + 52 * k, c.y + 42 * k }, { x0 + 52 * k + gw - 6 * k, c.y + 70 * k }, kGold, 3 * k, 0, 1.4f * k);
							TxtC(dl, g_num, 18 * k, x0 + 52 * k + (gw - 6 * k) / 2, c.y + 45 * k, kParch, "E");
						}
						Txt(dl, g_title, 24 * k, { x0 + 60 * k + gw, c.y + 42 * k }, IM_COL32(230, 240, 230, 255), tail);
					}
				}
			}

			// (Noah: night atmosphere) the sky reddens toward the night's last wave - a blood haze gathering at the top of
			// the screen, the moon's colour - and the lightning's flash at a wave's start
			if (s.nightActive && s.nightProgress > 0) {
				const float pr = s.nightProgress, pulse = 0.9f + 0.1f * std::sin(now * 0.6f);
				const ImU32 top = IM_COL32(150, 10, 18, static_cast<int>(70 * pr * pulse)), z = IM_COL32(150, 10, 18, 0);
				dl->AddRectFilledMultiColor({ 0, 0 }, { size.x, size.y * 0.42f }, top, top, z, z);
			}
			if (s.eggDark) {  // a hidden quest's dark: the world shrinks to a ring of sight
				const ImU32 e = IM_COL32(0, 0, 0, 225), z = IM_COL32(0, 0, 0, 0);
				const float w = size.y * 0.42f;
				dl->AddRectFilledMultiColor({ 0, 0 }, { size.x, w }, e, e, z, z);
				dl->AddRectFilledMultiColor({ 0, size.y - w }, size, z, z, e, e);
				dl->AddRectFilledMultiColor({ 0, 0 }, { w, size.y }, e, z, z, e);
				dl->AddRectFilledMultiColor({ size.x - w, 0 }, size, z, e, e, z);
			}
			if (s.lightningT > 0) {
				const float t = 0.45f - s.lightningT;  // (a double flicker: a bright stroke, a gap, a weaker one)
				const float f = t < 0.06f ? 1.0f : t < 0.12f ? 0.2f : t < 0.2f ? 0.7f : std::max(0.0f, 0.7f * (0.45f - t) / 0.25f);
				const float soft = Settings::ReduceFlashes() ? 0.25f : 1.0f;
				dl->AddRectFilled({ 0, 0 }, size, IM_COL32(200, 220, 255, static_cast<int>(110 * f * s.lightningI * soft)));
			}

			// Event nights colour the screen: a red haze under the Blood Moon, grey murk on a Fog Night, ash, iron.
			// (Fog Night has no screen tint: its fog is real ground mist in the world, Director::TickMist.)
			if (s.nightActive && s.nightEvent >= 0 && s.nightEvent < static_cast<int>(gen::kEvents.size())) {
				const auto  t = gen::kEvents[s.nightEvent].tint;
				const int   r = (t >> 24) & 0xFF, g = (t >> 16) & 0xFF, b = (t >> 8) & 0xFF, a = t & 0xFF;
				const bool  fog = s.nightEvent == gen::kEvent_bloodfog;
				const float pulse = 0.85f + 0.15f * std::sin(now * 0.8f);
				// (Noah: the blood fog was overwhelming) no wash over the whole screen - only a light gradient in from the borders
				const ImU32 e = IM_COL32(r, g, b, std::min(255, static_cast<int>(a * (fog ? 1.6f : 1.3f) * pulse))), z = IM_COL32(r, g, b, 0);
				const float w = size.y * (fog ? 0.26f : 0.22f);
				dl->AddRectFilledMultiColor({ 0, 0 }, { size.x, w }, e, e, z, z);
				dl->AddRectFilledMultiColor({ 0, size.y - w }, size, z, z, e, e);
				dl->AddRectFilledMultiColor({ 0, 0 }, { w, size.y }, e, z, z, e);
				dl->AddRectFilledMultiColor({ size.x - w, 0 }, size, z, e, e, z);
			}
			if (s.nukeFlash > 0) {
				dl->AddRectFilled({ 0, 0 }, size, IM_COL32(255, 250, 235, static_cast<int>(230 * std::min(1.0f, s.nukeFlash))));
			}

			// A freed shrine's wave: golden rings rolling out from it across the screen, a soft gold wash as it passes.
			if (s.pulseOn && s.pulseT > 0) {
				const ImVec2 c{ s.pulseX * size.x, s.pulseY * size.y };
				const float  life = 3.5f, el = life - s.pulseT;
				(void)c;  // (Noah: no gold rings any more - the wash, the sound and the music stay)
				const float wash = std::clamp(1.0f - el / 1.2f, 0.0f, 1.0f);
				dl->AddRectFilled({ 0, 0 }, size, IM_COL32(255, 220, 150, static_cast<int>(70 * wash)));
			}

			// (Noah: a wave counter) the rune dial under the compass: one rune per wave round a ring (the cleared ones lit,
			// the one being fought pulsing), the wave's number in the middle, "WAVE 4 / 9" and the dead still to fall beside
			// it, a themed wave's name over it. It flares as a wave begins; as one is cleared its ring cracks and fades.
			{
				static int   lastWave = 0;
				static float startT = 0, clearT = 0;
				static bool  wasRest = false;
				const float  dtw = ImGui::GetIO().DeltaTime;
				const bool   on = s.nightActive && s.waves > 0 && s.sanctuary.empty();  // (a sanctuary: nothing rises here)
				if (on && s.wave != lastWave) startT = 1.0f, lastWave = s.wave;
				if (on && s.waveRest > 0 && !wasRest) clearT = 1.0f;
				wasRest = on && s.waveRest > 0;
				if (!on) lastWave = 0;
				startT = std::max(0.0f, startT - dtw * 1.2f);
				clearT = std::max(0.0f, clearT - dtw * 0.9f);
				if (on) {
					const ImVec2 c{ size.x * 0.5f - 92 * k, (s.questTitle.empty() || s.perkMenu ? 128 : 192) * k };  // (below a trial's line)
					const float  R = 26 * k, flare = startT * startT;
					Panel(dl, { c.x - R - 14 * k, c.y - R - 10 * k }, { c.x + R + 196 * k, c.y + R + 10 * k }, k * 0.6f, 0.85f);
					dl->AddCircleFilled(c, R + 4 * k, kNavy, 40);
					const int   n = std::clamp(s.waves, 1, 40);
					const float crack = clearT;
					for (int i = 0; i < n; ++i) {
						const float a0 = -1.5708f + 6.2832f * i / n + 0.05f, a1 = -1.5708f + 6.2832f * (i + 1) / n - 0.05f;
						const bool  done = i + 1 < s.wave || (i + 1 == s.wave && s.waveRest > 0);
						const bool  cur = i + 1 == s.wave && s.waveRest <= 0;
						float       alpha = done ? 0.95f : cur ? 0.55f + 0.45f * std::sin(now * 5.0f) : 0.22f;
						if (i + 1 == s.wave && crack > 0) alpha *= 0.4f + 0.6f * (1.0f - crack);  // (cleared: the ring cracks)
						const float rr = R + (i + 1 == s.wave ? (crack * 5 + flare * 6) * k : 0.0f);
						dl->PathArcTo(c, rr, a0, a1, 6);
						dl->PathStroke(Fade(cur || done ? kRune : kIce, alpha), 0, (cur ? 4.0f : 3.0f) * k);
					}
					if (flare > 0) {
						dl->AddCircle(c, R + (10 + 30 * (1 - startT)) * k, IM_COL32(230, 228, 220, static_cast<int>(200 * flare)), 48, 3.0f * k);
					}
					TxtC(dl, g_num, 26 * k, c.x, c.y - 15 * k, kIce, std::to_string(s.wave), 2 * k, kRune);
					const float tx = c.x + R + 16 * k;
					const bool  themed = !s.waveTheme.empty();
					if (themed) {
						Txt(dl, g_title, 15 * k, { tx, c.y - R - 2 * k }, s.waveThemeColor ? s.waveThemeColor : IM_COL32(255, 90, 60, 255), s.waveTheme, 1.5f * k, IM_COL32(0, 0, 0, 255));
					}
					Txt(dl, g_title, 20 * k, { tx, c.y - (themed ? 8 : 16) * k }, kIce, fmt::format("WAVE {} / {}", s.wave, s.waves), 1.5f * k, kRune);
					const std::string sub = s.waveRest > 0 ? fmt::format("NEXT WAVE IN {:.0f}", std::ceil(s.waveRest)) : fmt::format("{} LEFT", std::max(0, s.waveLeft));
					Txt(dl, g_body, 15 * k, { tx, c.y + (themed ? 14 : 8) * k }, Fade(kRune, 0.95f), sub);
				}
			}

			// Night counter, top-left: the nights cut into a dark plaque in blood - each mark a brush stroke, thick in the
			// middle and tapering at the ends, wet (a sheen runs along it, a drop gathers and falls from its foot), groups of
			// five struck through. The newest is cut in with a flare when its night begins. From night 11, the numeral.
			if (s.round > 0) {
				const ImVec2 p{ 40 * k, 20 * k };
				const int    shown = s.round <= 10 ? s.round : 0;
				const float  h = 64 * k, step = 17 * k, gap = 24 * k, top = p.y + 34 * k;
				const int    groups = (shown + 4) / 5;
				const float  plateW = shown > 0 ? std::max(150 * k, 16 * k + (shown - groups) * step + groups * gap + 18 * k) : 150 * k;
				Panel(dl, { p.x - 10 * k, p.y - 6 * k }, { p.x + plateW, top + h + (s.nightActive && s.nightEvent >= 0 ? 44 : 16) * k }, k * 0.7f, 0.8f);  // (the wave itself: the rune dial under the compass)
				Txt(dl, g_title, 19 * k, { p.x + 4 * k, p.y + 2 * k }, Fade(kGold, 0.9f), s.nightActive ? "NIGHT" : "NIGHTS SURVIVED");
				dl->AddLine({ p.x + 4 * k, p.y + 27 * k }, { p.x + plateW - 14 * k, p.y + 27 * k }, Fade(kGoldDim, 0.7f), 1.0f * k);
				const float draw = std::clamp(1.0f - (roundFlash - 1.5f), 0.0f, 1.0f);  // the newest is cut in
				auto        jit = [](int i, int j) { return static_cast<float>(((i * 73 + j * 151) % 17) - 8) / 8.0f; };
				// one stroke from a to b (t: how much of it is cut so far)
				const auto stroke = [&](ImVec2 a, ImVec2 b, float w, float t, int i, bool fresh) {
					const ImVec2 e{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
					const float  dx = e.x - a.x, dy = e.y - a.y, len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
					const ImVec2 nrm{ -dy / len, dx / len };
					constexpr int N = 10;
					ImVec2        pts[2 * N + 2];
					const auto shape = [&](float wmul, ImU32 col) {
						for (int q = 0; q <= N; ++q) {
							const float u = static_cast<float>(q) / N, ww = w * wmul * (0.18f + 0.82f * std::pow(std::sin(3.1416f * u), 0.55f));
							const ImVec2 c{ a.x + dx * u, a.y + dy * u };
							pts[q] = { c.x + nrm.x * ww, c.y + nrm.y * ww };
							pts[2 * N + 1 - q] = { c.x - nrm.x * ww, c.y - nrm.y * ww };
						}
						float area = 0;
						for (int q = 0; q < 2 * N + 2; ++q) {
							const ImVec2& p0 = pts[q];
							const ImVec2& p1 = pts[(q + 1) % (2 * N + 2)];
							area += p0.x * p1.y - p1.x * p0.y;
						}
						if (area < 0) {
							std::reverse(pts, pts + 2 * N + 2);  // (ImGui fills clockwise shapes: the other way round they draw hollow)
						}
						dl->AddConvexPolyFilled(pts, 2 * N + 2, col);
					};
					const float glow = 0.55f + 0.45f * std::sin(now * 1.7f + i * 0.9f);
					shape(1.9f, IM_COL32(150, 0, 0, static_cast<int>((fresh ? 110 : 40) * glow)));   // a dull glow round it
					shape(1.0f, fresh ? IM_COL32(255, 70, 40, 255) : IM_COL32(150, 12, 14, 255));     // the blood
					shape(0.45f, fresh ? IM_COL32(255, 170, 120, 255) : IM_COL32(200, 40, 34, 255));   // its wet core
					// the sheen sliding along it
					const float sp = std::fmod(now * 0.35f + i * 0.37f, 1.0f);
					if (t >= 1.0f) {
						const ImVec2 sc{ a.x + dx * sp, a.y + dy * sp };
						dl->AddCircleFilled(sc, w * 0.35f, IM_COL32(255, 210, 190, static_cast<int>(90 * std::sin(3.1416f * sp))), 10);
					}
					// a drop gathering at its foot and falling
					if (t >= 1.0f && dy > 0) {
						const float cyc = std::fmod(now * 0.28f + i * 0.61f, 1.0f), fall = cyc < 0.7f ? 0.0f : (cyc - 0.7f) / 0.3f;
						const float grow = std::min(1.0f, cyc / 0.7f);
						const ImVec2 foot{ e.x, e.y };
						dl->AddLine(foot, { foot.x, foot.y + 6 * k * grow }, IM_COL32(150, 12, 14, 255), w * 0.5f);
						dl->AddCircleFilled({ foot.x, foot.y + 6 * k * grow + 30 * k * fall * fall }, w * 0.42f * (0.6f + 0.4f * grow), IM_COL32(170, 16, 16, static_cast<int>(255 * (1.0f - fall))), 10);
					}
				};
				float x = p.x + 12 * k;
				for (int i = 0; i < shown; ++i) {
					const int   inGroup = i % 5;
					const bool  newest = i == s.round - 1;
					const float t = newest ? draw : 1.0f;
					const bool  fresh = newest && roundFlash > 0;
					if (inGroup < 4) {
						stroke({ x + jit(i, 1) * 2 * k, top + jit(i, 2) * 3 * k }, { x + jit(i, 3) * 3 * k, top + h + jit(i, 4) * 3 * k }, 4.2f * k, t, i, fresh);
						x += step;
					} else {  // the fifth strikes through the four
						stroke({ x - 4 * step - 6 * k, top + h * 0.80f }, { x + 2 * k, top + h * 0.16f }, 4.6f * k, t, i, fresh);
						x += gap;
					}
				}
				if (s.round > 10) {  // from night 11: the count in blood-red numerals, breathing
					const float  br = 0.75f + 0.25f * std::sin(now * 1.6f);
					const ImU32  col = roundFlash > 0 ? IM_COL32(255, 60, 40, 255) : IM_COL32(170, 18, 18, 255);
					Txt(dl, g_title, 68 * k, { p.x + 6 * k, top - 8 * k }, Fade(col, std::max(0.35f, draw) * br), std::to_string(s.round), 4 * k * br, IM_COL32(255, 40, 20, 255));
				}
				if (s.nightActive && s.nightEvent >= 0 && s.nightEvent < static_cast<int>(gen::kEvents.size())) {
					const auto  t = gen::kEvents[s.nightEvent].tint;
					const ImU32 c = IM_COL32(std::min(255, static_cast<int>(((t >> 24) & 0xFF) * 1.6f) + 60), std::min(255, static_cast<int>(((t >> 16) & 0xFF) * 1.6f) + 60), std::min(255, static_cast<int>(((t >> 8) & 0xFF) * 1.6f) + 60), 255);
					Txt(dl, g_title, 22 * k, { p.x + 4 * k, top + h + 12 * k }, c, std::string(gen::kEvents[s.nightEvent].label), 2 * k, c);
				}
			}

			// Points: small and clean in the very bottom-left corner - a gold septim and the count, on a shadow that
			// fades out to the right (no frame)
			{
				static float shownPts = 0;
				shownPts += (s.points - shownPts) * std::min(1.0f, dt * 9.0f);
				if (std::abs(s.points - shownPts) < 1.0f) shownPts = static_cast<float>(s.points);
				const bool        rising = shownPts < s.points - 0.5f;
				const std::string num = std::to_string(static_cast<int>(std::lround(shownPts)));
				const float       numW = Measure(g_num, 26 * k, num).x;
				const ImVec2      coin{ 30 * k, size.y - 30 * k };
				const float       w = 62 * k + numW;
				dl->AddRectFilledMultiColor({ 0, size.y - 58 * k }, { w + 70 * k, size.y }, IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 140));
				// the coin: a rim, a face, a stamped dragon-diamond, a glint
				const float cr = 11 * k;
				if (rising) dl->AddCircleFilled(coin, cr * 1.9f, IM_COL32(255, 200, 80, 40), 32);
				dl->AddCircleFilled(coin, cr, IM_COL32(150, 108, 40, 255), 40);
				dl->AddCircleFilled({ coin.x - 0.8f * k, coin.y - 0.8f * k }, cr * 0.84f, IM_COL32(222, 178, 86, 255), 40);
				dl->AddCircle(coin, cr * 0.66f, IM_COL32(150, 108, 40, 200), 40, 1.2f * k);
				const ImVec2 dm[4] = { { coin.x, coin.y - cr * 0.42f }, { coin.x + cr * 0.32f, coin.y }, { coin.x, coin.y + cr * 0.42f }, { coin.x - cr * 0.32f, coin.y } };
				dl->AddConvexPolyFilled(dm, 4, IM_COL32(150, 108, 40, 230));
				dl->AddCircleFilled({ coin.x - cr * 0.45f, coin.y - cr * 0.45f }, 1.6f * k, IM_COL32(255, 248, 220, 220), 8);
				Txt(dl, g_num, 26 * k, { coin.x + cr + 9 * k, coin.y - 15 * k }, rising ? IM_COL32(255, 228, 150, 255) : kParch, num, rising ? 2 * k : 1.2f * k, rising ? IM_COL32(255, 200, 80, 255) : IM_COL32(0, 0, 0, 220));
				Txt(dl, g_body, 11 * k, { coin.x + cr + 10 * k, coin.y - 27 * k }, Fade(kCoin, 0.85f), "SEPTIMS", 1 * k, IM_COL32(0, 0, 0, 200));
				if (s.crew >= 0 && s.crew < static_cast<int>(gen::kCrew.size())) {
					const std::string c(gen::kCrew[s.crew].label);
					Txt(dl, g_body, 13 * k, { coin.x + cr + 22 * k + numW, coin.y - 8 * k }, IM_COL32(190, 190, 190, 200), c);
				}
				if (s.gainTime > 0) {
					const std::string g = (s.lastGain > 0 ? "+" : "") + std::to_string(s.lastGain);
					const float       rise = (1.0f - s.gainTime) * 22 * k;
					Txt(dl, g_num, 20 * k, { coin.x + cr + 9 * k, coin.y - 52 * k - rise }, Fade(s.lastGain < 0 ? IM_COL32(230, 60, 50, 255) : IM_COL32(240, 205, 90, 255), s.gainTime), g, 1.2f * k, IM_COL32(0, 0, 0, 200));
				}
				if (s.styleTime > 0 && !s.styleTag.empty()) {  // (Noah: points for style) its tag over the septims
					const float a = std::min(1.0f, s.styleTime / 0.4f), pop = 1.0f + 0.25f * std::max(0.0f, s.styleTime - 1.5f) / 0.3f;
					Txt(dl, g_title, 18 * k * pop, { coin.x + cr + 9 * k, coin.y - 84 * k }, Fade(kRune, a), s.styleTag, 2.0f * k, IM_COL32(20, 60, 140, 255));
				}
			}

			// Health / magicka / stamina: one forged plate, bottom centre - health the long blood bar in the middle (tenths
			// notched in, its number on it, a pale wound trailing each hit, a red pulse when it runs low), magicka on the
			// left and stamina on the right, all three draining from the left, each with its sigil; the perks sit above in sockets
			// and a new draught bursts in (its name rising over the row).
			if (s.hpMax > 0) {
				auto ease = [&](float& g, float v) { g = v > g ? v : std::max(v, g - dt * 0.35f); };
				ease(hpGhost, hpFrac);
				const float mpF = s.mpMax > 0 ? std::clamp(s.mp / s.mpMax, 0.0f, 1.0f) : 0, spF = s.spMax > 0 ? std::clamp(s.sp / s.spMax, 0.0f, 1.0f) : 0;
				ease(mpGhost, mpF), ease(spGhost, spF);
				const float cx = size.x * 0.5f, y = size.y - 64 * k, hw = 360 * k, sw = 220 * k, h = 16 * k, sh = 10 * k, gap = 30 * k;
				const float L = cx - hw / 2 - gap - sw - 26 * k, R = cx + hw / 2 + gap + sw + 26 * k;
				const float low = hpFrac < 0.3f ? (0.5f + 0.5f * std::sin(now * 7.0f)) * (1.0f - hpFrac / 0.3f) : 0.0f;
				// the plate: black iron with a bronze rim, points at both ends
				{
					const float t = y - 13 * k, b = y + h + 13 * k, m = (t + b) * 0.5f;
					const ImVec2 plate[6] = { { L - 18 * k, m }, { L, t }, { R, t }, { R + 18 * k, m }, { R, b }, { L, b } };
					dl->AddConvexPolyFilled(plate, 6, IM_COL32(10, 9, 11, 205));
					dl->AddPolyline(plate, 6, Fade(kGoldDim, 0.9f), ImDrawFlags_Closed, 1.4f * k);
					dl->AddLine({ L + 6 * k, t + 3 * k }, { R - 6 * k, t + 3 * k }, IM_COL32(255, 220, 160, 26), 1.0f * k);
				}
				// one bar: its trough, the trailing wound, the fill (lit top, deep bottom), a sheen, notches
				const auto bar = [&](float x0, float w, float top, float bh, float f, float ghost, ImU32 lit, ImU32 deep, bool fromRight, int notches) {
					const float x1 = x0 + w;
					dl->AddRectFilled({ x0 - 2 * k, top - 2 * k }, { x1 + 2 * k, top + bh + 2 * k }, IM_COL32(0, 0, 0, 200), 2 * k);
					dl->AddRectFilled({ x0, top }, { x1, top + bh }, IM_COL32(28, 12, 12, 230), 1 * k);
					const auto span = [&](float v) { return fromRight ? std::pair{ x1 - w * v, x1 } : std::pair{ x0, x0 + w * v }; };
					auto [g0, g1] = span(std::clamp(ghost, 0.0f, 1.0f));
					dl->AddRectFilled({ g0, top }, { g1, top + bh }, IM_COL32(236, 214, 178, 120));
					auto [f0, f1] = span(std::clamp(f, 0.0f, 1.0f));
					dl->AddRectFilledMultiColor({ f0, top }, { f1, top + bh }, lit, lit, deep, deep);
					dl->AddRectFilled({ f0, top + 1 * k }, { f1, top + bh * 0.38f }, IM_COL32(255, 255, 255, 34));
					for (int n = 1; n < notches; ++n) {
						const float nx = x0 + w * n / notches;
						dl->AddLine({ nx, top + 1 * k }, { nx, top + bh - 1 * k }, IM_COL32(0, 0, 0, 120), 1.0f * k);
					}
					dl->AddRect({ x0 - 2 * k, top - 2 * k }, { x1 + 2 * k, top + bh + 2 * k }, Fade(kGoldDim, 0.8f), 2 * k, 0, 1.0f * k);
				};
				// (Noah: all three drain from the right - full from the left end)
				bar(cx - hw / 2, hw, y, h, hpFrac, hpGhost, IM_COL32(220, 42, 36, 255), IM_COL32(110, 8, 10, 255), false, 10);  // 
				bar(cx - hw / 2 - gap - sw, sw, y + (h - sh) / 2, sh, mpF, mpGhost, IM_COL32(80, 150, 255, 255), IM_COL32(24, 52, 140, 255), false, 5);
				bar(cx + hw / 2 + gap, sw, y + (h - sh) / 2, sh, spF, spGhost, IM_COL32(110, 210, 90, 255), IM_COL32(30, 100, 30, 255), false, 5);
				if (low > 0) {  // running low: the health bar itself pulses red (Noah: just the bar - not a box round the plate)
					dl->AddRectFilled({ cx - hw / 2 - 2 * k, y - 2 * k }, { cx + hw / 2 + 2 * k, y + h + 2 * k }, IM_COL32(255, 40, 30, static_cast<int>(70 * low)), 2 * k);
					dl->AddRect({ cx - hw / 2 - 2 * k, y - 2 * k }, { cx + hw / 2 + 2 * k, y + h + 2 * k }, IM_COL32(255, 70, 50, static_cast<int>(220 * low)), 2 * k, 0, 1.6f * k);
				}
				TxtC(dl, g_num, 13 * k, cx, y + 0.5f * k, IM_COL32(255, 238, 220, 235), std::to_string(std::max(0, static_cast<int>(std::ceil(s.hp)))) + " / " + std::to_string(static_cast<int>(s.hpMax)));
				// the sigils at the outer ends: an arcane seal for magicka, a whirlwind for stamina
				{
					const ImVec2 mc{ cx - hw / 2 - gap - sw - 14 * k, y + h * 0.5f }, sc{ cx + hw / 2 + gap + sw + 14 * k, y + h * 0.5f };
					const float  r = 9 * k;
					for (auto [c, col] : { std::pair{ mc, IM_COL32(90, 160, 255, 255) }, std::pair{ sc, IM_COL32(120, 220, 100, 255) } }) {
						dl->AddCircleFilled(c, r, IM_COL32(14, 12, 14, 255), 20);
						dl->AddCircle(c, r, kGold, 20, 1.3f * k);
						if (c.x < cx) {  // magicka: an arcane seal - a ring, a slowly turning star of two triangles, a bright eye
							const float rr = r * 0.66f, rot = now * 0.35f;
							dl->AddCircleFilled(c, r * 0.8f, IM_COL32(40, 80, 200, static_cast<int>(60 + 30 * std::sin(now * 2.0f))), 20);
							dl->AddCircle(c, rr, col, 20, 1.1f * k);
							for (int t = 0; t < 2; ++t) {
								ImVec2 tri[3];
								for (int v = 0; v < 3; ++v) {
									const float an = rot + t * 3.14159f + v * 2.0944f - 1.5708f;
									tri[v] = { c.x + std::cos(an) * rr, c.y + std::sin(an) * rr };
								}
								dl->AddPolyline(tri, 3, IM_COL32(170, 210, 255, 255), ImDrawFlags_Closed, 1.1f * k);
							}
							for (int q = 0; q < 4; ++q) {  // four rune ticks on the rim
								const float an = -rot * 1.5f + q * 1.5708f;
								dl->AddLine({ c.x + std::cos(an) * r * 0.74f, c.y + std::sin(an) * r * 0.74f }, { c.x + std::cos(an) * r * 0.9f, c.y + std::sin(an) * r * 0.9f }, col, 1.2f * k);
							}
							dl->AddCircleFilled(c, r * 0.17f, IM_COL32(230, 245, 255, 255), 10);
						} else {  // stamina: a whirlwind - two arms spiralling in to the eye, turning, with a wisp flung off each
							const float spin = now * 1.4f;
							for (int arm = 0; arm < 2; ++arm) {
								const float a0 = spin + arm * 3.1416f;
								constexpr int kN = 28;
								for (int q = 0; q < kN; ++q) {
									const float t0 = static_cast<float>(q) / kN, t1 = static_cast<float>(q + 1) / kN;
									const float ra = r * (0.12f + 0.7f * t0), rb = r * (0.12f + 0.7f * t1);   // the radius opens out along the arm
									const float aa = a0 + t0 * 4.4f, ab = a0 + t1 * 4.4f;                     // ~1.4 turns
									const int   al = static_cast<int>(255 * (0.35f + 0.65f * t0));
									dl->AddLine({ c.x + std::cos(aa) * ra, c.y + std::sin(aa) * ra * 0.92f }, { c.x + std::cos(ab) * rb, c.y + std::sin(ab) * rb * 0.92f },
										(col & 0x00FFFFFF) | (static_cast<ImU32>(al) << 24), (0.9f + 1.1f * t0) * k);
								}
								const float wa = a0 + 4.4f + 0.35f;  // the wisp at the arm's end
								dl->AddCircleFilled({ c.x + std::cos(wa) * r * 0.86f, c.y + std::sin(wa) * r * 0.8f }, 1.1f * k, col, 8);
							}
							dl->AddCircleFilled(c, r * 0.12f, IM_COL32(220, 255, 210, 255), 10);  // the eye
						}
					}
				}
				// perk row: sockets, the draughts in them; a new draught bursts in
				static std::array<int, 16>   lastStacks{};
				static std::array<float, 16> burst{};
				static bool                  seeded = false;
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()) && i < 16; ++i) {
					if (seeded && s.perkStacks[i] > lastStacks[i]) {
						burst[i] = 1.4f;
					}
					lastStacks[i] = s.perkStacks[i];
					burst[i] = std::max(0.0f, burst[i] - dt);
				}
				seeded = true;
				int n = 0;
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					n += (s.perks >> i) & 1;
				}
				const float r = 19 * k, pg = 48 * k;
				float       x = cx - (n - 1) * pg * 0.5f;
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					if (!((s.perks >> i) & 1)) {
						continue;
					}
					const float py = y - 40 * k;
					const auto  c = gen::kPerkColor[i];
					const int   cr = (c >> 24) & 0xFF, cg = (c >> 16) & 0xFF, cb = (c >> 8) & 0xFF;
					const float bt = i < 16 ? burst[i] / 1.4f : 0.0f;                // 1 at the moment it's bought, down to 0
					const float pop = 1.0f + 0.9f * bt * bt;
					dl->AddCircleFilled({ x, py }, r + 5 * k, IM_COL32(8, 7, 9, 210), 40);       // the socket
					dl->AddCircle({ x, py }, r + 5 * k, Fade(kGoldDim, 0.9f), 40, 1.2f * k);
					if (bt > 0) {  // the burst: a ring flung outward, rays turning, a flash
						const float e = 1.0f - bt;
						dl->AddCircle({ x, py }, r * (1.2f + 3.2f * e), IM_COL32(cr, cg, cb, static_cast<int>(230 * bt)), 48, 3.0f * k * bt + 1.0f);
						dl->AddCircleFilled({ x, py }, r * 1.6f * pop, IM_COL32(std::min(255, cr + 80), std::min(255, cg + 80), std::min(255, cb + 80), static_cast<int>(90 * bt)), 40);
						for (int ray = 0; ray < 10; ++ray) {
							const float a = ray * 0.6283f + now * 1.5f, r0 = r * (1.3f + 1.4f * e), r1 = r0 + r * 0.9f * bt;
							dl->AddLine({ x + std::cos(a) * r0, py + std::sin(a) * r0 }, { x + std::cos(a) * r1, py + std::sin(a) * r1 }, IM_COL32(255, 236, 190, static_cast<int>(220 * bt)), 2.0f * k);
						}
						const std::string nm = Upper(std::string(gen::kPerks[i].label));
						TxtC(dl, g_title, 22 * k, x, py - r * 2.6f - 26 * k * e, IM_COL32(255, 230, 190, static_cast<int>(255 * std::min(1.0f, bt * 2.0f))), nm, 2 * k, IM_COL32(cr / 2, cg / 2, cb / 2, static_cast<int>(200 * bt)));
					}
					const float rr = r * pop;
					if (auto* icon = g_perkIcons[i]) {
						dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { x - rr, py - rr }, { x + rr, py + rr });
					} else {
						dl->AddCircleFilled({ x, py }, rr, Col(c));
					}
					{  // the trim: breathing softly in the brew's colour
						const float br = 0.6f + 0.4f * std::sin(now * 2.2f + i);
						dl->AddCircle({ x, py }, rr + 2.6f * k, IM_COL32(cr, cg, cb, static_cast<int>(60 * br)), 48, 5.0f * k);
						dl->AddCircle({ x, py }, rr + 1.2f * k, IM_COL32(std::min(255, cr + 60), std::min(255, cg + 60), std::min(255, cb + 60), static_cast<int>(200 * br)), 48, 1.6f * k);
					}
					if (s.perkStacks[i] > 1) {  // taken more than once: how many
						const std::string cnt = "x" + std::to_string(s.perkStacks[i]);
						const ImVec2 bc{ x + r * 0.82f, py + r * 0.66f };
						dl->AddCircleFilled(bc, 9 * k, IM_COL32(20, 14, 10, 235), 20);
						dl->AddCircle(bc, 9 * k, kGold, 20, 1.2f * k);
						TxtC(dl, g_num, 12 * k, bc.x, bc.y - 7 * k, kParch, cnt);
					}
					x += pg;
				}
			}

			// Spell dock, bottom-right, when the hands hold the Coffer's spells: the right hand's spell as a large card
			// (its element's emblem aflame, its name, element and magicka cost), the left hand's small above it.
			const bool spellHands = s.handSpell[0] >= 0 || s.handSpell[1] >= 0;
			if (spellHands) {
				static float shownS = 0;
				shownS += ((s.gunOut ? 1.0f : 0.6f) - shownS) * std::min(1.0f, dt * 8);
				const float al = shownS;
				// (Noah: cleaner and simpler) one slim card per hand: the element's emblem at the left, the spell's name,
				// and its real magicka cost as a pip - nothing else
				const auto card = [&](int idx, int cost, ImVec2 a, ImVec2 b, bool big, const char* hand) {
					const int           el = Arcana::ElementOf(idx);
					const std::uint32_t ec = Arcana::ElementColor(el);
					const ImU32         ecol = IM_COL32((ec >> 16) & 0xFF, (ec >> 8) & 0xFF, ec & 0xFF, 255);
					const float         h = b.y - a.y, rr = h * 0.5f;
					dl->AddRectFilled(a, b, IM_COL32(8, 8, 10, static_cast<int>((big ? 175 : 140) * al)), rr);
					dl->AddRect(a, b, Fade(ecol, (big ? 0.55f : 0.35f) * al), rr, 0, 1.2f * k);
					const float er = rr - 6 * k;
					DrawEmblem(dl, Arcana::kIdBase + idx, { a.x + rr, (a.y + b.y) * 0.5f }, er, al, now, big ? 0.9f : 0.6f);
					// the cost, right: a small rounded pip, red when you cannot pay it
					const bool        can = s.mp >= cost;
					const std::string cs = std::to_string(cost) + (Arcana::Concentration(idx) ? "/s" : "");
					const float       cz = (big ? 20 : 15) * k;
					const auto        cm = Measure(g_num, cz, cs);
					const ImVec2      p1{ b.x - 10 * k, (a.y + b.y) * 0.5f + cm.y * 0.5f + 3 * k }, p0{ p1.x - cm.x - 16 * k, p1.y - cm.y - 6 * k };
					dl->AddRectFilled(p0, p1, can ? IM_COL32(40, 70, 130, static_cast<int>(200 * al)) : IM_COL32(120, 30, 26, static_cast<int>(210 * al)), (p1.y - p0.y) * 0.5f);
					Txt(dl, g_num, cz, { p0.x + 8 * k, p0.y + 3 * k }, Fade(IM_COL32(236, 242, 255, 255), al), cs);
					// the name between, shrinking to fit; the hand above it, small
					const float x0 = a.x + rr * 2 + 6 * k, textR = p0.x - 10 * k;
					dl->PushClipRect(a, { textR, b.y }, true);
					float             nameSize = (big ? 22 : 16) * k;
					const std::string nm(Arcana::Name(idx));
					while (nameSize > 11 * k && x0 + Measure(g_title, nameSize, nm).x > textR) {
						nameSize -= 1 * k;
					}
					const bool legend = Arcana::TierOf(idx) >= 3;
					if (big) {
						std::string top = legend ? std::string(hand) + "  -  LEGENDARY" : std::string(hand);
						if (Arcana::NextRung(idx) >= 0) top += "  -  " + std::to_string(std::min(Classes::Mastery(idx), Classes::MasteryNeed(idx))) + "/" + std::to_string(Classes::MasteryNeed(idx)) + " to climb";
						Txt(dl, g_body, 12 * k, { x0, a.y + 7 * k }, legend ? Fade(IM_COL32(255, 200, 90, 255), 0.95f * al) : Fade(ecol, 0.9f * al), top);
						if (legend) TxtGold(dl, g_title, nameSize, { x0, a.y + 22 * k }, nm, al);
						else Txt(dl, g_title, nameSize, { x0, a.y + 22 * k }, Fade(kParch, al), nm);
					} else if (legend) {
						TxtGold(dl, g_title, nameSize, { x0, (a.y + b.y) * 0.5f - nameSize * 0.55f }, nm, 0.9f * al);
					} else {
						Txt(dl, g_title, nameSize, { x0, (a.y + b.y) * 0.5f - nameSize * 0.55f }, Fade(kParch, 0.9f * al), nm);
					}
					dl->PopClipRect();
				};
				// (Noah: both hands' cards the same size, and the shout's under them too - one column of equal cards)
				const ImVec2 a{ size.x - 400 * k, size.y - 158 * k }, b{ size.x - 40 * k, size.y - 106 * k };
				const int    mainHand = s.handSpell[1] >= 0 ? 1 : 0;
				card(s.handSpell[mainHand], s.handCost[mainHand], a, b, true, mainHand == 1 ? "RIGHT HAND" : "LEFT HAND");
				if (s.handSpell[1] >= 0 && s.handSpell[0] >= 0) {
					card(s.handSpell[0], s.handCost[0], { a.x, a.y - 60 * k }, { b.x, b.y - 60 * k }, true, "LEFT HAND");
				}
			}
			// The altar's bow in hand: its card in the dock (its emblem aflame with its reforging, its name), and while it
			// is drawn a thin ring round the crosshair filling in the element's colour, bright when the draw is full
			if (s.bowHeld >= 0 && !spellHands) {
				const int    id = Bows::kIdBase + s.bowHeld;
				const auto   bc = ArmsColor(id);
				const ImU32  ecol = IM_COL32((bc >> 16) & 0xFF, (bc >> 8) & 0xFF, bc & 0xFF, 255);
				const ImVec2 a{ size.x - 400 * k, size.y - 158 * k }, b{ size.x - 40 * k, size.y - 106 * k };
				const float  rr = (b.y - a.y) * 0.5f;
				dl->AddRectFilled(a, b, IM_COL32(8, 8, 10, 175), rr);
				dl->AddRect(a, b, Fade(ecol, 0.55f), rr, 0, 1.2f * k);
				DrawEmblem(dl, id, { a.x + rr, (a.y + b.y) * 0.5f }, rr - 6 * k, 1.0f, now, 0.9f);
				Txt(dl, g_body, 12 * k, { a.x + rr * 2 + 6 * k, a.y + 7 * k }, Fade(ecol, 0.9f), "BOW");
				Txt(dl, g_title, 22 * k, { a.x + rr * 2 + 6 * k, a.y + 22 * k }, kParch, ArmsName(id));
				const std::string tier(Bows::TierName(s.bowHeld, Bows::Level(s.bowHeld)));
				Txt(dl, g_body, 12 * k, { b.x - 18 * k - Measure(g_body, 12 * k, tier).x, a.y + 7 * k }, Fade(ecol, 0.9f), tier);
				constexpr bool kDrawRing = false;  // (Noah: only the white dot at the crosshair)
				if (kDrawRing && s.bowDraw > 0.02f) {
					const ImVec2 cc{ size.x * 0.5f, size.y * 0.5f };
					const float  R = 30 * k, a0 = -RE::NI_PI / 2, a1 = a0 + 2 * RE::NI_PI * std::clamp(s.bowDraw, 0.0f, 1.0f);
					const bool   full = s.bowDraw >= 0.98f;
					dl->AddCircle(cc, R, IM_COL32(20, 16, 12, 120), 96, 3 * k);
					dl->PathArcTo(cc, R, a0, a1, 96);
					dl->PathStroke(Fade(ecol, full ? 0.25f : 0.12f), 0, 9 * k);
					dl->PathArcTo(cc, R, a0, a1, 96);
					dl->PathStroke(full ? IM_COL32(255, 250, 235, 255) : Fade(ecol, 0.9f), 0, 2.2f * k);
				}
			}
			// Shout card, bottom-right under the spell / weapon dock (Noah: Nordic) - a carved iron plate cut at the corners
			// like a Nord shield-band, bronze-rimmed with knotwork studs; at its left a round iron shield bearing the three
			// claw-strokes of a Word Wall (the Thu'um), its bronze rim draining as the shout recovers; the shout's name in
			// the title face and the seconds left on a bronze rune-stone (READY when it can be spoken again)
			if (!s.shoutName.empty()) {
				static float shownSh = 0;
				shownSh += (1.0f - shownSh) * std::min(1.0f, dt * 6);
				const float  al = shownSh;
				const bool   cooling = s.shoutCd > 0.001f;
				const ImVec2 a{ size.x - 400 * k, size.y - 98 * k }, b{ size.x - 40 * k, size.y - 46 * k };  // (the spell cards' size)
				const float  h = b.y - a.y, cy = (a.y + b.y) * 0.5f, cut = 9 * k;
				const ImU32  bronze = Fade(kGold, al), bronzeDim = Fade(kGoldDim, al);
				const ImU32  frost = IM_COL32(170, 215, 255, 255);
				// the plate: cut corners, dark iron, a band of lighter iron along its top, a bronze rim with an inner line
				const ImVec2 plate[8] = { { a.x + cut, a.y }, { b.x - cut, a.y }, { b.x, a.y + cut }, { b.x, b.y - cut }, { b.x - cut, b.y }, { a.x + cut, b.y }, { a.x, b.y - cut }, { a.x, a.y + cut } };
				dl->AddConvexPolyFilled(plate, 8, IM_COL32(14, 13, 12, static_cast<int>(215 * al)));
				dl->AddRectFilledMultiColor({ a.x + cut, a.y + 1 * k }, { b.x - cut, cy }, IM_COL32(60, 54, 46, static_cast<int>(70 * al)), IM_COL32(60, 54, 46, static_cast<int>(70 * al)), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
				dl->AddPolyline(plate, 8, bronze, ImDrawFlags_Closed, 1.6f * k);
				{
					const float  in = 3.5f * k;
					const ImVec2 inner[8] = { { a.x + cut + in * 0.4f, a.y + in }, { b.x - cut - in * 0.4f, a.y + in }, { b.x - in, a.y + cut + in * 0.4f }, { b.x - in, b.y - cut - in * 0.4f },
						{ b.x - cut - in * 0.4f, b.y - in }, { a.x + cut + in * 0.4f, b.y - in }, { a.x + in, b.y - cut - in * 0.4f }, { a.x + in, a.y + cut + in * 0.4f } };
					dl->AddPolyline(inner, 8, bronzeDim, ImDrawFlags_Closed, 0.8f * k);
				}
				// knot-studs: a small bronze diamond at each cut corner and at the middle of the top and bottom edges
				auto stud = [&](ImVec2 c, float r) {
					const ImVec2 d[4] = { { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y } };
					dl->AddConvexPolyFilled(d, 4, bronze);
					dl->AddPolyline(d, 4, IM_COL32(40, 30, 18, static_cast<int>(200 * al)), ImDrawFlags_Closed, 0.8f * k);
				};
				stud({ b.x - cut * 0.5f, a.y + cut * 0.5f }, 2.6f * k);
				stud({ b.x - cut * 0.5f, b.y - cut * 0.5f }, 2.6f * k);
				// the shield
				const float  er = h * 0.5f + 3 * k;
				const ImVec2 c{ a.x + er - 6 * k, cy };
				dl->AddCircleFilled(c, er + 2 * k, IM_COL32(10, 9, 8, static_cast<int>(235 * al)), 40);
				dl->AddCircleFilled(c, er - 3 * k, IM_COL32(42, 38, 34, static_cast<int>(245 * al)), 40);
				for (int i = 0; i < 4; ++i) {  // its planks
					const float x = c.x - er * 0.6f + er * 0.4f * i;
					const float hh = std::sqrt(std::max(0.0f, (er - 3 * k) * (er - 3 * k) - (x - c.x) * (x - c.x)));
					dl->AddLine({ x, c.y - hh }, { x, c.y + hh }, IM_COL32(20, 18, 16, static_cast<int>(200 * al)), 1.0f * k);
				}
				dl->AddCircle(c, er - 3 * k, bronzeDim, 40, 2.4f * k);  // the rim, worn
				for (int i = 0; i < 8; ++i) {  // its rivets
					const float t = i * RE::NI_PI / 4 + RE::NI_PI / 8;
					dl->AddCircleFilled({ c.x + std::cos(t) * (er - 3 * k), c.y + std::sin(t) * (er - 3 * k) }, 1.5f * k, bronze, 8);
				}
				// the Word Wall's three claw-strokes: slanting, tapered, cut into the shield - frost-pale when it can be spoken
				{
					const ImU32 gl = cooling ? IM_COL32(120, 112, 100, static_cast<int>(230 * al)) : Fade(frost, al);
					for (int i = 0; i < 3; ++i) {
						const float  ox = (i - 1) * er * 0.36f;
						const ImVec2 top{ c.x + ox + er * 0.2f, c.y - er * 0.55f }, bot{ c.x + ox - er * 0.2f, c.y + er * 0.55f };
						const float  w = er * 0.11f;
						const ImVec2 q[4] = { top, { (top.x + bot.x) * 0.5f + w, (top.y + bot.y) * 0.5f }, bot, { (top.x + bot.x) * 0.5f - w, (top.y + bot.y) * 0.5f } };
						dl->AddConvexPolyFilled(q, 4, gl);
					}
					if (!cooling) {
						dl->AddCircle(c, er * 0.62f, Fade(frost, 0.18f * al * (0.6f + 0.4f * std::sin(now * 2.4f))), 32, 5.0f * k);
					}
				}
				if (cooling) {  // what is left of the cooldown: the rim's bronze, bright, from the top round
					const float a0 = -RE::NI_PI * 0.5f, a1 = a0 + 2 * RE::NI_PI * s.shoutCd;
					dl->PathArcTo(c, er - 3 * k, a0, a1, 48);
					dl->PathStroke(Fade(IM_COL32(255, 200, 120, 255), 0.35f * al), ImDrawFlags_None, 6.0f * k);
					dl->PathArcTo(c, er - 3 * k, a0, a1, 48);
					dl->PathStroke(Fade(IM_COL32(240, 200, 130, 255), al), ImDrawFlags_None, 2.4f * k);
				}
				if (s.shoutReady > 0.001f) {  // ready again: the shield flares frost
					dl->AddCircle(c, er + 2 * k + 6 * k * (1 - s.shoutReady), Fade(frost, s.shoutReady * al), 40, 2.5f * k);
					dl->AddPolyline(plate, 8, Fade(frost, 0.8f * s.shoutReady * al), ImDrawFlags_Closed, 2.2f * k);
				}
				// right: a bronze rune-stone with the seconds left, or READY
				const std::string rs = cooling ? std::to_string(static_cast<int>(std::ceil(s.shoutLeft))) + "s" : std::string("READY");
				const float       cz = 14 * k;
				const auto        cm = Measure(g_num, cz, rs);
				const ImVec2      p1{ b.x - 12 * k, cy + cm.y * 0.5f + 3 * k }, p0{ p1.x - cm.x - 14 * k, p1.y - cm.y - 6 * k };
				{
					const float  cc = 4 * k;
					const ImVec2 st[6] = { { p0.x + cc, p0.y }, { p1.x - cc, p0.y }, { p1.x, cy }, { p1.x - cc, p1.y }, { p0.x + cc, p1.y }, { p0.x, cy } };
					dl->AddConvexPolyFilled(st, 6, cooling ? IM_COL32(30, 27, 24, static_cast<int>(230 * al)) : IM_COL32(70, 52, 28, static_cast<int>(235 * al)));
					dl->AddPolyline(st, 6, cooling ? bronzeDim : bronze, ImDrawFlags_Closed, 1.0f * k);
				}
				Txt(dl, g_num, cz, { p0.x + 7 * k, p0.y + 3 * k }, cooling ? Fade(IM_COL32(220, 205, 180, 255), al) : Fade(IM_COL32(255, 236, 196, 255), al), rs);
				// the name, with THU'UM above it in small spaced capitals
				const float x0 = c.x + er + 8 * k, textR = p0.x - 8 * k;
				dl->PushClipRect(a, { textR, b.y }, true);
				float nameSize = 21 * k;
				std::string nm = s.shoutName;
				for (auto& ch : nm) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
				while (nameSize > 10 * k && x0 + Measure(g_title, nameSize, nm).x > textR) {
					nameSize -= 1 * k;
				}
				Txt(dl, g_body, 11 * k, { x0, a.y + 7 * k }, Fade(kGold, 0.8f * al), "T H U ' U M");
				Txt(dl, g_title, nameSize, { x0, a.y + 22 * k }, Fade(kParch, (cooling ? 0.65f : 1.0f) * al), nm, 1.5f * k, IM_COL32(0, 0, 0, static_cast<int>(160 * al)));
				dl->PopClipRect();
			}
			// (Noah: Skyrim's own and other mods' weapons and spells too) what is in your hands when it is not the altar's: a
			// slim card per hand in the same style - a drawn glyph of its kind, the hand, its name, and its damage (gold),
			// armour (grey) or magicka cost (blue)
			const bool skyHands = !spellHands && s.bowHeld < 0 && (s.skyKind[0] > 0 || s.skyKind[1] > 0);
			if (skyHands) {
				const auto card = [&](int i, ImVec2 a, ImVec2 b) {
					const std::uint32_t c = s.skyColor[i];
					const ImU32 ecol = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
					const float h = b.y - a.y, rr = h * 0.5f;
					dl->AddRectFilled(a, b, IM_COL32(8, 8, 10, 175), rr);
					dl->AddRect(a, b, Fade(ecol, 0.5f), rr, 0, 1.2f * k);
					const ImVec2 gc{ a.x + rr, (a.y + b.y) * 0.5f };
					const float  gr = rr - 8 * k;
					dl->AddCircleFilled(gc, gr + 3 * k, IM_COL32(20, 16, 12, 230), 32);
					dl->AddCircle(gc, gr + 3 * k, Fade(ecol, 0.8f), 32, 1.4f * k);
					switch (s.skyKind[i]) {
					case 1:  // a blade
						dl->AddLine({ gc.x - gr * 0.6f, gc.y + gr * 0.6f }, { gc.x + gr * 0.6f, gc.y - gr * 0.6f }, ecol, 3.0f * k);
						dl->AddLine({ gc.x - gr * 0.55f, gc.y + gr * 0.05f }, { gc.x - gr * 0.05f, gc.y + gr * 0.55f }, ecol, 2.4f * k);
						break;
					case 2:  // a bow
						dl->PathArcTo({ gc.x + gr * 0.5f, gc.y }, gr * 0.85f, 2.2f, 4.08f, 16);
						dl->PathStroke(ecol, 0, 2.6f * k);
						dl->AddLine({ gc.x - gr * 0.05f, gc.y - gr * 0.7f }, { gc.x - gr * 0.05f, gc.y + gr * 0.7f }, Fade(ecol, 0.7f), 1.2f * k);
						break;
					case 3:  // a spell: a four-pointed star
						for (int q = 0; q < 4; ++q) {
							const float an = q * 1.5708f;
							const ImVec2 d{ std::cos(an), std::sin(an) }, n{ -d.y, d.x };
							dl->AddTriangleFilled({ gc.x + n.x * gr * 0.18f, gc.y + n.y * gr * 0.18f }, { gc.x - n.x * gr * 0.18f, gc.y - n.y * gr * 0.18f }, { gc.x + d.x * gr * 0.8f, gc.y + d.y * gr * 0.8f }, ecol);
						}
						break;
					case 4:  // a shield
						dl->AddQuadFilled({ gc.x - gr * 0.55f, gc.y - gr * 0.6f }, { gc.x + gr * 0.55f, gc.y - gr * 0.6f }, { gc.x + gr * 0.45f, gc.y + gr * 0.1f }, { gc.x, gc.y + gr * 0.7f }, Fade(ecol, 0.85f));
						dl->AddTriangleFilled({ gc.x - gr * 0.55f, gc.y - gr * 0.6f }, { gc.x + gr * 0.45f, gc.y + gr * 0.1f }, { gc.x, gc.y + gr * 0.7f }, Fade(ecol, 0.85f));
						break;
					case 5:  // a staff
						dl->AddLine({ gc.x - gr * 0.5f, gc.y + gr * 0.7f }, { gc.x + gr * 0.35f, gc.y - gr * 0.45f }, ecol, 2.6f * k);
						dl->AddCircleFilled({ gc.x + gr * 0.42f, gc.y - gr * 0.52f }, gr * 0.22f, ecol, 12);
						break;
					default:  // a torch, or anything else held
						dl->AddCircleFilled(gc, gr * 0.35f, ecol, 16);
					}
					const bool  spell = s.skyKind[i] == 3;
					std::string vs = s.skyValue[i] > 0 ? std::to_string(s.skyValue[i]) : std::string();
					float       textR = b.x - 14 * k;
					if (!vs.empty()) {
						const float  cz = 18 * k;
						const auto   cm = Measure(g_num, cz, vs);
						const ImVec2 p1{ b.x - 10 * k, (a.y + b.y) * 0.5f + cm.y * 0.5f + 3 * k }, p0{ p1.x - cm.x - 16 * k, p1.y - cm.y - 6 * k };
						const ImU32  pc = spell ? (s.mp >= s.skyValue[i] ? IM_COL32(40, 70, 130, 200) : IM_COL32(120, 30, 26, 210)) : s.skyKind[i] == 4 ? IM_COL32(70, 70, 76, 210) : IM_COL32(120, 88, 36, 210);
						dl->AddRectFilled(p0, p1, pc, (p1.y - p0.y) * 0.5f);
						Txt(dl, g_num, cz, { p0.x + 8 * k, p0.y + 3 * k }, IM_COL32(240, 236, 226, 255), vs);
						textR = p0.x - 10 * k;
					}
					const float x0 = a.x + rr * 2 + 6 * k;
					dl->PushClipRect(a, { textR, b.y }, true);
					float nameSize = 22 * k;
					while (nameSize > 11 * k && x0 + Measure(g_title, nameSize, s.skyName[i]).x > textR) {
						nameSize -= 1 * k;
					}
					const bool both = i == 1 && s.skyKind[0] == 0 && (s.skyKind[1] == 2 || s.skyKind[1] == 5);
					Txt(dl, g_body, 12 * k, { x0, a.y + 7 * k }, Fade(ecol, 0.9f), both ? "BOTH HANDS" : i == 1 ? "RIGHT HAND" : "LEFT HAND");
					Txt(dl, g_title, nameSize, { x0, a.y + 22 * k }, kParch, s.skyName[i]);
					dl->PopClipRect();
				};
				const ImVec2 a{ size.x - 400 * k, size.y - 158 * k }, b{ size.x - 40 * k, size.y - 106 * k };
				const int    main = s.skyKind[1] > 0 ? 1 : 0;
				card(main, a, b);
				if (main == 1 && s.skyKind[0] > 0) {
					card(0, { a.x, a.y - 60 * k }, { b.x, b.y - 60 * k });
				}
			}
			// Weapon dock, bottom-right: the weapon in hand as a large card (its picture, Reforge marks, ability and
			// signature move), the other slots as small cards above it with their keys. Dimmed while sheathed.
			// (Noah: the old dock drew over the new cards) the night's weapon in hand is one slim card like the others: its
			// emblem aflame, the slot and its reforging, its name, and what it runs on - never while a bow is held
			if (!spellHands && !skyHands && s.bowHeld < 0) {
				const bool any = std::any_of(s.slotGun.begin(), s.slotGun.begin() + std::clamp(s.slotCount, 0, 3), [](int g) { return g >= 0; });
				static float shown = 0;
				shown += ((s.gunOut ? 1.0f : 0.6f) - shown) * std::min(1.0f, dt * 8);
				const int act = s.activeSlot >= 0 && s.activeSlot < s.slotCount ? s.activeSlot : -1;
				const int ag = act >= 0 ? s.slotGun[act] : -1;
				if (any && ag >= 0) {
					const float         al = shown;
					const ImVec2        a{ size.x - 400 * k, size.y - 158 * k }, b{ size.x - 40 * k, size.y - 106 * k };
					const float         rr = (b.y - a.y) * 0.5f;
					const std::uint32_t ec = gen::kGuns[ag].color;
					const ImU32         ecol = IM_COL32((ec >> 16) & 0xFF, (ec >> 8) & 0xFF, ec & 0xFF, 255);
					const int           lvl = std::clamp(s.slotLevel[act], 0, 3);
					const auto          fc = gen::kForgeColor[lvl];
					const ImU32         nameCol = lvl > 0 ? IM_COL32((fc >> 24) & 0xFF, (fc >> 16) & 0xFF, (fc >> 8) & 0xFF, 255) : kParch;
					dl->AddRectFilled(a, b, IM_COL32(8, 8, 10, static_cast<int>(175 * al)), rr);
					dl->AddRect(a, b, Fade(ecol, 0.55f * al), rr, 0, 1.2f * k);
					DrawEmblem(dl, ag, { a.x + rr, (a.y + b.y) * 0.5f }, rr - 6 * k, al, now, 0.9f);
					// right: what it runs on - a mana pip, or the endless mark
					float textR = b.x - 14 * k;
					{
						const bool        endless = s.gunKind == 0 || s.gunKind == 3 || !s.gunOut;
						const std::string vs = endless ? std::string("\xE2\x88\x9E") : std::to_string(static_cast<int>(std::ceil(s.manaCost)));
						const float       cz = (endless ? 22 : 18) * k;
						const auto        cm = Measure(g_num, cz, vs);
						const ImVec2      p1{ b.x - 10 * k, (a.y + b.y) * 0.5f + cm.y * 0.5f + 3 * k }, p0{ p1.x - cm.x - 16 * k, p1.y - cm.y - 6 * k };
						const ImU32       pc = endless ? IM_COL32(70, 62, 48, static_cast<int>(200 * al)) : s.mp >= s.manaCost ? IM_COL32(40, 70, 130, static_cast<int>(200 * al)) : IM_COL32(120, 30, 26, static_cast<int>(210 * al));
						dl->AddRectFilled(p0, p1, pc, (p1.y - p0.y) * 0.5f);
						Txt(dl, g_num, cz, { p0.x + 8 * k, p0.y + 3 * k }, Fade(IM_COL32(240, 236, 226, 255), al), vs);
						textR = p0.x - 10 * k;
					}
					const float x0 = a.x + rr * 2 + 6 * k;
					dl->PushClipRect(a, { textR, b.y }, true);
					const std::string head = "SLOT " + std::to_string(act + 1) + "  " + (lvl >= 3 ? "ASCENDANT" : lvl > 0 ? "REFORGED " + std::string(lvl == 1 ? "I" : "II") : "");
					Txt(dl, g_body, 12 * k, { x0, a.y + 7 * k }, Fade(ecol, 0.9f * al), head);
					const std::string nm(gen::kGuns[ag].label);
					float             nameSize = 22 * k;
					while (nameSize > 11 * k && x0 + Measure(g_title, nameSize, nm).x > textR) {
						nameSize -= 1 * k;
					}
					Txt(dl, g_title, nameSize, { x0, a.y + 22 * k }, Fade(nameCol, al), nm);
					dl->PopClipRect();
					if (s.slotCount > 1) {
						const std::string hint = "G  next weapon";
						Txt(dl, g_body, 12 * k, { b.x - Measure(g_body, 12 * k, hint).x - 10 * k, a.y - 18 * k }, Fade(IM_COL32(180, 172, 156, 200), al), hint);
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
						dl->AddRectFilled({ x0 + 14 * k, y + 25 * k }, { x0 + 14 * k + (w - 28 * k) * std::clamp(pu.t / 45.0f, 0.0f, 1.0f), y + 27 * k }, pu.col);
						Txt(dl, g_title, 20 * k, { x0 + 18 * k, y }, pu.col, txt, 1.5f * k, pu.col);
						Txt(dl, g_num, 20 * k, { x0 + w - 18 * k - Measure(g_num, 20 * k, sec).x, y }, kParch, sec);
						y -= 40 * k;
					}
				}
				// (no charge ring: a full charge shows in the spell itself, which pulses and flashes in the hands)
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

			if (s.headBurstT > 0) {  // a headshot kill: a quick white flare round the crosshair
				const ImVec2 c{ size.x * 0.5f, size.y * 0.5f };
				const float  f = s.headBurstT / 0.18f, soft = Settings::ReduceFlashes() ? 0.35f : 1.0f;
				dl->AddCircleFilled(c, (10 + 26 * (1 - f)) * k, IM_COL32(255, 255, 255, static_cast<int>(120 * f * soft)), 24);
				dl->AddCircle(c, (16 + 30 * (1 - f)) * k, IM_COL32(255, 235, 220, static_cast<int>(220 * f * soft)), 32, 2.0f * k);
			}

			// Countdown between nights: the Blood Moon rising. A ring of the Dirge's runes round it, one going dark for
			// each second spent; the moon's face fills with blood as the night comes on; cold-blue fire licks round the
			// ring, turning red and climbing higher in the last seconds, when the numeral beats like a heart.
			if (s.countdown > 0.05f) {
				const ImVec2 c{ size.x * 0.5f, size.y * 0.21f };
				const float  R = 52 * k, total = std::max(1.0f, gen::kRounds.breakSeconds);
				const float  frac = std::clamp(s.countdown / total, 0.0f, 1.0f), dread = 1.0f - frac;
				const bool   last = s.countdown <= 5.0f;
				const float  beat = last ? std::pow(std::max(0.0f, std::sin((s.countdown - std::floor(s.countdown)) * RE::NI_PI)), 6.0f) : 0.0f;
				// the fire round it (the emblems' flame ring): blue, warming to red as the night nears
				if (g_flame) {
					const auto  tx = ImTextureID(reinterpret_cast<std::uintptr_t>(g_flame));
					// (Noah: slower - it raced) a slow burn, each frame melting into the next
					const float fps = 6.0f + 4.0f * dread;
					for (int pass = 0; pass < 2; ++pass) {
						const float ft = (now + pass * 1.3f) * fps, blend = ft - std::floor(ft);
						const float hh = R * (1.55f + 0.25f * dread + 0.08f * beat) * (pass ? 0.9f : 1.0f);
						const int   rr = static_cast<int>(70 + 185 * dread), gg = static_cast<int>(140 - 100 * dread), bb = static_cast<int>(255 - 225 * dread);
						const float al = (pass ? 120.0f : 190.0f) * (0.6f + 0.4f * dread);
						for (int q = 0; q < 2; ++q) {
							const int   f = (static_cast<int>(ft) + q) % 16;
							const float u = (f % 4) * 0.25f, v = (f / 4) * 0.25f, w = q ? blend : 1.0f - blend;
							dl->AddImage(tx, { c.x - hh, c.y - hh }, { c.x + hh, c.y + hh }, { u, v }, { u + 0.25f, v + 0.25f }, IM_COL32(rr, gg, bb, static_cast<int>(al * w)));
						}
					}
				}
				// the moon: dark stone, filling with blood from below as time runs out
				dl->AddCircleFilled(c, R * 1.02f, IM_COL32(6, 5, 8, 235), 64);
				dl->AddCircleFilled(c, R * 0.86f, IM_COL32(28, 24, 30, 255), 64);
				{
					const float lvl = c.y + R * 0.86f - 2 * R * 0.86f * dread;  // the blood's surface
					// fill the blood in thin rows (the disc below the wavy surface)
					for (float y = std::max(lvl, c.y - R * 0.86f); y < c.y + R * 0.86f; y += 1.5f * k) {
						const float half = std::sqrt(std::max(0.0f, R * R * 0.7396f - (y - c.y) * (y - c.y)));
						const float wave = std::sin(now * 2.0f + y * 0.05f) * 1.5f * k;
						if (y < lvl + 3 * k + wave) {
							continue;
						}
						const int depth = static_cast<int>(std::clamp((y - lvl) / (R * 1.72f), 0.0f, 1.0f) * 70);
						dl->AddLine({ c.x - half, y }, { c.x + half, y }, IM_COL32(150 - depth, 14, 12, 235), 1.6f * k);
					}
					dl->AddCircle(c, R * 0.86f, IM_COL32(120, 30, 24, 200), 64, 1.5f * k);
				}
				// the runes round the rim: one goes dark for every second spent (the last ones burn brightest)
				{
					constexpr int kRunes = 24;
					const int lit = static_cast<int>(std::ceil(frac * kRunes));
					for (int i = 0; i < kRunes; ++i) {
						const float ang = -RE::NI_PI / 2 + i * 2 * RE::NI_PI / kRunes;
						const ImVec2 p{ c.x + std::cos(ang) * R * 1.18f, c.y + std::sin(ang) * R * 1.18f };
						const bool  on = i < lit;
						const ImU32 col = on ? (last ? IM_COL32(255, 90, 60, 255) : IM_COL32(150, 205, 255, 255)) : IM_COL32(60, 52, 48, 200);
						const float rr = 4.2f * k;
						if (on) {
							dl->AddCircleFilled(p, rr * 2.0f, last ? IM_COL32(255, 60, 30, 60) : IM_COL32(90, 160, 255, 50), 12);
						}
						switch (i % 4) {  // four rune shapes, turned with the ring
						case 0: dl->AddLine({ p.x, p.y - rr }, { p.x, p.y + rr }, col, 1.6f * k); dl->AddLine({ p.x, p.y - rr * 0.2f }, { p.x + rr * 0.7f, p.y - rr }, col, 1.4f * k); break;
						case 1: dl->AddLine({ p.x - rr * 0.6f, p.y + rr }, { p.x, p.y - rr }, col, 1.5f * k); dl->AddLine({ p.x + rr * 0.6f, p.y + rr }, { p.x, p.y - rr }, col, 1.5f * k); break;
						case 2: dl->AddLine({ p.x - rr * 0.6f, p.y - rr }, { p.x + rr * 0.6f, p.y + rr }, col, 1.5f * k); dl->AddLine({ p.x + rr * 0.6f, p.y - rr }, { p.x - rr * 0.6f, p.y + rr }, col, 1.5f * k); break;
						default: dl->AddQuad({ p.x, p.y - rr }, { p.x + rr * 0.7f, p.y }, { p.x, p.y + rr }, { p.x - rr * 0.7f, p.y }, col, 1.4f * k); break;
						}
					}
					dl->AddCircle(c, R * 1.02f, Fade(kGold, 0.9f), 64, 1.8f * k);
					dl->AddCircle(c, R * 1.34f, Fade(kGold, 0.55f), 64, 1.2f * k);
				}
				// the numeral, beating in the last seconds
				const std::string n = std::to_string(static_cast<int>(std::ceil(s.countdown)));
				const float  fs = std::round((46 + 10 * beat) * k);
				const ImU32  nc = last ? IM_COL32(255, 220, 200, 255) : kParch;
				TxtC(dl, g_num, fs, std::round(c.x), std::round(c.y - fs * 0.53f), nc, n, (2 + 6 * beat) * k, last ? IM_COL32(255, 40, 20, 255) : IM_COL32(60, 120, 255, 255));
				// the title on a plate under it
				const std::string t = "NIGHT " + std::to_string(s.round + 1) + (last ? "  -  THE DEAD RISE" : "  APPROACHES");
				const ImVec2 ts = Measure(g_title, 22 * k, t);
				const float  py = c.y + R * 1.5f + 6 * k;
				const ImVec2 pa{ c.x - ts.x / 2 - 22 * k, py }, pb{ c.x + ts.x / 2 + 22 * k, py + ts.y + 12 * k };
				const float  pe = (pb.y - pa.y) * 0.5f;
				const ImVec2 plate[6] = { { pa.x - pe, (pa.y + pb.y) / 2 }, { pa.x, pa.y }, { pb.x, pa.y }, { pb.x + pe, (pa.y + pb.y) / 2 }, { pb.x, pb.y }, { pa.x, pb.y } };
				dl->AddConvexPolyFilled(plate, 6, IM_COL32(10, 9, 12, 215));
				dl->AddPolyline(plate, 6, last ? IM_COL32(220, 70, 50, 255) : Fade(kGold, 0.9f), ImDrawFlags_Closed, 1.4f * k);
				TxtC(dl, g_title, 22 * k, c.x, py + 6 * k, last ? IM_COL32(255, 150, 120, 255) : Fade(kGold, 0.95f), t, last ? 2 * k : 0, IM_COL32(255, 40, 20, 255));
			}

			// Boss bar: name over a long ornate bar at the top.
			if (!s.bossName.empty()) {
				const float w = size.x * 0.42f, h = 14 * k, x = size.x * 0.5f - w / 2, y = 74 * k;  // (where the compass was: it steps aside for the fight)
				TxtC(dl, g_title, 26 * k, size.x * 0.5f, y - 36 * k, s.bossEnraged ? IM_COL32(255, 120, 60, 255) : kParch, s.bossName, s.bossEnraged ? 3 * k : 0, IM_COL32(255, 60, 20, 255));
				static float bossGhost = 1;
				bossGhost = s.bossHp > bossGhost ? s.bossHp : std::max(s.bossHp, bossGhost - dt * 0.25f);
				FlameBorder(dl, { x - 3 * k, y - 3 * k }, { x + w + 3 * k, y + h + 3 * k }, 4, now, 1.0f, k);
				Bar(dl, { x, y }, w, h, s.bossHp, bossGhost, s.bossEnraged ? IM_COL32(205, 28, 20, 255) : IM_COL32(170, 22, 20, 255), k);
				Divider(dl, size.x * 0.5f, y + h + 12 * k, w * 0.3f, k);
			}

			// The city's curse quest, top-right (off: the rites are Skyrim quests now, in the journal with their own markers)
			constexpr bool kOwnQuestUi = false;
			if (kOwnQuestUi && (!s.questTitle.empty() || s.questNoteTime > 0)) {
				const float w = 400 * k;
				const bool  note = s.questNoteTime > 0 && !s.questNote.empty();
				const ImVec2 a{ size.x - w - 40 * k, 88 * k }, b{ size.x - 40 * k, 88 * k + (note ? 128 : 104) * k };  // under Skyrim's place name (top right)
				Panel(dl, a, b, k, 0.92f);
				Txt(dl, g_body, 14 * k, { a.x + 14 * k, a.y + 8 * k }, Fade(kGold, 0.85f), s.questCity + "  -  TRIALS  " + std::to_string(s.questStep) + "/3");
				Txt(dl, g_title, 22 * k, { a.x + 14 * k, a.y + 28 * k }, kParch, s.questTitle);
				Txt(dl, g_body, 16 * k, { a.x + 14 * k, a.y + 58 * k }, IM_COL32(205, 200, 185, 240), s.questGoal);
				static float qGhost = 0;
				qGhost = s.questFrac < qGhost ? s.questFrac : qGhost + (s.questFrac - qGhost) * std::min(1.0f, dt * 4);
				Bar(dl, { a.x + 14 * k, a.y + 88 * k }, w - 28 * k, 5 * k, qGhost, s.questFrac, IM_COL32(214, 178, 96, 255), k);
				if (note) {
					Txt(dl, g_body, 15 * k, { a.x + 14 * k, a.y + 100 * k }, Fade(IM_COL32(255, 200, 110, 255), std::min(1.0f, s.questNoteTime)), s.questNote);
				}
			}
			// the trial under way at night: a slim line under the compass - its name, its count, its clock
			if (!s.questTitle.empty() && !s.perkMenu) {
				const float y0 = 100 * k, cxs = size.x * 0.5f;
				const ImU32 warn = IM_COL32(255, 96, 70, 255);
				const bool  blink = s.questWarn && std::fmod(now, 0.8f) < 0.4f;
				TxtC(dl, g_title, 20 * k, cxs, y0, Fade(kGold, 0.95f), s.questTitle, 2 * k, IM_COL32(150, 70, 255, 200));
				std::string line = s.questGoal;
				if (s.questTimer >= 0) {
					const int t = std::max(0, static_cast<int>(std::ceil(s.questTimer)));
					line += "     " + std::to_string(t / 60) + ":" + (t % 60 < 10 ? "0" : "") + std::to_string(t % 60);
				}
				TxtC(dl, g_body, 17 * k, cxs, y0 + 27 * k, blink ? warn : IM_COL32(225, 216, 196, 235), line, 1.2f * k, IM_COL32(0, 0, 0, 220));
				const float bw = 220 * k;
				const float fr = std::clamp(s.questFrac, 0.0f, 1.0f);
				dl->AddRectFilled({ cxs - bw * 0.5f, y0 + 52 * k }, { cxs + bw * 0.5f, y0 + 55 * k }, IM_COL32(20, 16, 12, 170));
				dl->AddRectFilled({ cxs - bw * 0.5f, y0 + 52 * k }, { cxs - bw * 0.5f + bw * fr, y0 + 55 * k }, IM_COL32(214, 178, 96, 255));
			}
			if (!s.sanctuary.empty()) {
				TxtC(dl, g_title, 24 * k, size.x * 0.5f, 104 * k, Fade(kGold, 0.9f), "SANCTUARY OF " + s.sanctuary, 2 * k, IM_COL32(255, 220, 120, 255));
				TxtC(dl, g_body, 16 * k, size.x * 0.5f, 134 * k, IM_COL32(220, 215, 200, 220), "The Echo Altar is awake here. The dead cannot rise.");
			}
			// Kindling a ward-fire: a glowing gold ring round the crosshair that fills smoothly (the game sends the
			// progress a few times a second; the ring runs on between, at the kindling's own pace)
			{
				static float shown = 0;
				if (s.wardFrac > 0.001f) {
					const float pace = dt / 5.0f;
					shown = s.wardFrac > shown ? std::min(s.wardFrac + pace * 3, shown + std::max(pace, (s.wardFrac - shown) * std::min(1.0f, dt * 10))) : s.wardFrac;
					shown = std::clamp(shown, 0.0f, 1.0f);
				} else {
					shown = 0;
				}
				if (shown > 0.001f) {
					const ImVec2 cc{ size.x * 0.5f, size.y * 0.5f };
					const float  R = 46 * k, a0 = -RE::NI_PI / 2, a1 = a0 + 2 * RE::NI_PI * shown;
					const int    segs = std::max(8, static_cast<int>(256 * shown));
					dl->AddCircle(cc, R, IM_COL32(20, 16, 12, 170), 256, 7 * k);       // the dark track
					dl->AddCircle(cc, R, Fade(kGold, 0.18f), 256, 1.2f * k);           // its faint gold edge
					for (const auto [w, al] : { std::pair{ 16.0f, 0.06f }, { 11.0f, 0.10f }, { 7.0f, 0.22f } }) {  // the glow
						dl->PathArcTo(cc, R, a0, a1, segs);
						dl->PathStroke(Fade(IM_COL32(255, 190, 80, 255), al * (0.85f + 0.15f * std::sin(now * 5))), 0, w * k);
					}
					dl->PathArcTo(cc, R, a0, a1, segs);
					dl->PathStroke(IM_COL32(255, 214, 120, 255), 0, 3.6f * k);             // the gold itself
					dl->PathArcTo(cc, R, a0, a1, segs);
					dl->PathStroke(IM_COL32(255, 248, 220, 210), 0, 1.2f * k);             // its bright core
					const ImVec2 head{ cc.x + std::cos(a1) * R, cc.y + std::sin(a1) * R };  // the spark at its tip
					dl->AddCircleFilled(head, 9 * k, IM_COL32(255, 200, 90, 50), 24);
					dl->AddCircleFilled(head, 4.5f * k, IM_COL32(255, 230, 160, 160), 24);
					dl->AddCircleFilled(head, 2.2f * k, IM_COL32(255, 255, 240, 255), 16);
					TxtC(dl, g_title, 20 * k, cc.x, cc.y + R + 14 * k, IM_COL32(255, 214, 140, 255), "KINDLING THE WARD-FIRE", 2 * k, IM_COL32(255, 140, 30, 255));
				}
			}
			// Nuke: the dead regroup.
			if (s.nukeLeft > 0.05f && (s.nukeLeft > 5 || std::fmod(now, 0.5f) >= 0.2f)) {
				const std::string txt = "NUKE  -  THE DEAD REGROUP  " + std::to_string(static_cast<int>(std::ceil(s.nukeLeft)));
				TxtC(dl, g_body, 22 * k, size.x * 0.5f, size.y - 236 * k, IM_COL32(255, 160, 60, 255), txt);
			}
			// Banner: centred line between ornaments, fades and drifts in.
			if (s.bannerTime > 0 && !s.banner.empty()) {
				// (Noah: boxes overlapped) below the stack under the compass (a trial's line, the sanctuary, the wave dial)
				const float a = std::min(1.0f, s.bannerTime * 2), y = size.y * 0.27f + (1.0f - a) * 10 * k;
				const auto  ts = Measure(g_title, 32 * k, s.banner);
				TxtC(dl, g_title, 32 * k, size.x * 0.5f, y, Fade(kParch, a), s.banner, 2 * k, IM_COL32(0, 0, 0, 255));
				Divider(dl, size.x * 0.5f, y + ts.y + 10 * k, std::min(size.x * 0.4f, ts.x * 0.6f + 40 * k), k, a);
			}

			// health bars over the wounded: black iron plates with chamfered ends and bronze caps, the blood (always red)
			// draining from the right with a pale wound that follows it down; the tougher the foe, the hotter the red fire
			// round its plate (FlameBorder: specials smoulder, elites blaze, champions roar); other foes' names in iron (these replace Skyrim's bar under the compass)
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
					const ImU32  trim = b.fury >= 2 ? IM_COL32(200, 64, 40, 255) : b.fury == 1 ? IM_COL32(150, 70, 52, 255) : IM_COL32(118, 104, 86, 255);
					const float  p = 3.0f * k * sc, e = h * 0.5f + p + 4.0f * k * sc;  // plate margin, chamfer reach
					const ImVec2 plate[6] = { { a.x - e, c.y }, { a.x - p, a.y - p }, { z.x + p, a.y - p }, { z.x + e, c.y }, { z.x + p, z.y + p }, { a.x - p, z.y + p } };
					FlameBorder(dl, { a.x - p, a.y - p }, { z.x + p, z.y + p }, b.fury, now + (b.id % 97) * 0.37f, al, k * sc);
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
					if (!b.name.empty()) {
						const float fs = std::max(9.0f * k, (b.rank == 2 ? 12.5f : b.rank == 1 ? 11.5f : 11.0f) * k * sc);
						const ImU32 nc = b.rank == 2 ? IM_COL32(255, 196, 150, 255) : b.rank == 1 ? IM_COL32(255, 176, 150, 255) : IM_COL32(222, 212, 192, 255);
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

			// The ritual circle: a ring of glowing runes on the ground round the shrine - pale blue light, every rune
			// its own glyph, lying flat along the ring. The runes the rite has reached burn white-blue (the fill runs
			// smoothly round, not a step a second); the rest glimmer faintly. Red when the dead break it.
			if (s.ringOn && s.ringPts.size() > 8 && !s.perkMenu) {
				const float fl = std::clamp(s.ringFlash, 0.0f, 1.0f);
				static float shown = 0;
				{
					const float total = s.ringFrac < 0.999f ? std::max(1.0f, s.ringSeconds / std::max(0.001f, 1.0f - s.ringFrac)) : 1.0f;
					const float pace = 1.0f / total;  // of the ring a second
					if (s.ringFrac < shown - 0.02f) {
						shown = s.ringFrac;  // (broken: it starts over)
					} else {
						shown = std::min(std::min(1.0f, s.ringFrac + pace * 0.95f), shown + std::max(pace * dt * (s.ringInside ? 0.0f : 1.0f), (s.ringFrac - shown) * std::min(1.0f, dt * 4)));
					}
				}
				const ImU32 cold = IM_COL32(120, 170, 255, 255), white = IM_COL32(236, 244, 255, 255), hot = IM_COL32(255, 70, 50, 255);
				const int   n = static_cast<int>(s.ringPts.size());
				ImVec2      sum{ 0, 0 };
				int         front = 0;
				const auto  P = [&](int i) { return ImVec2{ s.ringPts[i][0] * size.x, s.ringPts[i][1] * size.y }; };
				const auto  visible = [&](int i) { return i >= 0 && i < n && s.ringPts[i][2] > 0; };
				// the faint line the runes sit on, and a softer one either side
				for (int i = 0; i + 1 < n; ++i) {
					if (!visible(i) || !visible(i + 1)) continue;
					const ImVec2 a = P(i), b = P(i + 1);
					if (std::abs(a.x - b.x) > size.x * 0.5f) continue;
					const bool on = (i + 0.5f) / (n - 1) < shown;
					const ImU32 c = fl > 0.01f ? hot : on ? white : cold;
					dl->AddLine(a, b, Fade(c, (on ? 0.16f : 0.06f) + 0.2f * fl), (on ? 16.0f : 10.0f) * k);
					dl->AddLine(a, b, Fade(c, (on ? 0.55f : 0.22f) + 0.3f * fl), 1.4f * k);
					sum.x += a.x, sum.y += a.y, ++front;
				}
				// the runes: 60 glyphs round the ring, each built from 2-4 strokes on a little grid, laid along it
				static const std::array<std::array<std::int8_t, 16>, 12> kGlyph = { {
					{ 0, -2, 0, 2, 0, -1, 2, -2, 9 }, { -1, 2, 0, -2, 1, 2, 0, -2, -1, 0, 1, 0, 9 }, { 0, -2, 0, 2, -1, 0, 1, 0, 9 }, { -1, -2, 1, 2, 1, -2, -1, 2, 9 },
					{ 0, -2, 0, 2, 0, 0, 2, 2, 9 }, { -1, -2, 1, -2, 0, -2, 0, 2, 9 }, { -1, -2, -1, 2, -1, 0, 1, -1, -1, 0, 1, 1, 9 }, { 0, -2, 1, 0, 1, 0, 0, 2, 0, 2, -1, 0, 9 },
					{ -1, -2, 1, -2, -1, 2, 1, 2, -1, -2, 1, 2, 9 }, { 0, -2, 0, 2, -1, -1, 1, -1, -1, 1, 1, 1, 9 }, { -1, 2, 0, -2, 0, -2, 1, 2, 9 }, { 1, -2, -1, 0, -1, 0, 1, 2, 0, -1, 0, 2, 9 },
				} };
				constexpr int kRunes = 60;
				for (int r = 0; r < kRunes; ++r) {
					const float f = (r + 0.5f) / kRunes;
					const int   i = std::clamp(static_cast<int>(f * (n - 1)), 1, n - 2);
					if (!visible(i - 1) || !visible(i + 1)) continue;
					const ImVec2 c = P(i), a = P(i - 1), b = P(i + 1);
					if (std::abs(a.x - b.x) > size.x * 0.5f) continue;
					ImVec2 t{ b.x - a.x, b.y - a.y };
					const float tl = std::max(0.001f, std::sqrt(t.x * t.x + t.y * t.y));
					t = { t.x / tl, t.y / tl };
					// the spacing between ring points sets the rune's size (near ones big, far ones small); its height
					// runs across the ring, flattened as the ground is seen at a slant
					const float spacing = tl * 0.5f * (n - 1) / kRunes;
					const float sx = spacing * 0.16f, sy = std::min(spacing * 0.22f, 22.0f * k);
					const ImVec2 up{ -t.y, t.x };
					const float  flat = std::clamp(std::abs(t.x) * 0.35f + 0.25f, 0.25f, 0.6f);
					const bool   on = f < shown;
					const bool   edge = on && f > shown - 1.5f / kRunes;  // the one being lit now
					const float  pulse = 0.5f + 0.5f * std::sin(now * 2.2f + r * 0.7f);
					const ImU32  col = fl > 0.01f ? hot : on ? white : cold;
					const float  al = on ? (edge ? 1.0f : 0.85f + 0.15f * pulse) : 0.22f + 0.12f * pulse;
					const auto&  g = kGlyph[(r * 7 + 3) % kGlyph.size()];
					for (int q = 0; q + 3 < 16 && g[q] != 9; q += 4) {
						const ImVec2 p0{ c.x + t.x * g[q] * sx + up.x * g[q + 1] * sy * flat, c.y + t.y * g[q] * sx + up.y * g[q + 1] * sy * flat };
						const ImVec2 p1{ c.x + t.x * g[q + 2] * sx + up.x * g[q + 3] * sy * flat, c.y + t.y * g[q + 2] * sx + up.y * g[q + 3] * sy * flat };
						dl->AddLine(p0, p1, Fade(col, al * 0.22f), (on ? 7.0f : 5.0f) * k);  // its glow
						dl->AddLine(p0, p1, Fade(col, al), (on ? 2.0f : 1.4f) * k);
					}
					if (edge) {  // the lighting rune flares
						dl->AddCircleFilled(c, spacing * 0.5f, Fade(white, 0.10f + 0.08f * pulse), 24);
					}
				}
				if (s.ringLabelOn) {
					const ImVec2 c{ s.ringLabelX * size.x, s.ringLabelY * size.y };
					const std::string head = s.ringHold ? (s.ringInside ? "STAND IN THE CIRCLE" : "HOLD THE CIRCLE") : (s.ringInside ? "THE DEAD ARE IN THE CIRCLE" : "KEEP THE DEAD OUT");
					TxtC(dl, g_title, 20 * k, c.x, c.y, fl > 0.01f || s.ringInside ? IM_COL32(255, 120, 100, 255) : kParch, head, 2 * k, cold);
					TxtC(dl, g_num, 26 * k, c.x, c.y + 26 * k, IM_COL32(230, 238, 255, 240), std::to_string(std::max(0, s.ringSeconds)) + "s", 1.5f * k, cold);
				}
			}

			// The quest's way-mark: a burning diamond over the objective, or at the screen's edge pointing the way.
			if (kOwnQuestUi && s.objOn && !s.perkMenu) {
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
				const std::uint32_t c = ArmsColor(g);
				return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, static_cast<int>(255 * std::clamp(a, 0.0f, 1.0f)));
			};
			const auto rollOrb = [&](ImVec2 c, float R) {
				// (Noah: hyper-realistic) a window of dark glass in a cold steel bezel, the altar's own heart stone turning
				// inside it (the menu's renders of it), six rune studs round the rim; as each stone strikes the heart a bolt
				// of blue lightning forks from its stud into the stone and stays crackling, the stone's light rising; when
				// the sixth lands, a white flash and what it won in front of the stone
				const OrbTex tx = MenuOrbTextures();
				const auto   T = [](ID3D11ShaderResourceView* t) { return ImTextureID(reinterpret_cast<std::uintptr_t>(t)); };
				const int    struck = s.rollPhase == 2 ? std::min(6, static_cast<int>(s.rollT * 6.0f + 1e-4f)) : s.rollPhase == 3 ? 6 : 0;
				const float  power = struck / 6.0f;
				// the glass: deep, darker at the rim (many soft discs: no banding)
				for (int q = 0; q < 16; ++q) {
					const float t = 1.0f - q / 16.0f;
					dl->AddCircleFilled(c, R * t, IM_COL32(static_cast<int>(2 + 10 * (1 - t)), static_cast<int>(4 + 16 * (1 - t)), static_cast<int>(8 + 30 * (1 - t)), 40), 64);
				}
				dl->AddCircleFilled(c, R, IM_COL32(2, 3, 6, 120), 64);
				// the heart stone, turning (faster while the altar rolls), its light swelling with every strike
				if (tx.soft) {
					const float g = R * (1.1f + 0.5f * power);
					dl->AddImage(T(tx.soft), { c.x - g, c.y - g }, { c.x + g, c.y + g }, { 0, 0 }, { 1, 1 }, IM_COL32(70, 150, 255, static_cast<int>(60 + 150 * power)));
				}
				if (tx.crystal[0]) {
					static float turn = 0;
					turn += dt * (s.rollPhase == 2 ? 120.0f + 320.0f * s.rollT : 21.3f);  // (frames a second of 384: 18 s a turn at rest)
					const int nF = tx.crystal[3] ? 384 : tx.crystal[1] ? 192 : 96;
					const int f = static_cast<int>(turn) % nF, ff = f % 96;
					const ImVec2 uv0{ (ff % 12) / 12.0f, (ff / 12) / 8.0f }, uv1{ uv0.x + 1.0f / 12.0f, uv0.y + 0.125f };
					const float  hh = R * 1.55f, ww = hh * 320.0f / 480.0f;
					const ImVec2 mid{ c.x, c.y + (0.5f - 0.56f) * hh * 2.0f * 0.5f };
					dl->PushClipRect({ c.x - R, c.y - R }, { c.x + R, c.y + R }, true);
					dl->AddImage(T(tx.crystal[f / 96] ? tx.crystal[f / 96] : tx.crystal[0]), { mid.x - ww * 0.5f, mid.y - hh * 0.5f }, { mid.x + ww * 0.5f, mid.y + hh * 0.5f }, uv0, uv1,
						IM_COL32(static_cast<int>(200 + 55 * power), static_cast<int>(215 + 40 * power), 255, 255));
					dl->PopClipRect();
				}
				// the bolts: from each struck stud into the stone - a forked, flickering line of blue-white light
				const auto bolt = [&](ImVec2 a, ImVec2 b, int seed) {
					constexpr int kSeg = 9;
					const ImVec2  d{ b.x - a.x, b.y - a.y };
					const float   len = std::sqrt(d.x * d.x + d.y * d.y);
					if (len < 2) return;
					const ImVec2 n{ -d.y / len, d.x / len };
					const int    tick = static_cast<int>(now * 22.0f);  // (it re-forks some twenty times a second, as lightning does)
					ImVec2       pts[kSeg + 1];
					for (int j = 0; j <= kSeg; ++j) {
						const float t = static_cast<float>(j) / kSeg;
						const std::uint32_t h = static_cast<std::uint32_t>((tick * 73856093) ^ (seed * 19349663) ^ (j * 83492791));
						const float jig = (j == 0 || j == kSeg) ? 0.0f : ((h % 1000) / 500.0f - 1.0f) * len * 0.09f * std::sin(t * 3.1416f);
						pts[j] = { a.x + d.x * t + n.x * jig, a.y + d.y * t + n.y * jig };
					}
					for (int j = 0; j < kSeg; ++j) {
						dl->AddLine(pts[j], pts[j + 1], IM_COL32(40, 110, 255, 60), 6.0f * k);
						dl->AddLine(pts[j], pts[j + 1], IM_COL32(110, 190, 255, 150), 2.6f * k);
						dl->AddLine(pts[j], pts[j + 1], IM_COL32(240, 250, 255, 255), 1.0f * k);
					}
				};
				for (int q = 0; q < 6; ++q) {
					const float  a = -RE::NI_PI / 2 + q * RE::NI_PI / 3.0f;
					const ImVec2 p{ c.x + std::cos(a) * (R - 5 * k), c.y + std::sin(a) * (R - 5 * k) };
					if (q < struck) {
						bolt(p, { c.x, c.y - R * 0.05f }, q);
					}
				}
				// the bezel: brushed steel - lit from above, dark below, a fine bright inner lip
				for (int q = 0; q < 24; ++q) {
					const float a0 = q * 6.2832f / 24, a1 = (q + 1) * 6.2832f / 24, lum = 0.5f + 0.5f * std::cos((a0 + a1) * 0.5f + 2.3f);
					const int   v = static_cast<int>(50 + 150 * lum);
					dl->PathArcTo(c, R + 2.5f * k, a0, a1 + 0.02f, 4);
					dl->PathStroke(IM_COL32(v, v + 8, std::min(255, v + 22), 255), 0, 4.0f * k);
				}
				dl->AddCircle(c, R + 4.6f * k, IM_COL32(8, 10, 14, 255), 64, 1.2f * k);
				dl->AddCircle(c, R, IM_COL32(150, 200, 255, 120), 64, 1.0f * k);
				// the glass's reflection: a soft crescent of sky across the top
				dl->PathArcTo(c, R * 0.82f, -2.6f, -0.55f, 24);
				dl->PathStroke(IM_COL32(220, 235, 255, 26), 0, R * 0.22f);
				// the rune studs: dark iron until struck, then burning blue
				for (int q = 0; q < 6; ++q) {
					const float  a = -RE::NI_PI / 2 + q * RE::NI_PI / 3.0f;
					const ImVec2 p{ c.x + std::cos(a) * (R + 2.5f * k), c.y + std::sin(a) * (R + 2.5f * k) };
					if (q < struck && tx.soft) {
						const float g = 9 * k;
						dl->AddImage(T(tx.soft), { p.x - g, p.y - g }, { p.x + g, p.y + g }, { 0, 0 }, { 1, 1 }, IM_COL32(110, 190, 255, 220));
					}
					dl->AddCircleFilled(p, 3.2f * k, q < struck ? IM_COL32(200, 235, 255, 255) : IM_COL32(40, 46, 56, 255), 12);
					dl->AddCircle(p, 3.2f * k, IM_COL32(10, 12, 16, 255), 12, 1.0f * k);
				}
				if (s.rollPhase == 3 && s.rollGun >= 0) {
					const float since = s.rollT * gen::kShowOffer;
					if (since < 0.6f && tx.soft) {  // the landing's white flash
						const float f = since / 0.6f, g = R * (1.0f + 1.5f * f);
						dl->AddImage(T(tx.soft), { c.x - g, c.y - g }, { c.x + g, c.y + g }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(230 * (1.0f - f))));
					}
					DrawEmblem(dl, s.rollGun, c, R * 0.55f, std::min(1.0f, since / 0.3f), now);
				}
			};
			const auto keyCap = [&](ImVec2 at, const std::string& key, bool live) {
				if (g_padLast) {  // the controller's own button, not a letter
					const std::string_view g = key == "\xE2\x86\x91" ? "DU" : key == "\xE2\x86\x93" ? "DD" : key == "\xE2\x86\x90" ? "DL" : key == "\xE2\x86\x92" ? "DR" : std::string_view(key);
					PadGlyph(dl, { at.x + 13 * k, at.y + 13 * k }, 13 * k, g, live ? 1.0f : 0.45f);
					return;
				}
				const ImVec2 kb{ at.x + 26 * k, at.y + 26 * k };
				dl->AddRectFilled(at, kb, live ? IM_COL32(60, 30, 18, 230) : IM_COL32(26, 22, 20, 210), 3 * k);
				dl->AddRect(at, kb, live ? kGold : kGoldDim, 3 * k, 0, 1.2f * k);
				TxtC(dl, g_num, 17 * k, at.x + 13 * k, at.y + 3 * k, live ? kParch : IM_COL32(150, 140, 125, 255), key);
			};
			(void)keyCap;  // (the altar is driven by its focus now: Shops Row)
			// the roll on the shrine itself, over its crown: the shrine gathers its light (a ring of runes drawn in), then a
			// reel of the spells' emblems turns round the orb - fast, then slowing like a wheel of fortune, the name of the
			// one in front under it - and stops on the one won (1 of every spell there is), which bursts into light and
			// hangs there to be taken
			// the roll's beams: from each column's rune stone into the heart stone, one after another as the altar gathers
			// its power, all six while it turns - jagged, flickering threads of blue light with a bright core
			if (s.beamLit > 0) {
				const ImVec2 to{ s.beamToX * size.x, s.beamToY * size.y };
				for (int q = 0; q < s.beamLit && q < 6; ++q) {
					const ImVec2 from{ s.beamFrom[q * 2] * size.x, s.beamFrom[q * 2 + 1] * size.y };
					// the newest beam is still on its way: it shoots out of its stone and races to the heart (eased in)
					const bool   racing = q == s.beamLit - 1 && s.beamGrow < 1.0f;
					const float  reach = racing ? std::min(1.0f, std::pow(std::clamp(s.beamGrow / 0.55f, 0.0f, 1.0f), 1.6f)) : 1.0f;
					const ImVec2 full{ to.x - from.x, to.y - from.y };
					const ImVec2 d{ full.x * reach, full.y * reach };
					const float  len = std::sqrt(d.x * d.x + d.y * d.y);
					if (len < 4.0f) continue;
					const ImVec2 nrm{ -d.y / len, d.x / len };
					constexpr int kSeg = 14;
					ImVec2        pts[kSeg + 1];
					const float   fl = 0.75f + 0.25f * std::sin(now * 23.0f + q * 1.7f);
					for (int j = 0; j <= kSeg; ++j) {
						const float t = static_cast<float>(j) / kSeg;
						const float jig = (j == 0 || j == kSeg) ? 0.0f : std::sin(now * 31.0f + q * 3.1f + j * 2.3f) * std::sin(t * 3.1416f) * len * 0.035f;
						pts[j] = { from.x + d.x * t + nrm.x * jig, from.y + d.y * t + nrm.y * jig };
					}
					for (int j = 0; j < kSeg; ++j) {
						dl->AddLine(pts[j], pts[j + 1], IM_COL32(60, 140, 255, static_cast<int>(70 * fl)), 9.0f * k);
						dl->AddLine(pts[j], pts[j + 1], IM_COL32(120, 200, 255, static_cast<int>(150 * fl)), 4.0f * k);
						dl->AddLine(pts[j], pts[j + 1], IM_COL32(235, 248, 255, static_cast<int>(230 * fl)), 1.6f * k);
					}
					dl->AddCircleFilled(from, 7.0f * k, IM_COL32(150, 210, 255, static_cast<int>(150 * fl)), 16);
					if (racing && reach < 1.0f) {  // its head: a bright bolt tip
						const ImVec2 head{ from.x + d.x, from.y + d.y };
						dl->AddCircleFilled(head, 9.0f * k, IM_COL32(120, 200, 255, 120), 16);
						dl->AddCircleFilled(head, 4.0f * k, IM_COL32(240, 250, 255, 255), 12);
					} else if (racing) {  // just struck: a ring flashes out from the heart
						const float f = std::clamp((s.beamGrow - 0.55f) / 0.45f, 0.0f, 1.0f);
						dl->AddCircle(to, (14.0f + 40.0f * f) * k, IM_COL32(170, 225, 255, static_cast<int>(220 * (1.0f - f))), 40, 3.0f * k);
					}
				}
				const int struck = s.beamLit - (s.beamGrow < 0.55f ? 1 : 0);
				if (struck > 0) {
					dl->AddCircleFilled(to, (8.0f + 2.5f * struck) * k, IM_COL32(170, 220, 255, 60 + 15 * struck), 24);
				}
			}
			// the roll over the heart stone: no orbits, no glow - the names roll by (each with what it is), slowing like a
			// wheel, as the column stones strike the heart one by one; the sixth strike reveals the one won, in its colour
			if (s.rollOnScreen && s.rollPhase >= 2) {
				const ImVec2 c{ s.rollScreenX * size.x, s.rollScreenY * size.y };
				const float  R = std::clamp(110.0f * 420.0f / std::max(200.0f, s.rollDist), 26.0f, 110.0f) * k;
				const float  nameH = std::clamp(R / k * 0.30f, 22.0f, 34.0f) * k, typeH = nameH * 0.56f;
				const float  cy = c.y + R * 1.1f;  // under the stone, clear of the beams
				const int    base = s.rollBow ? Bows::kIdBase : Arcana::kIdBase;
				if (s.rollPhase == 2 && s.rollTarget >= 0) {
					// a slot reel: the name in front with its kind under it, the ones before and after sliding past above and
					// below, fading out
					const float pos = Shops::ReelPos(s.rollT), frac = pos - std::floor(pos);
					const float rowH = nameH + typeH + 10 * k;
					dl->PushClipRect({ c.x - 420 * k, cy - rowH * 1.5f }, { c.x + 420 * k, cy + rowH * 1.5f }, true);
					for (int jj = -2; jj <= 2; ++jj) {
						const int   id = base + Shops::ReelSpell(s.rollTarget, pos, jj);
						const float off = (jj - frac + 0.5f) * rowH;  // (the slot slides up as the reel turns)
						const float a = std::clamp(1.0f - std::abs(off) / (rowH * 1.4f), 0.0f, 1.0f);
						if (a <= 0.01f) continue;
						const float y = cy + off - (nameH + typeH) * 0.5f;
						if (Legendary(id)) TxtGoldC(dl, g_title, nameH * (0.8f + 0.2f * a), c.x, y, ArmsName(id), a);
						else TxtC(dl, g_title, nameH * (0.8f + 0.2f * a), c.x, y, Fade(IM_COL32(236, 240, 255, 255), a), ArmsName(id), 2 * k, IM_COL32(10, 20, 50, static_cast<int>(255 * a)));
						TxtC(dl, g_body, typeH, c.x, y + nameH + 2 * k, Fade(spellCol(id, 1.0f), a * 0.95f), ArmsType(id), 1.5f * k, IM_COL32(0, 0, 0, static_cast<int>(255 * a)));
					}
					dl->PopClipRect();
					const float hw = 200 * k;  // the window's edges: two thin rune-blue rules
					dl->AddLine({ c.x - hw, cy - rowH * 0.5f }, { c.x + hw, cy - rowH * 0.5f }, Fade(kGold, 0.6f), 1.2f * k);
					dl->AddLine({ c.x - hw, cy + rowH * 0.5f }, { c.x + hw, cy + rowH * 0.5f }, Fade(kGold, 0.6f), 1.2f * k);
				} else if (s.rollPhase == 3 && s.rollGun >= 0) {
					const float since = s.rollT * gen::kShowOffer, in = std::min(1.0f, since / 0.35f);
					const ImU32 ec = spellCol(s.rollGun, 1.0f);
					const bool  legend = s.rollGun >= Arcana::kIdBase && s.rollGun < Bows::kIdBase && Arcana::TierOf(s.rollGun - Arcana::kIdBase) >= 3;
					const float y = cy - (nameH + typeH) * 0.5f;
					if (legend) {
						TxtC(dl, g_title, nameH * 0.7f, c.x, y - nameH * 0.9f, Fade(IM_COL32(255, 222, 130, 255), in), "LEGENDARY", 2 * k, IM_COL32(120, 60, 0, static_cast<int>(255 * in)));
					}
					if (legend) TxtGoldC(dl, g_title, nameH * (1.25f - 0.15f * in), c.x, y, ArmsName(s.rollGun), in);
					else TxtC(dl, g_title, nameH * (1.25f - 0.15f * in), c.x, y, Fade(IM_COL32(250, 246, 235, 255), in), ArmsName(s.rollGun), 2.5f * k, Fade(ec, in));
					TxtC(dl, g_body, typeH, c.x, y + nameH * 1.15f + 2 * k, Fade(ec, in), ArmsType(s.rollGun), 1.5f * k, IM_COL32(0, 0, 0, static_cast<int>(255 * in)));
					const float th = nameH * 1.15f + typeH + 10 * k;
					Divider(dl, c.x, y + th, std::max(60.0f * k, R * 0.9f), k);
				}
			}
			if (s.perkMenu) {
				const float w = 470 * k;
				const bool  shout = s.shrineShout >= 0 && s.shrineShout < static_cast<int>(gen::kShouts.size());
				// (Noah: scroll down to the altar's trials) one panel, its trials below the rest; as tall as the screen allows,
				// the rows sliding to keep the one in focus in view
				const float trialsH = (s.trialsOn ? 112 * k + 5 * 70 * k + 104 * k : 0.0f) + (s.whispers.empty() ? 0.0f : 56 * k + 46 * k * s.whispers.size());
				const float contentH = (shout ? 610 : 532) * k + 86 * k + 78 * k + 56 * k + (s.chooseOn ? 52 * k : 0.0f) + trialsH;  // (+ Leave)  // (+ the bow wheel)  // (+ the dragon's bond, + a spell to choose)
				const float h = std::min(contentH, size.y - 60 * k);
				const ImVec2 a{ size.x * 0.5f - w * 0.5f, std::max(24 * k, size.y * 0.5f - h * 0.5f - 10 * k) }, b{ a.x + w, a.y + h };  // (Noah: centred, clear of the HUD)
				Panel(dl, a, b, k, 0.92f);
				static float scroll = 0, scrollGoal = 0;
				static float focusY0 = 0, focusY1 = 0;
				scrollGoal = std::clamp(scrollGoal, 0.0f, std::max(0.0f, contentH - h));
				scroll += (scrollGoal - scroll) * std::min(1.0f, dt * 10.0f);
				const float cx = (a.x + b.x) * 0.5f;
				dl->PushClipRect({ a.x, a.y + 6 * k }, { b.x, b.y - 66 * k }, true);
				TxtC(dl, g_title, 27 * k, cx, a.y + 16 * k - scroll, IM_COL32(232, 214, 176, 255), "THE ECHO ALTAR", 2.5f * k, IM_COL32(70, 140, 255, 255));
				Divider(dl, cx, a.y + 54 * k - scroll, w * 0.38f, k);
				float y = a.y + 68 * k - scroll;
				const float L = a.x + 20 * k, Rr = b.x - 20 * k;
				// (Noah: one way through the altar) the row in focus - D-pad / W S to move, A / Enter to take it
				const auto focusRow = [&](int row, float y0, float y1) {
					if (s.altarFocus != row || s.chooseOn) return;
					focusY0 = y0 + scroll, focusY1 = y1 + scroll;  // (in the content's own space)
					const ImVec2 ra{ a.x + 10 * k, y0 }, rb{ b.x - 10 * k, y1 };
					dl->AddRectFilled(ra, rb, IM_COL32(230, 228, 220, 18 + static_cast<int>(10 * std::sin(now * 4.0f))), 4 * k);
					dl->AddRect(ra, rb, kGold, 4 * k, 0, 1.5f * k);
					const float my = (y0 + y1) * 0.5f;
					dl->AddTriangleFilled({ ra.x - 2 * k, my - 8 * k }, { ra.x - 2 * k, my + 8 * k }, { ra.x + 7 * k, my }, kGold);
				};
				const auto bullet = [&](ImVec2 at, int row, bool live) {  // where each row's key once was: a rune-diamond, lit in focus
					const ImVec2 c{ at.x + 13 * k, at.y + 13 * k };
					const bool   on = s.altarFocus == row && !s.chooseOn;
					const float  r = (on ? 8.0f : 6.0f) * k;
					dl->AddQuadFilled({ c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y }, on ? kGold : live ? IM_COL32(80, 130, 190, 230) : IM_COL32(50, 55, 65, 200));
				};
				auto price = [&](float py, int cost, bool can) {
					const std::string pr = std::to_string(cost);
					const float pw = Measure(g_num, 21 * k, pr).x;
					const ImU32 pc = can ? IM_COL32(236, 196, 112, 255) : IM_COL32(190, 70, 56, 255);
					Txt(dl, g_num, 21 * k, { Rr - pw, py }, pc, pr);
					const ImVec2 coin{ Rr - pw - 13 * k, py + 12 * k };
					dl->AddCircleFilled(coin, 7 * k, Fade(pc, 0.9f));
					dl->AddCircle(coin, 4.5f * k, IM_COL32(60, 40, 20, 200), 0, 1.3f * k);
				};
				// [1] roll a spell - or, a third one won, the choice of which of your two it replaces
				if (s.chooseOn) {
					Txt(dl, g_title, 21 * k, { L, y + 2 * k }, kParch, "A THIRD SPELL: " + ArmsName(Arcana::kIdBase + s.chooseNew));
					Txt(dl, g_body, 16 * k, { L, y + 32 * k }, IM_COL32(196, 182, 160, 235), "You carry two of the altar's spells. Give one back:");
					float cy = y + 60 * k;
					const std::string keys[3] = { g_padLast ? std::string("X") : Settings::KeyName(0), g_padLast ? std::string("Y") : Settings::KeyName(1), g_padLast ? std::string("B") : Settings::KeyName(4) };
					for (int q = 0; q < 2; ++q) {
						const int idx = q == 0 ? s.chooseA : s.chooseB;
						if (idx < 0) continue;
						(void)keys;
						if (s.chooseFocus == q) {
							dl->AddRectFilled({ a.x + 10 * k, cy - 4 * k }, { b.x - 10 * k, cy + 30 * k }, IM_COL32(200, 160, 92, 34), 4 * k);
							dl->AddRect({ a.x + 10 * k, cy - 4 * k }, { b.x - 10 * k, cy + 30 * k }, kGold, 4 * k, 0, 1.5f * k);
						}
						dl->AddQuadFilled({ L + 13 * k, cy + 5 * k }, { L + 21 * k, cy + 13 * k }, { L + 13 * k, cy + 21 * k }, { L + 5 * k, cy + 13 * k }, s.chooseFocus == q ? kGold : IM_COL32(150, 112, 60, 230));
						DrawEmblem(dl, Arcana::kIdBase + idx, { L + 52 * k, cy + 13 * k }, 13 * k, 1.0f, now, 0.5f);
						Txt(dl, g_body, 18 * k, { L + 74 * k, cy + 2 * k }, IM_COL32(236, 222, 196, 255), "Replace " + ArmsName(Arcana::kIdBase + idx) + (Arcana::Level(idx) > 0 ? "  (its reforging is lost)" : ""));
						cy += 34 * k;
					}
					if (s.chooseFocus == 2) {
						dl->AddRectFilled({ a.x + 10 * k, cy - 4 * k }, { b.x - 10 * k, cy + 30 * k }, IM_COL32(200, 160, 92, 34), 4 * k);
						dl->AddRect({ a.x + 10 * k, cy - 4 * k }, { b.x - 10 * k, cy + 30 * k }, kGold, 4 * k, 0, 1.5f * k);
					}
					dl->AddQuadFilled({ L + 13 * k, cy + 5 * k }, { L + 21 * k, cy + 13 * k }, { L + 13 * k, cy + 21 * k }, { L + 5 * k, cy + 13 * k }, s.chooseFocus == 2 ? kGold : IM_COL32(150, 112, 60, 230));
					Txt(dl, g_body, 18 * k, { L + 36 * k, cy + 2 * k }, IM_COL32(200, 186, 160, 235), "Keep yours  -  half the roll back (" + std::to_string(s.chooseRefund) + ")");
					y += 166 * k;
				} else
				{
					const float R = 46 * k;
					focusRow(0, y - 6 * k, y + 2 * R + 14 * k);
					rollOrb({ L + R, y + R + 4 * k }, R);
					const float tx = L + 2 * R + 16 * k;
					bullet({ tx, y + 4 * k }, 0, s.rollPhase == 0);
					if (s.rollPhase == 3 && Legendary(s.rollGun)) TxtGold(dl, g_title, 21 * k, { tx + 36 * k, y + 5 * k }, ArmsName(s.rollGun));
					else Txt(dl, g_title, 21 * k, { tx + 36 * k, y + 5 * k }, kParch, s.rollPhase == 0 ? (Classes::Get() == 2 ? "CALL A BOW" : Classes::Get() == 0 ? "CALL A SUMMON" : "CALL A SPELL") : s.rollPhase == 1 ? "THE SHRINE GATHERS..." : s.rollPhase == 2 ? (s.rollBow ? "THE BOWS TURN..." : "THE SPELLS TURN...") : ArmsName(s.rollGun));
					Txt(dl, g_body, 17 * k, { tx, y + 38 * k }, IM_COL32(196, 182, 160, 235),
						s.rollPhase == 3 ? (s.rollBow ? "Activate the altar to take it - your bow goes back" : "Activate the shrine to take it") : "Two of the altar's spells at a time");
					if (s.rollPhase == 0) {
						price(y + 66 * k, s.rollCost, s.points >= s.rollCost);
					}
					y += 2 * R + 22 * k;
				}
				Divider(dl, cx, y, w * 0.30f, k, 0.6f);
				y += 14 * k;
				// [1b] the bow wheel: one elemental bow at a time
				{
					const bool idle = s.rollPhase == 0;
					focusRow(1, y - 6 * k, y + 58 * k);
					bullet({ L, y + 2 * k }, 1, idle);
					Txt(dl, g_title, 21 * k, { L + 36 * k, y + 3 * k }, kParch, Classes::Get() == 3 ? "CALL A SPELL-WEAPON" : Classes::Get() == 2 ? "CALL A BOW OR CROSSBOW" : "ARMS (RANGER AND SPELLSWORD)");
					if (s.bowCarried >= 0) {
						DrawEmblem(dl, Bows::kIdBase + s.bowCarried, { L + 200 * k, y + 15 * k }, 12 * k, 1.0f, now, 0.5f);
						Txt(dl, g_body, 16 * k, { L + 218 * k, y + 6 * k }, spellCol(Bows::kIdBase + s.bowCarried, 1.0f), ArmsName(Bows::kIdBase + s.bowCarried));
					}
					Txt(dl, g_body, 17 * k, { L + 36 * k, y + 32 * k }, IM_COL32(196, 182, 160, 235), s.bowCarried >= 0 ? "A new one takes your bow's place" : "Fire, frost, storm and more - one at a time");
					if (idle) {
						price(y + 30 * k, s.bowRollCost, s.points >= s.bowRollCost);
					}
					y += 64 * k;
				}
				Divider(dl, cx, y, w * 0.30f, k, 0.6f);
				y += 14 * k;
				// [2] reforge the spell in hand
				{
					const int  act = s.activeSlot >= 0 && s.activeSlot < s.slotCount ? s.activeSlot : -1;
					const int  gw = act >= 0 ? s.slotGun[act] : -1;
					const int  sp = gw >= 0 ? -1 : (s.handSpell[1] >= 0 ? s.handSpell[1] : s.handSpell[0]);  // else a Coffer spell in hand
					const int  g = s.bowHeld >= 0 ? Bows::kIdBase + s.bowHeld : gw >= 0 ? gw : sp >= 0 ? Arcana::kIdBase + sp : -1;
					const int  lvl = s.bowHeld >= 0 ? Bows::Level(s.bowHeld) : gw >= 0 ? std::clamp(s.slotLevel[act], 0, gen::kForgeMax) : sp >= 0 ? Arcana::Level(sp) : 0;
					const bool can = g >= 0 && lvl < gen::kForgeMax;
					focusRow(2, y - 6 * k, y + 58 * k);
					bullet({ L, y + 2 * k }, 2, can);
					Txt(dl, g_title, 21 * k, { L + 36 * k, y + 3 * k }, kParch, "REFORGE");
					Txt(dl, g_body, 17 * k, { L + 36 * k, y + 32 * k }, IM_COL32(196, 182, 160, 235),
						g < 0 ? "Hold a spell or bow to reforge it" : ArmsName(g) + (lvl >= gen::kForgeMax ? "  -  ascended" : s.bowHeld >= 0 ? "  -  next: " + std::string(Bows::TierName(s.bowHeld, lvl + 1)) : std::string("  -  more power, less magicka")));
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
					const int  have = s.perkStacks[sel];
					focusRow(3, y - 6 * k, y + 36 * k + std::min(44 * k, (Rr - L) / n - 4 * k) + 12 * k + 72 * k);
					bullet({ L, y + 2 * k }, 3, s.points >= p.cost && have == 0);
					Txt(dl, g_title, 21 * k, { L + 36 * k, y + 3 * k }, kParch, "TAKE A PERK");
					const std::string rule = owned >= n ? "every perk taken - death takes them all" : "one of each a life - death takes them all";
					Txt(dl, g_body, 16 * k, { Rr - Measure(g_body, 16 * k, rule).x, y + 6 * k }, IM_COL32(170, 156, 132, 220), rule);
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
							dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { c.x - r, c.y - r }, { c.x + r, c.y + r }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, on ? 255 : 200));
							if (drunk) {  // already taken this life: a gold tick on its rim
								const ImVec2 t{ c.x + r * 0.72f, c.y + r * 0.62f };
								dl->AddCircleFilled(t, 6.5f * k, IM_COL32(20, 14, 8, 230), 16);
								dl->AddCircle(t, 6.5f * k, kGold, 16, 1.2f * k);
								dl->AddLine({ t.x - 3 * k, t.y }, { t.x - 0.8f * k, t.y + 2.6f * k }, kGold, 1.6f * k);
								dl->AddLine({ t.x - 0.8f * k, t.y + 2.6f * k }, { t.x + 3.4f * k, t.y - 2.6f * k }, kGold, 1.6f * k);
							}
						}
					}
					y += d + 12 * k;
					Txt(dl, g_title, 19 * k, { L, y }, IM_COL32(250, 236, 205, 255), std::string(p.label) + (have > 0 ? "   - taken" : ""));
					if (have == 0) {
						price(y, p.cost, s.points >= p.cost);
					}
					TxtWrap(dl, g_body, 16 * k, { L, y + 28 * k }, IM_COL32(190, 178, 156, 230), std::string(gen::kPerkDesc[sel]), Rr - L, 2);
					y += 78 * k;
				}
				// [4] the freed city's Thu'um
				if (shout) {
					const auto& sh = gen::kShouts[s.shrineShout];
					const ImU32 scol = IM_COL32((sh.color >> 16) & 0xFF, (sh.color >> 8) & 0xFF, sh.color & 0xFF, 255);
					Divider(dl, cx, y, w * 0.30f, k, 0.6f);
					y += 14 * k;
					focusRow(4, y - 6 * k, y + 58 * k);
					bullet({ L, y + 2 * k }, 4, true);
					Txt(dl, g_title, 21 * k, { L + 36 * k, y + 3 * k }, scol, "LEARN THE THU'UM: " + std::string(sh.label), 1.5f * k, scol);
					Txt(dl, g_body, 17 * k, { L + 36 * k, y + 32 * k }, IM_COL32(210, 196, 170, 235), std::string(sh.w1) + "  " + std::string(sh.w2) + "  " + std::string(sh.w3) + "  -  freely given");
					y += 64 * k;
				}
				// [5] the glider's dragon: its next bond
				{
					static constexpr const char* kBond[4] = { "", "WIND-RIDER", "TALON STRIKE", "ALDUIN'S THRALL" };
					static constexpr const char* kWhat[4] = { "",
						"Jump mid-glide: two quick wing-beats and a great one to climb - its fire turns blue",
						"Shout Dragon's Updraft: a whirlwind lifts you, then glide - attack looking down to dive-bomb",
						"Turned black, eyes like embers - its fire burns hotter, and green" };
					static constexpr ImU32 kCol[4] = { IM_COL32(220, 214, 200, 255), IM_COL32(170, 225, 255, 255), IM_COL32(120, 190, 255, 255), IM_COL32(255, 92, 48, 255) };
					const int  have = std::clamp(s.dragonBond, 0, 3), next = have + 1;
					const bool maxed = have >= 3;
					Divider(dl, cx, y, w * 0.30f, k, 0.6f);
					y += 14 * k;
					const bool can = !maxed && !s.bondLocked && s.points >= Movement::kBondCost[std::min(have, 2)];
					focusRow(5, y - 6 * k, y + 66 * k);
					bullet({ L, y + 2 * k }, 5, can);
					const ImU32 tc = kCol[maxed ? 3 : next];
					static constexpr const char* kNum[4] = { "", "I", "II", "III" };
					Txt(dl, g_title, 19 * k, { L + 36 * k, y + 4 * k }, tc, "DRAGON BOND " + std::string(kNum[maxed ? 3 : next]) + ": " + kBond[maxed ? 3 : next], 1.5f * k, tc);
					for (int q = 0; q < 3; ++q) {  // the bonds: three scales, lit by those made
						const ImVec2 pc{ L + 12 * k + q * 16 * k, y + 50 * k };
						dl->AddTriangleFilled({ pc.x - 6 * k, pc.y - 5 * k }, { pc.x + 6 * k, pc.y - 5 * k }, { pc.x, pc.y + 7 * k }, q < have ? kCol[q + 1] : IM_COL32(60, 52, 46, 220));
					}
					const std::string what = maxed ? std::string("Every bond made - it is yours to the end") : s.bondLocked ? std::string("Win Whiterun's Echo Altar first") : std::string(kWhat[next]);
					TxtWrap(dl, g_body, 16 * k, { L + 60 * k, y + 32 * k }, IM_COL32(196, 182, 160, 235), what, Rr - L - 60 * k, 2);
					if (!maxed && !s.bondLocked) {
						price(y + 2 * k, Movement::kBondCost[have], s.points >= Movement::kBondCost[have]);
					}
					y += 72 * k;
				}
				// [5b] the city's trials: any three of the five, then its Barrow-Jarl
				if (s.trialsOn) {
					const float TL = L, TR = Rr, tcx = cx;
					Divider(dl, cx, y, w * 0.30f, k, 0.6f);
					y += 14 * k;
					TxtC(dl, g_title, 23 * k, tcx, y, IM_COL32(232, 214, 176, 255), "TRIALS OF " + Upper(s.trialCity), 2.0f * k, IM_COL32(0, 0, 0, 200));
					int doneCount = 0;
					for (int q = 0; q < 5; ++q) {
						doneCount += (s.trialDone >> q) & 1;
					}
					const bool full = doneCount >= 4 || s.trialActive == 6;
					TxtC(dl, g_body, 16 * k, tcx, y + 34 * k, IM_COL32(196, 182, 160, 235), "Three shared trials and your class's own, in any order  -  " + std::to_string(std::min(doneCount, 4)) + " of 4 done");
					float ty = y + 66 * k;
					static constexpr const char* kRoman[5] = { "I", "II", "III", "IV", "V" };
					static constexpr int kOrder[4] = { 0, 1, 4, 3 };  // (the Unbroken is folded into the Long Night's Breath; the steel slot is the class trial)
					for (int qi = 0; qi < 4; ++qi) {
						const int  q = kOrder[qi];
						const bool done = (s.trialDone >> q) & 1, active = s.trialActive == q + 1, open = !done && !active && !full;
						if (active) {
							dl->AddRectFilled({ a.x + 10 * k, ty - 6 * k }, { b.x - 10 * k, ty + 62 * k }, IM_COL32(120, 60, 20, static_cast<int>(70 + 40 * std::sin(now * 3.0f))), 3 * k);
						}
						focusRow(10 + q, ty - 6 * k, ty + 62 * k);
						const bool sel = s.altarFocus == 10 + q;
						TxtC(dl, g_title, 18 * k, TL + 13 * k, ty + 4 * k, sel || done ? kGold : IM_COL32(170, 156, 132, 220), kRoman[qi]);
						const ImU32 tc = done ? kGold : active ? IM_COL32(255, 196, 120, 255) : open ? kParch : IM_COL32(140, 130, 116, 220);
						Txt(dl, g_title, 20 * k, { TL + 38 * k, ty + 2 * k }, tc, q == 3 ? Curse::TrialTitle(3) : std::string(gen::kTrials[q].title));
						TxtWrap(dl, g_body, 16 * k, { TL + 38 * k, ty + 30 * k }, done || open || active ? IM_COL32(196, 182, 160, 235) : IM_COL32(130, 120, 108, 210), s.trialDesc[q], TR - TL - 40 * k, 1);
						const char* state = done ? "DONE" : active ? "UNDER WAY" : "";
						if (*state) {
							Txt(dl, g_body, 15 * k, { TR - Measure(g_body, 15 * k, state).x, ty + 6 * k }, done ? kGold : IM_COL32(255, 176, 90, 255), state);
						}
						if (done) {  // a gold tick
							const ImVec2 t{ TR - Measure(g_body, 15 * k, "DONE").x - 14 * k, ty + 15 * k };
							dl->AddLine({ t.x - 4 * k, t.y }, { t.x - 1 * k, t.y + 4 * k }, kGold, 2.0f * k);
							dl->AddLine({ t.x - 1 * k, t.y + 4 * k }, { t.x + 5 * k, t.y - 4 * k }, kGold, 2.0f * k);
						}
						ty += 70 * k;
					}
					Divider(dl, tcx, ty + 2 * k, w * 0.30f, k, 0.6f);
					ty += 16 * k;
					const bool  bossUp = s.trialActive == 6;
					const ImU32 bc = bossUp ? IM_COL32(255, 92, 60, 255) : IM_COL32(170, 120, 104, 230);
					Txt(dl, g_title, 20 * k, { TL, ty }, bc, "THE BARROW-JARL: " + s.trialBoss, bossUp ? 1.5f * k : 0.0f, bc);
					TxtWrap(dl, g_body, 16 * k, { TL, ty + 30 * k }, IM_COL32(196, 182, 160, 235), s.trialBossLine, TR - TL, 2);
					y = ty + 88 * k;
				}
				// [5c] (Noah) the hidden quest's whispers: never told outright - a line more for each step found
				if (!s.whispers.empty()) {
					Divider(dl, cx, y, w * 0.30f, k, 0.6f);
					y += 14 * k;
					TxtC(dl, g_title, 21 * k, cx, y, kIce, "WHISPERS", 2.0f * k, kRune);
					y += 36 * k;
					for (const auto& wsp : s.whispers) {
						TxtWrap(dl, g_body, 16 * k, { L, y }, IM_COL32(190, 210, 240, 235), "\xE2\x80\x9C" + wsp + "\xE2\x80\x9D", Rr - L, 2);
						y += 46 * k;
					}
					y += 6 * k;
				}
				// [6] leave
				{
					Divider(dl, cx, y, w * 0.30f, k, 0.6f);
					y += 14 * k;
					focusRow(20, y - 6 * k, y + 34 * k);
					bullet({ L, y + 2 * k }, 20, true);
					Txt(dl, g_title, 20 * k, { L + 36 * k, y + 4 * k }, IM_COL32(210, 196, 170, 255), "LEAVE THE ALTAR");
					y += 44 * k;
					// (the classes overhaul) changing class here
					Txt(dl, g_body, 16 * k, { L, y }, IM_COL32(190, 182, 166, 230), std::string(g_padLast ? "Back" : "C") + "   Change class  (" + std::to_string(Classes::SwitchCost()) + " septims)  -  now " + std::string(Classes::Name(Classes::Get())));
					y += 26 * k;
					Txt(dl, g_body, 16 * k, { L, y }, IM_COL32(190, 182, 166, 230), std::string(g_padLast ? "R3" : "Q") + "   Fill your quivers  (" + std::to_string(400 + 100 * std::max(0, s.round)) + " septims)");
					y += 26 * k;
					if (const auto wl = Classes::WonderLine(); !wl.empty()) {  // (your class's wonder weapon: its trial)
						TxtGold(dl, g_body, 16 * k, { L, y }, wl);
						y += 26 * k;
					}
				}
				dl->PopClipRect();
				// keep the row in focus in view (next frame)
				// (focusY0/1: the focused row in the content's own space; on screen it stands at focusY - scroll)
				if (focusY1 - scrollGoal > b.y - 80 * k) scrollGoal = focusY1 - (b.y - 80 * k);
				if (focusY0 - scrollGoal < a.y + 20 * k) scrollGoal = focusY0 - (a.y + 20 * k);
				if (contentH - h > 1.0f) {  // a thin bar: how far down you are
					const float track = h - 90 * k, frac = std::clamp(scroll / (contentH - h), 0.0f, 1.0f);
					const float bh = std::max(30 * k, track * h / contentH);
					dl->AddRectFilled({ b.x - 9 * k, a.y + 20 * k + (track - bh) * frac }, { b.x - 5 * k, a.y + 20 * k + (track - bh) * frac + bh }, Fade(kGold, 0.6f), 2 * k);
				}
				Divider(dl, cx, b.y - 58 * k, w * 0.38f, k, 0.7f);
				{
					const bool        ps = Settings::PlayStationIcons();
					const std::string hint = g_padLast ? std::string("D-pad: move   \xC2\xB7   ") + (ps ? "Cross" : "A") + ": select   \xC2\xB7   " + (ps ? "Circle" : "B") + ": leave"
					                                   : std::string("W S: move   \xC2\xB7   A D: choose a perk   \xC2\xB7   Enter: select");
					TxtC(dl, g_body, 16 * k, cx, b.y - 44 * k, IM_COL32(200, 186, 160, 225), hint);
				}
			} else if (s.rollPhase > 0) {
				// the roll with the menu closed: a small strip under the crosshair
				const float R = 30 * k;
				const ImVec2 c{ size.x * 0.5f - 90 * k, size.y * 0.5f + 150 * k };
				rollOrb(c, R);
				const std::string t = s.rollPhase == 1 ? "The shrine gathers..." : s.rollPhase == 2 ? "The spells turn..." : ArmsName(s.rollGun) + "  -  take it at the shrine";
				Txt(dl, g_title, 19 * k, { c.x + R + 12 * k, c.y - 12 * k }, s.rollPhase == 3 ? spellCol(s.rollGun, 1.0f) : kParch, t, 1.5f * k, IM_COL32(0, 0, 0, 255));
			}
		}

		// ---- main menu and party panel ----
		// The menu cursor: Skyrim's own, and when Skyrim stops moving it (a controller was used, so it thinks the pad
		// has the menu) the mouse's own movement carries it on, so the mouse is never stuck
		std::atomic<float> g_mouseDX{ 0 }, g_mouseDY{ 0 };
		ImVec2             g_cursor{ -1, -1 };
		std::atomic<bool>  g_partyOpen{ false };  // the Party window over the Esc journal
		std::atomic<int>   g_partyFocus{ 0 }, g_partyPress{ 0 };  // (Noah: the party window by D-pad / arrows) the button in focus, and A / Enter pressed
		ImVec2             g_skCursor{ -1, -1 };  // exactly where Skyrim's own menu cursor is (its menus take the clicks there)
		void UpdateCursor(ImVec2 a_size)
		{
			auto* mc = RE::MenuCursor::GetSingleton();
			const float dx = g_mouseDX.exchange(0), dy = g_mouseDY.exchange(0);
			if (!mc) {
				g_cursor = { -1, -1 };
				return;
			}
			const auto& r = mc->GetRuntimeData();
			const float sx = r.screenWidthX > 1 ? a_size.x / r.screenWidthX : 1.0f, sy = r.screenWidthY > 1 ? a_size.y / r.screenWidthY : 1.0f;
			const ImVec2 sk{ r.cursorPosX * sx, r.cursorPosY * sy };
			static ImVec2 lastSk{ -1, -1 };
			if (std::abs(sk.x - lastSk.x) + std::abs(sk.y - lastSk.y) > 0.5f || g_cursor.x < 0) {
				g_cursor = sk;  // Skyrim moved it: follow
			} else if (dx != 0 || dy != 0) {
				g_cursor.x = std::clamp(g_cursor.x + dx * a_size.y / 1080.0f, 0.0f, a_size.x);
				g_cursor.y = std::clamp(g_cursor.y + dy * a_size.y / 1080.0f, 0.0f, a_size.y);
			}
			lastSk = sk;
			g_skCursor = sk;
		}
		ImVec2 Cursor(ImVec2) { return g_cursor; }

		// Our cursor, in the quest ping's style (the burning diamond over quest targets): a tall diamond tipped up and
		// to the left so its point is the hotspot, a dark heart, an amber rim, a soft glow round it and a hot core near
		// the tip that breathes - on the main menu and in every Skyrim menu with a cursor.
		void DrawPingCursor(ImDrawList* a_dl, ImVec2 a_tip, float k, float a_now)
		{
			if (g_cursorTex) {  // the Moonlit Spirit cursor (an animated Windows cursor: 8 frames played 0..7..0, 9/60 s each)
				static constexpr int kSeq[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 1, 0 };
				const int   f = kSeq[static_cast<int>(a_now * 60.0f / 9.0f) % 16];
				const float S = 56.0f * k;                              // drawn size (the art is 128 px)
				const ImVec2 at{ a_tip.x - 44.0f / 128.0f * S, a_tip.y - 30.0f / 128.0f * S };  // its hotspot on the point
				a_dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_cursorTex)), at, { at.x + S, at.y + S }, { f / 8.0f, 0.0f }, { (f + 1) / 8.0f, 1.0f });
				return;
			}
			const ImU32 col = IM_COL32(236, 168, 72, 255), dark = IM_COL32(20, 12, 8, 235);
			const float pulse = 0.75f + 0.25f * std::sin(a_now * 3.0f);
			const float R = 12.0f * k, ca = std::cos(-0.55f), sa = std::sin(-0.55f);
			const ImVec2 mid{ a_tip.x - sa * R, a_tip.y + ca * R };  // the diamond's centre, one half-height down its axis
			auto at = [&](float x, float y) { return ImVec2{ mid.x + x * ca - y * sa, mid.y + x * sa + y * ca }; };
			auto diamond = [&](float r, float w, ImU32 c, bool fill, float th) {
				const ImVec2 a = at(0, -r), b = at(w * r, 0), cc = at(0, r), d = at(-w * r, 0);
				if (fill) a_dl->AddQuadFilled(a, b, cc, d, c);
				else a_dl->AddQuad(a, b, cc, d, c, th);
			};
			for (int i = 3; i >= 1; --i) {
				diamond(R * (1.0f + 0.35f * i), 0.62f, Fade(col, 0.07f * pulse), true, 0);
			}
			diamond(R, 0.62f, dark, true, 0);
			diamond(R, 0.62f, col, false, 2.0f * k);
			const float c = R * 0.38f;
			const ImVec2 hc = at(0, -R * 0.42f);  // the hot core, up toward the point
			a_dl->AddQuadFilled({ hc.x - sa * -c, hc.y + ca * -c }, { hc.x + ca * c * 0.62f, hc.y + sa * c * 0.62f }, { hc.x - sa * c, hc.y + ca * c }, { hc.x - ca * c * 0.62f, hc.y - sa * c * 0.62f }, Fade(IM_COL32(255, 210, 120, 255), pulse));
			a_dl->AddCircleFilled(a_tip, 1.6f * k, IM_COL32(255, 236, 190, 255), 8);  // the exact point
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

		// (Noah) the Party tab, in the Tab menu's empty top-left corner: host a Steam party and invite, or join a friend's;
		// who is in the lobby with each one's night (kills, headshots, best streak, septims, revives); last night's score
		// with the party's MVP; and your character's records. Alone (or without Steam) it shows just you.
		// a: the panel's top-left corner
		void PartyPanel(ImDrawList* dl, ImVec2 size, ImVec2 a, float k, bool click)
		{
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
			const float w = 600 * k, rowH = 40 * k;
			const int   buttons = !net ? 0 : Net::InLobby() ? (Net::IsHost() ? 2 : 1) : 1 + static_cast<int>(std::min<std::size_t>(friends.size(), 3));
			const int   lines = 1 + static_cast<int>(party.size());
			const float h = 70 * k + buttons * (rowH + 6 * k) + 40 * k + lines * 26 * k + 30 * k + 64 * k + 30 * k + 82 * k + 20 * k + 56 * k;
			const ImVec2 b{ a.x + w, std::min(a.y + h, size.y - 10 * k) };
			Panel(dl, a, b, k, 0.95f);
			float y = a.y + 14 * k;
			const float L = a.x + 20 * k;
			Txt(dl, g_title, 26 * k, { L, y }, kIce, "PARTY", 2 * k, kRune);
			y += 34 * k;
			const std::string st = !net ? "Solo" : Net::Status() + (Net::InLobby() ? "   (" + std::to_string(Net::Members() + 1) + " in party)" : "");
			Txt(dl, g_body, 15 * k, { L, y }, IM_COL32(200, 214, 235, 235), st);
			y += 26 * k;
			// (Noah: by controller) D-pad up / down (or the arrow keys) moves the focus through the buttons, A / Enter presses it
			const int focus = buttons > 0 ? std::clamp(g_partyFocus.load(), 0, buttons - 1) : 0;
			g_partyFocus = focus;
			const bool press = g_partyPress.exchange(0) > 0;
			int        rowIdx = 0;
			auto row = [&](const std::string& label, ImU32 accent = kGold) {
				const ImVec2 ra{ L, y }, rb{ b.x - 20 * k, y + rowH };
				bool hit = Button(dl, ra, rb, label, cur, click, k, accent);
				if (rowIdx == focus) {
					dl->AddRect({ ra.x - 3 * k, ra.y - 3 * k }, { rb.x + 3 * k, rb.y + 3 * k }, kRune, 4 * k, 0, 2.0f * k);
					dl->AddTriangleFilled({ ra.x - 12 * k, (ra.y + rb.y) * 0.5f - 7 * k }, { ra.x - 12 * k, (ra.y + rb.y) * 0.5f + 7 * k }, { ra.x - 5 * k, (ra.y + rb.y) * 0.5f }, kRune);
					hit = hit || press;
				}
				++rowIdx;
				y += rowH + 6 * k;
				return hit;
			};
			if (net) {
				if (!Net::InLobby()) {
					if (row("HOST A PARTY  -  INVITE FRIENDS")) Net::HostLobby();
					for (std::size_t i = 0; i < friends.size() && i < 3; ++i) {
						if (row("JOIN " + friends[i].name, IM_COL32(120, 200, 255, 255))) Net::JoinLobby(friends[i].lobby);
					}
				} else if (Net::IsHost()) {
					if (row("INVITE FRIENDS (STEAM)")) Net::InviteFriends();
					if (row("CLOSE THE PARTY", IM_COL32(220, 90, 70, 255))) Net::LeaveLobby();
				} else if (row("LEAVE " + Net::HostName() + "'S PARTY", IM_COL32(220, 90, 70, 255))) {
					Net::LeaveLobby();
				}
			}
			// the lobby, each with their night
			y += 6 * k;
			const float cols[6] = { L, L + 210 * k, L + 285 * k, L + 360 * k, L + 435 * k, L + 520 * k };
			const char* heads[6] = { me.night > 0 ? "TONIGHT" : "PLAYER", "KILLS", "HEADS", "STREAK", "SEPTIMS", "REVIVES" };
			for (int c = 0; c < 6; ++c) Txt(dl, g_title, 13 * k, { cols[c], y }, Fade(kRune, 0.9f), heads[c]);
			y += 22 * k;
			dl->AddLine({ L, y - 3 * k }, { b.x - 20 * k, y - 3 * k }, kRuneDim, 1.0f * k);
			auto line = [&](const std::string& who, const Stats::Line& v, ImU32 col) {
				const std::string vals[6] = { who, std::to_string(v.kills), std::to_string(v.heads), std::to_string(v.best), std::to_string(v.septims), std::to_string(v.revives) };
				for (int c = 0; c < 6; ++c) Txt(dl, g_body, 16 * k, { cols[c], y }, col, vals[c]);
				y += 26 * k;
			};
			auto* pl = RE::PlayerCharacter::GetSingleton();
			line(pl && pl->GetName() ? std::string(pl->GetName()) + " (you)" : "You", me, kIce);
			for (const auto& f : party) line(f.name, f.line, IM_COL32(200, 214, 235, 235));
			// last night
			y += 10 * k;
			Txt(dl, g_title, 15 * k, { L, y }, Fade(kRune, 0.95f), last.night > 0 ? fmt::format("LAST NIGHT - NIGHT {}", last.night) : std::string("LAST NIGHT"));
			y += 22 * k;
			if (last.night > 0) {
				Txt(dl, g_body, 15 * k, { L, y }, IM_COL32(220, 230, 245, 235), fmt::format("{} kills   {} headshots   best streak {}   {} septims", last.kills, last.heads, last.best, last.septims));
				y += 22 * k;
				std::string mvp = pl && pl->GetName() ? pl->GetName() : "You";
				int         best = last.kills * 2 + last.heads + last.style / 50;
				for (const auto& f : party) {
					const int sc = f.line.kills * 2 + f.line.heads + f.line.style / 50;
					if (sc > best) best = sc, mvp = f.name;
				}
				Txt(dl, g_body, 15 * k, { L, y }, IM_COL32(220, 230, 245, 235), fmt::format("style {}   waves {}   mini-bosses {}{}", last.style, last.waves, last.minis, party.empty() ? std::string() : "   MVP: " + mvp));
				y += 22 * k;
			} else {
				Txt(dl, g_body, 15 * k, { L, y }, IM_COL32(170, 185, 210, 220), "Survive a night and its score shows here.");
				y += 44 * k;
			}
			// records
			y += 8 * k;
			Txt(dl, g_title, 15 * k, { L, y }, Fade(kRune, 0.95f), "RECORDS");
			y += 22 * k;
			Txt(dl, g_body, 15 * k, { L, y }, IM_COL32(220, 230, 245, 235), fmt::format("best night {}   nights survived {}   kills {}   headshots {}", rec.bestNight, rec.nights, rec.kills, rec.heads));
			y += 22 * k;
			Txt(dl, g_body, 15 * k, { L, y }, IM_COL32(220, 230, 245, 235), fmt::format("best streak {}   mini-bosses {}   wonders forged {}   hidden quests {}", rec.bestStreak, rec.minis, rec.wonders, rec.quests));
			// the keys, both ways
			y += 30 * k;
			dl->AddLine({ L, y - 6 * k }, { b.x - 20 * k, y - 6 * k }, kRuneDim, 1.0f * k);
			const bool ps = Settings::PlayStationIcons();
			Txt(dl, g_body, 14 * k, { L, y }, IM_COL32(170, 190, 220, 230), "Keyboard:  P  open / close     Up / Down  move     Enter  select");
			Txt(dl, g_body, 14 * k, { L, y + 20 * k }, IM_COL32(170, 190, 220, 230), std::string("Controller:  ") + (ps ? "Triangle" : "Y") + "  open / close     D-pad  move     " + (ps ? "Cross" : "A") + "  select");
		}

		// The player's settings, in the System menu's corner: the field of view, first and third person. Click on a
		// bar to set it there, or the - / + either side to step it.
		// The settings strip: bottom-left, under the frames of the Tab menu and the Esc journal (System), two sliders
		// side by side so it never covers their lists.
		void SettingsPanel(ImDrawList* dl, ImVec2 size, float k, bool click)
		{
			const ImVec2 cur = Cursor(size);
			const float  w = 560 * k, h = 232 * k;  // narrow enough to stay left of Skyrim's level bar
			const ImVec2 a{ 48 * k, size.y - 14 * k - h }, b{ a.x + w, a.y + h };
			Panel(dl, a, b, k, 0.95f);
			Txt(dl, g_title, 21 * k, { a.x + 18 * k, a.y + 12 * k }, kGold, "THE CURSE OF ALDUIN - VIEW", 2 * k, IM_COL32(255, 160, 60, 255));
			const float colW = (w - 54 * k) / 2;
			for (int i = 0; i < 2; ++i) {
				const bool  first = i == 0;
				const float x0 = a.x + 18 * k + i * (colW + 18 * k), x1 = x0 + colW;
				const float y = a.y + 54 * k;
				const float v = first ? Settings::FovFirst() : Settings::FovThird();
				const std::string label = std::string(first ? "First person" : "Third person") + "   " + std::to_string(static_cast<int>(v));
				Txt(dl, g_body, 16 * k, { x0, y }, IM_COL32(220, 210, 190, 240), label);
				const float  bx0 = x0 + 32 * k, bx1 = x1 - 32 * k, by = y + 46 * k;
				const ImVec2 m0{ x0, by - 12 * k }, m1{ x0 + 22 * k, by + 12 * k }, p0{ x1 - 22 * k, by - 12 * k }, p1{ x1, by + 12 * k };
				dl->AddRectFilled({ bx0, by - 4 * k }, { bx1, by + 4 * k }, IM_COL32(30, 26, 22, 230), 4 * k);
				const float t = (v - Settings::kFovMin) / (Settings::kFovMax - Settings::kFovMin);
				dl->AddRectFilled({ bx0, by - 4 * k }, { bx0 + (bx1 - bx0) * t, by + 4 * k }, kGold, 4 * k);
				dl->AddCircleFilled({ bx0 + (bx1 - bx0) * t, by }, 9 * k, IM_COL32(250, 236, 200, 255));
				for (auto [q0, q1, sign] : { std::tuple{ m0, m1, -1.0f }, std::tuple{ p0, p1, 1.0f } }) {
					const bool hov = cur.x >= q0.x && cur.x <= q1.x && cur.y >= q0.y && cur.y <= q1.y;
					dl->AddRect(q0, q1, hov ? kGold : kGoldDim, 3 * k, 0, 1.5f * k);
					TxtC(dl, g_title, 18 * k, (q0.x + q1.x) / 2, q0.y + 1 * k, kParch, sign < 0 ? "-" : "+");
					if (hov && click) {
						Settings::SetFov(first, v + sign * 5.0f);
					}
				}
				if (click && cur.x >= bx0 - 6 * k && cur.x <= bx1 + 6 * k && std::abs(cur.y - by) < 14 * k) {
					Settings::SetFov(first, Settings::kFovMin + (Settings::kFovMax - Settings::kFovMin) * std::clamp((cur.x - bx0) / (bx1 - bx0), 0.0f, 1.0f));
				}
			}
			{  // (Noah) brightness, right under the field of view
				const float x0 = a.x + 18 * k, x1 = b.x - 18 * k, y = a.y + 140 * k;
				const float v = Settings::Brightness();
				char lab[48];
				std::snprintf(lab, sizeof(lab), "Brightness   %.2f", v);
				Txt(dl, g_body, 16 * k, { x0, y }, IM_COL32(220, 210, 190, 240), lab);
				const float  bx0 = x0 + 32 * k, bx1 = x1 - 32 * k, by = y + 46 * k;
				const ImVec2 m0{ x0, by - 12 * k }, m1{ x0 + 22 * k, by + 12 * k }, p0{ x1 - 22 * k, by - 12 * k }, p1{ x1, by + 12 * k };
				dl->AddRectFilled({ bx0, by - 4 * k }, { bx1, by + 4 * k }, IM_COL32(30, 26, 22, 230), 4 * k);
				const float t = (v - Settings::kBrightMin) / (Settings::kBrightMax - Settings::kBrightMin);
				dl->AddRectFilled({ bx0, by - 4 * k }, { bx0 + (bx1 - bx0) * t, by + 4 * k }, kGold, 4 * k);
				dl->AddCircleFilled({ bx0 + (bx1 - bx0) * t, by }, 9 * k, IM_COL32(250, 236, 200, 255));
				for (auto [q0, q1, sign] : { std::tuple{ m0, m1, -1.0f }, std::tuple{ p0, p1, 1.0f } }) {
					const bool hov = cur.x >= q0.x && cur.x <= q1.x && cur.y >= q0.y && cur.y <= q1.y;
					dl->AddRect(q0, q1, hov ? kGold : kGoldDim, 3 * k, 0, 1.5f * k);
					TxtC(dl, g_title, 18 * k, (q0.x + q1.x) / 2, q0.y + 1 * k, kParch, sign < 0 ? "-" : "+");
					if (hov && click) {
						Settings::SetBrightness(v + sign * 0.05f);
					}
				}
				if (click && cur.x >= bx0 - 6 * k && cur.x <= bx1 + 6 * k && std::abs(cur.y - by) < 14 * k) {
					Settings::SetBrightness(Settings::kBrightMin + (Settings::kBrightMax - Settings::kBrightMin) * std::clamp((cur.x - bx0) / (bx1 - bx0), 0.0f, 1.0f));
				}
			}
		}

		// ---- the main menu (Spec 13): the Curse of Alduin entrance
		// Layered art (tools/menuart.py) with parallax that follows the cursor and drifts slowly, fog in two depths,
		// the rune-stone's soul-fire and the ground ring pulsing, braziers flickering, embers and ash rising, now and
		// then a cold lightning flash; the title; Skyrim's own menu entries redrawn as our buttons (Skyrim keeps
		// doing the work: mouse, keyboard and pad act on its real list, we only draw over it).
		struct MenuEntry { std::string text; int index; bool sel, disabled; };
		// Skyrim's main list as it stands (its entries and which is selected); empty if it isn't up. Its own list is
		// moved off the screen while ours shows (the keys still drive it; our buttons drive it by mouse).
		// once each: the members of a menu's clip in the log (names, shown or not, where) - and on the main menu, any
		// stray badge of Skyrim's (a logo, an icon, a spinner) is hidden: ours is the whole menu
		void DumpMembers(RE::GFxMovieView* a_mv, const char* a_path, bool a_hideBadges)
		{
			RE::GFxValue obj;
			if (!a_mv || !a_mv->GetVariable(&obj, a_path) || !obj.IsObject()) {
				return;
			}
			obj.VisitMembers([&](const char* a_name, const RE::GFxValue& a_v) {
				if (!a_v.IsDisplayObject()) {
					return;
				}
				RE::GFxValue::DisplayInfo di;
				const_cast<RE::GFxValue&>(a_v).GetDisplayInfo(&di);
				std::string n = a_name ? a_name : "";
				std::string low = n;
				std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				const bool badge = a_hideBadges && (low.find("logo") != std::string::npos || low.find("icon") != std::string::npos || low.find("spinner") != std::string::npos ||
				                                    low.find("busy") != std::string::npos || low.find("bethesda") != std::string::npos || low.find("marketplace") != std::string::npos);
				logger::info("ui: {}.{} visible {} alpha {:.0f} at {:.0f},{:.0f}{}", a_path, n, di.GetVisible(), di.GetAlpha(), di.GetX(), di.GetY(), badge ? " (hidden)" : "");
				if (badge) {
					RE::GFxValue::DisplayInfo off;
					off.SetVisible(false);
					const_cast<RE::GFxValue&>(a_v).SetDisplayInfo(off);
				}
			});
		}

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
			if (static bool dumped = false; !dumped) {
				dumped = true;
				DumpMembers(mv, "_root.MenuHolder.Menu_mc", true);
				DumpMembers(mv, "_root.MenuHolder", true);
				DumpMembers(mv, "_root", true);
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

		// the main menu's painting (tools/menuscene.py): sky, cloud veil, aurora, far range, the barrow ridge, the front
		ID3D11ShaderResourceView* g_mSky = nullptr;
		ID3D11ShaderResourceView* g_mClouds = nullptr;
		ID3D11ShaderResourceView* g_mAurora = nullptr;
		ID3D11ShaderResourceView* g_mFar = nullptr;
		ID3D11ShaderResourceView* g_mMid = nullptr;
		ID3D11ShaderResourceView* g_mNear = nullptr;
		// the shrine (tools/menushrine.py): the floor, its light, the crystal turning (24 frames), the floating rocks
		ID3D11ShaderResourceView* g_sGround = nullptr;
		ID3D11ShaderResourceView* g_sGlow = nullptr;
		ID3D11ShaderResourceView* g_sCrystal = nullptr;
		ID3D11ShaderResourceView* g_sSoft = nullptr;
		std::array<ID3D11ShaderResourceView*, 4> g_sCrystalSheet{};  // 384 frames on four 12 x 8 sheets (menu_crystal.png, menu_crystal2..4.png)
		OrbTex MenuOrbTextures() { return { { g_sCrystalSheet[0] ? g_sCrystalSheet[0] : g_sCrystal, g_sCrystalSheet[1], g_sCrystalSheet[2], g_sCrystalSheet[3] }, g_sSoft }; }
		ID3D11ShaderResourceView* g_sMtn = nullptr;      // the shrine scene's mountains across the lake (tools/menulake.py)
		ID3D11ShaderResourceView* g_sRefl = nullptr;     // the mountains upside down in the lake
		ID3D11ShaderResourceView* g_sLake = nullptr;     // the water itself: the sky mirrored through its ripples, Fresnel-dark toward us (tools/menuwater.py)
		ID3D11ShaderResourceView* g_sAurora = nullptr;   // the northern lights
		ID3D11ShaderResourceView* g_sLife = nullptr;     // lily pads on the lake, reeds and bushes in the near corners
		ID3D11ShaderResourceView* g_sMotes = nullptr;    // motes rising from the shrine's pool: 16x8 flipbook (tools/orbfx.py)
		std::array<ID3D11ShaderResourceView*, 9> g_sShard{}, g_sShardR{};  // the rocks, and their runes' light (pulsing)
		ID3D11ShaderResourceView* g_sRockTurn = nullptr, * g_sRockTurnR = nullptr;
		std::array<ID3D11ShaderResourceView*, 3> g_sRockSheet{}, g_sRockSheetR{};  // (Noah: no after-images) 144 frames on three 8 x 6 sheets  // one rock turning (48 frames, 8 x 6) and its runes' light
		ID3D11ShaderResourceView* g_sRuneCircle = nullptr;  // the freed altar's golden rune circle (no longer drawn: Noah)
		ID3D11ShaderResourceView* g_mGroundFog = nullptr;
		ID3D11ShaderResourceView* g_mClouds2 = nullptr;    // drifting night clouds that tile left-right (tools/menuscene.py drift_clouds)  // fog wisps that tile left-right (tools/menuscene.py ground_fog)
		constexpr float kMenuRune[2] = { 1812.0f, 992.0f };  // the middle of the rune-stone's face in the painting
		constexpr float kMenuArch[2] = { 1176.0f, 455.0f };  // the barrow door
		ID3D11ShaderResourceView* g_mTitle = nullptr;

		float g_pressT = -10, g_pressY = 0;  // the main menu entry last pressed (its flare)
		// Skyrim's question box on the main menu (Start a new game?): where it sits on screen, from its own bounds
		bool MenuQuestionBounds(ImVec2 a_size, ImVec4& a_out)
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
			RE::GFxMovieView* mv = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue      conf, root;
			if (!mv || !mv->GetVariable(&conf, "_root.MenuHolder.Menu_mc.ConfirmPanel_mc") || !conf.IsDisplayObject() || !mv->GetVariable(&root, "_root")) {
				return false;
			}
			RE::GFxValue::DisplayInfo ci;
			conf.GetDisplayInfo(&ci);
			if (!ci.GetVisible() || ci.GetAlpha() <= 20.0) {
				return false;
			}
			// the union of its visible parts (the panel itself also holds a full-height backdrop we don't want)
			float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
			static bool listed = false;
			conf.VisitMembers([&](const char* a_name, const RE::GFxValue& a_val) {
				if (!a_val.IsDisplayObject()) {
					return;
				}
				RE::GFxValue::DisplayInfo di;
				const_cast<RE::GFxValue&>(a_val).GetDisplayInfo(&di);
				RE::GFxValue b;
				if (!di.GetVisible() || di.GetAlpha() < 10.0 || !const_cast<RE::GFxValue&>(a_val).Invoke("getBounds", &b, &root, 1) || !b.IsObject()) {
					return;
				}
				auto num = [&](const char* n) { RE::GFxValue v; return b.GetMember(n, &v) && v.IsNumber() ? static_cast<float>(v.GetNumber()) : 0.0f; };
				const float bx0 = num("xMin"), bx1 = num("xMax"), by0 = num("yMin"), by1 = num("yMax");
				if (!listed) {
					logger::info("menu: question part '{}' ({:.0f},{:.0f})-({:.0f},{:.0f})", a_name, bx0, by0, bx1, by1);
				}
				if (by1 - by0 > 400 || bx1 <= bx0) {
					return;  // the full-height backdrop
				}
				x0 = std::min(x0, bx0), y0 = std::min(y0, by0), x1 = std::max(x1, bx1), y1 = std::max(y1, by1);
			});
			listed = true;
			const auto vis = mv->GetVisibleFrameRect();
			RE::GViewport vp;
			mv->GetViewport(&vp);
			const float vw = vp.width > 0 ? static_cast<float>(vp.width) : a_size.x, vh = vp.height > 0 ? static_cast<float>(vp.height) : a_size.y;
			const float sx = vw / std::max(1.0f, vis.right - vis.left), sy = vh / std::max(1.0f, vis.bottom - vis.top);
			a_out = { vp.left + (x0 - vis.left) * sx, vp.top + (y0 - vis.top) * sy, vp.left + (x1 - vis.left) * sx, vp.top + (y1 - vis.top) * sy };
			static int said = 0;
			if (said++ < 2) {
				logger::info("menu: question box bounds ({:.0f},{:.0f})-({:.0f},{:.0f}) stage, ({:.0f},{:.0f})-({:.0f},{:.0f}) screen", x0, y0, x1, y1, a_out.x, a_out.y, a_out.z, a_out.w);
			}
			return x1 > x0 && y1 > y0;
		}

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

		// ---- the main menu's Load (Noah: Skyrim's own list was cut off - ours shows every save, in our style)
		struct SaveRow
		{
			std::string   file, name, place, played, date;
			int           level = 0;
			bool          autosave = false;
			std::uint64_t when = 0;
		};
		std::mutex           g_saveLock;
		std::vector<SaveRow> g_saves;
		std::atomic<bool>    g_savesReady{ false };
		void RequestSaves()
		{
			g_savesReady = false;
			Tasks::Add([] {
				std::vector<SaveRow> out;
				if (auto* sl = RE::BGSSaveLoadManager::GetSingleton()) {
					sl->PopulateSaveList();
					for (auto* e : sl->saveGameList) {
						if (!e) continue;
						if (!e->loaded) e->PopulateFileEntryData();
						SaveRow r;
						r.file = e->fileName.c_str() ? e->fileName.c_str() : "";
						if (r.file.empty()) continue;
						r.name = e->characterName.c_str() ? e->characterName.c_str() : "";
						r.place = e->currentLocation.c_str() ? e->currentLocation.c_str() : "";
						r.played = e->playTime.c_str() ? e->playTime.c_str() : "";
						r.level = static_cast<int>(e->characterLevel);
						r.autosave = e->saveType.any(RE::BGSSaveLoadFileEntry::SaveType::kAutosave, RE::BGSSaveLoadFileEntry::SaveType::kQuicksave) ||
						             r.file.rfind("Autosave", 0) == 0 || r.file.rfind("Quicksave", 0) == 0;
						const auto& ft = e->fileTime;
						r.when = (static_cast<std::uint64_t>(ft.hi) << 32) | ft.lo;
						if (r.when > 116444736000000000ull) {
							const std::time_t t = static_cast<std::time_t>((r.when - 116444736000000000ull) / 10000000ull);
							std::tm lt{};
							localtime_s(&lt, &t);
							char buf[64];
							std::strftime(buf, sizeof(buf), "%b %d, %Y  %I:%M %p", &lt);
							r.date = buf;
						}
						out.push_back(std::move(r));
					}
				}
				std::sort(out.begin(), out.end(), [](const SaveRow& a, const SaveRow& b) { return a.when > b.when; });
				logger::info("menu: {} saves listed for our Load", out.size());
				std::scoped_lock l(g_saveLock);
				g_saves = std::move(out);
				g_savesReady = true;
			});
		}
		void LoadSave(const std::string& a_file)
		{
			logger::info("menu: loading '{}'", a_file);
			Tasks::Add([a_file] {
				if (auto* sl = RE::BGSSaveLoadManager::GetSingleton()) sl->Load(a_file.c_str());
			});
		}
		// the panel: every save, newest first - its character, level, where, when and how long played; scrolls with the
		// wheel, the d-pad / arrows; click or A / Enter loads it, B / Esc (or Back) returns. Returns false to go back.
		bool LoadPanel(ImDrawList* fg, ImVec2 size, float k, float now, ImVec2 cur, bool click, int moves, int accepts, int backs, int wheel, float dt)
		{
			static int   sel = 0;
			static float scroll = 0;
			static bool  loading = false;
			static ImVec2 lastCur{ -1, -1 };
			static bool  mouseOn = true;
			const float right = size.x - 90 * k, left = right - 760 * k;
			const float top = size.y * 0.17f, bottom = size.y * 0.86f;
			Txt(fg, g_title, 26 * k, { right - Measure(g_title, 26 * k, "LOAD").x, top - 56 * k }, kGold, "LOAD", 2 * k, IM_COL32(255, 160, 60, 255));
			// the panel
			Panel(fg, { left, top }, { right, bottom }, k, 0.92f);
			if (!g_savesReady) {
				TxtC(fg, g_body, 20 * k, (left + right) * 0.5f, (top + bottom) * 0.5f - 10 * k, IM_COL32(200, 194, 180, 220), "Reading your saves...");
			}
			std::vector<SaveRow> saves;
			{
				std::scoped_lock l(g_saveLock);
				saves = g_saves;
			}
			const int   n = g_savesReady ? static_cast<int>(saves.size()) : 0;
			const float rowH = 74 * k, listTop = top + 18 * k, listBot = bottom - 70 * k;
			const int   fits = std::max(1, static_cast<int>((listBot - listTop) / rowH));
			if (std::abs(cur.x - lastCur.x) + std::abs(cur.y - lastCur.y) > 3.0f) mouseOn = true;
			lastCur = cur;
			if (moves || accepts) mouseOn = moves ? false : mouseOn;
			if (n > 0) {
				sel = std::clamp(sel + moves + wheel, 0, n - 1);
			}
			if (g_savesReady && n == 0) {
				TxtC(fg, g_body, 20 * k, (left + right) * 0.5f, (top + bottom) * 0.5f - 10 * k, IM_COL32(200, 194, 180, 220), "No saves yet - start a New game");
			}
			// keep the chosen one in view (eased)
			const float maxScroll = std::max(0.0f, static_cast<float>(n - fits));
			float want = std::clamp(scroll, static_cast<float>(sel - fits + 1), static_cast<float>(sel));
			want = std::clamp(want, 0.0f, maxScroll);
			scroll += (want - scroll) * std::min(1.0f, dt * 14.0f);
			int hovered = -1;
			fg->PushClipRect({ left, listTop - 2 * k }, { right, listBot + 2 * k }, true);
			for (int i = std::max(0, static_cast<int>(scroll) - 1); i < std::min(n, static_cast<int>(scroll) + fits + 2); ++i) {
				const auto&  r = saves[i];
				const float  y = listTop + (i - scroll) * rowH;
				const ImVec2 a{ left + 14 * k, y + 3 * k }, b{ right - 22 * k, y + rowH - 3 * k };
				const bool   over = cur.x >= a.x && cur.x <= b.x && cur.y >= a.y && cur.y <= b.y && cur.y >= listTop && cur.y <= listBot;
				if (over && mouseOn) hovered = i;
				const bool lit = i == sel;
				if (lit) {
					const float pl = 0.75f + 0.25f * std::sin(now * 3.0f);
					fg->AddRectFilledMultiColor(a, b, IM_COL32(255, 160, 60, static_cast<int>(70 * pl)), IM_COL32(120, 60, 20, 10), IM_COL32(120, 60, 20, 10), IM_COL32(255, 160, 60, static_cast<int>(70 * pl)));
					fg->AddRect(a, b, Fade(kGold, 0.8f), 2 * k, 0, 1.3f * k);
				} else {
					fg->AddLine({ a.x + 10 * k, b.y + 2 * k }, { b.x - 10 * k, b.y + 2 * k }, IM_COL32(200, 160, 92, 40), 1.0f * k);
				}
				const std::string nm = r.name.empty() ? std::string("Unknown") : r.name;
				Txt(fg, g_title, 22 * k, { a.x + 14 * k, a.y + 8 * k }, lit ? kParch : IM_COL32(220, 210, 190, 235), nm);
				std::string line = "Level " + std::to_string(r.level) + (r.place.empty() ? "" : "   -   " + r.place);
				Txt(fg, g_body, 16 * k, { a.x + 14 * k, a.y + 38 * k }, IM_COL32(190, 180, 160, 220), line);
				// right: when, how long played, and the kind of save
				const std::string tag = r.autosave ? "AUTOSAVE" : "SAVE";
				Txt(fg, g_body, 11 * k, { b.x - 12 * k - Measure(g_body, 11 * k, tag).x, a.y + 8 * k }, r.autosave ? IM_COL32(150, 170, 200, 200) : Fade(kGold, 0.8f), tag);
				Txt(fg, g_num, 15 * k, { b.x - 12 * k - Measure(g_num, 15 * k, r.date).x, a.y + 24 * k }, IM_COL32(220, 200, 160, 230), r.date);
				if (!r.played.empty()) {
					const std::string pt = "Played " + r.played;
					Txt(fg, g_body, 14 * k, { b.x - 12 * k - Measure(g_body, 14 * k, pt).x, a.y + 44 * k }, IM_COL32(170, 162, 145, 210), pt);
				}
			}
			fg->PopClipRect();
			if (hovered >= 0) sel = hovered;
			// the scroll bar
			if (n > fits) {
				const float th = (listBot - listTop) * fits / n, ty = listTop + (listBot - listTop - th) * (scroll / std::max(1.0f, maxScroll));
				fg->AddRectFilled({ right - 14 * k, listTop }, { right - 9 * k, listBot }, IM_COL32(40, 34, 28, 160), 3 * k);
				fg->AddRectFilled({ right - 14 * k, ty }, { right - 9 * k, ty + th }, Fade(kGold, 0.8f), 3 * k);
				const std::string cnt = std::to_string(sel + 1) + " / " + std::to_string(n);
				Txt(fg, g_num, 14 * k, { left + 18 * k, bottom - 54 * k }, kGoldDim, cnt);
			}
			// the keys, and Back
			const std::string hint = g_padLast ? "A  Load      B  Back      D-pad  Choose" : "Click or Enter to load      Esc to go back      Wheel / arrows to choose";
			Txt(fg, g_body, 15 * k, { left + 18 * k + (n > fits ? 90 * k : 0.0f), bottom - 50 * k }, IM_COL32(190, 182, 166, 220), hint);
			const bool back = Button(fg, { right - 170 * k, bottom - 60 * k }, { right - 22 * k, bottom - 18 * k }, "Back", cur, click, k);
			if (back || backs > 0) {
				return false;
			}
			if (!loading && n > 0 && ((click && hovered >= 0) || accepts > 0)) {
				loading = true;
				LoadSave(saves[sel].file);
			}
			if (loading) {
				TxtC(fg, g_title, 22 * k, (left + right) * 0.5f, bottom + 14 * k, kGold, "Loading...");
			}
			return true;
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
			// parallax: the cursor leans the world a little, and it breathes on its own; near layers move more
			const ImVec2 cur = Cursor(size);
			static ImVec2 lean{ 0, 0 };
			const ImVec2 want = cur.x >= 0 ? ImVec2{ std::clamp(cur.x / size.x - 0.5f, -0.5f, 0.5f), std::clamp(cur.y / size.y - 0.5f, -0.5f, 0.5f) } : ImVec2{ 0, 0 };
			lean.x += (want.x - lean.x) * std::min(1.0f, dt * 1.5f);
			lean.y += (want.y - lean.y) * std::min(1.0f, dt * 1.5f);
			const ImVec2 drift{ std::sin(now * 0.05f) * 0.5f, std::cos(now * 0.037f) * 0.5f };
			float cw = size.x, ch = size.x * 9.0f / 16.0f;  // the painting covers the screen (16:9)
			if (ch < size.y) {
				ch = size.y, cw = size.y * 16.0f / 9.0f;
			}
			const float push = 1.0f + 0.01f * (0.5f + 0.5f * std::sin(now * 0.035f));  // a very slow breath in and out
			fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, 255));
			// one layer of the painting: its own depth decides how far it leans with the cursor
			auto layer = [&](ID3D11ShaderResourceView* t, float depth, ImU32 tint, ImVec2 uvShift = { 0, 0 }) -> ImVec2 {
				const float over = 1.04f + 0.03f * depth;
				const float w = cw * over * push, h = ch * over * push;
				const ImVec2 o{ (size.x - w) / 2 - (lean.x + drift.x * 0.6f) * w * 0.012f * depth, (size.y - h) / 2 - (lean.y + drift.y * 0.6f) * h * 0.008f * depth };
				if (t) {
					fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(t)), o, { o.x + w, o.y + h }, uvShift, { 1 + uvShift.x, 1 + uvShift.y }, tint);
				}
				return o;
			};
			auto toScreen = [&](float depth, float px, float py) {  // a point of the 2560x1440 painting in a layer of that depth
				const float over = 1.04f + 0.03f * depth;
				const float w = cw * over * push, h = ch * over * push;
				const ImVec2 o{ (size.x - w) / 2 - (lean.x + drift.x * 0.6f) * w * 0.012f * depth, (size.y - h) / 2 - (lean.y + drift.y * 0.6f) * h * 0.008f * depth };
				return ImVec2{ o.x + px / 2560.0f * w, o.y + py / 1440.0f * h };
			};
			const ImU32 white = IM_COL32(255, 255, 255, static_cast<int>(255 * fade));
			if (!g_mSky && g_menuBg) {  // (the painting is missing: the old single picture)
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_menuBg)), { (size.x - cw) / 2, (size.y - ch) / 2 }, { (size.x + cw) / 2, (size.y + ch) / 2 });
			}
			// the sky: stars and the Blood Moon, its glow breathing; the aurora shimmering; a cloud veil sliding past
			layer(g_mSky, 0.2f, white);
			{
				const ImVec2 m = toScreen(0.2f, 0.255f * 2560, 0.315f * 1440);
				const float  br = 0.5f + 0.5f * std::sin(now * 0.6f);
				fg->AddCircleFilled(m, 62 * k, IM_COL32(255, 110, 80, static_cast<int>((3 + 3 * br) * fade)), 48);  // (a faint breath only)
			}
			// the brightest stars twinkle, and now and then a shooting star crosses the sky
			struct Star { float x, y, ph, sp, sz; };
			static std::vector<Star> stars;  // (the lake shows them again, upside down)
			{
				static std::mt19937 sr{ 23 };
				std::uniform_real_distribution<float> U(0, 1);
				if (stars.empty()) {
					while (stars.size() < 46) {
						const float x = U(sr), y = 0.02f + 0.40f * U(sr) * U(sr);
						if (std::hypot((x - 0.255f) * 16.0f / 9.0f, y - 0.315f) < 0.16f || (x > 0.2f && x < 0.8f && y < 0.2f)) {
							continue;  // clear of the moon and the title
						}
						stars.push_back({ x, y, U(sr) * 6.28f, 0.8f + 2.6f * U(sr), 0.7f + 1.3f * U(sr) * U(sr) });
					}
				}
				for (const auto& st : stars) {
					const ImVec2 p = toScreen(0.2f, st.x * 2560, st.y * 1440);
					const float  tw = std::pow(0.5f + 0.5f * std::sin(now * st.sp + st.ph), 3.0f);
					const float  r = (1.2f + 2.6f * st.sz * tw) * k;
					const int    al = static_cast<int>((60 + 170 * tw) * fade);
					fg->AddLine({ p.x - r * 2.2f, p.y }, { p.x + r * 2.2f, p.y }, IM_COL32(205, 220, 255, al / 2), 1.0f * k);
					fg->AddLine({ p.x, p.y - r * 2.2f }, { p.x, p.y + r * 2.2f }, IM_COL32(205, 220, 255, al / 2), 1.0f * k);
					fg->AddCircleFilled(p, std::max(0.8f * k, r * 0.45f), IM_COL32(235, 240, 255, al), 8);
				}
				// now and then a shooting star: a long, slightly curving streak - a white-hot head in a blue bloom, a tail
				// that tapers and cools behind it, a few sparks shed along the way
				static float  nextShoot = 5.0f, shootT = -1.0f, life = 1.2f;
				static ImVec2 s0{}, sd{};
				static float  bend = 0;
				if (shootT < 0 && now > nextShoot) {
					shootT = 0;
					const float dir = U(sr) < 0.5f ? 1.0f : -1.0f;
					s0 = { size.x * (dir > 0 ? 0.08f + 0.35f * U(sr) : 0.55f + 0.35f * U(sr)), size.y * (0.03f + 0.14f * U(sr)) };
					sd = { dir * size.x * (0.30f + 0.15f * U(sr)), size.y * (0.12f + 0.08f * U(sr)) };
					bend = (U(sr) - 0.5f) * 0.12f;
					life = 1.1f + 0.5f * U(sr);
					nextShoot = now + 8.0f + 9.0f * U(sr);
				}
				if (shootT >= 0) {
					shootT += dt;
					const float t = shootT / life;
					if (t >= 1.0f) {
						shootT = -1;
					} else {
						auto path = [&](float f) {  // along the streak, 0..1, bowing a little
							return ImVec2{ s0.x + sd.x * f - sd.y * bend * std::sin(f * 3.1416f), s0.y + sd.y * f + sd.x * bend * std::sin(f * 3.1416f) * 0.3f };
						};
						const float head = 1.0f - (1.0f - t) * (1.0f - t);                    // fast in, easing out
						const float bright = std::min(1.0f, t * 6.0f) * std::min(1.0f, (1.0f - t) * 2.5f) * fade;
						const float tail = 0.28f * std::min(1.0f, t * 3.0f);                 // the tail grows out behind
						constexpr int kSeg = 48;
						for (int i = 0; i < kSeg; ++i) {
							const float f0 = head - tail * i / kSeg, f1 = head - tail * (i + 1) / kSeg;
							if (f1 < 0) {
								break;
							}
							const float w = 1.0f - static_cast<float>(i) / kSeg, w2 = w * w;
							const ImVec2 p0 = path(f0), p1 = path(f1);
							fg->AddLine(p0, p1, IM_COL32(90, 140, 255, static_cast<int>(45 * w2 * bright)), (9.0f * w + 2) * k);     // the blue glow
							fg->AddLine(p0, p1, IM_COL32(170, 205, 255, static_cast<int>(120 * w2 * bright)), (3.4f * w + 0.8f) * k);
							fg->AddLine(p0, p1, IM_COL32(255, 250, 240, static_cast<int>(255 * w2 * w * bright)), (1.6f * w + 0.4f) * k);  // the hot core
						}
						const ImVec2 h = path(head);
						if (g_sSoft) {
							const auto st = ImTextureID(reinterpret_cast<std::uintptr_t>(g_sSoft));
							const float r1 = 26 * k, r2 = 9 * k;
							fg->AddImage(st, { h.x - r1, h.y - r1 }, { h.x + r1, h.y + r1 }, { 0, 0 }, { 1, 1 }, IM_COL32(120, 170, 255, static_cast<int>(110 * bright)));
							fg->AddImage(st, { h.x - r2, h.y - r2 }, { h.x + r2, h.y + r2 }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * bright)));
						}
						fg->AddCircleFilled(h, 2.0f * k, IM_COL32(255, 255, 255, static_cast<int>(255 * bright)), 10);
						for (int j = 0; j < 6; ++j) {  // sparks falling away behind the head
							const float f = head - tail * (0.15f + 0.13f * j);
							if (f < 0) {
								break;
							}
							const ImVec2 p = path(f);
							const float  fl = 0.5f + 0.5f * std::sin(now * 40.0f + j * 2.3f);
							fg->AddCircleFilled({ p.x + std::sin(j * 7.1f) * 6 * k, p.y + (j + 1) * 2.2f * k }, 1.1f * k, IM_COL32(220, 235, 255, static_cast<int>(160 * fl * bright * (1 - j / 6.0f))), 6);
						}
					}
				}
			}
			const bool lakeScene = g_sGround && g_sMtn && g_sRefl;
			// the northern lights over the lake: the curtains drawn in narrow columns, each lifted and brightened on its
			// own slow wave, so the folds ripple along them and the light breathes
			auto auroraStrips = [&](bool mirrored, float alpha) {
				if (!g_sAurora) {
					return;
				}
				const auto  tx = ImTextureID(reinterpret_cast<std::uintptr_t>(g_sAurora));
				constexpr int kN = 96;
				for (int i = 0; i < kN; ++i) {
					const float x0 = 2560.0f * i / kN, x1 = 2560.0f * (i + 1) / kN, xm = (x0 + x1) * 0.5f;
					const float lift = 26.0f * std::sin(now * 0.31f + xm * 0.0042f) + 12.0f * std::sin(now * 0.73f - xm * 0.011f);
					const float glow = 0.62f + 0.38f * std::sin(now * 0.45f + xm * 0.0027f) * std::sin(now * 0.19f - xm * 0.0013f + 1.0f);
					const int   al = static_cast<int>(std::clamp(alpha * glow, 0.0f, 1.0f) * 255 * fade);
					const float sway = std::sin(now * 0.12f) * 0.004f;
					if (!mirrored) {
						const ImVec2 a = toScreen(0.2f, x0, 0 - lift), b = toScreen(0.2f, x1, 1440 - lift);
						fg->AddImage(tx, a, b, { x0 / 2560 + sway, 0 }, { x1 / 2560 + sway, 1 }, IM_COL32(255, 255, 255, al));
					} else {  // the lake's copy: sky rows 160..800 mirrored into 800..1440
						const ImVec2 a = toScreen(0.4f, x0, 800 + lift * 0.6f), b = toScreen(0.4f, x1, 1440 + lift * 0.6f);
						fg->AddImage(tx, a, b, { x0 / 2560 + sway, 800.0f / 1440 }, { x1 / 2560 + sway, 160.0f / 1440 }, IM_COL32(200, 225, 255, al));
					}
				}
			};
			if (lakeScene) {
				auroraStrips(false, 0.95f);
			} else if (g_mAurora) {
				for (int band = 0; band < 2; ++band) {
					const float w8 = 0.5f + 0.5f * std::sin(now * (0.21f + 0.07f * band) + band * 2.1f);
					layer(g_mAurora, 0.2f, IM_COL32(150, 255, 210, static_cast<int>((14 + 30 * w8) * fade)), { std::sin(now * (0.05f + 0.02f * band) + band) * 0.006f, 0 });
				}
			}
			const float cloudK = lakeScene ? 0.45f : 1.0f;  // (thin over the lake: the aurora shows through)
			layer(g_mClouds, 0.25f, IM_COL32(255, 255, 255, static_cast<int>(200 * fade * cloudK)), { std::fmod(now * 0.0035f, 1.0f), 0 });
			// clouds drifting across the night: two banks at their own speeds (the near one faster), passing over the moon
			if (g_mClouds2) {
				const auto tex = ImTextureID(reinterpret_cast<std::uintptr_t>(g_mClouds2));
				const float u0 = std::fmod(now * 0.0045f, 1.0f), u1 = std::fmod(now * 0.0075f + 0.4f, 1.0f);
				fg->AddImage(tex, { 0, 0 }, { size.x, size.y * 0.40f }, { u0, 0 }, { u0 + 1.0f, 1 }, IM_COL32(255, 255, 255, static_cast<int>(150 * fade * cloudK)));
				fg->AddImage(tex, { 0, size.y * 0.06f }, { size.x, size.y * 0.46f }, { u1, 0.15f }, { u1 + 0.7f, 1 }, IM_COL32(220, 220, 235, static_cast<int>(120 * fade * cloudK)));
			}
			auto groundFog = [&](float y0, float y1, float speed, float rep, int al, ImU32 rgb) {
				ID3D11ShaderResourceView* t = g_mGroundFog ? g_mGroundFog : g_menuFog;
				if (!t) {
					return;
				}
				const auto tex = ImTextureID(reinterpret_cast<std::uintptr_t>(t));
				const float u0 = std::fmod(now * speed, 1.0f), u1 = std::fmod(-now * speed * 0.63f + 0.37f, 1.0f);
				const float bob = std::sin(now * 0.21f) * 0.012f * size.y;
				fg->AddImage(tex, { 0, y0 + bob }, { size.x, y1 + bob }, { u0, 0 }, { u0 + rep, 1 }, (rgb & 0x00FFFFFF) | (static_cast<ImU32>(std::min(255.0f, al * 0.85f * fade)) << 24));
				fg->AddImage(tex, { 0, y0 - bob * 0.6f + (y1 - y0) * 0.08f }, { size.x, y1 }, { u1, 0 }, { u1 + rep * 0.7f, 1 }, (rgb & 0x00FFFFFF) | (static_cast<ImU32>(std::min(255.0f, al * 0.7f * fade)) << 24));
			};
			// a bank of mist lying on water: densest along the line y_mid, thinning away both up and down (the fog texture's
			// soft top edge drawn twice, the lower copy flipped), so it never ends in a hard line (Noah)
			auto waterMist = [&](float y0, float yMid, float y1, float speed, float rep, int al, ImU32 rgb) {
				ID3D11ShaderResourceView* t = g_mGroundFog ? g_mGroundFog : g_menuFog;
				if (!t) {
					return;
				}
				const auto tex = ImTextureID(reinterpret_cast<std::uintptr_t>(t));
				const float bob = std::sin(now * 0.21f) * 0.006f * size.y;
				for (int k = 0; k < 2; ++k) {
					const float u = std::fmod(k == 0 ? now * speed : -now * speed * 0.63f + 0.37f, 1.0f), r = k == 0 ? rep : rep * 0.7f;
					const ImU32 c = (rgb & 0x00FFFFFF) | (static_cast<ImU32>(std::min(255.0f, al * (k == 0 ? 0.85f : 0.7f) * fade)) << 24);
					fg->AddImage(tex, { 0, y0 + bob }, { size.x, yMid + bob }, { u, 0 }, { u + r, 1 }, c);
					fg->AddImage(tex, { 0, yMid + bob }, { size.x, y1 + bob }, { u, 1 }, { u + r, 0 }, c);
				}
			};
			if (g_sGround) {
				// THE SHRINE (Noah's picture): a round rune floor on a dark plain, a blue crystal turning and pulsing over
				// it, broken rocks floating round it. All of it at one depth of the parallax, in the painting's pixels.
				constexpr float kD = 0.6f, kCX = 1160.0f, kCY = 1190.0f, kS = 3.4f, kCos = 0.9563f, kSin = 0.2924f, kCrystalZ = 76.9f;  // (tools/menualtar.py: the new altar's heart stone)
				const float u = (cw * (1.04f + 0.03f * kD) * push) / 2560.0f;  // painting pixels -> screen
				auto at = [&](float X, float Y, float Z) { return toScreen(kD, kCX + X * kS, kCY - (Z * kCos - Y * kSin) * kS); };
				auto tex = [](ID3D11ShaderResourceView* t) { return ImTextureID(reinterpret_cast<std::uintptr_t>(t)); };
				auto sprite = [&](ID3D11ShaderResourceView* t, ImVec2 c, float w, float h, float rot, ImU32 col, ImVec2 uv0 = { 0, 0 }, ImVec2 uv1 = { 1, 1 }) {
					const float cs = std::cos(rot), sn = std::sin(rot);
					auto P = [&](float x, float y) { return ImVec2{ c.x + x * cs - y * sn, c.y + x * sn + y * cs }; };
					fg->AddImageQuad(tex(t), P(-w / 2, -h / 2), P(w / 2, -h / 2), P(w / 2, h / 2), P(-w / 2, h / 2), uv0, { uv1.x, uv0.y }, uv1, { uv0.x, uv1.y }, col);
				};
				const float pulse = 0.5f + 0.5f * std::sin(now * 1.6f);            // the core's heartbeat
				const float slow = 0.5f + 0.5f * std::sin(now * 0.55f + 1.0f);
				// the far range across the lake, then the lake: deep water darkening toward us; the stars and the aurora
				// upside down in it, the mountains' reflection over them - all of it in thin rows, each row nudged
				// sideways on its own wave (bigger and slower toward us), so the reflection wavers like real water; bright
				// ripple lines run out toward the far shore
				{
					constexpr float kL = 0.4f;  // (the mountains and their reflection share one depth, so they meet at the shore)
					if (g_sMtn) {
						layer(g_sMtn, kL, white);
					}
					// the water sways in thin rows, each nudged sideways on its own wave (bigger and slower toward us); the
					// mountains' reflection below uses the same waves so the two move as one surface
					constexpr int kRows = 128;
					auto rowOff = [&](int r) {
						const float y0 = 800 + 640.0f * r / kRows, tt = (r + 0.5f) / kRows;
						return std::sin(now * (1.6f - 0.8f * tt) + y0 * (0.19f - 0.12f * tt)) * (1.5f + 16.0f * tt * tt) + std::sin(now * 0.7f + y0 * 0.031f) * 3.0f * tt;
					};
					const ImVec2 h0 = toScreen(kL, -60, 800), h1 = toScreen(kL, 2620, 1460);
					fg->AddRectFilledMultiColor(h0, h1, IM_COL32(20, 27, 52, static_cast<int>(255 * fade)), IM_COL32(20, 27, 52, static_cast<int>(255 * fade)),
						IM_COL32(6, 9, 20, static_cast<int>(255 * fade)), IM_COL32(6, 9, 20, static_cast<int>(255 * fade)));
					if (g_sLake) {
						const auto lt = ImTextureID(reinterpret_cast<std::uintptr_t>(g_sLake));
						for (int r = 0; r < kRows; ++r) {
							const float y0 = 800 + 640.0f * r / kRows, y1 = 800 + 640.0f * (r + 1) / kRows;
							const float du = rowOff(r) / 2560.0f;
							fg->AddImage(lt, toScreen(kL, 0, y0), toScreen(kL, 2560, y1 + 0.6f), { du, r / static_cast<float>(kRows) }, { 1 + du, (r + 1) / static_cast<float>(kRows) },
								IM_COL32(255, 255, 255, static_cast<int>(255 * fade)));
						}
					}
					for (const auto& st : stars) {
						const float py = 1600 - st.y * 1440;
						if (py < 806 || py > 1430) {
							continue;
						}
						const float tt = (py - 800) / 640;
						const float wob = std::sin(now * 1.3f + py * 0.08f) * (2 + 10 * tt);
						const ImVec2 p = toScreen(kL, st.x * 2560 + wob, py);
						const float  tw = std::pow(0.5f + 0.5f * std::sin(now * st.sp + st.ph), 3.0f);
						const int    al = static_cast<int>((30 + 90 * tw) * (1 - 0.6f * tt) * fade);
						fg->AddLine({ p.x, p.y - 2 * k }, { p.x, p.y + (3 + 5 * st.sz) * k }, IM_COL32(200, 215, 255, al / 2), (1.0f + st.sz) * k);
						fg->AddCircleFilled(p, (0.7f + 0.8f * st.sz * tw) * k, IM_COL32(225, 235, 255, al), 8);
					}
					auroraStrips(true, 0.42f);
					if (g_sRefl) {
						const auto rt = ImTextureID(reinterpret_cast<std::uintptr_t>(g_sRefl));
						for (int r = 0; r < kRows; ++r) {
							const float y0 = 800 + 640.0f * r / kRows, y1 = 800 + 640.0f * (r + 1) / kRows;
							const float du = rowOff(r) / 2560.0f;
							fg->AddImage(rt, toScreen(kL, 0, y0), toScreen(kL, 2560, y1 + 0.6f), { du, r / static_cast<float>(kRows) }, { 1 + du, (r + 1) / static_cast<float>(kRows) },
								IM_COL32(255, 255, 255, static_cast<int>(255 * fade)));
						}
					}
					// rings spreading out over the water from under the shrine, as if it breathed on the lake: each a bright
					// crest with a dark trough inside it, flattened by the angle we look at the water from, fading as it widens
					{
						const ImVec2 q = at(0, 0, -26.0f);
						const float  flat = 0.2924f;  // sin of the view angle: how round rings look squashed on the water
						for (int i = 0; i < 7; ++i) {
							const float ph = std::fmod(now * 0.085f + i / 7.0f, 1.0f);
							const float R = (190.0f + ph * 1500.0f) * u;
							const float life = std::sin(ph * 3.1416f) * (1.0f - ph);
							const int   hi = static_cast<int>(70 * life * fade), lo = static_cast<int>(60 * life * fade);
							const float th = (1.2f + 3.0f * ph) * k;
							fg->AddEllipse(q, { R, R * flat }, IM_COL32(160, 195, 245, hi), 0.0f, 96, th);
							fg->AddEllipse({ q.x, q.y - 1.5f * th }, { R * 0.985f, R * flat * 0.985f }, IM_COL32(2, 4, 12, lo), 0.0f, 96, th * 1.4f);
							fg->AddEllipse({ q.x, q.y + 2.0f * th }, { R * 1.02f, R * flat * 1.02f }, IM_COL32(110, 150, 220, hi / 3), 0.0f, 96, th * 2.2f);
						}
					}
					// the shrine's own light on the water under it: a wavering column of blue
					if (g_sSoft) {
						const ImVec2 q = at(0, 0, -60.0f);
						for (int j = 0; j < 10; ++j) {
							const float yy = q.y + j * 22.0f * u * kS * 0.35f;
							const float wv = std::sin(now * 2.1f + j * 0.9f) * 10.0f * u;
							sprite(g_sSoft, { q.x + wv, yy }, (260 - 14 * j) * u, 26 * u, 0, IM_COL32(90, 170, 255, static_cast<int>((34 - 3 * j + 10 * pulse) * fade)));
						}
					}
				}
				if (g_sLife) {  // lily pads riding the swell, reeds in the corners
					layer(g_sLife, 0.4f, white, { std::sin(now * 0.35f) * 0.0006f, std::sin(now * 0.9f) * 0.0009f });
				}
				waterMist(size.y * 0.47f, size.y * 0.565f, size.y * 0.68f, 0.006f, 1.6f, 70, IM_COL32(120, 135, 180, 0));  // mist on the far water, at the mountains' feet
				layer(g_sGround, kD, white);
				if (g_sGlow) {  // the runes and the pool breathe with the core
					layer(g_sGlow, kD, IM_COL32(255, 255, 255, static_cast<int>((150 + 105 * pulse) * fade)));
				}
				// (the old motes running round the rune ring are gone: the motes flipbook below carries the light now)
				// the rune stones floating over the columns (tools/menualtar.py), bobbing and slowly turning; the ones at the back
				// (-Y: higher on the painting) behind the heart stone
				struct Rock { float x, y, z, bob, ph, rot0, spin; };
				static constexpr Rock kRocks[6] = { { 147.4f, 0.0f, 99.4f, 7, 0.0f, 0.1f, 0.10f }, { 73.3f, -127.7f, 119.9f, 8, 1.3f, -0.2f, -0.08f },
					{ -73.4f, -127.3f, 158.2f, 7, 2.1f, 0.15f, 0.12f }, { -147.0f, 0.0f, 119.9f, 8, 0.7f, -0.1f, 0.07f }, { -73.4f, 127.3f, 158.2f, 7, 2.9f, 0.2f, -0.11f },
					{ 74.2f, 127.7f, 158.2f, 8, 1.8f, -0.15f, 0.09f } };
				struct Placed { int i; ImVec2 p; float depth, scale; };
				std::array<Placed, 6> placed{};
				for (int i = 0; i < 6; ++i) {
					const auto& r = kRocks[i];
					const float z = r.z + std::sin(now * 0.8f + r.ph) * r.bob + 4.0f * pulse;  // they lift a little with each beat
					placed[i] = { i, at(r.x, r.y, z), -r.y, 1.0f };
				}
				std::sort(placed.begin(), placed.end(), [](const Placed& x, const Placed& y) { return x.depth > y.depth; });  // far first
				auto drawRock = [&](const Placed& pl) {
					if (!g_sShard[pl.i]) {
						return;
					}
					const auto& r = kRocks[pl.i];
					const float sz = 200.0f * u;
					const int   shade = static_cast<int>(std::clamp(235.0f + pl.depth * 0.25f, 170.0f, 255.0f));
					const float rot = r.rot0 + std::sin(now * r.spin * 3.0f + r.ph) * 0.06f;
					if (g_sRockTurn) {  // (Noah: as the altar's rocks do in the game) each turns slowly round its upright, its own way
						const bool  all3 = g_sRockSheet[1] && g_sRockSheet[2];
						const int   nF = all3 ? 144 : 48;
						const float fpos = std::fmod(now * r.spin * 80.0f / 360.0f * nF + r.ph * 15.0f * nF / 48.0f + 100.0f * nF, static_cast<float>(nF));  // (6-10 degrees a second, as the altar's)
						const int   f0 = static_cast<int>(fpos) % nF, f1 = (f0 + 1) % nF;
						const float fr = fpos - std::floor(fpos);
						const ImU32 tint = IM_COL32(shade, shade, std::min(255, shade + 10), 255);
						const float breath = 0.5f + 0.5f * std::sin(now * 2.4f + r.ph * 1.7f);
						for (auto [f, wgt] : { std::pair{ f0, 1.0f }, std::pair{ f1, fr } }) {
							const int    sh = f / 48, ff = f % 48;
							const ImVec2 uv0{ (ff % 8) / 8.0f, (ff / 8) / 6.0f }, uv1{ uv0.x + 0.125f, uv0.y + 1.0f / 6.0f };
							sprite(g_sRockSheet[sh], pl.p, sz, sz, rot, (tint & 0x00FFFFFF) | (static_cast<ImU32>(255 * fade * wgt) << 24), uv0, uv1);
							if (g_sRockSheetR[sh]) sprite(g_sRockSheetR[sh], pl.p, sz, sz, rot, IM_COL32(255, 255, 255, static_cast<int>((60 + 195 * breath * breath) * fade * wgt)), uv0, uv1);
						}
						return;
					}
					sprite(g_sShard[pl.i], pl.p, sz, sz, rot, IM_COL32(shade, shade, std::min(255, shade + 10), static_cast<int>(255 * fade)));
					if (g_sShardR[pl.i]) {  // the runes breathe blue light, each rock a little out of step
						const float breath = 0.5f + 0.5f * std::sin(now * 2.4f + r.ph * 1.7f);
						sprite(g_sShardR[pl.i], pl.p, sz, sz, rot, IM_COL32(255, 255, 255, static_cast<int>((60 + 195 * breath * breath) * fade)));
					}
				};
				for (const auto& pl : placed) {
					if (pl.depth > 0) drawRock(pl);
				}
				// (the pool's motes and light shaft after the far rocks: a rock swinging round the back of the orb passes behind them)
				// (Noah: realistic particles) motes of light rising out of the altar's base: each its own life - born in the pool,
				// drifting up on a slow spiral with a little turbulence, brightening, then thinning out to nothing above the
				// stone; small, soft, a few larger and dimmer (out of focus), those behind the stone drawn before it
				const ImVec2 kPool = at(0, 0, 0), kTop = at(0, 0, kCrystalZ);
				const auto motes = [&](bool a_front) {
					if (!g_sSoft) return;
					const float rise = (kPool.y - kTop.y) * 1.65f;
					for (int q = 0; q < 140; ++q) {
						const float seed = q * 0.6180339f;
						const float speed = 0.075f + 0.05f * std::fmod(seed * 7.3f, 1.0f);
						const float life = std::fmod(now * speed + seed, 1.0f);              // 0 born .. 1 gone
						const float ang = seed * 6.2832f + now * (0.35f + 0.25f * std::fmod(seed * 3.1f, 1.0f)) + life * 2.5f;
						const float rad = (18.0f + 70.0f * std::fmod(seed * 5.7f, 1.0f)) * u * (0.55f + 0.9f * life);
						const bool  front = std::cos(ang) > 0;
						if (front != a_front) continue;
						const float tx = std::sin(now * 1.3f + q) * 6.0f * u, ty = std::sin(now * 0.9f + q * 1.7f) * 4.0f * u;  // turbulence
						const ImVec2 pos{ kPool.x + std::sin(ang) * rad + tx, kPool.y - life * rise + std::cos(ang) * rad * 0.25f + ty };
						const float fadeIn = std::clamp(life / 0.12f, 0.0f, 1.0f), fadeOut = std::clamp((1.0f - life) / 0.35f, 0.0f, 1.0f);
						const bool  big = q % 9 == 0;                                             // out of focus, nearer the eye
						const float sz = (big ? 9.0f : 2.2f + 2.2f * std::fmod(seed * 11.0f, 1.0f)) * u * kS;
						const float al = fadeIn * fadeOut * (big ? 0.25f : 0.9f) * (0.6f + 0.4f * std::sin(now * 5.0f + q)) * fade;
						sprite(g_sSoft, pos, sz * 6, sz * 6, 0, IM_COL32(90, 170, 255, static_cast<int>(70 * al)));
						sprite(g_sSoft, pos, sz * 2, sz * 2, 0, IM_COL32(200, 235, 255, static_cast<int>(230 * al)));
					}
				};
				motes(false);
				// the light rising from the pool to the crystal: a soft shaft (the round light stretched tall)
				if (g_sSoft) {
					const ImVec2 pool = at(0, 0, 0), top = at(0, 0, kCrystalZ);
					const ImVec2 mid{ pool.x, (pool.y + top.y) * 0.5f };
					const float  len = (pool.y - top.y) * 1.25f, bw = (70 + 16 * pulse) * u * kS;
					sprite(g_sSoft, mid, bw * 2.0f, len, 0, IM_COL32(110, 190, 255, static_cast<int>((30 + 20 * pulse) * fade)));
					sprite(g_sSoft, mid, bw * 0.7f, len, 0, IM_COL32(170, 220, 255, static_cast<int>((25 + 20 * pulse) * fade)));
					sprite(g_sSoft, pool, bw * 3.2f, bw * 1.0f, 0, IM_COL32(120, 200, 255, static_cast<int>((90 + 60 * pulse) * fade)));  // the pool's glow on the floor
				}
				// the crystal: turning (its frames), swelling with the heartbeat, its light round it
				{
					const float  bob = std::sin(now * 0.9f) * 6 * u;  // the orb rises and falls; its light and aura go with it
					const ImVec2 c = [&] { auto p = at(0, 0, kCrystalZ); p.y += bob; return p; }();
					if (g_sSoft) {
						// (Noah: a realistic glow, no white circle) light scattering off the stone into the night air: a wide faint
						// halo and a closer one, both blue, breathing slowly - no hot core
						const float br = 0.8f + 0.2f * std::sin(now * 1.1f);
						const float g1 = 560 * u, g2 = 300 * u, g3 = 170 * u;
						sprite(g_sSoft, c, g1 * 2, g1 * 2.2f, 0, IM_COL32(40, 90, 200, static_cast<int>(38 * br * fade)));
						sprite(g_sSoft, c, g2 * 2, g2 * 2.3f, 0, IM_COL32(70, 140, 240, static_cast<int>(48 * br * fade)));
						sprite(g_sSoft, c, g3 * 2, g3 * 2.6f, 0, IM_COL32(110, 180, 255, static_cast<int>(40 * br * fade)));
					}
					// purple fire made of magic round it (the in-game orb's own flipbook: tools/magicfire.py, 16x8 frames), two
					// passes out of step and a little apart in size
					const auto aura = [&](float scale, float t0, ImU32 tint) {
						if (!g_fire) {
							return;
						}
						const int   fr = static_cast<int>((now + t0) * 24.0f) % 128;  // (slow, as the rocks drift; 24 frames a second)
						const float uu = (fr % 16) / 16.0f, vv = (fr / 16) / 8.0f;
						const float hh = 108.0f * u * scale / 0.36f, lift = 0.30f * hh;  // (the orb sits below the frame's middle)
						fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_fire)), { c.x - hh, c.y - hh - lift }, { c.x + hh, c.y + hh - lift }, { uu, vv }, { uu + 1.0f / 16.0f, vv + 0.125f }, tint);
					};
					(void)aura;  // (the old orb's purple fire: the heart stone has its runes' own light)
					if (g_sCrystal) {
						// (Noah: smooth at 60 fps, a little faster) a turn in 18 s, cross-faded between its 96 frames
						const int   nF = g_sCrystalSheet[3] ? 384 : g_sCrystalSheet[1] ? 192 : 96;  // (a sheet missing: the frames it has)
						const float fpos = std::fmod(now * nF / 18.0f, static_cast<float>(nF));
						const int   f0 = static_cast<int>(fpos) % nF, f1 = (f0 + 1) % nF;
						const float fr = fpos - std::floor(fpos);
						const float sc = 1.0f;  // (Noah: smooth - no heartbeat swelling on the turning stone)
						const float w = 427 * u * sc, h = 640 * u * sc;  // (320 x 480 frames, square pixels)
						const ImVec2 mid{ c.x, c.y + (0.5f - 0.56f) * h };  // (the frame's origin sits at 56% of its height)
						for (auto [f, wgt] : { std::pair{ f0, 1.0f }, std::pair{ f1, fr } }) {
							const int    ff = f % 96;
							const ImVec2 uv0{ (ff % 12) / 12.0f, (ff / 12) / 8.0f }, uv1{ uv0.x + 1.0f / 12.0f, uv0.y + 0.125f };
							sprite(g_sCrystalSheet[f / 96], mid, w, h, 0, IM_COL32(255, 255, 255, static_cast<int>(255 * fade * wgt)), uv0, uv1);
						}
					}

					motes(true);  // (the motes in front of the stone; the white heart is gone)
					// (the old drawn sparks over the pool are gone too: the motes flipbook is the shrine's only particles)
				}
				// (Noah: as the altar in the game) its golden rune circle over the shrine, laid flat and turning smoothly
				if ((false) && g_sRuneCircle) {  // (Noah: the golden rune over the menu's altar removed)
					const float th = now * 10.0f * RE::NI_PI / 180.0f;
					const auto disc = [&](float R, float Z, ImU32 col) {
						ImVec2 p[4];
						const float cs = std::cos(th), sn = std::sin(th);
						const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
						for (int q = 0; q < 4; ++q) {
							const float x = (sx[q] * cs - sy[q] * sn) * R, y = (sx[q] * sn + sy[q] * cs) * R;
							p[q] = at(x, y, Z);
						}
						fg->AddImageQuad(tex(g_sRuneCircle), p[0], p[1], p[2], p[3], { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, col);
					};
					const float bob = std::sin(now * 0.7f) * 9.0f * 0.76f;  // (it floats, as every turning piece of the altar does)
					disc(175.0f * 0.76f, (330.0f - 6.0f - 73.8f) * 0.76f + bob, IM_COL32(255, 220, 150, static_cast<int>(70 * fade)));
					disc(150.0f * 0.76f, (330.0f - 73.8f) * 0.76f + bob, IM_COL32(255, 240, 210, static_cast<int>(235 * fade)));
				}
				for (const auto& pl : placed) {
					if (pl.depth <= 0) drawRock(pl);
				}
				groundFog(size.y * 0.86f, size.y * 1.04f, 0.011f, 0.9f, 150, IM_COL32(80, 90, 125, 0));  // a little mist at our feet
			} else {
			// the far range, then mist drifting in the valley in front of it
			layer(g_mFar, 0.45f, white);
			// fog rolling over the ground: two copies of the wisps sliding past each other at different speeds, so it
			// churns as it drifts (valley fog here, ground fog over the slope, a thick bank across the front)
			groundFog(size.y * 0.26f, size.y * 0.52f, 0.006f, 1.6f, 230, IM_COL32(150, 162, 200, 0));
			// the barrow ridge and the horde coming down it; the champions' violet light in the barrow door breathing
			layer(g_mMid, 0.8f, white);
			{
				const ImVec2 a = toScreen(0.8f, kMenuArch[0], kMenuArch[1]);
				const float  br = 0.6f + 0.4f * std::sin(now * 1.3f);
				fg->AddCircleFilled(a, 90 * k, IM_COL32(150, 80, 255, static_cast<int>(10 * br * fade)), 40);
			}
			// ground mist rolling between the horde and the front
			groundFog(size.y * 0.40f, size.y * 0.80f, 0.009f, 1.2f, 255, IM_COL32(140, 150, 188, 0));
			// the front: the rune-stone (its runes pulse, a soft violet light on the snow), the graves, the outcrop
			layer(g_mNear, 0.95f, IM_COL32(215, 215, 222, static_cast<int>(255 * fade)));  // the front sits in shadow (leans almost with the ground it stands on)
			{
				// the runes cut down its face, lit from within: each glows up in turn, the light spilling onto the snow
				const ImVec2 r = toScreen(0.95f, kMenuRune[0], kMenuRune[1]);
				const float  br = 0.55f + 0.45f * std::sin(now * 1.7f) * std::sin(now * 0.43f + 1.0f);
				const float  u = (cw * (1.04f + 0.03f * 0.95f) * push) / 2560.0f;  // painting pixels -> screen
				fg->AddCircleFilled({ r.x, r.y + 150 * u }, 120 * u, IM_COL32(150, 70, 255, static_cast<int>((8 + 10 * br) * fade)), 40);
				fg->AddCircleFilled(r, 60 * u, IM_COL32(150, 70, 255, static_cast<int>((10 + 14 * br) * fade)), 40);
				static const float kRune[6][8] = {  // each rune as up to two strokes (x0 y0 x1 y1, painting pixels from its centre)
					{ 0, -14, 0, 14, 0, -6, 9, -14 }, { -8, 12, 0, -14, 8, 12, 0, -14 }, { 0, -14, 0, 14, -9, 0, 9, 0 },
					{ -8, -14, 8, 14, 8, -14, -8, 14 }, { 0, -14, 0, 14, 0, 2, 9, 12 }, { -8, -12, 8, -12, 0, -12, 0, 14 } };
				for (int i = 0; i < 6; ++i) {
					const float ry = r.y + (i - 2.5f) * 34 * u, rx = r.x + ((i % 2) ? 2.0f : -2.0f) * u;
					const float lit = std::clamp(0.45f + 0.55f * std::sin(now * 1.4f - i * 0.9f), 0.0f, 1.0f) * fade;
					for (int sgm = 0; sgm < 2; ++sgm) {
						const float* q = kRune[i] + sgm * 4;
						const ImVec2 p0{ rx + q[0] * u, ry + q[1] * u }, p1{ rx + q[2] * u, ry + q[3] * u };
						fg->AddLine(p0, p1, IM_COL32(150, 80, 255, static_cast<int>(70 * lit)), 7.0f * u);
						fg->AddLine(p0, p1, IM_COL32(215, 170, 255, static_cast<int>(235 * lit)), 2.4f * u);
					}
				}
			}
			groundFog(size.y * 0.62f, size.y * 1.02f, 0.013f, 0.9f, 255, IM_COL32(105, 114, 148, 0));  // the thick bank across the front, darker
			}
			// snow and grave-ash drifting down on the wind, a few soul-motes rising from the rune-stone
			{
				struct Mote { float x, y, sp, sway, ph, r; int kind; };
				static std::vector<Mote> mo;
				static std::mt19937 rng{ 7 };
				std::uniform_real_distribution<float> U(0, 1);
				if (mo.empty()) {
					for (int i = 0; i < 170; ++i) {
						mo.push_back({ U(rng), U(rng), 0.02f + 0.05f * U(rng), 0.004f + 0.012f * U(rng), U(rng) * 6.28f, 0.6f + 1.6f * U(rng), i % 10 == 0 ? 1 : 0 });
					}
				}
				const ImVec2 rs = g_sGround ? toScreen(0.6f, 1060.0f, 1185.0f) : toScreen(0.95f, kMenuRune[0], kMenuRune[1]);  // the soul-motes rise from the shrine's pool
				for (auto& m : mo) {
					m.y += (m.kind == 1 ? -0.5f : 1.0f) * m.sp * dt;
					m.x += (m.kind == 1 ? 0.0f : 0.012f) * dt;  // the wind
					if (m.y > 1.03f) m.y = -0.03f, m.x = U(rng);
					if (m.y < -0.03f || (m.kind == 1 && m.y < rs.y / size.y - 0.35f)) m.y = rs.y / size.y + 0.02f * U(rng), m.x = rs.x / size.x + 0.02f * (U(rng) - 0.5f);
					if (m.x > 1.03f) m.x = -0.03f;
					const ImVec2 p{ (m.x + std::sin(now * 0.8f + m.ph) * m.sway) * size.x, m.y * size.y };
					const float  tw = 0.6f + 0.4f * std::sin(now * 3.0f + m.ph * 3);
					const ImU32  c = m.kind == 1 ? IM_COL32(190, 140, 255, static_cast<int>(170 * tw * fade)) : IM_COL32(220, 228, 240, static_cast<int>(120 * fade));
					if (m.kind == 1) fg->AddCircleFilled(p, m.r * k * 3.0f, Fade(c, 0.15f));
					fg->AddCircleFilled(p, m.r * k, c);
				}
			}
			// vignette
			fg->AddRectFilledMultiColor({ 0, 0 }, { size.x, size.y * 0.25f }, IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
			fg->AddRectFilledMultiColor({ 0, size.y * 0.75f }, size, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150));
			fg->AddRectFilledMultiColor({ size.x * 0.62f, 0 }, size, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 0));  // the menu side
			// the title, settling in
			if (g_mTitle) {
				const float tw = size.x * 0.62f, th = tw * 396.0f / 2479.0f;
				const float ty = size.y * 0.06f + (1.0f - EaseOut(std::clamp(openT / 1.6f, 0.0f, 1.0f))) * 14 * k;
				const ImVec2 a{ (size.x - tw) / 2 - lean.x * 6 * k, ty };
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_mTitle)), a, { a.x + tw, a.y + th }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * std::clamp(openT / 1.6f, 0.0f, 1.0f))));
			}
			// a question (Start a new game?): the scene dims and one dark field sits exactly under Skyrim's own box, so its
			// words and frame read as one panel
			{
				static float qa = 0;
				ImVec4       qb{};
				const bool   up = MenuQuestionBounds(size, qb);
				qa = std::clamp(qa + (up ? 1.0f : -1.0f) * dt * 6.0f, 0.0f, 1.0f);
				static ImVec4 last{};
				if (up) {
					last = qb;
				}
				if (qa > 0 && last.z > last.x) {
					fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, static_cast<int>(90 * qa)));
					const ImVec2 a{ last.x - 34 * k, last.y - 26 * k }, z{ last.z + 34 * k, last.w + 22 * k };
					fg->AddRectFilled(a, z, IM_COL32(6, 6, 9, static_cast<int>(232 * qa)), 2 * k);
					fg->AddRect(a, z, IM_COL32(118, 104, 86, static_cast<int>(160 * qa)), 2 * k, 0, 1.2f * k);
				}
			}
			// Skyrim's questions and hints laid back over the picture: only the bright words
			if (g_frameSrv && g_keyPs) {
				g_keyCursor = { -1000, -1000 };
				fg->AddCallback(KeyOn, nullptr);
				fg->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_frameSrv)), { 0, 0 }, size, { 0, 0 }, { 1, 1 });
				fg->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
			}
			// the menu: Skyrim's own entries as words only, the way the base game shows them (no plates, no boxes): the
			// chosen word bright with a soft cold glow, the rest dim; keys and pad move Skyrim's selection, the mouse moves
			// it by hovering a word, a click or Enter does what Skyrim would
			{
				RE::GFxMovieView* mv = nullptr;
				const auto entries = ReadMainList(&mv);
				// CO-OP PARTY is one more word in the list (under LOAD); pressed, the list turns into the party's own words
				// (host / invite / join a friend / leave, and BACK), in the same style
				static bool partyMode = false;
				static std::vector<Net::FriendGame> friends;
				static float refresh = 0;
				const bool coop = Net::Available();
				if (!coop) {
					partyMode = false;
				}
				if (partyMode && (refresh -= dt) <= 0) {
					refresh = 2.0f;
					friends = Net::InLobby() ? std::vector<Net::FriendGame>{} : Net::FriendsInParties();
				}
				struct Row { std::string text; bool sel, disabled; int kind, arg; ImU32 accent; };  // kind: 0 Skyrim's (arg = its index), 1 party, 2 host, 3 invite, 4 leave, 5 join (arg = friend), 6 back
				std::vector<Row> rows;
				static int partyHover = -1;
				static bool settingsMode = false;  // The Curse of Alduin's settings, as words here too (the pad can change them)
				static bool loadMode = false;      // our own Load (every save, in our style)
				if (loadMode) {
					// (the rows give way to the panel: drawn below)
				} else if (settingsMode) {
					rows.push_back({ "First person view   <  " + std::to_string(static_cast<int>(Settings::FovFirst())) + "  >", false, false, 8, 0, 0 });
					rows.push_back({ "Third person view   <  " + std::to_string(static_cast<int>(Settings::FovThird())) + "  >", false, false, 8, 1, 0 });
					{
						char b[48];
						std::snprintf(b, sizeof(b), "Brightness   <  %.2f  >", Settings::Brightness());
						rows.push_back({ b, false, false, 8, 2, 0 });
					}
					rows.push_back({ "Back", false, false, 6, 0, 0 });
				} else if (!partyMode) {
					for (const auto& e : entries) {
						std::string up = e.text;
						std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
						rows.push_back({ e.text, false, e.disabled, up == "LOAD" ? 9 : 0, e.index, 0 });
						if (rows.back().kind == 0) rows.back().sel = e.sel;
						if (up == "LOAD") {
							if (coop) rows.push_back({ "Co-op Party", false, false, 1, 0, 0 });
							rows.push_back({ "Settings", false, false, 7, 0, 0 });
						}
					}
					if (!entries.empty() && std::none_of(rows.begin(), rows.end(), [](const Row& r) { return r.kind == 7; })) {
						if (coop) rows.push_back({ "Co-op Party", false, false, 1, 0, 0 });
						rows.push_back({ "Settings", false, false, 7, 0, 0 });
					}
				} else {
					if (!Net::InLobby()) {
						rows.push_back({ "Host a Party", false, false, 2, 0, 0 });
						for (std::size_t i = 0; i < friends.size() && i < 4; ++i) {
							rows.push_back({ "Join " + friends[i].name, false, false, 5, static_cast<int>(i), IM_COL32(120, 200, 255, 255) });
						}
					} else if (Net::IsHost()) {
						rows.push_back({ "Invite Friends", false, false, 3, 0, 0 });
						rows.push_back({ "Close the Party", false, false, 4, 0, IM_COL32(230, 110, 90, 255) });
					} else {
						rows.push_back({ "Leave " + Net::HostName() + "'s Party", false, false, 4, 0, IM_COL32(230, 110, 90, 255) });
					}
					rows.push_back({ "Back", false, false, 6, 0, 0 });
				}
				// pad / keys: our own focus walks every word; Skyrim's list follows it (or lets go on our words, so its A
				// does nothing there); the mouse takes over again as soon as it moves
				static int  padFocus = -1;
				static bool padOn = false;
				static bool lastParty = false;
				static ImVec2 lastCur{ -1, -1 };
				const int   moves = g_padMove.exchange(0), accepts = g_padAccept.exchange(0), backs = g_padBack.exchange(0), sides = g_padSide.exchange(0), wheel = g_wheel.exchange(0);
				if (lastParty != (partyMode || settingsMode)) {
					padFocus = -1, lastParty = partyMode || settingsMode;
				}
				if (std::abs(cur.x - lastCur.x) + std::abs(cur.y - lastCur.y) > 3.0f) {
					padOn = false;
				}
				lastCur = cur;
				if (moves || accepts || backs || sides) {
					if (!padOn) {
						padFocus = -1;
						for (std::size_t i = 0; i < rows.size(); ++i) {
							if (rows[i].kind == 0 && rows[i].sel) padFocus = static_cast<int>(i);
						}
						if (padFocus < 0) padFocus = 0;
					}
					padOn = true;
				}
				if (padOn && !rows.empty()) {
					padFocus = std::clamp(padFocus, 0, static_cast<int>(rows.size()) - 1);
					for (int m = 0; m < std::abs(moves); ++m) {
						int nf = padFocus;
						do {
							nf = (nf + (moves > 0 ? 1 : -1) + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
						} while (rows[nf].disabled && nf != padFocus);
						padFocus = nf;
					}
					if (mv) {  // Skyrim's own selection follows ours (none while ours is on one of our words)
						RE::GFxValue list;
						if (mv->GetVariable(&list, "_root.MenuHolder.Menu_mc.MainList") && list.IsDisplayObject()) {
							const double want = rows[padFocus].kind == 0 ? rows[padFocus].arg : -1.0;
							RE::GFxValue cv;
							list.GetMember("selectedIndex", &cv);
							if (!cv.IsNumber() || cv.GetNumber() != want) {
								list.SetMember("selectedIndex", RE::GFxValue(want));
							}
						}
					}
				}
				if (loadMode) {
					if (!LoadPanel(fg, size, k, now, cur, click, moves, accepts, backs, wheel, dt)) {
						loadMode = false;
					}
					click = false;
				}
				static int hoverIdx = -1;
				static std::vector<float> glow;
				glow.resize(rows.size(), 0.0f);
				const float rowH = 54 * k;
				const float right = size.x - 110 * k;
				const float top = size.y * 0.60f - rowH * rows.size() * 0.5f;
				int hovered = -1;
				if (settingsMode) {
					Txt(fg, g_title, 22 * k, { right - Measure(g_title, 22 * k, "SETTINGS").x, top - 78 * k }, kGold, "SETTINGS", 2 * k, IM_COL32(255, 160, 60, 255));
					const std::string hint = g_padLast ? "D-pad Left / Right to change" : "Click, or Left / Right, to change";
					Txt(fg, g_body, 18 * k, { right - Measure(g_body, 18 * k, hint).x, top - 44 * k }, IM_COL32(200, 194, 180, 230), hint);
				}
				if (partyMode) {  // the party's state over its words
					const std::string st = Net::Status() + (Net::InLobby() ? "   (" + std::to_string(Net::Members() + 1) + " in party)" : "");
					const float sfs = 18 * k;
					Txt(fg, g_title, 22 * k, { right - Measure(g_title, 22 * k, "CO-OP PARTY").x, top - 78 * k }, kGold, "CO-OP PARTY", 2 * k, IM_COL32(255, 160, 60, 255));
					Txt(fg, g_body, sfs, { right - Measure(g_body, sfs, st).x, top - 44 * k }, IM_COL32(200, 194, 180, 230), st);
					const std::string hint = "Players meet in Skyrim Together; the party shares the nights.";
					Txt(fg, g_body, 14 * k, { right - Measure(g_body, 14 * k, hint).x, top + rowH * rows.size() + 14 * k }, IM_COL32(160, 154, 140, 200), hint);
				}
				for (std::size_t i = 0; i < rows.size(); ++i) {
					const auto& e = rows[i];
					const float cy = top + rowH * (i + 0.5f);
					const float stagger = std::clamp((openT - 0.25f - 0.07f * i) / 0.4f, 0.0f, 1.0f);  // they fade in one after another
					const bool  lit = padOn ? static_cast<int>(i) == padFocus : (e.kind == 0 ? e.sel : static_cast<int>(i) == partyHover);
					glow[i] += ((lit ? 1.0f : 0.0f) - glow[i]) * std::min(1.0f, dt * 10.0f);
					std::string up = e.text;
					std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
					const float  fs = (27.0f + 4.0f * glow[i]) * k;
					const ImVec2 ts = Measure(g_title, fs, up);
					const ImVec2 at{ right - ts.x - (1.0f - EaseOut(stagger)) * 30 * k, cy - ts.y * 0.5f };
					const bool over = cur.x >= at.x - 12 * k && cur.x <= right + 12 * k && cur.y >= cy - rowH * 0.5f && cur.y <= cy + rowH * 0.5f && !e.disabled;
					if (over) {
						hovered = static_cast<int>(i);
					}
					ImU32 tc = e.disabled ? IM_COL32(95, 98, 106, static_cast<int>(190 * stagger)) :
					           IM_COL32(static_cast<int>(165 + 90 * glow[i]), static_cast<int>(170 + 80 * glow[i]), static_cast<int>(178 + 62 * glow[i]), static_cast<int>((205 + 50 * glow[i]) * stagger));
					if (e.accent && !e.disabled) {
						tc = (e.accent & 0x00FFFFFF) | (static_cast<ImU32>((190 + 65 * glow[i]) * stagger) << 24);
					}
					if (glow[i] > 0.02f) {  // the chosen word: a soft glow behind its letters, breathing
						const float pulse = 0.8f + 0.2f * std::sin(now * 3.0f);
						Txt(fg, g_title, fs, at, tc, up, 3.0f * k * glow[i], IM_COL32(255, 186, 80, static_cast<int>(255 * pulse * glow[i])));
					} else {
						Txt(fg, g_title, fs, at, tc, up);
					}
				}
				{  // dev: press a word by its text
					std::scoped_lock dl_(g_devPickLock);
					if (!g_devPick.empty()) {
						for (std::size_t i = 0; i < rows.size(); ++i) {
							std::string up = rows[i].text;
							std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
							if (up.rfind(g_devPick, 0) == 0) {
								hovered = static_cast<int>(i), click = true;
							}
						}
						logger::info("dev: menupick '{}' -> {}", g_devPick, hovered);
						g_devPick.clear();
					}
				}
				if (padOn && padFocus >= 0 && padFocus < static_cast<int>(rows.size())) {
					if (accepts > 0 && rows[padFocus].kind != 0) {
						hovered = padFocus, click = true;  // (Skyrim's own words take A / Enter themselves)
					}
					if (backs > 0 && (partyMode || settingsMode)) {
						partyMode = settingsMode = false;
					}
					if (sides != 0 && rows[padFocus].kind == 8) {
						if (rows[padFocus].arg == 2) {
							Settings::SetBrightness(Settings::Brightness() + 0.05f * sides);
						} else {
							const bool first = rows[padFocus].arg == 0;
							Settings::SetFov(first, (first ? Settings::FovFirst() : Settings::FovThird()) + 5.0f * sides);
						}
					}
				}
				partyHover = hovered >= 0 && rows[hovered].kind != 0 ? hovered : -1;
				if (!rows.empty()) {
					if (hovered >= 0 && hovered != hoverIdx && rows[hovered].kind == 0 && !rows[hovered].sel) {
						PressMainList(mv, rows[hovered].arg, false);
					}
					hoverIdx = hovered;
					click = click && !Net::OverlayBusy();  // a click that closed the Steam overlay is not ours
					if (click && hovered >= 0) {
						const auto& r = rows[hovered];
						switch (r.kind) {
						case 0: PressMainList(mv, r.arg, true); break;
						case 1: partyMode = true, refresh = 0; break;
						case 2: Net::HostLobby(); break;
						case 3: Net::InviteFriends(); break;
						case 4: Net::LeaveLobby(); break;
						case 5: if (r.arg < static_cast<int>(friends.size())) Net::JoinLobby(friends[r.arg].lobby); break;
						case 6: partyMode = settingsMode = false; break;
						case 7: settingsMode = true; break;
						case 9: {  // our Load: Skyrim's list lets go of its choice (its Enter / A would press it under ours)
							loadMode = true, RequestSaves();
							RE::GFxValue list;
							if (mv && mv->GetVariable(&list, "_root.MenuHolder.Menu_mc.MainList") && list.IsDisplayObject()) {
								list.SetMember("selectedIndex", RE::GFxValue(-1.0));
							}
							break;
						}
						case 8: {  // a click steps it up (round to the start past the top)
							if (r.arg == 2) {
								const float bv = Settings::Brightness() + 0.05f;
								Settings::SetBrightness(bv > Settings::kBrightMax + 1e-3f ? Settings::kBrightMin : bv);
								break;
							}
							const bool  first = r.arg == 0;
							const float v = (first ? Settings::FovFirst() : Settings::FovThird()) + 5.0f;
							Settings::SetFov(first, v > Settings::kFovMax ? Settings::kFovMin : v);
							break;
						}
						}
						g_pressT = now, g_pressY = top + rowH * (hovered + 0.5f);
					}
				}
			}
			// a pressed word answers: a brief cold flash on it
			if (const float pt = now - g_pressT; pt >= 0 && pt < 0.35f) {
				const float a = 1.0f - pt / 0.35f;
				fg->AddCircleFilled({ size.x - 110 * k - 80 * k, g_pressY }, 90 * k * (0.6f + 0.4f * (1 - a)), IM_COL32(255, 200, 110, static_cast<int>(40 * a)), 40);
			}
			// the curtain that lifts as the menu opens
			if (fade < 1.0f) {
				fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, static_cast<int>(255 * (1.0f - fade))));
			}
			// our cursor, the quest ping's diamond
			if (cur.x >= 0) {
				DrawPingCursor(fg, cur, k, now);
			}
		}


		// ---- The Curse of Alduin intro: once, when a new character first stands outside Riverwood. A storybook legend
		// cut in woodcut on parchment (tools/legend.py draws it: Data/Interface/CurseOfAlduin/legend/*), one picture
		// per couplet of the narration. Each picture is LAYERED and moves: the back pans slowly, the subject drifts on
		// its own parallax, flames / glows / the running dead cycle through three cut frames, the front moves across.
		// The couplet is carved under the picture and inks in, left to right, as it is spoken. Its own music and a
		// stinger at each page; Skyrim's own sound is silent. The world waits (paused); hold Space / Enter / E / pad A
		// to skip. Textures load when it starts and are released when it ends.
		struct FilmScene
		{
			const char* name;              // legend/<name>_back.jpg, _mid/_fx0..2/_front.png
			float       dur;
			float       z0, z1;            // zoom from, to
			float       x0, y0, x1, y1;    // pan of the picture (fraction of the spare picture, -1..1)
			float       frontDx0, frontDx1, frontBob;  // the front layer's own move across the scene (picture px), and its bob
			float       midBob;            // the subject's slow rise and fall (picture px)
			gen::Sound  cue;
			float       cueAt;
			gen::Sound  vo;
			float       voAt;              // the line starts (seconds into the scene)
			float       l2At;              // its second half is spoken
		};
		using GS = gen::Sound;
		// timed to the rhymed narration (vo_01..vo_09, measured: each couplet's second line starts at l2At)
		const FilmScene kFilm[] = {
			{ "helgen", 8.1f, 1.10f, 1.04f, -0.5f, 0.3f, 0.4f, -0.2f, -90, 110, 10, 4, GS::intro_helgen, 0.05f, GS::vo_01, 0.9f, 4.2f },
			{ "sky", 10.2f, 1.04f, 1.12f, 0.0f, -0.4f, 0.0f, 0.2f, 0, 0, 2, 12, GS::intro_dirge, 0.05f, GS::vo_02, 0.6f, 5.4f },
			{ "graves", 9.6f, 1.12f, 1.03f, 0.4f, 0.5f, -0.3f, 0.0f, 0, 0, 0, 3, GS::intro_graves, 0.05f, GS::vo_03, 0.6f, 5.0f },
			{ "dusk", 9.9f, 1.03f, 1.10f, 0.0f, -0.6f, 0.0f, 0.4f, 0, 0, 0, 0, GS::intro_graves, 0.05f, GS::vo_04, 0.6f, 4.9f },
			{ "horde", 11.9f, 1.12f, 1.04f, -0.3f, 0.2f, 0.3f, 0.0f, 0, 0, 6, 0, GS::intro_horde, 0.05f, GS::vo_05, 0.6f, 6.3f },
			{ "champions", 11.0f, 1.02f, 1.10f, 0.0f, 0.3f, 0.0f, -0.3f, 0, 0, 0, 5, GS::intro_champions, 0.05f, GS::vo_06, 0.6f, 5.2f },
			{ "wyrm", 10.9f, 1.10f, 1.03f, 0.3f, -0.2f, -0.3f, 0.2f, 0, 0, 0, 14, GS::intro_wyrm, 0.05f, GS::vo_07, 0.6f, 4.7f },
			{ "shrine", 10.3f, 1.03f, 1.12f, 0.2f, 0.3f, -0.1f, -0.1f, 0, 0, 0, 10, GS::intro_shrine, 0.05f, GS::vo_08, 0.6f, 5.35f },
			{ "dragonborn", 12.0f, 1.02f, 1.08f, 0.0f, 0.2f, 0.0f, -0.2f, 0, 0, 0, 4, GS::intro_title, 0.05f, GS::vo_09, 0.9f, 6.45f },
		};
		constexpr int kFilmScenes = static_cast<int>(std::size(kFilm));
		constexpr float kFilmXfade = 1.0f;
		enum FilmLayer { kLBack, kLMid, kLFx0, kLFx1, kLFx2, kLFront, kLayers };
		constexpr const char* kLayerName[kLayers] = { "back", "mid", "fx0", "fx1", "fx2", "front" };
		std::atomic<bool>         g_filmReq{ false };
		std::atomic<bool>         g_filmOn{ false };
		// the veil: black over everything while a new game turns into the film and the film into the game, so the
		// start prompts (answered for you) never flash and the world fades in only once it has settled
		float                                 g_veil = 0;
		bool                                  g_outro = false;
		std::chrono::steady_clock::time_point g_outroStart, g_outroBusy, g_reqSince, g_filmStart;
		std::atomic<long long>                g_holdUntilMs{ 0 };  // a new character is on its way to Riverwood: black until the film takes over
		std::atomic<bool>         g_veilNow{ false };              // black at once (no fade in)
		std::atomic<bool>         g_filmSkipHeld{ false };
		float                     g_filmT = 0, g_filmSkip = 0, g_filmEnd = -1;
		int                       g_filmCue = -1;
		int                       g_filmVo = -1;
		bool                      g_filmPaused = false;
		bool                      g_saveAfterFilm = false;          // the new character is saved once it stands in the world
		std::uint32_t             g_filmPauses = 0;  // pauses already in force when the film began
		ID3D11ShaderResourceView* g_filmTex[kFilmScenes][kLayers]{};
		struct FilmText { ID3D11ShaderResourceView* tex{}; float w{ 0 }, h{ 0 }; };
		FilmText                  g_filmLine[kFilmScenes][2]{};
		FilmText                  g_filmRunes, g_filmSkipText;
		ID3D11ShaderResourceView* g_filmPaper = nullptr;  // the page

		void FilmRelease()
		{
			for (auto& sc : g_filmTex) {
				for (auto*& t : sc) {
					if (t) t->Release();
					t = nullptr;
				}
			}
			for (auto& sc : g_filmLine) {
				for (auto& l : sc) {
					if (l.tex) l.tex->Release();
					l = {};
				}
			}
			for (auto* l : { &g_filmRunes, &g_filmSkipText }) {
				if (l->tex) l->tex->Release();
				*l = {};
			}
			if (g_filmPaper) {
				g_filmPaper->Release();
				g_filmPaper = nullptr;
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
			Audio::FilmQuiet(false);
			g_filmOn = false;
			logger::info("intro: film ended at {:.1f}s", g_filmT);
		}

		bool LoadTex(const std::string& a_rel, ID3D11ShaderResourceView** a_out, float* a_w = nullptr, float* a_h = nullptr)
		{
			const std::string  n = "Data\\Interface\\CurseOfAlduin\\" + a_rel;
			const std::wstring w(n.begin(), n.end());
			ID3D11Resource*    res = nullptr;
			if (FAILED(DirectX::CreateWICTextureFromFile(g_device, w.c_str(), &res, a_out))) {
				return false;
			}
			if (res) {
				ID3D11Texture2D* t2 = nullptr;
				if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t2))) && t2) {
					D3D11_TEXTURE2D_DESC d{};
					t2->GetDesc(&d);
					if (a_w) *a_w = static_cast<float>(d.Width);
					if (a_h) *a_h = static_cast<float>(d.Height);
					t2->Release();
				}
				res->Release();
			}
			return true;
		}

		void FilmStart()
		{
			int loaded = 0;
			for (int i = 0; i < kFilmScenes; ++i) {
				for (int l = 0; l < kLayers; ++l) {
					const std::string f = std::string("legend\\") + kFilm[i].name + "_" + kLayerName[l] + (l == kLBack ? ".jpg" : ".png");
					loaded += LoadTex(f, &g_filmTex[i][l]) ? 1 : 0;
				}
				for (int k = 0; k < 2; ++k) {
					char key[16];
					std::snprintf(key, sizeof(key), "l%02d%c", i + 1, k ? 'b' : 'a');
					auto& t = g_filmLine[i][k];
					LoadTex(std::string("legend\\text_") + key + ".png", &t.tex, &t.w, &t.h);
				}
			}
			LoadTex("legend\\text_runes.png", &g_filmRunes.tex, &g_filmRunes.w, &g_filmRunes.h);
			LoadTex("legend\\text_skip.png", &g_filmSkipText.tex, &g_filmSkipText.w, &g_filmSkipText.h);
			LoadTex("legend\\page_paper.jpg", &g_filmPaper);
			if (auto* ui = RE::UI::GetSingleton()) {
				g_filmPauses = ui->numPausesGame;
				++ui->numPausesGame;  // the world waits while the film plays
				g_filmPaused = true;
			}
			Audio::FilmQuiet(true);
			Audio::IntroMusic(true);
			g_filmT = 0, g_filmSkip = 0, g_filmEnd = -1, g_filmCue = -1, g_filmVo = -1;
			g_filmStart = std::chrono::steady_clock::now();
			g_filmOn = true;
			logger::info("intro: film starts ({} layers, page {})", loaded, g_filmPaper != nullptr);
		}

		void DrawFilm(ImVec2 size, float dt)
		{
			auto*       fg = ImGui::GetForegroundDrawList();
			const float k = size.y / 1080.0f;
			// the film's clock is the wall clock, so pictures and words keep time with the narrator however the frames fall
			g_filmT = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_filmStart).count();
			float total = 0;
			for (const auto& sc : kFilm) total += sc.dur;
			g_filmSkip = g_filmSkipHeld ? g_filmSkip + dt : std::max(0.0f, g_filmSkip - dt * 2);
			if (g_filmEnd < 0 && (g_filmT >= total || g_filmSkip >= 0.9f)) {
				g_filmEnd = g_filmT;
			}
			const float outFade = g_filmEnd < 0 ? 0.0f : std::clamp((g_filmT - g_filmEnd) / 1.2f, 0.0f, 1.0f);
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
			if (g_filmEnd < 0 && cur != g_filmCue && local >= kFilm[cur].cueAt) {  // the page turns: its stinger, under the narrator
				g_filmCue = cur;
				Audio::Ui(kFilm[cur].cue, 0.40f);
			}
			if (g_filmEnd < 0 && cur != g_filmVo && local >= kFilm[cur].voAt) {
				g_filmVo = cur;
				Audio::Voice(kFilm[cur].vo);
			}
			const auto tid = [](ID3D11ShaderResourceView* t) { return ImTextureID(reinterpret_cast<std::uintptr_t>(t)); };
			// the page
			fg->AddRectFilled({ 0, 0 }, size, IM_COL32(219, 199, 160, 255));
			if (g_filmPaper) {
				const float aspect = size.x / size.y, src = 1600.0f / 900.0f;
				const float uw = aspect < src ? aspect / src : 1.0f, vh = aspect < src ? 1.0f : src / aspect;
				fg->AddImage(tid(g_filmPaper), { 0, 0 }, size, { 0.5f - uw / 2, 0.5f - vh / 2 }, { 0.5f + uw / 2, 0.5f + vh / 2 });
			}
			// the picture: 16:9, centred, the words under it
			const float ph = std::min(size.y * 0.66f, size.x * 0.84f * 9.0f / 16.0f), pw = ph * 16.0f / 9.0f;
			const ImVec2 p0{ (size.x - pw) / 2, size.y * 0.045f }, p1{ p0.x + pw, p0.y + ph };
			const float  ps = pw / 1600.0f;  // picture px -> screen px
			auto scene = [&](int i, float t, float alpha) {
				const auto& sc = kFilm[i];
				if (alpha <= 0 || !g_filmTex[i][kLBack]) {
					return;
				}
				const float u = std::clamp(t / sc.dur, 0.0f, 1.0f);
				const float e = u * u * (3 - 2 * u) * 0.4f + u * 0.6f;
				const float z = sc.z0 + (sc.z1 - sc.z0) * e;
				const float span = 1.0f / z, spare = 1.0f - span;
				const float px = sc.x0 + (sc.x1 - sc.x0) * e, py = sc.y0 + (sc.y1 - sc.y0) * e;
				const ImU32 col = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));
				auto layer = [&](ID3D11ShaderResourceView* tex, float parallax, float ox, float oy) {
					if (!tex) return;
					// the same window into every layer, its centre pushed further for the nearer layers (kept on the sheet)
					const float cx = std::clamp(0.5f + px * spare / 2 * parallax, span / 2, 1 - span / 2);
					const float cy = std::clamp(0.5f + py * spare / 2 * parallax, span / 2, 1 - span / 2);
					fg->AddImage(tid(tex), { p0.x + ox * ps, p0.y + oy * ps }, { p1.x + ox * ps, p1.y + oy * ps }, { cx - span / 2, cy - span / 2 }, { cx + span / 2, cy + span / 2 }, col);
				};
				layer(g_filmTex[i][kLBack], 1.0f, 0, 0);
				layer(g_filmTex[i][kLMid], 1.25f, 0, std::sin(t * 0.9f) * sc.midBob);
				const int fr = static_cast<int>(t * 6.0f) % 3;  // three cut frames, six a second
				layer(g_filmTex[i][kLFx0 + fr], 1.25f, 0, 0);
				const float fx = sc.frontDx0 + (sc.frontDx1 - sc.frontDx0) * e;
				layer(g_filmTex[i][kLFront], 1.5f, fx, std::sin(t * 1.7f) * sc.frontBob);
			};
			fg->PushClipRect(p0, p1, true);
			const float inA = std::clamp(local / kFilmXfade, 0.0f, 1.0f);
			if (cur > 0 && local < kFilmXfade) {
				scene(cur - 1, kFilm[cur - 1].dur + local, 1.0f);
			}
			scene(cur, local, cur == 0 ? 1.0f : inA * inA * (3 - 2 * inA));
			fg->PopClipRect();
			// the words, carved: a band of the legend's own script, then the couplet inking in as it is spoken
			const float ts = size.x / 1600.0f * 0.92f;
			if (g_filmRunes.tex) {
				const float w = std::min(pw, g_filmRunes.w * ts * 0.8f), h = g_filmRunes.h * w / g_filmRunes.w;
				fg->AddImage(tid(g_filmRunes.tex), { (size.x - w) / 2, p1.y + 14 * k }, { (size.x + w) / 2, p1.y + 14 * k + h }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, 150));
			}
			const auto& sc = kFilm[cur];
			auto ink = [&](const FilmText& t, float y, float at) {
				if (!t.tex) return;
				const float reveal = std::clamp((local - at) / 0.9f, 0.0f, 1.0f);           // inked in left to right
				const float leave = std::clamp((sc.dur - 0.35f - local) / 0.5f, 0.0f, 1.0f);  // gone before the page turns
				if (reveal <= 0 || leave <= 0) return;
				float w = t.w * ts, h = t.h * ts;
				if (w > size.x - 80 * k) {
					h *= (size.x - 80 * k) / w, w = size.x - 80 * k;
				}
				const ImVec2 a{ (size.x - w) / 2, y };
				fg->PushClipRect({ a.x - 4, a.y - 4 }, { a.x + w * reveal + 4, a.y + h + 4 }, true);
				fg->AddImage(tid(t.tex), a, { a.x + w, a.y + h }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(255 * leave)));
				fg->PopClipRect();
			};
			const float ty = p1.y + 62 * k;
			ink(g_filmLine[cur][0], ty, sc.voAt - 0.1f);
			ink(g_filmLine[cur][1], ty + 64 * k, sc.l2At - 0.1f);
			// skip: the carved hint, and a ring that fills while the key is held
			{
				const float ha = std::clamp((g_filmT - 2.0f) / 1.0f, 0.0f, 1.0f) * (1.0f - outFade);
				// bottom centre, clear of the page's swirled borders: the words, then the ring after them
				const float sw = g_filmSkipText.tex ? g_filmSkipText.w * ts : 0, sh = g_filmSkipText.tex ? g_filmSkipText.h * ts : 0;
				const ImVec2 c{ (size.x + sw) / 2 + 22 * k, size.y - 30 * k };
				if (g_filmSkipText.tex) {
					fg->AddImage(tid(g_filmSkipText.tex), { c.x - 22 * k - sw, c.y - sh / 2 }, { c.x - 22 * k, c.y + sh / 2 }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(200 * ha)));
				}
				fg->AddCircle(c, 12 * k, IM_COL32(34, 24, 18, static_cast<int>(130 * ha)), 32, 2.0f * k);
				if (g_filmSkip > 0) {
					fg->PathArcTo(c, 12 * k, -1.5708f, -1.5708f + 6.2832f * std::min(1.0f, g_filmSkip / 0.9f), 32);
					fg->PathStroke(IM_COL32(170, 40, 30, static_cast<int>(255 * ha)), 0, 3.5f * k);
				}
			}
			const float inFade = 1.0f - std::clamp(g_filmT / 1.2f, 0.0f, 1.0f);
			if (inFade > 0) fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, static_cast<int>(255 * inFade)));
			if (outFade > 0) fg->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, static_cast<int>(255 * outFade)));
			if (outFade >= 1.0f) {
				FilmStop();
				g_outro = true, g_veil = 1.0f;
				g_saveAfterFilm = true;
				g_outroStart = g_outroBusy = std::chrono::steady_clock::now();
				Tasks::Add([] {  // into the game in first person
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
			// our own type (SIL Open Font License, in Data/Interface/CurseOfAlduin/fonts): Cinzel for titles and numbers,
			// Cormorant Garamond for the words; Windows' Georgia / Segoe if they are missing
			auto own = [&](const char* a_file) -> ImFont* {
				const std::string p = std::string("Data\\Interface\\CurseOfAlduin\\fonts\\") + a_file;
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
				const auto path = L"Data\\Interface\\CurseOfAlduin\\" + std::wstring(gen::kPerks[i].id.begin(), gen::kPerks[i].id.end()) + L".png";
				if (SUCCEEDED(DirectX::CreateWICTextureFromFile(g_device, path.c_str(), nullptr, &g_perkIcons[i]))) {
					++icons;
				}
			}
			int wicons = 0;
			for (std::size_t i = 0; i < gen::kGuns.size(); ++i) {
				const auto path = L"Data\\Interface\\CurseOfAlduin\\w_" + std::wstring(gen::kGuns[i].id.begin(), gen::kGuns[i].id.end()) + L".png";
				if (SUCCEEDED(DirectX::CreateWICTextureFromFile(g_device, path.c_str(), nullptr, &g_weaponIcons[i]))) {
					++wicons;
				}
			}
			for (int b = 0; b < Bows::Count() && b < static_cast<int>(g_bowIcons.size()); ++b) {  // the altar's bows' emblems
				const std::string_view el = Bows::Element(b);
				const auto path = L"Data\\Interface\\CurseOfAlduin\\bw_" + std::wstring(el.begin(), el.end()) + L".png";
				DirectX::CreateWICTextureFromFile(g_device, path.c_str(), nullptr, &g_bowIcons[b]);
			}
			for (int e = 0; e < 8; ++e) {
				const std::string_view n = Arcana::ElementName(e);
				const auto path = L"Data\\Interface\\CurseOfAlduin\\el_" + std::wstring(n.begin(), n.end()) + L".png";
				DirectX::CreateWICTextureFromFile(g_device, path.c_str(), nullptr, &g_elementIcons[e]);
				static constexpr const wchar_t* kSchools[5] = { L"destruction", L"conjuration", L"restoration", L"alteration", L"illusion" };
				for (int sc = 0; sc < 5; ++sc) {
					const auto sp = std::wstring(L"Data\\Interface\\CurseOfAlduin\\sc_") + kSchools[sc] + L"_" + std::wstring(n.begin(), n.end()) + L".png";
					DirectX::CreateWICTextureFromFile(g_device, sp.c_str(), nullptr, &g_schoolIcons[sc][e]);
				}
			}
			const bool bg = SUCCEEDED(DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_bg.png", nullptr, &g_menuBg));
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_fog.png", nullptr, &g_menuFog);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\mapicons.png", nullptr, &g_mapIcons);  // the map's marker icons (tools/mapicons.py)
			for (auto [file, tex] : { std::pair{ L"menu_sky.jpg", &g_mSky }, { L"menu_clouds.png", &g_mClouds }, { L"menu_aurora.png", &g_mAurora },
					 { L"menu_far.png", &g_mFar }, { L"menu_mid.png", &g_mMid }, { L"menu_near.png", &g_mNear }, { L"menu_groundfog.png", &g_mGroundFog }, { L"menu_clouds2.png", &g_mClouds2 } }) {
				DirectX::CreateWICTextureFromFile(g_device, (std::wstring(L"Data\\Interface\\CurseOfAlduin\\") + file).c_str(), nullptr, tex);
			}
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_title.png", nullptr, &g_mTitle);
			for (auto [file, tex] : { std::pair{ L"menu_shrine_ground.png", &g_sGround }, { L"menu_shrine_glow.png", &g_sGlow }, { L"menu_crystal.png", &g_sCrystal }, { L"menu_softglow.png", &g_sSoft }, { L"menu_mountains.png", &g_sMtn }, { L"menu_lake_refl.png", &g_sRefl }, { L"menu_lake.png", &g_sLake }, { L"menu_aurora2.png", &g_sAurora }, { L"menu_lake_life.png", &g_sLife }, { L"menu_motes.png", &g_sMotes } }) {
				DirectX::CreateWICTextureFromFile(g_device, (std::wstring(L"Data\\Interface\\CurseOfAlduin\\") + file).c_str(), nullptr, tex);
			}
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_rock_turn.png", nullptr, &g_sRockTurn);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_rock_turn_r.png", nullptr, &g_sRockTurnR);
			g_sRockSheet[0] = g_sRockTurn, g_sRockSheetR[0] = g_sRockTurnR, g_sCrystalSheet[0] = g_sCrystal;
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_rock_turn_2.png", nullptr, &g_sRockSheet[1]);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_rock_turn_2_r.png", nullptr, &g_sRockSheetR[1]);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_rock_turn_3.png", nullptr, &g_sRockSheet[2]);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_rock_turn_3_r.png", nullptr, &g_sRockSheetR[2]);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_crystal2.png", nullptr, &g_sCrystalSheet[1]);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_crystal3.png", nullptr, &g_sCrystalSheet[2]);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_crystal4.png", nullptr, &g_sCrystalSheet[3]);
			for (int i = 0; i < 9; ++i) {
				DirectX::CreateWICTextureFromFile(g_device, (L"Data\\Interface\\CurseOfAlduin\\menu_shard" + std::to_wstring(i) + L".png").c_str(), nullptr, &g_sShard[i]);
				DirectX::CreateWICTextureFromFile(g_device, (L"Data\\Interface\\CurseOfAlduin\\menu_shard" + std::to_wstring(i) + L"_r.png").c_str(), nullptr, &g_sShardR[i]);
			}
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\frame.png", nullptr, &g_frame);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\cursor_moonlit.png", nullptr, &g_cursorTex);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\flame_ring.png", nullptr, &g_flame);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\menu_fire.png", nullptr, &g_fire);
			DirectX::CreateWICTextureFromFile(g_device, L"Data\\Interface\\CurseOfAlduin\\divider.png", nullptr, &g_divider);
			g_init = true;
			logger::info("hud: ready ({} perk icons, {} weapon icons, menu art {})", icons, wicons, bg);
			return true;
		}


		// ---- The Codex (Noah: the lore and the guide easier to read than a book): reading either book opens this over
		// Skyrim's book, which is hidden: a tab per book, its chapters down the left, the chapter on a parchment page with
		// its woodcut. Wheel or W/S scroll, A/D or the list change chapter, Q/E or the tabs change book, Esc closes.
		int                       g_cxOpenBook = -1;  // the book being read (-1: none)
		int                       g_cxCh = 0;         // its chapter (index into codex::kChapters)
		float                     g_cxY = 0, g_cxYGoal = 0, g_cxH = 0;
		ID3D11ShaderResourceView* g_cxPaper = nullptr;
		std::unordered_map<std::string, std::pair<ID3D11ShaderResourceView*, ImVec2>> g_cxArt;
		int CodexBookOf(const RE::TESObjectBOOK* a_book)
		{
			if (!a_book || !a_book->GetFile(0) || a_book->GetFile(0)->GetFilename() != gen::kPlugin) {
				return -1;
			}
			const auto local = a_book->GetLocalFormID();
			return local == gen::kDirgeBook ? 0 : local == gen::kGuideBook ? 1 : -1;
		}
		std::vector<int> CodexChapters(int a_book)
		{
			std::vector<int> out;
			for (int i = 0; i < static_cast<int>(codex::kChapters.size()); ++i) {
				if (codex::kChapters[i].book == a_book) {
					out.push_back(i);
				}
			}
			return out;
		}

		// a paragraph with <b> and <i>, wrapped word by word; returns its height
		float RichText(ImDrawList* dl, std::string_view a_text, ImVec2 a_at, float a_width, float px, bool a_draw)
		{
			constexpr ImU32 kInk = IM_COL32(44, 30, 20, 255), kBold = IM_COL32(122, 26, 18, 255), kItal = IM_COL32(86, 62, 38, 255);
			bool  bold = false, ital = false;
			float x = 0, y = 0;
			const float lineH = px * 1.42f;
			std::string word;
			auto flush = [&](bool a_space) {
				if (word.empty()) {
					if (a_space) x += Measure(g_body, px, " ").x;
					return;
				}
				ImFont*     f = bold ? g_title : g_body;
				const float w = Measure(f, px, word).x;
				if (x > 0 && x + w > a_width) {
					x = 0, y += lineH;
				}
				if (a_draw) {
					dl->AddText(f, px, { a_at.x + x, a_at.y + y }, bold ? kBold : ital ? kItal : kInk, word.c_str());
				}
				x += w;
				if (a_space) x += Measure(g_body, px, " ").x;
				word.clear();
			};
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const char c = a_text[i];
				if (c == '<') {
					const auto end = a_text.find('>', i);
					if (end != std::string_view::npos) {
						const auto tag = a_text.substr(i, end - i + 1);
						flush(false);
						if (tag == "<b>") bold = true;
						else if (tag == "</b>") bold = false;
						else if (tag == "<i>") ital = true;
						else if (tag == "</i>") ital = false;
						i = end;
						continue;
					}
				}
				if (c == ' ') {
					flush(true);
				} else {
					word += c;
				}
			}
			flush(false);
			return y + lineH;
		}

		void DrawCodex(ImVec2 size, float now, int clicks)
		{
			auto*       dl = ImGui::GetForegroundDrawList();
			const float k = size.y / 1080.0f;
			const ImVec2 cur = g_cursor;
			auto hover = [&](ImVec2 a, ImVec2 b) { return cur.x >= a.x && cur.x <= b.x && cur.y >= a.y && cur.y <= b.y; };
			bool click = clicks > 0;
			// input since the last frame
			if (const int b = g_cxBook.exchange(0); b != 0) {
				g_cxOpenBook = (g_cxOpenBook + (b > 0 ? 1 : 1)) % 2, g_cxCh = CodexChapters(g_cxOpenBook).front(), g_cxY = g_cxYGoal = 0;
			}
			auto chs = CodexChapters(g_cxOpenBook);
			if (chs.empty()) {
				return;
			}
			int at = static_cast<int>(std::find(chs.begin(), chs.end(), g_cxCh) - chs.begin());
			if (at >= static_cast<int>(chs.size())) {
				at = 0, g_cxCh = chs[0];
			}
			if (const int d = g_cxChapter.exchange(0); d != 0) {
				at = std::clamp(at + d, 0, static_cast<int>(chs.size()) - 1);
				g_cxCh = chs[at], g_cxY = g_cxYGoal = 0;
			}
			g_cxYGoal += g_cxScroll.exchange(0) * 70.0f * k;

			// the room: near black, a cold vignette
			dl->AddRectFilled({ 0, 0 }, size, IM_COL32(3, 3, 5, 255));  // (black, the modpack's ground)
			dl->AddRectFilledMultiColor({ 0, 0 }, { size.x, size.y * 0.5f }, IM_COL32(30, 70, 130, 45), IM_COL32(30, 70, 130, 45), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
			const float W = std::min(size.x - 60 * k, 1680 * k), H = 960 * k;
			const ImVec2 o{ (size.x - W) * 0.5f, (size.y - H) * 0.5f };

			// the two books, as tabs
			const float tabY = o.y + 8 * k;
			float       tx = o.x + 20 * k;
			for (int b = 0; b < 2; ++b) {
				const std::string t = Upper(std::string(codex::kBooks[b]));
				const ImVec2      m = Measure(g_title, 26 * k, t);
				const ImVec2      a{ tx, tabY }, z{ tx + m.x, tabY + m.y + 10 * k };
				const bool        on = b == g_cxOpenBook, hv = hover(a, z);
				Txt(dl, g_title, 26 * k, a, on ? kGold : hv ? IM_COL32(230, 215, 185, 255) : IM_COL32(150, 140, 125, 255), t, on ? 2.0f * k : 0, kGold);
				if (on) {
					dl->AddLine({ a.x, z.y }, { z.x, z.y }, kGold, 2 * k);
				}
				if (hv && click && !on) {
					g_cxOpenBook = b, chs = CodexChapters(b), at = 0, g_cxCh = chs[0], g_cxY = g_cxYGoal = 0, click = false;
				}
				tx += m.x + 60 * k;
			}
			// close
			{
				const ImVec2 c{ o.x + W - 26 * k, tabY + 18 * k };
				const bool   hv = hover({ c.x - 22 * k, c.y - 22 * k }, { c.x + 22 * k, c.y + 22 * k });
				const ImU32  col = hv ? kGold : IM_COL32(170, 160, 140, 255);
				dl->AddLine({ c.x - 11 * k, c.y - 11 * k }, { c.x + 11 * k, c.y + 11 * k }, col, 2.4f * k);
				dl->AddLine({ c.x + 11 * k, c.y - 11 * k }, { c.x - 11 * k, c.y + 11 * k }, col, 2.4f * k);
				if (hv && click) {
					if (auto* q = RE::UIMessageQueue::GetSingleton()) {
						q->AddMessage(RE::BookMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
					}
					click = false;
				}
			}

			const float top = o.y + 70 * k, bottom = o.y + H - 40 * k;
			// the chapters
			const float lw = 400 * k;
			Panel(dl, { o.x, top }, { o.x + lw, bottom }, k);
			float ly = top + 34 * k;
			for (int j = 0; j < static_cast<int>(chs.size()); ++j) {
				const auto&  ch = codex::kChapters[chs[j]];
				const ImVec2 a{ o.x + 18 * k, ly - 6 * k }, z{ o.x + lw - 18 * k, ly + 34 * k };
				const bool   on = j == at, hv = hover(a, z);
				if (on || hv) {
					dl->AddRectFilled(a, z, on ? IM_COL32(230, 228, 220, 40) : IM_COL32(255, 255, 255, 14), 4 * k);
				}
				if (on) {
					dl->AddRectFilled(a, { a.x + 4 * k, z.y }, kGold);
				}
				TxtWrap(dl, g_title, 20 * k, { a.x + 16 * k, ly }, on ? kParch : hv ? IM_COL32(225, 212, 188, 255) : IM_COL32(165, 155, 138, 255), std::string(ch.title), lw - 70 * k, 1);
				if (hv && click) {
					at = j, g_cxCh = chs[j], g_cxY = g_cxYGoal = 0, click = false;
				}
				ly += 46 * k;
			}

			// the page
			const ImVec2 pa{ o.x + lw + 24 * k, top }, pz{ o.x + W, bottom };
			if (!g_cxPaper) {
				static bool tried = false;
				if (!tried) {
					tried = true;
					LoadTex("legend\\page_paper.jpg", &g_cxPaper, nullptr, nullptr);
				}
			}
			if (g_cxPaper) {
				dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(g_cxPaper)), pa, pz);
			} else {
				dl->AddRectFilledMultiColor(pa, pz, IM_COL32(236, 224, 194, 255), IM_COL32(230, 216, 184, 255), IM_COL32(214, 196, 158, 255), IM_COL32(222, 206, 170, 255));
			}
			dl->AddRect(pa, pz, IM_COL32(200, 202, 206, 210), 0, 0, 2 * k);
			const auto& ch = codex::kChapters[g_cxCh];
			const float mx = 70 * k, colW = std::min(pz.x - pa.x - 2 * mx, 900 * k);
			const float cx = (pa.x + pz.x) * 0.5f, x0 = cx - colW * 0.5f;
			const float viewTop = pa.y + 26 * k, viewBot = pz.y - 26 * k;
			const float maxY = std::max(0.0f, g_cxH - (viewBot - viewTop));
			g_cxYGoal = std::clamp(g_cxYGoal, 0.0f, maxY);
			g_cxY += (g_cxYGoal - g_cxY) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
			dl->PushClipRect({ pa.x, viewTop }, { pz.x, viewBot }, true);
			float y = viewTop + 30 * k - g_cxY;
			{
				const std::string t(ch.title);
				const float       tw = Measure(g_title, 38 * k, t).x;
				dl->AddText(g_title, 38 * k, { cx - tw * 0.5f, y }, IM_COL32(70, 34, 20, 255), t.c_str());
				y += 54 * k;
				dl->AddLine({ cx - 160 * k, y }, { cx - 10 * k, y }, IM_COL32(110, 74, 40, 200), 1.5f * k);
				dl->AddLine({ cx + 10 * k, y }, { cx + 160 * k, y }, IM_COL32(110, 74, 40, 200), 1.5f * k);
				dl->AddQuadFilled({ cx, y - 6 * k }, { cx + 6 * k, y }, { cx, y + 6 * k }, { cx - 6 * k, y }, IM_COL32(122, 26, 18, 255));
				y += 30 * k;
			}
			if (!ch.image.empty()) {
				auto it = g_cxArt.find(std::string(ch.image));
				if (it == g_cxArt.end()) {
					ID3D11ShaderResourceView* t = nullptr;
					float w = 0, h = 0;
					LoadTex("..\\..\\Textures\\CurseOfAlduin\\Book\\" + std::string(ch.image) + ".png", &t, &w, &h);
					it = g_cxArt.emplace(std::string(ch.image), std::make_pair(t, ImVec2{ w, h })).first;
				}
				if (auto* t = it->second.first; t && it->second.second.x > 0) {
					const float w = std::min(colW * 0.7f, it->second.second.x * 1.4f * k), h = w * it->second.second.y / it->second.second.x;
					dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(t)), { cx - w * 0.5f, y }, { cx + w * 0.5f, y + h });
					y += h + 26 * k;
				}
			}
			for (const auto& para : ch.paras) {
				y += RichText(dl, para, { x0, y }, colW, 23 * k, true) + 16 * k;
			}
			g_cxH = y + g_cxY - viewTop + 30 * k;
			dl->PopClipRect();
			// more below / above
			if (g_cxY < maxY - 2) {
				dl->AddRectFilledMultiColor({ pa.x + 2, viewBot - 50 * k }, { pz.x - 2, viewBot }, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(60, 40, 20, 70), IM_COL32(60, 40, 20, 70));
			}
			// prev / next chapter along the bottom of the page
			auto arrowBtn = [&](bool a_next) {
				const bool ok = a_next ? at + 1 < static_cast<int>(chs.size()) : at > 0;
				if (!ok) return;
				const std::string t = a_next ? std::string(codex::kChapters[chs[at + 1]].title) + "  >" : "<  " + std::string(codex::kChapters[chs[at - 1]].title);
				const ImVec2      m = Measure(g_title, 18 * k, t);
				const ImVec2      a{ a_next ? pz.x - m.x - 24 * k : pa.x + 24 * k, bottom + 8 * k }, z{ a.x + m.x, a.y + m.y };
				const bool        hv = hover(a, z);
				Txt(dl, g_title, 18 * k, a, hv ? kGold : IM_COL32(170, 158, 138, 255), t);
				if (hv && click) {
					at += a_next ? 1 : -1, g_cxCh = chs[at], g_cxY = g_cxYGoal = 0, click = false;
				}
			};
			arrowBtn(false);
			arrowBtn(true);
			TxtC(dl, g_body, 15 * k, o.x + lw * 0.5f, bottom + 10 * k, IM_COL32(140, 130, 115, 255), "Wheel / W S  scroll    A D  chapter    Q E  book    Esc  close");
			if (cur.x >= 0) {
				DrawPingCursor(dl, cur, k, now);
			}
		}

		bool CodexOpen() { return g_cxOpenBook >= 0; }
		// each frame: is one of our two books open in Skyrim's book menu? then the Codex shows in its place
		bool CodexWanted(RE::UI* a_ui)
		{
			const bool book = a_ui && a_ui->IsMenuOpen(RE::BookMenu::MENU_NAME);
			const int  which = book ? CodexBookOf(RE::BookMenu::GetTargetForm()) : -1;
			if (which < 0) {
				g_cxOpenBook = -1;
				return false;
			}
			if (g_cxOpenBook < 0) {  // just opened: at the book picked, its first chapter (the scroll and keys from before forgotten)
				g_cxOpenBook = which, g_cxCh = CodexChapters(which).front(), g_cxY = g_cxYGoal = 0;
				g_cxScroll = 0, g_cxChapter = 0, g_cxBook = 0;
				logger::info("hud: the Codex opens at {}", codex::kBooks[which]);
			}
			if (auto m = a_ui->GetMenu(RE::BookMenu::MENU_NAME); m && m->uiMovie && m->uiMovie->GetVisible()) {
				m->uiMovie->SetVisible(false);  // Skyrim's book under ours never shows
			}
			return true;
		}

		bool InGame()
		{
			auto* ui = RE::UI::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!ui || !player || !player->Is3DLoaded() || ui->GameIsPaused() || ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
				return false;
			}
			// (with Skyrim Souls the menus no longer pause the game: they still hide the HUD)
			for (auto name : { RE::InventoryMenu::MENU_NAME, RE::MagicMenu::MENU_NAME, RE::MapMenu::MENU_NAME, RE::StatsMenu::MENU_NAME, RE::TweenMenu::MENU_NAME,
					 RE::JournalMenu::MENU_NAME, RE::ContainerMenu::MENU_NAME, RE::BarterMenu::MENU_NAME, RE::GiftMenu::MENU_NAME, RE::BookMenu::MENU_NAME,
					 RE::LockpickingMenu::MENU_NAME, RE::Console::MENU_NAME, RE::MessageBoxMenu::MENU_NAME, RE::FavoritesMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME,
					 RE::TrainingMenu::MENU_NAME, RE::CraftingMenu::MENU_NAME, RE::LevelUpMenu::MENU_NAME, RE::RaceSexMenu::MENU_NAME, RE::DialogueMenu::MENU_NAME }) {
				if (ui->IsMenuOpen(name)) {
					return false;
				}
			}
			return true;
		}

		void Frame(IDXGISwapChain* a_swap)
		{
			const auto  now = std::chrono::steady_clock::now();
			const float dt = std::clamp(std::chrono::duration<float>(now - g_last).count(), 0.0001f, 0.25f);
			g_last = now;

			DevTools::PollFromRender();
			DevTools::RunQueuedOnMain();  // Present runs on the main thread, also while menus pause the game
			Tasks::Drain();
			Hud::State s;
			{
				std::scoped_lock l(g_lock);
				g_state.bannerTime = std::max(0.0f, g_state.bannerTime - dt);
				g_state.gainTime = std::max(0.0f, g_state.gainTime - dt);
				g_state.hitTime = std::max(0.0f, g_state.hitTime - dt);
				g_state.headBurstT = std::max(0.0f, g_state.headBurstT - dt);
				g_state.styleTime = std::max(0.0f, g_state.styleTime - dt);
				g_state.lightningT = std::max(0.0f, g_state.lightningT - dt);
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
			std::string popupName;
			if (uiM) {
				static const std::array<std::string_view, 6> kQuiet{ "Main Menu", "Cursor Menu", "Fader Menu", "Mist Menu", "HUD Menu", "Kinect Menu" };
				// (other mods' overlays - SkyParkour's prompts, a loot window, widgets - are always open or don't take
				// the game over: only a menu that pauses, is modal or brings the cursor counts as a pop-up)
				auto takesOver = [](const RE::IMenu& m) {
					using F = RE::UI_MENU_FLAGS;
					return !m.menuFlags.any(F::kAlwaysOpen) && m.menuFlags.any(F::kPausesGame, F::kModal, F::kUsesCursor);
				};
				for (auto& [name, entry] : uiM->menuMap) {
					if (entry.menu && uiM->IsMenuOpen(name) && std::find(kQuiet.begin(), kQuiet.end(), std::string_view(name.c_str())) == kQuiet.end() && takesOver(*entry.menu)) {
						popup = true;
						popupName = name.c_str();
						break;
					}
				}
			}
			// the Steam overlay (Host a Party's invite window) is not one of Skyrim's pop-ups: our main menu stays drawn under it
			const bool steamOver = Net::OverlayBusy();
			{
				static std::string lastPopup;
				if (popupName != lastPopup) {
					logger::info("hud: pop-up over the game '{}'{}", popupName, steamOver ? " (Steam overlay up)" : "");
					lastPopup = popupName;
				}
			}
			const bool mainMenu = uiM && uiM->IsMenuOpen(RE::MainMenu::MENU_NAME) && (!popup || steamOver);
			const bool codex = !mainMenu && CodexWanted(uiM);
			// Skyrim's cursor never shows: ours (DrawPingCursor) is drawn in its place, on the main menu and in game
			// (the world map moves Skyrim's cursor without its cursor menu: ours is drawn there too)
			const bool cursorUp = uiM && (uiM->IsMenuOpen(RE::CursorMenu::MENU_NAME) || uiM->IsMenuOpen(RE::MapMenu::MENU_NAME));
			if (cursorUp) {
				if (auto cm = uiM->GetMenu(RE::CursorMenu::MENU_NAME); cm && cm->uiMovie && cm->uiMovie->GetVisible()) {
					cm->uiMovie->SetVisible(false);
				}
			}
			if (cursorUp || mainMenu || codex) {  // (and Windows' own pointer stays hidden over the game: ours is the only one)
				::SetCursor(nullptr);
			}
			// the Tab menu (Skills / Magic / Items / Map), on its own: the party panel sits in its empty top-left corner
			const bool journal = uiM && uiM->IsMenuOpen(RE::TweenMenu::MENU_NAME) && !uiM->IsMenuOpen(RE::MainMenu::MENU_NAME) &&
			                     !uiM->IsMenuOpen(RE::InventoryMenu::MENU_NAME) && !uiM->IsMenuOpen(RE::MagicMenu::MENU_NAME) &&
			                     !uiM->IsMenuOpen(RE::MapMenu::MENU_NAME) && !uiM->IsMenuOpen(RE::StatsMenu::MENU_NAME);

			const bool escJournal = uiM && uiM->IsMenuOpen(RE::JournalMenu::MENU_NAME) && !uiM->IsMenuOpen(RE::MainMenu::MENU_NAME);  // Esc: Quests / Stats / System
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
					if (since > 5.0f || (since > 1.2f && quiet > 0.5f)) {
						g_outro = false;
						logger::info("intro: into the game after {:.1f}s of black", since);
						if (g_saveAfterFilm) {  // the new character's first save: a death now reloads this character, never an older one
							g_saveAfterFilm = false;
							Tasks::Add([] {
								auto* pl = RE::PlayerCharacter::GetSingleton();
								auto* sl = RE::BGSSaveLoadManager::GetSingleton();
								if (pl && sl && !pl->IsDead()) {
									const std::string name = std::string("CurseOfAlduin - ") + pl->GetName() + " - Night Begins";
									sl->Save(name.c_str());
									logger::info("intro: saved the new character ('{}')", name);
								}
							});
						}
					}
				}
				if (!g_filmReq) {
					g_reqSince = t;
				}
				const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
			const bool want = !mainMenu && (g_filmOn || g_outro || ms < g_holdUntilMs || (g_filmReq && t - g_reqSince < std::chrono::seconds(20)));
				if (g_veilNow.exchange(false) && !mainMenu) {
					g_veil = 1.0f;  // a new character's arrival: black at once, never a glimpse of the road first
				}
				g_veil = mainMenu ? 0.0f : want ? std::min(1.0f, g_veil + dt * 4.0f) : std::max(0.0f, g_veil - dt / 1.2f);
			}
			// (a menu with a pointer - the world map, the inventory - still draws: our cursor is the only one there)
			if (((!InGame() && !mainMenu && !journal && !escJournal && !cursorUp && !codex) || (DevTools::HudOff() && !mainMenu && !codex)) && !g_filmOn && g_veil <= 0) {
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

			UpdateCursor(io.DisplaySize);
			ImGui_ImplDX11_NewFrame();
			ImGui::NewFrame();
			if (g_filmOn) {
				DrawFilm(io.DisplaySize, dt);
			} else if (codex) {
				DrawCodex(io.DisplaySize, static_cast<float>(ImGui::GetTime()), clicks);
			} else if (mainMenu) {
				DrawMenu(io.DisplaySize, static_cast<float>(ImGui::GetTime()), clicks > 0);
			} else if (journal || escJournal) {
				// (Noah: the Party tab in Skyrim's system menu) in the Esc journal a PARTY tab at the top left (click it, or P /
				// Triangle-Y): it opens the party window - the lobby, everyone's night, last night's score and your records
				if (escJournal) {
					const float kk = io.DisplaySize.y / 1080.0f;
					auto*       dl = ImGui::GetForegroundDrawList();
					const ImVec2 cur = g_cursor;
					const ImVec2 ta{ 36 * kk, 30 * kk }, tb{ 306 * kk, 76 * kk };
					const bool   over = cur.x >= ta.x && cur.x <= tb.x && cur.y >= ta.y && cur.y <= tb.y;
					if (over && clicks > 0) g_partyOpen = !g_partyOpen;
					Panel(dl, ta, tb, kk, 0.95f);
					const std::string lab = std::string(g_partyOpen ? "CLOSE PARTY" : "PARTY") + "   P / " + (Settings::PlayStationIcons() ? "Triangle" : "Y");  // (Noah: both keys shown)
					TxtC(dl, g_title, 18 * kk, (ta.x + tb.x) * 0.5f, ta.y + 12 * kk, over || g_partyOpen ? kIce : IM_COL32(200, 214, 235, 235), lab, over ? 1.5f * kk : 0.0f, kRune);
					if (g_partyOpen) {
						PartyPanel(dl, io.DisplaySize, { 36 * kk, 90 * kk }, kk, clicks > 0 && !over);
					}
				} else {
					g_partyOpen = false;
				}
			} else if (InGame() && !DevTools::HudOff()) {
				Draw(s, io.DisplaySize);
				// looking at the Echo Altar: Skyrim's own "Commune" prompt gives way to ours (s.orbAim), back for everything else
				if (static bool dumped = false; !dumped) {
					if (auto hud = uiM ? uiM->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr; hud && hud->uiMovie) {
						dumped = true;
						DumpMembers(hud->uiMovie.get(), "_root.HUDMovieBaseInstance", false);
					}
				}
				// (Noah: only our quick loot on everything - Skyrim's "Search" prompt never shows over a body, chest or ash pile)
				// (set every frame while ours shows: Skyrim fades its prompt back in each time the crosshair finds something)
				// (Noah: looking away from the Echo Altar left Skyrim's A button and its line behind) hidden too while the
				// crosshair is on nothing - it only comes back for something of Skyrim's own to use
				bool onNothing = true;
				if (auto* pick = RE::CrosshairPickData::GetSingleton(); pick && pick->target.get()) onNothing = false;
				const bool hideRoll = s.orbAim || s.ownAim || s.lootOn || onNothing;
				if (static int lastAim = -1; true) {  // (every frame: Skyrim keeps fading its own back in)
					if (auto hud = uiM ? uiM->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr; hud && hud->uiMovie) {
						lastAim = hideRoll ? 1 : 0;
						// its words come back when it is Skyrim's turn; its button glyph and the grey line under it never do
						// (Noah: left behind on the screen after looking away from a foe, they were all that showed)
						const RE::GFxValue a(lastAim ? 0.0 : 100.0), off(0.0);
						for (const char* v : { "_root.HUDMovieBaseInstance.RolloverText._alpha", "_root.HUDMovieBaseInstance.RolloverInfoText._alpha",
							     "_root.HUDMovieBaseInstance.RolloverInfoInstance._alpha", "_root.HUDMovieBaseInstance.RolloverNameInstance._alpha" }) {
							hud->uiMovie->SetVariable(v, a);
						}
						for (const char* v : { "_root.HUDMovieBaseInstance.RolloverButton_tf._alpha", "_root.HUDMovieBaseInstance.RolloverGrayBar_mc._alpha",
							     "_root.HUDMovieBaseInstance.RolloverButton._alpha", "_root.HUDMovieBaseInstance.RolloverButtonInstance._alpha",
							     "_root.HUDMovieBaseInstance.RolloverGrayBar._alpha", "_root.HUDMovieBaseInstance.GrayBarInstance._alpha",
							     "_root.HUDMovieBaseInstance.ActivateButton_tf._alpha", "_root.HUDMovieBaseInstance.ActivateButton._alpha" }) {
							hud->uiMovie->SetVariable(v, off);
						}
					}
				}
				// Skyrim's enemy bar under the compass gives way to ours over each foe
				if (static int vb = 0; vb++ % 15 == 0) {
					if (auto hud = uiM ? uiM->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr; hud && hud->uiMovie) {
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.EnemyHealth_mc._visible", RE::GFxValue(false));
						// Skyrim's compass gives way to ours - its shout meter too (Noah: the cooldown is drawn round our compass's
						// border instead: src/Hud.cpp DrawCompass)
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.CompassShoutMeterHolder._alpha", RE::GFxValue(0.0));
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.CompassShoutMeterHolder._visible", RE::GFxValue(false));
						// (Noah: the base Skyrim sneak symbol again) its sneak eye stays its own
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.StealthMeterInstance._alpha", RE::GFxValue(100.0));
						// (Noah: the experience notices sat behind our night plaque) Skyrim's notices top-left move down under it
						static double msgY0 = 1e9;
						if (msgY0 > 1e8) {
							RE::GFxValue y0;
							if (hud->uiMovie->GetVariable(&y0, "_root.HUDMovieBaseInstance.MessagesBlock._y") && y0.IsNumber()) msgY0 = y0.GetNumber();
						}
						if (msgY0 < 1e8) {
							hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.MessagesBlock._y", RE::GFxValue(msgY0 + 150.0));
						}
						// (Noah: no pop-ups) Skyrim's quest and objective notices ("Stand in the runic circle...") never show
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.QuestUpdateBaseInstance._alpha", RE::GFxValue(0.0));
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.QuestUpdateBaseInstance._visible", RE::GFxValue(false));
						// and its crosshair to our dot
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.CrosshairInstance._alpha", RE::GFxValue(0.0));
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.CrosshairAlert._alpha", RE::GFxValue(0.0));
						// (Noah: the arrow count sat behind our bow card) the altar's bows never run dry, so Skyrim's arrow line
						// bottom-right goes while one is in hand; any other bow keeps it
						hud->uiMovie->SetVariable("_root.HUDMovieBaseInstance.ArrowInfoInstance._alpha", RE::GFxValue(s.bowHeld >= 0 ? 0.0 : 100.0));
					}
				}
			}
			if (cursorUp && !mainMenu && !codex && !g_filmOn && g_skCursor.x >= 0) {  // in Skyrim's own menus: ours, right where its cursor is
				DrawPingCursor(ImGui::GetForegroundDrawList(), g_skCursor, io.DisplaySize.y / 1080.0f, static_cast<float>(ImGui::GetTime()));
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
			Watchdog::Beat();
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
		void OnMouseMove(float a_dx, float a_dy)
		{
			g_mouseDX = g_mouseDX + a_dx, g_mouseDY = g_mouseDY + a_dy;
			if (std::abs(a_dx) + std::abs(a_dy) > 3.0f) {
				g_padLast = false;
			}
		}
		// the main menu by pad or keys: up / down through every word (Skyrim's and ours), A / Enter presses ours
		void OnMenuButton(const RE::ButtonEvent* a_b)
		{
			if (a_b && a_b->IsDown()) {
				g_padLast = a_b->GetDevice() == RE::INPUT_DEVICE::kGamepad;
			}
			auto* ui = RE::UI::GetSingleton();
			// the Party tab in the Esc journal: P, or Triangle / Y
			if (a_b && a_b->IsDown() && ui && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) && !ui->IsMenuOpen(RE::MainMenu::MENU_NAME) &&
				((a_b->GetDevice() == RE::INPUT_DEVICE::kKeyboard && a_b->GetIDCode() == 0x19) || (a_b->GetDevice() == RE::INPUT_DEVICE::kGamepad && a_b->GetIDCode() == 0x8000))) {
				g_partyOpen = !g_partyOpen;
				g_partyFocus = 0;
			}
			if (a_b && a_b->IsDown() && ui && g_partyOpen && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME)) {  // (Noah) the party window by D-pad / arrows
				const auto id = a_b->GetIDCode();
				const auto dev = a_b->GetDevice();
				if ((dev == RE::INPUT_DEVICE::kGamepad && id == 0x0001) || (dev == RE::INPUT_DEVICE::kKeyboard && id == 0xC8)) g_partyFocus = g_partyFocus - 1;
				else if ((dev == RE::INPUT_DEVICE::kGamepad && id == 0x0002) || (dev == RE::INPUT_DEVICE::kKeyboard && id == 0xD0)) g_partyFocus = g_partyFocus + 1;
				else if ((dev == RE::INPUT_DEVICE::kGamepad && id == 0x1000) || (dev == RE::INPUT_DEVICE::kKeyboard && id == 0x1C)) g_partyPress = 1;
			}
			if (a_b && a_b->IsDown() && ui && g_cxOpenBook >= 0 && ui->IsMenuOpen(RE::BookMenu::MENU_NAME)) {  // the Codex
				const auto id = a_b->GetIDCode();
				const auto dev = a_b->GetDevice();
				if (dev == RE::INPUT_DEVICE::kKeyboard) {
					if (id == 0xC8 || id == 0x11) --g_cxScroll;        // up, W
					else if (id == 0xD0 || id == 0x1F) ++g_cxScroll;   // down, S
					else if (id == 0xC9) g_cxScroll -= 6;              // page up
					else if (id == 0xD1) g_cxScroll += 6;              // page down
					else if (id == 0xCB || id == 0x1E) --g_cxChapter;  // left, A
					else if (id == 0xCD || id == 0x20) ++g_cxChapter;  // right, D
					else if (id == 0x10 || id == 0x12) ++g_cxBook;     // Q, E
				} else if (dev == RE::INPUT_DEVICE::kMouse) {
					if (id == 8) g_cxScroll -= 1;
					else if (id == 9) g_cxScroll += 1;
				} else if (dev == RE::INPUT_DEVICE::kGamepad) {
					if (id == 0x0001) --g_cxScroll;
					else if (id == 0x0002) ++g_cxScroll;
					else if (id == 0x0004) --g_cxChapter;
					else if (id == 0x0008) ++g_cxChapter;
					else if (id == 0x0100 || id == 0x0200) ++g_cxBook;  // the shoulders
				}
				return;
			}
			// F1, in the world: the Codex, at the Survivor's Guide
			if (a_b && a_b->IsDown() && a_b->GetDevice() == RE::INPUT_DEVICE::kKeyboard && a_b->GetIDCode() == 0x3B && InGame()) {
				Tasks::AddUI([] {
					auto* book = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectBOOK>(gen::kGuideBook, gen::kPlugin);
					auto* pl = RE::PlayerCharacter::GetSingleton();
					auto* em = RE::ActorEquipManager::GetSingleton();
					if (book && pl && em && !RE::UI::GetSingleton()->IsMenuOpen(RE::BookMenu::MENU_NAME)) {
						if (pl->GetInventoryCounts([&](RE::TESBoundObject& o) { return &o == book; }).empty()) {
							pl->AddObjectToContainer(book, nullptr, 1, nullptr);  // (the guide was dropped: a fresh copy)
						}
						em->EquipObject(pl, book);  // (reading it from the pack: Skyrim opens its book, the Codex shows over it)
					}
				});
				return;
			}
			if (!a_b || !a_b->IsDown() || !ui || !ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
				return;
			}
			const auto id = a_b->GetIDCode();
			if (a_b->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
				if (id == 0x0001) --g_padMove;        // d-pad up
				else if (id == 0x0002) ++g_padMove;   // d-pad down
				else if (id == 0x1000) ++g_padAccept; // A
				else if (id == 0x2000) ++g_padBack;   // B
				else if (id == 0x0004) --g_padSide;   // d-pad left
				else if (id == 0x0008) ++g_padSide;   // d-pad right
			} else if (a_b->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
				if (id == 0xC8 || id == 0x11) --g_padMove;       // up, W
				else if (id == 0xD0 || id == 0x1F) ++g_padMove; // down, S
				else if (id == 0x1C || id == 0x12) ++g_padAccept; // Enter, E
				else if (id == 0x0E || id == 0x01) ++g_padBack;   // Backspace, Esc
				else if (id == 0xCB || id == 0x1E) --g_padSide;   // left, A
				else if (id == 0xCD || id == 0x20) ++g_padSide;   // right, D
			} else if (a_b->GetDevice() == RE::INPUT_DEVICE::kMouse) {
				if (id == 8) --g_wheel;        // the wheel, up
				else if (id == 9) ++g_wheel;   // ... and down
			}
		}
		void DevMenuPick(const std::string& a_text)
		{
			std::scoped_lock l(g_devPickLock);
			g_devPick = a_text;
			std::transform(g_devPick.begin(), g_devPick.end(), g_devPick.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
		}
		// The film is off for now (in a co-op party Skyrim Together's own screens kept pausing it, and it began again
		// and again): a new character goes from black straight into the game, and is saved there.
		constexpr bool kFilmOn = false;
		void IntroStart()
		{
			if (kFilmOn) {
				g_filmReq = true;
				return;
			}
			g_outro = true, g_veil = 1.0f;
			g_saveAfterFilm = false;  // (the race menu is still to come: the first save waits for it, AfterCharacterMade)
			g_holdUntilMs = 0;  // (the long hold was for the film: without it, black lasted 15-20 s)
			g_outroStart = g_outroBusy = std::chrono::steady_clock::now();
			Tasks::Add([] {
				if (auto* cam = RE::PlayerCamera::GetSingleton()) {
					cam->ForceFirstPerson();
				}
			});
			logger::info("intro: no film (off for now): straight into the game");
		}
		void AfterCharacterMade()
		{
			g_outro = true, g_veil = 1.0f;
			g_saveAfterFilm = true;
			g_holdUntilMs = 0;
			g_outroStart = g_outroBusy = std::chrono::steady_clock::now();
			logger::info("intro: character made: a moment of black, then the game (and its first save)");
		}
		bool IntroActive() { return g_filmOn || g_filmReq; }
		bool IntroBusy()
		{
			const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			return g_filmOn || g_filmReq || g_outro || ms < g_holdUntilMs;
		}
		void IntroVeil(float a_seconds, bool a_instant)
		{
			if (a_instant) {
				g_veilNow = true;
			}
			g_holdUntilMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + static_cast<long long>(a_seconds * 1000);
		}
		void IntroSkipHeld(bool a_held) { g_filmSkipHeld = a_held; }

		// DevTools: press an entry of Skyrim's main list (0 Continue, 1 New, ...), or drive the party panel
		void DevMain(int a_index)
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
			if (menu && menu->uiMovie) {
				PressMainList(menu->uiMovie.get(), a_index, true);
				logger::info("dev: main list {} pressed", a_index);
			}
		}
		void DevParty(const std::string& a_what)
		{
			if (a_what == "host") {
				Net::HostLobby();
			} else if (a_what == "invite") {
				Net::InviteFriends();
			} else if (a_what == "leave") {
				Net::LeaveLobby();
			}
			logger::info("dev: party {}", a_what);
		}

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
