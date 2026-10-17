#include "BO3Z.h"

#include <ScreenGrab.h>
#include <wincodec.h>

#include <filesystem>
#include <fstream>
#include <sstream>

// Test channel (sheet: systems.devtools). Lines written to My Games/Skyrim Special Edition/SKSE/CurseOfAlduin-cmd.txt
// are run and the file removed:
//   shot <name>   save the next frame (with the HUD) to SKSE/CurseOfAlduin-shots/<name>.png
//   rec <name> <seconds> [fps]   record the screen (half size, raw frames) to SKSE/CurseOfAlduin-rec/<name>/ - the
//                 frames are made into a video outside the game (Noah: recordings of the tests to review)
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
		// the recorder: a ring of staging copies of the back buffer, read two frames late (no stall), halved and handed
		// to a writer thread
		struct Rec
		{
			std::string                    name;
			float                          seconds = 0, fps = 20;
			bool                           on = false;
			std::chrono::steady_clock::time_point start{}, next{};
			ID3D11Texture2D*               ring[3]{};
			bool                           filled[3]{};
			int                            slot = 0, frames = 0;
			UINT                           w = 0, h = 0;
			DXGI_FORMAT                    fmt{};
			std::filesystem::path          dir;
		};
		Rec                                        g_rec;
		std::string                                g_recRequest;
		std::mutex                                 g_recLock;
		std::condition_variable                    g_recCv;
		std::deque<std::pair<std::filesystem::path, std::vector<std::uint8_t>>> g_recQueue;
		void RecWriter()
		{
			for (;;) {
				std::pair<std::filesystem::path, std::vector<std::uint8_t>> job;
				{
					std::unique_lock l(g_recLock);
					g_recCv.wait(l, [] { return !g_recQueue.empty(); });
					job = std::move(g_recQueue.front());
					g_recQueue.pop_front();
				}
				std::ofstream f(job.first, std::ios::binary);
				f.write(reinterpret_cast<const char*>(job.second.data()), static_cast<std::streamsize>(job.second.size()));
			}
		}
		std::atomic<bool>        g_hudOff{ false };
		std::chrono::steady_clock::time_point g_lastGameTick{};
		struct PendingKey
		{
			RE::INPUT_DEVICE  device;
			std::uint32_t     id;
			RE::BSFixedString ev;
			int               stage;  // 0 press, 1 release (or held until hold seconds pass)
			float             hold = 0.0f;
			std::chrono::steady_clock::time_point t0{};
		};
		std::vector<PendingKey> g_pendingKeys;
		std::deque<std::uint32_t> g_pendingChars;  // 'type <text>': one character a frame into Skyrim's text boxes
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
			} else if (cmd == "why" && player) {
				// movement diagnostics: why can't the player sprint?
				auto*       av = player->AsActorValueOwner();
				auto*       st = player->AsActorState();
				auto*       cm = RE::ControlMap::GetSingleton();
				logger::info("dev: why stamina {:.0f}/{:.0f} carry {:.0f} inv {:.0f} speed {:.0f} overenc {} sneakFlag {} sprintFlag {} walking {} running {} controls {:08X} drawn {} inCombat {} hour {:.2f}",
					av->GetActorValue(RE::ActorValue::kStamina), av->GetPermanentActorValue(RE::ActorValue::kStamina),
					av->GetActorValue(RE::ActorValue::kCarryWeight), av->GetActorValue(RE::ActorValue::kInventoryWeight),
					av->GetActorValue(RE::ActorValue::kSpeedMult), player->IsOverEncumbered(),
					static_cast<int>(st->actorState1.sneaking), static_cast<int>(st->actorState1.sprinting), static_cast<int>(st->actorState1.walking), static_cast<int>(st->actorState1.running),
					cm ? static_cast<std::uint32_t>(cm->GetRuntimeData().enabledControls.underlying()) : 0u, st->IsWeaponDrawn(), player->IsInCombat(),
					RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetHour() : -1.0f);
			} else if (cmd == "z3d" && player) {
				// what did the engine attach to the nearest undead? (invisible-zombie hunt)
				RE::Actor* best = nullptr;
				float      bd = 1e9f;
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
					auto a = h.get();
					if (a && a.get() != player && Director::IsRoundUndead(a.get())) {
						if (const float d = a->GetPosition().GetDistance(player->GetPosition()); d < bd) {
							bd = d, best = a.get();
						}
					}
				}
				if (!best) {
					logger::info("dev: z3d no undead loaded");
				} else {
					auto* npc = best->GetActorBase();
					auto* race = best->GetRace();
					logger::info("dev: z3d {:08X} dist {:.0f} base {:08X} race {:08X} skin {:08X} female {} 3D {} loaded {} scale {:.2f}", best->GetFormID(), bd, npc ? npc->GetFormID() : 0,
						race ? race->GetFormID() : 0, npc && npc->skin ? npc->skin->GetFormID() : 0, npc ? npc->IsFemale() : false, best->Get3D() != nullptr, best->Is3DLoaded(), best->GetScale());
					int lines = 0;
					std::function<void(RE::NiAVObject*, int)> walk = [&](RE::NiAVObject* o, int depth) {
						if (!o || lines > 120) {
							return;
						}
						if (auto* g = o->AsGeometry()) {
							++lines;
							logger::info("dev: z3d   geom '{}' hidden {} culled {} world ({:.0f},{:.0f},{:.0f}) r {:.0f} skinned {}", o->name.c_str(), (o->GetFlags().any(RE::NiAVObject::Flag::kHidden)),
								o->GetAppCulled(), o->world.translate.x, o->world.translate.y, o->world.translate.z, o->worldBound.radius, g->GetGeometryRuntimeData().skinInstance != nullptr);
						} else if (depth <= 1) {
							++lines;
							logger::info("dev: z3d   node '{}' hidden {}", o->name.c_str(), o->GetFlags().any(RE::NiAVObject::Flag::kHidden));
						}
						if (auto* n = o->AsNode()) {
							for (auto& c : n->GetChildren()) {
								walk(c.get(), depth + 1);
							}
						}
					};
					walk(best->Get3D(), 0);
				}
			} else if (cmd == "boltinfo") {
				for (const auto& g : gen::kGuns) {
					if (!g.bulletSpell) {
						continue;
					}
					auto* raw = RE::TESDataHandler::GetSingleton()->LookupForm(g.bulletSpell, gen::kPlugin);
					auto* sp = raw ? raw->As<RE::SpellItem>() : nullptr;
					auto* eff = sp && !sp->effects.empty() ? sp->effects[0] : nullptr;
					auto* base = eff ? eff->baseEffect : nullptr;
					auto* projForm = RE::TESDataHandler::GetSingleton()->LookupForm(g.bulletSpell - 2, gen::kPlugin);
					logger::info("dev: boltinfo {} spell {:08X} type {} effects {} base {:08X} arch {} cast {} deliv {} proj {:08X} | PROJ? {:03X} -> {:08X} type {}", g.id,
						raw ? raw->GetFormID() : 0, raw ? static_cast<int>(raw->GetFormType()) : -1, sp ? sp->effects.size() : 0, base ? base->GetFormID() : 0,
						base ? static_cast<int>(base->data.archetype) : -1, base ? static_cast<int>(base->data.castingType) : -1, base ? static_cast<int>(base->data.delivery) : -1,
						base && base->data.projectileBase ? base->data.projectileBase->GetFormID() : 0, g.bulletSpell - 2, projForm ? projForm->GetFormID() : 0,
						projForm ? static_cast<int>(projForm->GetFormType()) : -1);
				}
			} else if (cmd == "cast") {
				std::string mode;
				in >> mode;
				Gun::DevCast(mode);
			} else if (cmd == "menupick") {  // menupick <start of a main-menu word>: as if clicked
				std::string rest;
				std::getline(in, rest);
				Hud::DevMenuPick(rest.size() > 1 ? rest.substr(1) : rest);
			} else if (cmd == "shrine") {  // shrine <0 call|1 reforge|2 bless|3 relic|4 leave|5 bond>
				int a = 0;
				in >> a;
				Shops::DevAct(a);
			} else if (cmd == "breath") {  // breath <0 fire|1 frost|2 lightning>
				int e = 0;
				in >> e;
				Movement::DevBreath(e);
			} else if (cmd == "flap") {
				Movement::DevFlap();
			} else if (cmd == "takeoff") {
				Movement::DevTakeOff();
			} else if (cmd == "flyoff") {
				Movement::DevFlyOff();
			} else if (cmd == "bond") {  // bond <0-3>: the glider's dragon's bond, for testing
				int b = 0;
				in >> b;
				Save::SetDragonBond(b);
				logger::info("dev: dragon bond {}", Save::DragonBond());
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
			} else if (cmd == "hold") {
				// hold <mouse id> <seconds>: press a mouse button and keep it down (charge-up tests)
				std::uint32_t id = 0;
				float         secs = 1.0f;
				in >> id >> secs;
				const RE::BSFixedString ev(RE::ControlMap::GetSingleton()->GetUserEventName(id, RE::INPUT_DEVICE::kMouse, RE::UserEvents::INPUT_CONTEXT_ID::kGameplay));
				g_pendingKeys.push_back({ RE::INPUT_DEVICE::kMouse, id, ev, 0, secs });
				logger::info("dev: hold mouse {} for {:.1f}s ({})", id, secs, ev.c_str());
			} else if (cmd == "keyhold") {
				// keyhold <dik> <seconds>: hold a key down (sprinting, running)
				std::uint32_t id = 0;
				float         secs = 1.0f;
				in >> id >> secs;
				const RE::BSFixedString ev(RE::ControlMap::GetSingleton()->GetUserEventName(id, RE::INPUT_DEVICE::kKeyboard, RE::UserEvents::INPUT_CONTEXT_ID::kGameplay));
				g_pendingKeys.push_back({ RE::INPUT_DEVICE::kKeyboard, id, ev, 0, secs });
				logger::info("dev: keyhold {} ({}) {:.1f} s", id, ev.c_str(), secs);
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
			} else if (cmd == "type") {
				std::string rest;
				std::getline(in, rest);
				for (std::size_t i = 1; i < rest.size(); ++i) {
					g_pendingChars.push_back(static_cast<unsigned char>(rest[i]));
				}
				logger::info("dev: type '{}'", rest.size() > 1 ? rest.substr(1) : "");
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
			} else if (cmd == "spawn" && player) {
				// spawn <enemy row id> [count]: raise that undead in front of you (model and sound checks)
				std::string id;
				int         n = 1;
				in >> id >> n;
				Director::DevSpawn(id, std::max(1, n));
			} else if (cmd == "sound") {
				// sound <sound id>: play one of the mod's sounds (2D)
				std::string id;
				in >> id;
				for (std::size_t i = 0; i < gen::kSounds.size(); ++i) {
					if (gen::kSounds[i].editorId == "UN_" + id) {
						Audio::Ui(static_cast<gen::Sound>(i));
						logger::info("dev: sound {}", id);
					}
				}
			} else if (cmd == "spawnat" && player) {
				// spawnat <row> <x> <y> <z> [yaw deg]: raise that undead at a point (art captures)
				std::string  id;
				RE::NiPoint3 at;
				float        yaw = 0;
				in >> id >> at.x >> at.y >> at.z >> yaw;
				Director::DevSpawnAt(id, at, yaw);
			} else if (cmd == "audio") {
				logger::info("dev: audio {}", Audio::Stats());
			} else if (cmd == "intro") {
				Hud::IntroStart();
			} else if (cmd == "mainlist") {  // mainlist <index>: press an entry of Skyrim's main menu list (1 = New)
				int i = 0;
				in >> i;
				Hud::DevMain(i);
			} else if (cmd == "party") {  // party host|invite|leave
				std::string w;
				in >> w;
				Hud::DevParty(w);
			} else if (cmd == "undead") {
				std::string v;
				in >> v;
				Director::DevShowUndead(v != "hide");
			} else if (cmd == "hud") {
				int v = 1;
				in >> v;
				g_hudOff = v == 0;
				logger::info("dev: hud {}", v);
			} else if (cmd == "cam") {
				// cam <x> <y> <z> <yaw deg> <pitch deg>: free camera (tfc) placed exactly there (art captures); 'cam off' leaves it
				auto* pc = RE::PlayerCamera::GetSingleton();
				std::string first;
				in >> first;
				if (first == "off") {
					if (pc && pc->IsInFreeCameraMode()) {
						pc->ToggleFreeCameraMode(false);
					}
					return;
				}
				RE::NiPoint3 at;
				float        yaw = 0, pitch = 0;
				at.x = std::stof(first);
				in >> at.y >> at.z >> yaw >> pitch;
				if (pc && !pc->IsInFreeCameraMode()) {
					pc->ToggleFreeCameraMode(false);
				}
				if (pc && pc->currentState && pc->IsInFreeCameraMode()) {
					auto* fc = static_cast<RE::FreeCameraState*>(pc->currentState.get());
					fc->translation = at;
					fc->rotation.x = pitch * 0.0174533f;
					fc->rotation.y = yaw * 0.0174533f;
					logger::info("dev: cam at {:.0f},{:.0f},{:.0f} yaw {:.1f} pitch {:.1f}", at.x, at.y, at.z, yaw, pitch);
				}
			} else if (cmd == "fp") {
				// fp 0|1: hide/show your first-person arms (art captures from your own eyes)
				int v = 1;
				in >> v;
				if (auto* root = player ? player->Get3D(true) : nullptr) {
					root->GetFlags().set(RE::NiAVObject::Flag::kHidden);
					if (v) {
						root->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
					}
				}
				logger::info("dev: first-person arms {}", v ? "shown" : "hidden");
			} else if (cmd == "gfx") {
				// gfx <menu, '_' for spaces> <callback> [number]: call one of a menu's own Scaleform callbacks (the button
				// a message box would get from a click: 'gfx MessageBoxMenu buttonPress 0')
				std::string menu, fn;
				double      num = 0;
				in >> menu >> fn;
				const bool hasNum = static_cast<bool>(in >> num);
				std::replace(menu.begin(), menu.end(), '_', ' ');
				if (auto* ui = RE::UI::GetSingleton()) {
					if (auto m = ui->GetMenu(menu); m && m->fxDelegate && m->uiMovie) {
						RE::GFxValue arg(num);
						m->fxDelegate->Callback(m->uiMovie.get(), fn.c_str(), hasNum ? &arg : nullptr, hasNum ? 1 : 0);
						logger::info("dev: gfx {} {}({})", menu, fn, hasNum ? std::to_string(num) : "");
					} else {
						logger::warn("dev: gfx: menu '{}' not open or has no delegate", menu);
					}
				}
			} else if (cmd == "menus") {
				std::string open;
				if (auto* ui = RE::UI::GetSingleton()) {
					for (auto& [name, entry] : ui->menuMap) {
						if (entry.menu && ui->IsMenuOpen(name)) {
							open += std::string(name.c_str()) + ", ";
						}
					}
					logger::info("dev: menus open: {} (pauses {})", open, ui->numPausesGame);
				}
			} else if (cmd == "camget") {
				auto* pc = RE::PlayerCamera::GetSingleton();
				if (pc && pc->cameraRoot) {
					const auto& t = pc->cameraRoot->world.translate;
					float yaw = 0, pitch = 0;
					if (pc->IsInFreeCameraMode() && pc->currentState) {
						auto* fc = static_cast<RE::FreeCameraState*>(pc->currentState.get());
						yaw = fc->rotation.y / 0.0174533f, pitch = fc->rotation.x / 0.0174533f;
						logger::info("dev: camget free: t {:.0f},{:.0f},{:.0f} rot {:.3f},{:.3f} zud {:.3f},{:.3f}", fc->translation.x, fc->translation.y, fc->translation.z, fc->rotation.x, fc->rotation.y, fc->zUpDown.x, fc->zUpDown.y);
					} else if (player) {
						yaw = player->GetAngleZ() / 0.0174533f, pitch = player->GetAngleX() / 0.0174533f;
					}
					logger::info("dev: camget {:.0f} {:.0f} {:.0f} yaw {:.1f} pitch {:.1f} | player {:.0f} {:.0f} {:.0f} | cell {}", t.x, t.y, t.z, yaw, pitch,
						player ? player->GetPositionX() : 0.f, player ? player->GetPositionY() : 0.f, player ? player->GetPositionZ() : 0.f,
						player && player->GetParentCell() ? player->GetParentCell()->GetFormEditorID() : "");
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
		bool HudOff() { return g_hudOff.load(); }

		// a key pressed (and held a_hold seconds) through Skyrim's own input queue: scripts listening for it (SKSE's
		// RegisterForKey) see it as if pressed - the controller's bridge to mods that only know keyboard keys
		void PressKey(RE::INPUT_DEVICE a_device, std::uint32_t a_id, float a_hold)
		{
			// (with its user event - Activate, Jump...: a bare key code reaches no one who listens for the event)
			auto* cm = RE::ControlMap::GetSingleton();
			const RE::BSFixedString ev(cm ? cm->GetUserEventName(a_id, a_device, RE::UserEvents::INPUT_CONTEXT_ID::kGameplay) : std::string_view{});
			g_pendingKeys.push_back({ a_device, a_id, ev, 0, a_hold });
		}
		void PollFromRender()
		{
			if (++g_frame % 30 != 0) {
				return;
			}
			auto dir = Dir();
			if (!dir) {
				return;
			}
			const auto file = *dir / "CurseOfAlduin-cmd.txt";
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
				} else if (s.rfind("rec ", 0) == 0) {
					g_recRequest = s.substr(4);
				} else if (s.rfind("key ", 0) == 0 || s.rfind("type ", 0) == 0 || s.rfind("gfx ", 0) == 0 || s.rfind("menus", 0) == 0 || s.rfind("mouse ", 0) == 0 || s.rfind("hold ", 0) == 0 || s.rfind("keyhold ", 0) == 0 || s.rfind("close ", 0) == 0) {
					g_mainQueue.push_back(s);
				} else {
					g_gameQueue.push_back(s);
				}
			}
		}

		void Record(ID3D11DeviceContext* a_context, IDXGISwapChain* a_swap)
		{
			std::string req;
			{
				std::scoped_lock l(g_lock);
				req.swap(g_recRequest);
			}
			auto& r = g_rec;
			if (!req.empty() && !r.on) {
				std::istringstream in(req);
				r = Rec{};
				in >> r.name >> r.seconds >> r.fps;
				r.fps = std::clamp(r.fps <= 0 ? 20.0f : r.fps, 1.0f, 60.0f);
				auto dir = Dir();
				if (!dir || r.name.empty() || r.seconds <= 0) {
					return;
				}
				r.dir = *dir / "CurseOfAlduin-rec" / r.name;
				std::error_code ec;
				std::filesystem::create_directories(r.dir, ec);
				ID3D11Texture2D* back = nullptr;
				if (FAILED(a_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
					return;
				}
				D3D11_TEXTURE2D_DESC d{};
				back->GetDesc(&d);
				ID3D11Device* dev = nullptr;
				back->GetDevice(&dev);
				back->Release();
				d.Usage = D3D11_USAGE_STAGING, d.BindFlags = 0, d.CPUAccessFlags = D3D11_CPU_ACCESS_READ, d.MiscFlags = 0, d.SampleDesc.Count = 1, d.SampleDesc.Quality = 0, d.MipLevels = 1, d.ArraySize = 1;
				bool ok = dev != nullptr;
				for (auto& t : r.ring) {
					ok = ok && SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &t));
				}
				if (dev) dev->Release();
				if (!ok) {
					logger::warn("dev: rec - no staging textures");
					for (auto& t : r.ring) if (t) t->Release(), t = nullptr;
					return;
				}
				r.w = d.Width, r.h = d.Height, r.fmt = d.Format;
				r.on = true;
				r.start = r.next = std::chrono::steady_clock::now();
				static std::once_flag once;
				std::call_once(once, [] { std::thread(RecWriter).detach(); });
				std::ofstream(r.dir / "meta.txt") << (r.w / 2) << " " << (r.h / 2) << " " << r.fps << " " << static_cast<int>(r.fmt) << "\n";
				logger::info("dev: rec {} {:.1f} s at {:.0f} fps ({}x{} format {})", r.name, r.seconds, r.fps, r.w, r.h, static_cast<int>(r.fmt));
			}
			if (!r.on) {
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			const bool done = now - r.start > std::chrono::duration<float>(r.seconds);
			// read back the copy made two frames ago
			const int  old = (r.slot + 1) % 3;
			if (r.filled[old]) {
				D3D11_MAPPED_SUBRESOURCE m{};
				if (SUCCEEDED(a_context->Map(r.ring[old], 0, D3D11_MAP_READ, 0, &m))) {
					const UINT                w = r.w / 2, h = r.h / 2;
					std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
					const bool bgra = r.fmt == DXGI_FORMAT_B8G8R8A8_UNORM || r.fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || r.fmt == DXGI_FORMAT_B8G8R8X8_UNORM;
					const bool r10 = r.fmt == DXGI_FORMAT_R10G10B10A2_UNORM;
					for (UINT y = 0; y < h; ++y) {
						const auto* row0 = static_cast<const std::uint8_t*>(m.pData) + static_cast<std::size_t>(y * 2) * m.RowPitch;
						const auto* row1 = row0 + m.RowPitch;
						auto*       o = px.data() + static_cast<std::size_t>(y) * w * 4;
						for (UINT x = 0; x < w; ++x) {
							int c[3] = { 0, 0, 0 };
							for (const auto* rr : { row0, row1 }) {
								for (int k = 0; k < 2; ++k) {
									const auto* q = rr + (x * 2 + k) * 4;
									if (r10) {
										const std::uint32_t v = *reinterpret_cast<const std::uint32_t*>(q);
										c[0] += (v & 0x3FF) >> 2, c[1] += ((v >> 10) & 0x3FF) >> 2, c[2] += ((v >> 20) & 0x3FF) >> 2;
									} else if (bgra) {
										c[0] += q[2], c[1] += q[1], c[2] += q[0];
									} else {
										c[0] += q[0], c[1] += q[1], c[2] += q[2];
									}
								}
							}
							o[x * 4 + 0] = static_cast<std::uint8_t>(c[0] / 4), o[x * 4 + 1] = static_cast<std::uint8_t>(c[1] / 4), o[x * 4 + 2] = static_cast<std::uint8_t>(c[2] / 4), o[x * 4 + 3] = 255;
						}
					}
					a_context->Unmap(r.ring[old], 0);
					{
						std::scoped_lock l(g_recLock);
						g_recQueue.push_back({ r.dir / fmt::format("f{:05d}.raw", r.frames++), std::move(px) });
					}
					g_recCv.notify_one();
				}
				r.filled[old] = false;
			}
			if (done) {
				bool pending = false;
				for (bool f : r.filled) pending = pending || f;
				if (!pending) {
					for (auto& t : r.ring) if (t) t->Release(), t = nullptr;
					r.on = false;
					std::ofstream(r.dir / "done.txt") << r.frames << "\n";
					logger::info("dev: rec {} done, {} frames", r.name, r.frames);
				}
				r.slot = (r.slot + 1) % 3;
				return;
			}
			if (now >= r.next) {  // this frame: copied to the ring
				r.next += std::chrono::microseconds(static_cast<long long>(1e6f / r.fps));
				if (r.next < now) r.next = now;
				ID3D11Texture2D* back = nullptr;
				if (SUCCEEDED(a_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
					a_context->CopyResource(r.ring[r.slot], back);
					back->Release();
					r.filled[r.slot] = true;
				}
			}
			r.slot = (r.slot + 1) % 3;
		}

		void CaptureIfRequested(ID3D11DeviceContext* a_context, IDXGISwapChain* a_swap)
		{
			if (a_context && a_swap) {
				Record(a_context, a_swap);
			}
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
			const auto out = *dir / "CurseOfAlduin-shots";
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
			if (!g_pendingChars.empty()) {
				q->AddCharEvent(g_pendingChars.front());
				g_pendingChars.pop_front();
			}
			for (auto it = g_pendingKeys.begin(); it != g_pendingKeys.end();) {
				const auto now = std::chrono::steady_clock::now();
				const float held = it->stage == 0 ? 0.0f : std::chrono::duration<float>(now - it->t0).count();
				if (it->stage == 0) {
					q->AddButtonEvent(it->device, static_cast<std::int32_t>(it->id), 1.0f, 0.0f, it->ev);
					it->stage = 1;
					it->t0 = now;
					++it;
				} else if (held < it->hold) {
					q->AddButtonEvent(it->device, static_cast<std::int32_t>(it->id), 1.0f, std::max(0.01f, held), it->ev);
					++it;
				} else {
					q->AddButtonEvent(it->device, static_cast<std::int32_t>(it->id), 0.0f, std::max(0.1f, held), it->ev);
					it = g_pendingKeys.erase(it);
				}
			}
		}

		void RunQueuedOnMain()
		{
			FeedKeys();
			// the game queue normally runs from the player's update; when that stops (free camera, a stuck menu)
			// its lines still run from here after a moment, so the test channel can never lock itself out
			if (std::chrono::steady_clock::now() - g_lastGameTick > std::chrono::milliseconds(1500)) {
				std::vector<std::string> g;
				{
					std::scoped_lock l(g_lock);
					g.swap(g_gameQueue);
				}
				for (auto& s : g) {
					RunMain(s);
				}
			}
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
			g_lastGameTick = std::chrono::steady_clock::now();
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
