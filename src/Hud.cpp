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
				Text(dl, gen::kHud_points_popup, size, k, (s.lastGain > 0 ? "+" : "") + std::to_string(s.lastGain), 1.0f, s.gainTime, s.lastGain < 0 ? 0xE03030FF : 0);
			}
			if (s.countdown > 0.05f) {  // between nights
				const std::string line = "NIGHT " + std::to_string(s.round + 1) + " IN " + std::to_string(static_cast<int>(std::ceil(s.countdown)));
				const float       fs = 46 * k;
				const auto        ts = g_font->CalcTextSizeA(fs, FLT_MAX, 0, line.c_str());
				const float       pulse = 0.75f + 0.25f * std::sin(s.countdown * 6.0f);
				dl->AddText(g_font, fs, { size.x * 0.5f - ts.x * 0.5f + 2 * k, size.y * 0.16f + 2 * k }, IM_COL32(0, 0, 0, 180), line.c_str());
				dl->AddText(g_font, fs, { size.x * 0.5f - ts.x * 0.5f, size.y * 0.16f }, IM_COL32(200, 30, 30, static_cast<int>(255 * pulse)), line.c_str());
			}
			// Perk row above Skyrim's health bar (bottom centre), up to 9.
			{
				int n = 0;
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					n += (s.perks >> i) & 1;
				}
				const float r = 20 * k, gap = 46 * k;
				auto        c = Anchor(gen::kHud_perk_row, size, k);
				float       x = c.x - (n - 1) * gap * 0.5f;
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					if (!((s.perks >> i) & 1)) {
						continue;
					}
					if (auto* icon = g_perkIcons[i]) {
						dl->AddImage(ImTextureID(reinterpret_cast<std::uintptr_t>(icon)), { x - r, c.y - r }, { x + r, c.y + r });
					} else {
						dl->AddCircleFilled({ x, c.y }, r, Col(gen::kPerkColor[i]));
						dl->AddCircle({ x, c.y }, r, IM_COL32(0, 0, 0, 200), 0, 2 * k);
						const std::string letter(1, gen::kPerks[i].label[0]);
						const auto        ts = g_font->CalcTextSizeA(20 * k, FLT_MAX, 0, letter.c_str());
						dl->AddText(g_font, 20 * k, { x - ts.x / 2, c.y - ts.y / 2 }, IM_COL32(255, 255, 255, 235), letter.c_str());
					}
					x += gap;
				}
			}
			if (s.crew >= 0) {
				constexpr gen::HudItem crewItem{ "crew", 1, gen::kHud_points.ox, gen::kHud_points.oy + 36, 24, 0xB8B8B8FF };
				Text(dl, crewItem, size, k, std::string(gen::kCrew[s.crew].label), 1.0f, 0.8f);
			}

			if (s.gunOut) {
				Text(dl, gen::kHud_ammo, size, k, s.reloading ? "RELOADING" : s.gunName + "   " + std::to_string(s.clip) + " / " + std::to_string(s.reserve), 1.0f,
					1.0f, s.clip == 0 && !s.reloading ? 0xE03030FF : 0);
				if (s.ads < 0.95f) {
					const auto  c = Anchor(gen::kHud_crosshair, size, k);
					const float g = 10 * k * (1.0f - s.ads * 0.6f), l = 9 * k;
					const auto  col = Col(gen::kHud_crosshair.rgba, 0.9f * (1.0f - s.ads));
					dl->AddLine({ c.x - g - l, c.y }, { c.x - g, c.y }, col, 2 * k);
					dl->AddLine({ c.x + g, c.y }, { c.x + g + l, c.y }, col, 2 * k);
					dl->AddLine({ c.x, c.y - g - l }, { c.x, c.y - g }, col, 2 * k);
					dl->AddLine({ c.x, c.y + g }, { c.x, c.y + g + l }, col, 2 * k);
				} else {
					const auto c = Anchor(gen::kHud_crosshair, size, k);
					dl->AddCircleFilled(c, 2.5f * k, IM_COL32(255, 60, 60, 230));
				}
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
			if (s.crewScreen) {
				dl->AddRectFilled({ 0, 0 }, size, IM_COL32(0, 0, 0, 170));
				const char* title = "CHOOSE YOUR CHARACTER";
				const auto  ts = g_font->CalcTextSizeA(56 * k, FLT_MAX, 0, title);
				dl->AddText(g_font, 56 * k, { (size.x - ts.x) / 2, size.y * 0.16f }, IM_COL32(200, 20, 20, 255), title);
				const float w = size.x * 0.19f, gap = size.x * 0.02f, x0 = (size.x - (4 * w + 3 * gap)) / 2, y0 = size.y * 0.30f, h = size.y * 0.42f;
				for (int i = 0; i < 4; ++i) {
					const auto& c = gen::kCrew[i];
					const float x = x0 + i * (w + gap);
					const bool  on = i == s.crewHighlight;
					dl->AddRectFilled({ x, y0 }, { x + w, y0 + h }, on ? IM_COL32(70, 10, 10, 235) : IM_COL32(25, 25, 25, 220), 6 * k);
					dl->AddRect({ x, y0 }, { x + w, y0 + h }, on ? IM_COL32(230, 40, 40, 255) : IM_COL32(90, 90, 90, 255), 6 * k, 0, on ? 4 * k : 2 * k);
					dl->AddText(g_font, 30 * k, { x + 16 * k, y0 + 14 * k }, IM_COL32(150, 150, 150, 255), std::to_string(i + 1).c_str());
					dl->AddText(g_font, 38 * k, { x + 16 * k, y0 + 60 * k }, IM_COL32(240, 230, 200, 255), std::string(c.label).c_str());
					dl->AddText(g_font, 22 * k, { x + 16 * k, y0 + 120 * k }, IM_COL32(200, 200, 200, 255), std::string(c.quote).c_str(), nullptr, w - 32 * k);
					dl->AddText(g_font, 24 * k, { x + 16 * k, y0 + h - 60 * k }, IM_COL32(230, 200, 60, 255), c.passiveText.data());
				}
				const char* hint = "Press 1-4 to pick, Enter to confirm";
				const auto  hs = g_font->CalcTextSizeA(30 * k, FLT_MAX, 0, hint);
				dl->AddText(g_font, 30 * k, { (size.x - hs.x) / 2, y0 + h + 40 * k }, IM_COL32(220, 220, 220, 255), hint);
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
