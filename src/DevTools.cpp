#include "BO3Z.h"

#include <ScreenGrab.h>
#include <wincodec.h>

#include <filesystem>
#include <fstream>
#include <sstream>

// Test channel (sheet: systems.devtools). Lines written to My Games/Skyrim Special Edition/SKSE/BO3Zombies-cmd.txt
// are run and the file removed:
//   shot <name>   save the next frame (with the HUD) to SKSE/BO3Zombies-shots/<name>.png
//   hour <h>      set Skyrim's clock to hour h (0-24)
//   state         write round, points, gun and player position to the log
namespace bo3z
{
	namespace
	{
		std::mutex               g_lock;
		std::vector<std::string> g_mainQueue;   // render-safe: key, mouse, close
		std::vector<std::string> g_gameQueue;   // run from PlayerCharacter::Update: console, hour, give, tp, ...
		std::string              g_shotName;
		struct PendingKey
		{
			RE::INPUT_DEVICE  device;
			std::uint32_t     id;
			RE::BSFixedString ev;
			int               stage;  // 0 press, 1 release
		};
		std::vector<PendingKey> g_pendingKeys;
		int                      g_frame = 0;

		std::optional<std::filesystem::path> Dir()
		{
			auto dir = SKSE::log::log_directory();
			return dir ? std::optional{ *dir } : std::nullopt;
		}

		void RunMain(const std::string& a_line)
		{
			std::istringstream in(a_line);
			std::string        cmd;
			in >> cmd;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (cmd == "hour") {
				float h = 0;
				in >> h;
				if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->gameHour) {
					cal->gameHour->value = h;
					logger::info("dev: hour set to {}", h);
				}
			} else if (cmd == "state" && player) {
				const auto p = player->GetPosition();
				auto*      cell = player->GetParentCell();
				std::string slots;
				for (int i = 0; i < 3; ++i) {
					const auto s = Save::GetSlot(i);
					slots += fmt::format(" [{}:{} {}/{}]", i, s.gun >= 0 ? gen::kGuns[s.gun].id : "-", s.clip, s.reserve);
				}
				logger::info("dev: state round={} points={} crew={} perks={:09b}{} pos=({:.0f},{:.0f},{:.0f}) cell={} hour={:.2f} introDone={} box={}", Save::Get(gen::kAV_round),
					Points::Get(), Save::Crew(), Save::PerkBits(), slots, p.x, p.y, p.z, cell ? cell->GetFormEditorID() : "?",
					RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetHour() : -1.0f, Forms::IntroDone(), Shops::BoxTown());
				Director::LogState();
			} else if (cmd == "use" && player) {
				// use <object id>: activate the nearest placed one exactly as the player would.
				std::string id;
				in >> id;
				RE::TESObjectREFR* best = nullptr;
				float              bestD = 8000.0f;
				for (int i = 0; i < static_cast<int>(gen::kObjects.size()); ++i) {
					if (gen::kObjects[i].id != id) {
						continue;
					}
					auto* base = Forms::Object(i);
					RE::TES::GetSingleton()->ForEachReferenceInRange(player, 8000.0f, [&](RE::TESObjectREFR* r) {
						if (r && r->GetBaseObject() == base && !r->IsDeleted()) {
							if (const float d = r->GetPosition().GetDistance(player->GetPosition()); d < bestD) {
								bestD = d, best = r;
							}
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
				}
				logger::info("dev: use {} -> {}", id, best ? fmt::format("ref {:08X} at {:.0f}", best->GetFormID(), bestD) : "none nearby");
				if (best) {
					best->ActivateRef(player, 0, nullptr, 1, false);
				}
			} else if (cmd == "anim" && player) {
				std::string ev;
				in >> ev;
				const bool ok = player->NotifyAnimationGraph(RE::BSFixedString(ev.c_str()));
				logger::info("dev: anim {} -> {}", ev, ok);
			} else if (cmd == "points") {
				int n = 0;
				in >> n;
				Points::Add(n);
			} else if (cmd == "give") {
				std::string id;
				in >> id;
				for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
					if (gen::kGuns[i].id == id) {
						Gun::Give(i);
					}
				}
			} else if (cmd == "tp" && player) {
				std::string town;
				in >> town;
				for (const auto& l : gen::kLocations) {
					std::string t(gen::kTowns[l.town]);
					std::transform(t.begin(), t.end(), t.begin(), ::tolower);
					if (t == town) {
						if (auto* a = RE::TESForm::LookupByID<RE::TESObjectREFR>(l.anchor)) {
							player->MoveTo(a);
							logger::info("dev: moved to {}", town);
						}
						break;
					}
				}
			} else if (cmd == "key" || cmd == "mouse") {
				// key <dik> [menu]: press and release a key inside Skyrim's own input queue (no window focus needed)
				std::uint32_t id = 0;
				std::string   ctx, name;
				in >> id >> ctx >> name;
				const auto device = cmd == "key" ? RE::INPUT_DEVICE::kKeyboard : RE::INPUT_DEVICE::kMouse;
				const auto context = ctx == "menu" ? RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode : RE::UserEvents::INPUT_CONTEXT_ID::kGameplay;
				const RE::BSFixedString ev(!name.empty() ? std::string_view(name) : RE::ControlMap::GetSingleton()->GetUserEventName(id, device, context));
				g_pendingKeys.push_back({ device, id, ev, 0 });
				logger::info("dev: {} {} ({})", cmd, id, ev.c_str());
			} else if (cmd == "console") {
				std::string rest;
				std::getline(in, rest);
				if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>()) {
					if (auto* script = factory->Create()) {
						script->SetCommand(rest);
						script->CompileAndRun(player);
						delete script;
						logger::info("dev: console{}", rest);
					}
				}
			} else if (cmd == "close") {
				std::string menu;
				std::getline(in, menu);
				if (!menu.empty() && menu[0] == ' ') {
					menu.erase(0, 1);
				}
				RE::UIMessageQueue::GetSingleton()->AddMessage(menu, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				logger::info("dev: close '{}'", menu);
			} else if (cmd == "crew") {
				int c = 0;
				in >> c;
				Save::SetCrew(c);
			} else {
				logger::warn("dev: unknown command '{}'", a_line);
			}
		}
	}

	namespace DevTools
	{
		void PollFromRender()
		{
			if (++g_frame % 30 != 0) {
				return;
			}
			auto dir = Dir();
			if (!dir) {
				return;
			}
			const auto file = *dir / "BO3Zombies-cmd.txt";
			std::error_code ec;
			if (!std::filesystem::exists(file, ec)) {
				return;
			}
			std::ifstream in(file);
			std::string   line;
			std::vector<std::string> lines;
			while (std::getline(in, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (!line.empty()) {
					lines.push_back(line);
				}
			}
			in.close();
			std::filesystem::remove(file, ec);
			std::scoped_lock l(g_lock);
			for (auto& s : lines) {
				if (s.rfind("shot ", 0) == 0) {
					g_shotName = s.substr(5);
				} else if (s.rfind("key ", 0) == 0 || s.rfind("mouse ", 0) == 0 || s.rfind("close ", 0) == 0) {
					g_mainQueue.push_back(s);
				} else {
					g_gameQueue.push_back(s);
				}
			}
		}

		void CaptureIfRequested(ID3D11DeviceContext* a_context, IDXGISwapChain* a_swap)
		{
			std::string name;
			{
				std::scoped_lock l(g_lock);
				name.swap(g_shotName);
			}
			auto dir = Dir();
			if (name.empty() || !dir || !a_context) {
				return;
			}
			ID3D11Texture2D* back = nullptr;
			if (FAILED(a_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
				return;
			}
			const auto out = *dir / "BO3Zombies-shots";
			std::error_code ec;
			std::filesystem::create_directories(out, ec);
			const auto path = out / (name + ".png");
			::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			const auto hr = DirectX::SaveWICTextureToFile(a_context, back, GUID_ContainerFormatPng, path.wstring().c_str(), nullptr, nullptr, true);
			back->Release();
			logger::info("dev: shot {} -> {} (hr {:08X})", name, path.string(), static_cast<std::uint32_t>(hr));
		}

		void FeedKeys()
		{
			auto* q = RE::BSInputEventQueue::GetSingleton();
			if (!q) {
				return;
			}
			for (auto it = g_pendingKeys.begin(); it != g_pendingKeys.end();) {
				if (it->stage == 0) {
					q->AddButtonEvent(it->device, static_cast<std::int32_t>(it->id), 1.0f, 0.0f, it->ev);
					it->stage = 1;
					++it;
				} else {
					q->AddButtonEvent(it->device, static_cast<std::int32_t>(it->id), 0.0f, 0.1f, it->ev);
					it = g_pendingKeys.erase(it);
				}
			}
		}

		void RunQueuedOnMain()
		{
			FeedKeys();
			std::vector<std::string> q;
			{
				std::scoped_lock l(g_lock);
				q.swap(g_mainQueue);
			}
			for (auto& s : q) {
				RunMain(s);
			}
		}

		void RunQueuedInGame()
		{
			std::vector<std::string> q;
			{
				std::scoped_lock l(g_lock);
				q.swap(g_gameQueue);
			}
			for (auto& s : q) {
				RunMain(s);
			}
		}
	}
}
