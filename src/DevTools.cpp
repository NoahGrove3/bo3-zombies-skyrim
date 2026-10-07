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
		std::vector<std::string> g_mainQueue;
		std::string              g_shotName;
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
				logger::info("dev: state round={} points={} crew={} perks={:07b}{} pos=({:.0f},{:.0f},{:.0f}) cell={} hour={:.2f} introDone={} box={}", Save::Get(gen::kAV_round),
					Points::Get(), Save::Crew(), (Save::Flags() >> 4) & 0x7F, slots, p.x, p.y, p.z, cell ? cell->GetFormEditorID() : "?",
					RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetHour() : -1.0f, Forms::IntroQuest() && Forms::IntroQuest()->IsCompleted(), Shops::BoxTown());
				Director::LogState();
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
				} else {
					g_mainQueue.push_back(s);
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

		void RunQueuedOnMain()
		{
			std::vector<std::string> q;
			{
				std::scoped_lock l(g_lock);
				q.swap(g_mainQueue);
			}
			for (auto& s : q) {
				RunMain(s);
			}
		}
	}
}
