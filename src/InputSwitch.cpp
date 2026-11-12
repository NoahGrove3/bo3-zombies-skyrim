// (Noah: "The mouse cursor still doesn't let me select the main menu buttons like continue, new, load, quit, and it's
// invisible when using the race menu") with a controller plugged in, Skyrim keeps every menu in its controller mode: no
// cursor, clicks ignored. Now whichever was used last wins - move the mouse or press a key and the game is in keyboard
// and mouse mode (the cursor back, the clicks taken); touch the controller and it's back in controller mode. The
// controller is watched directly (XInput), since Skyrim stops reading it while it is switched off.
#include "BO3Z.h"

#include <Xinput.h>

namespace bo3z::InputSwitch
{
	namespace
	{
		std::atomic<bool> g_kbm{ false };   // keyboard and mouse in use (the controller set aside)
		std::atomic<bool> g_changed{ false };

		template <int N>
		struct PadEnabled
		{
			static bool thunk(RE::BSIInputDevice* a_this) { return !g_kbm.load() && func(a_this); }
			static inline REL::Relocation<decltype(thunk)> func;
		};

		using GetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		GetState_t g_getState = nullptr;

		void Set(bool a_kbm, const char* a_why)
		{
			if (g_kbm.exchange(a_kbm) != a_kbm) {
				g_changed = true;
				static int told = 0;
				if (told++ < 30) logger::info("input: {} mode ({})", a_kbm ? "keyboard and mouse" : "controller", a_why);
			}
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> xin{ RE::VTABLE_BSWin32GamepadDevice[0] };
		PadEnabled<0>::func = xin.write_vfunc(0x7, PadEnabled<0>::thunk);
		REL::Relocation<std::uintptr_t> ps{ RE::VTABLE_BSPCOrbisGamepadDevice[0] };
		PadEnabled<1>::func = ps.write_vfunc(0x7, PadEnabled<1>::thunk);
		for (const wchar_t* dll : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" }) {
			if (auto* h = ::LoadLibraryW(dll)) {
				g_getState = reinterpret_cast<GetState_t>(::GetProcAddress(h, "XInputGetState"));
				if (g_getState) break;
			}
		}
		logger::info("input: keyboard/mouse and controller switch by last use (controller watched: {})", g_getState != nullptr);
	}

	bool KeyboardMouse() { return g_kbm.load(); }

	// keyboard keys and the mouse (from the input sink)
	void OnKeyboardOrMouse(bool a_moved) { Set(true, a_moved ? "the mouse moved" : "a key or click"); }
	// (the main menu: the mouse moved slowly, a count or two a frame, never reached one big step) its movement added up over
	// half a second - a real move, not a nudge of the desk
	void OnMouseMove(int a_dx, int a_dy)
	{
		static auto  since = std::chrono::steady_clock::now();
		static float acc = 0.0f;
		const auto   now = std::chrono::steady_clock::now();
		if (now - since > std::chrono::milliseconds(500)) acc = 0.0f, since = now;
		acc += static_cast<float>(std::abs(a_dx) + std::abs(a_dy));
		if (acc > 25.0f) Set(true, "the mouse moved"), acc = 0.0f;
	}

	// each frame: the controller read directly; a menu told when the mode changed (its buttons, its cursor)
	void Tick()
	{
		// (the log: keyboard-and-mouse, then 11 ms later "the controller was used" - a stick that never rests at the middle)
		// the controller counts only when it moves away from where it lay when the keyboard and mouse took over: a button
		// newly pressed, a trigger squeezed, a stick pushed well past where it sat
		if (g_getState) {
			static XINPUT_GAMEPAD rest[4]{};
			static bool           have[4]{};
			static bool           wasKbm = false;
			const bool            kbm = g_kbm.load();
			for (DWORD i = 0; i < 4; ++i) {
				XINPUT_STATE st{};
				if (g_getState(i, &st) != ERROR_SUCCESS) { have[i] = false; continue; }
				const auto& g = st.Gamepad;
				if (!kbm || !wasKbm || !have[i]) { rest[i] = g, have[i] = true; continue; }   // (the resting place, taken as the switch happens)
				const auto& r = rest[i];
				const auto far = [](SHORT a, SHORT b) { return std::abs(static_cast<int>(a) - static_cast<int>(b)) > 9000; };
				const bool pressed = (g.wButtons & ~r.wButtons) != 0 || g.bLeftTrigger > r.bLeftTrigger + 80 || g.bRightTrigger > r.bRightTrigger + 80;
				const bool moved = far(g.sThumbLX, r.sThumbLX) || far(g.sThumbLY, r.sThumbLY) || far(g.sThumbRX, r.sThumbRX) || far(g.sThumbRY, r.sThumbRY);
				if (pressed || moved) Set(false, pressed ? "a controller button" : "a controller stick");
			}
			wasKbm = kbm;
		}
		if (!g_changed.exchange(false)) return;
		SKSE::GetTaskInterface()->AddUITask([] {
			auto* ui = RE::UI::GetSingleton();
			if (!ui) return;
			bool cursor = false;
			const bool kbm = g_kbm.load();
			for (auto& [name, entry] : ui->menuMap) {
				if (entry.menu && ui->IsMenuOpen(name)) {
					entry.menu->RefreshPlatform();
					// (the main menu was opened in controller mode: no cursor flag, its lists on the controller's platform) a
					// menu that follows the platform gets the cursor flag with it, and its own lists told directly
					if (entry.menu->menuFlags.any(RE::IMenu::Flag::kUpdateUsesCursor)) {
						if (kbm) entry.menu->menuFlags.set(RE::IMenu::Flag::kUsesCursor);
						else entry.menu->menuFlags.reset(RE::IMenu::Flag::kUsesCursor);
					}
					if (name == RE::MainMenu::MENU_NAME && entry.menu->uiMovie) {
						RE::GFxValue mc;
						if (entry.menu->uiMovie->GetVariable(&mc, "_root.MenuHolder.Menu_mc") && mc.IsObject()) {
							RE::GFxValue args[2]{ RE::GFxValue(kbm ? 0.0 : 1.0), RE::GFxValue(false) };
							const bool ok = mc.Invoke("SetPlatform", nullptr, args, 2);
							for (const char* l : { "MainList", "MainListHolder.List_mc" }) {
								RE::GFxValue list;
								if (entry.menu->uiMovie->GetVariable(&list, (std::string("_root.MenuHolder.Menu_mc.") + l).c_str()) && list.IsObject()) {
									list.Invoke("SetPlatform", nullptr, args, 2);
									list.SetMember("bMouseDrivenNav", RE::GFxValue(kbm));
								}
							}
							logger::info("input: the main menu told it's on {} ({})", kbm ? "keyboard and mouse" : "controller", ok ? "done" : "no SetPlatform");
						}
					}
					if (entry.menu->UsesCursor()) cursor = true;
				}
			}
			// the cursor itself: Skyrim's own cursor menu, shown for the mouse, put away for the controller
			if (auto* q = RE::UIMessageQueue::GetSingleton(); q && cursor) {
				q->AddMessage(RE::CursorMenu::MENU_NAME, g_kbm.load() ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
		});
	}
}
