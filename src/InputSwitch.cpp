// (Noah: "The mouse cursor still doesn't let me select the main menu buttons like continue, new, load, quit, and it's
// invisible when using the race menu") with a controller plugged in, Skyrim keeps every menu in its controller mode: no
// cursor, clicks ignored. Now whichever was used last wins - move the mouse or press a key and the game is in keyboard
// and mouse mode (the cursor back, the clicks taken); touch the controller and it's back in controller mode. The
// controller is watched directly (XInput), since Skyrim stops reading it while it is switched off.
#include "CurseOfAlduin.h"

#include <Xinput.h>

namespace coa::InputSwitch
{
	namespace
	{
		std::atomic<bool> g_kbm{ false };   // keyboard and mouse in use (the controller set aside)
		std::atomic<bool> g_changed{ false };
		std::atomic<std::chrono::steady_clock::time_point> g_lastKbm{ std::chrono::steady_clock::time_point{} };   // the mouse or a key, last

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
	void OnKeyboardOrMouse(bool a_moved) { g_lastKbm = std::chrono::steady_clock::now(); Set(true, a_moved ? "the mouse moved" : "a key or click"); }
	// (the main menu: the mouse moved slowly, a count or two a frame, never reached one big step) its movement added up over
	// half a second - a real move, not a nudge of the desk
	void OnMouseMove(int a_dx, int a_dy)
	{
		static auto  since = std::chrono::steady_clock::now();
		static float acc = 0.0f;
		const auto   now = std::chrono::steady_clock::now();
		if (now - since > std::chrono::milliseconds(500)) acc = 0.0f, since = now;
		acc += static_cast<float>(std::abs(a_dx) + std::abs(a_dy));
		if (std::abs(a_dx) + std::abs(a_dy) > 0) g_lastKbm = now;
		if (acc > 25.0f) Set(true, "the mouse moved"), acc = 0.0f;
	}

	// each frame: the controller read directly; a menu told when the mode changed (its buttons, its cursor)
	void Tick()
	{
		// (the log: keyboard-and-mouse, then 11 ms later "the controller was used" - a stick that never rests at the middle)
		// the controller counts only when it moves away from where it lay when the keyboard and mouse took over: a button
		// newly pressed, a trigger squeezed, a stick pushed well past where it sat
		if (g_getState) {
			// (Noah, Nov 28, the log: "keyboard and mouse mode (the mouse moved)", then "controller mode (a controller stick)" 40 ms
			// later, over and over - "menus ignore mouse, switches to pad, keys don't respond, cursor wrong") a controller lying on
			// the desk took the game back from the mouse many times a second. Now it must really be used: a stick pushed well
			// over (past 60%, away from where it lay) and held there a fifth of a second, or a button pressed and held two reads;
			// and for half a second after the mouse or keys were last used, only a button counts.
			static XINPUT_GAMEPAD rest[4]{};
			static bool           have[4]{};
			static bool           wasKbm = false;
			static float          held[4]{};
			static int            pressedFor[4]{};
			static auto           lastTick = std::chrono::steady_clock::now();
			const auto            tnow = std::chrono::steady_clock::now();
			const float           dt = std::min(0.1f, std::chrono::duration<float>(tnow - lastTick).count());
			lastTick = tnow;
			const bool            kbm = g_kbm.load();
			const bool            quiet = tnow - g_lastKbm.load() < std::chrono::milliseconds(500);
			for (DWORD i = 0; i < 4; ++i) {
				XINPUT_STATE st{};
				if (g_getState(i, &st) != ERROR_SUCCESS) { have[i] = false; continue; }
				const auto& g = st.Gamepad;
				if (!kbm || !wasKbm || !have[i]) { rest[i] = g, have[i] = true, held[i] = 0, pressedFor[i] = 0; continue; }   // (the resting place, taken as the switch happens)
				const auto& r = rest[i];
				const auto pushed = [](SHORT a, SHORT b) { return std::abs(static_cast<int>(a)) > 19660 && std::abs(static_cast<int>(a) - static_cast<int>(b)) > 13000; };
				const bool pressed = (g.wButtons & ~r.wButtons) != 0 || g.bLeftTrigger > r.bLeftTrigger + 120 || g.bRightTrigger > r.bRightTrigger + 120;
				const bool moved = pushed(g.sThumbLX, r.sThumbLX) || pushed(g.sThumbLY, r.sThumbLY) || pushed(g.sThumbRX, r.sThumbRX) || pushed(g.sThumbRY, r.sThumbRY);
				pressedFor[i] = pressed ? pressedFor[i] + 1 : 0;
				held[i] = moved ? held[i] + dt : 0.0f;
				if (pressedFor[i] >= 2) {
					Set(false, "a controller button");
					static int told = 0;
					if (told++ < 10) logger::info("input: pad {} buttons {:04X} (resting {:04X}) triggers {} {}", i, g.wButtons, r.wButtons, g.bLeftTrigger, g.bRightTrigger);
				} else if (!quiet && held[i] > 0.2f) {
					Set(false, "a controller stick");
					static int told = 0;
					if (told++ < 10) logger::info("input: pad {} sticks L {},{} R {},{} (resting L {},{} R {},{})", i, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY, r.sThumbLX, r.sThumbLY, r.sThumbRX, r.sThumbRY);
				}
			}
			wasKbm = kbm;
		}
		// (Noah, Nov 28: Skyrim's "This save relies on Creations... Activate now?" box over the main menu took no clicks) a menu
		// opened after the last switch came up on whichever platform Skyrim thought it was: each newly opened menu is told too
		if (auto* ui = RE::UI::GetSingleton()) {
			static std::size_t lastOpen = 0;
			const std::size_t  open = ui->menuStack.size();
			if (open != lastOpen) lastOpen = open, g_changed = true;
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
					if (entry.menu->menuFlags.any(RE::IMenu::Flag::kUpdateUsesCursor) || name == RE::MessageBoxMenu::MENU_NAME) {
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
