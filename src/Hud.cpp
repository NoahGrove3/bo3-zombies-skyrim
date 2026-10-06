#include "BO3Z.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>

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

		void Draw(const Hud::State& s, ImVec2 size)
		{
			auto*       dl = ImGui::GetForegroundDrawList();
			const float k = size.y / 1080.0f;

			// Round counter: chalk tallies for 1-5, digits after (BO3).
			if (s.round > 0) {
				const auto& it = gen::kHud_round_counter;
				if (s.round <= 5) {
					auto        p = Anchor(it, size, k);
					const float h = it.fontPx * k, w = 6 * k, gap = 16 * k;
					for (int i = 0; i < std::min(s.round, 4); ++i) {
						const float x = p.x + i * gap;
						dl->AddLine({ x, p.y - h * 0.5f }, { x + 3 * k, p.y + h * 0.5f }, Col(it.rgba), w);
					}
					if (s.round == 5) {
						dl->AddLine({ p.x - 8 * k, p.y + h * 0.3f }, { p.x + 3 * gap + 8 * k, p.y - h * 0.3f }, Col(it.rgba), w);
					}
				} else {
					Text(dl, it, size, k, std::to_string(s.round), 0.0f);
				}
			}

			Text(dl, gen::kHud_points, size, k, std::to_string(s.points), 1.0f);
			if (s.gainTime > 0) {
				Text(dl, gen::kHud_points_popup, size, k, "+" + std::to_string(s.lastGain), 1.0f, s.gainTime);
			}

			if (s.gunOut) {
				Text(dl, gen::kHud_ammo, size, k, s.reloading ? "RELOADING" : std::string(gen::kGun_m1911.label) + "   " + std::to_string(s.clip) + " / " + std::to_string(s.reserve), 1.0f,
					1.0f, s.clip == 0 && !s.reloading ? 0xE03030FF : 0);
				const auto  c = Anchor(gen::kHud_crosshair, size, k);
				const float g = 10 * k, l = 9 * k;
				const auto  col = Col(gen::kHud_crosshair.rgba, 0.9f);
				dl->AddLine({ c.x - g - l, c.y }, { c.x - g, c.y }, col, 2 * k);
				dl->AddLine({ c.x + g, c.y }, { c.x + g + l, c.y }, col, 2 * k);
				dl->AddLine({ c.x, c.y - g - l }, { c.x, c.y - g }, col, 2 * k);
				dl->AddLine({ c.x, c.y + g }, { c.x, c.y + g + l }, col, 2 * k);
			}
			if (s.hitTime > 0) {
				const auto  c = Anchor(gen::kHud_hitmarker, size, k);
				const float a = 6 * k, b = 14 * k;
				const auto  col = s.hitKill ? IM_COL32(230, 40, 40, 255) : Col(gen::kHud_hitmarker.rgba);
				for (int sx : { -1, 1 }) {
					for (int sy : { -1, 1 }) {
						dl->AddLine({ c.x + sx * a, c.y + sy * a }, { c.x + sx * b, c.y + sy * b }, col, 2.5f * k);
					}
				}
			}
			if (s.bannerTime > 0 && !s.banner.empty()) {
				Text(dl, gen::kHud_banner, size, k, s.banner, 0.5f, std::min(1.0f, s.bannerTime));
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
			if (::GetFileAttributesA("C:\\Windows\\Fonts\\impact.ttf") != INVALID_FILE_ATTRIBUTES) {
				g_font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\impact.ttf", 72.0f);
			}
			if (!g_font) {
				g_font = io.Fonts->AddFontDefault();
			}
			if (!ImGui_ImplDX11_Init(g_device, g_context)) {
				g_failed = true;
				logger::error("hud: ImGui DX11 init failed");
				return false;
			}
			g_init = true;
			logger::info("hud: ready");
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

			Hud::State s;
			{
				std::scoped_lock l(g_lock);
				g_state.bannerTime = std::max(0.0f, g_state.bannerTime - dt);
				g_state.gainTime = std::max(0.0f, g_state.gainTime - dt);
				g_state.hitTime = std::max(0.0f, g_state.hitTime - dt);
				s = g_state;
			}
			if (!InGame() || !Init(a_swap)) {
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

			ID3D11RenderTargetView* oldRtv = nullptr;
			ID3D11DepthStencilView* oldDsv = nullptr;
			g_context->OMGetRenderTargets(1, &oldRtv, &oldDsv);
			g_context->OMSetRenderTargets(1, &rtv, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			g_context->OMSetRenderTargets(1, &oldRtv, oldDsv);
			if (oldRtv) oldRtv->Release();
			if (oldDsv) oldDsv->Release();
			rtv->Release();
		}

		HRESULT WINAPI PresentHook(IDXGISwapChain* a_swap, UINT a_sync, UINT a_flags)
		{
			try {
				Frame(a_swap);
			} catch (...) {
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
