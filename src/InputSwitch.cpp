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

	// each frame: the controller read directly; a menu told when the mode changed (its buttons, its cursor)
	void Tick()
	{
		if (g_getState && g_kbm.load()) {
			static DWORD lastPacket[4]{};
			static bool  primed[4]{};
			for (DWORD i = 0; i < 4; ++i) {
				XINPUT_STATE s{};
				if (g_getState(i, &s) != ERROR_SUCCESS) continue;
				const auto& g = s.Gamepad;
				const bool moved = std::abs(g.sThumbLX) > 12000 || std::abs(g.sThumbLY) > 12000 || std::abs(g.sThumbRX) > 12000 || std::abs(g.sThumbRY) > 12000;
				const bool pressed = g.wButtons != 0 || g.bLeftTrigger > 60 || g.bRightTrigger > 60;
				if (primed[i] && s.dwPacketNumber != lastPacket[i] && (moved || pressed)) Set(false, "the controller was used");
				lastPacket[i] = s.dwPacketNumber, primed[i] = true;
			}
		}
		if (!g_changed.exchange(false)) return;
		SKSE::GetTaskInterface()->AddUITask([] {
			auto* ui = RE::UI::GetSingleton();
			if (!ui) return;
			bool cursor = false;
			for (auto& [name, entry] : ui->menuMap) {
				if (entry.menu && ui->IsMenuOpen(name)) {
					entry.menu->RefreshPlatform();
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
