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
	static std::vector<RE::SpellItem*>& DevAddedSpells() { static std::vector<RE::SpellItem*> v; return v; }   // (arequip: the spells it taught, for arclean)
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

		// (trailer filming) the free camera on the move: a glide between two set-ups, or an orbit round a point. Driven from
		// Present each frame, so it moves smoothly whatever the game is doing
		struct CamAnim
		{
			int          mode = 0;  // 0 off, 1 path, 2 orbit, 3 track (a fixed camera that keeps an actor's head in frame)
			RE::FormID   target = 0;
			bool         onPlayer = false;
			float        lastLog = 0;
			RE::NiPoint3 a, b, c;
			float        yaw0 = 0, yaw1 = 0, pitch0 = 0, pitch1 = 0, dur = 1, radius = 0, height = 0, deg0 = 0, degPerSec = 0, tilt = 0;
			bool         ease = true;
			std::chrono::steady_clock::time_point t0{};
		};
		CamAnim g_camAnim;
		RE::FreeCameraState* FreeCam()
		{
			auto* pc = RE::PlayerCamera::GetSingleton();
			if (!pc) return nullptr;
			if (!pc->IsInFreeCameraMode()) pc->ToggleFreeCameraMode(false);
			return pc->IsInFreeCameraMode() && pc->currentState ? static_cast<RE::FreeCameraState*>(pc->currentState.get()) : nullptr;
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
				logger::info("dev: state round={} points={} perks={:09b} pos=({:.0f},{:.0f},{:.0f}) cell={} hour={:.2f} introDone={} box={}", Save::Get(gen::kAV_round),
					Points::Get(), Save::PerkBits(), p.x, p.y, p.z, cell ? cell->GetFormEditorID() : "?",
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
			} else if (cmd == "popups") {  // popups 0|1: the one-time cards off / on (remembered)
				int on = 1;
				in >> on;
				Hud::SetPopups(on != 0);
			} else if (cmd == "allkit" && player) {
				// allkit: (trailer filming) every class's kit and Armory at its top tier, every relic, the four wonders, Alduin's
				// Bane, the glider at its last bond with Dragon's Updraft, and Cursed Essence to spend
				Kit::DevAll();
				for (int c = 0; c < 4; ++c) Classes::DevGiveWonder(c);
				Echo::DevGiveBane();
				Movement::TestKit();
				Movement::DevUnlockGlider();
				if (const int have = Points::Get(); have < 150000) Points::Refund(150000 - have);
				logger::info("dev: allkit given");
			} else if (cmd == "basekit" && player) {
				// basekit: (Noah's tier testing) every class's kit and Armory at Base, the four wonders, Alduin's Bane, the glider
				// back to no bond (its bonds bought at the altar), and exactly 200,000 Cursed Essence to raise it all with
				Kit::DevBase();
				for (int c = 0; c < 4; ++c) Classes::DevGiveWonder(c);
				Echo::DevGiveBane();
				Save::SetDragonBond(0);
				Movement::DevUnlockGlider();  // (Noah: the glider and the shout, for clips)
				if (const int have = Points::Get(); have < 200000) Points::Refund(200000 - have);
				else if (have > 200000) Points::Spend(have - 200000);
				logger::info("dev: basekit given ({} Cursed Essence)", Points::Get());
			} else if (cmd == "mount" && player) {
				// mount [base hex]: ride the nearest horse; with none near, place one (default Shadowmere) - run it again to climb on
				std::string hex;
				in >> hex;
				RE::Actor* best = nullptr;
				float      bestD = 3000.0f;
				RE::TES::GetSingleton()->ForEachReferenceInRange(player, 3000.0f, [&](RE::TESObjectREFR* r) {
					if (auto* a = r ? r->As<RE::Actor>() : nullptr; a && a != player && a->IsHorse() && !a->IsDead()) {
						if (const float d = a->GetPosition().GetDistance(player->GetPosition()); d < bestD) {
							bestD = d, best = a;
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				if (best) {
					best->ActivateRef(player, 0, nullptr, 1, false);
					logger::info("dev: mount ref {:08X} at {:.0f}", best->GetFormID(), bestD);
				} else {
					const RE::FormID id = hex.empty() ? 0x0009CCD7 : static_cast<RE::FormID>(std::stoul(hex, nullptr, 16));
					if (auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(id)) {
						auto h = player->PlaceObjectAtMe(base, false);
						logger::info("dev: mount placed horse {:08X} - mount again to ride", h ? h->GetFormID() : 0);
					} else {
						logger::info("dev: mount: no base {:08X}", id);
					}
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
			} else if (cmd == "menupick") {  // menupick <start of a main-menu word>: as if clicked
				std::string rest;
				std::getline(in, rest);
				Hud::DevMenuPick(rest.size() > 1 ? rest.substr(1) : rest);
			} else if (cmd == "shrine") {  // shrine <0 call|1 reforge|2 bless|4 leave|5 bond>
				int a = 0;
				in >> a;
				Shops::DevAct(a);
			} else if (cmd == "breath") {  // breath <0 fire|1 frost|2 lightning>
				int e = 0;
				in >> e;
				Movement::DevBreath(e);
			} else if (cmd == "breathproj") {  // breathproj <element> <projectile form id hex, 0 default> [gap between gouts, s] [-1 = without the shout's spell]
				int         e = 0;
				std::string id;
				float       sc = -1, sg = 1;
				in >> e >> id >> sc >> sg;
				Movement::DevBreathProj(e, static_cast<RE::FormID>(std::stoul(id, nullptr, 16)), sc, sg);
				logger::info("dev: breath {} -> {} gap {} spell {}", e, id, sc, sg);
			} else if (cmd == "flap") {
				Movement::DevFlap();
			} else if (cmd == "chase") {  // chase off | chase <dist> <yaw deg (0 behind)> <pitch deg> [height] [spin deg/s]
				std::string a;
				in >> a;
				if (a == "off") {
					Movement::DevChase(false, 0, 0, 0, 0, 0);
				} else {
					float yaw = 0, pitch = 15, h = 0, spin = 0;
					in >> yaw >> pitch >> h >> spin;
					Movement::DevChase(true, std::stof(a), yaw, pitch, h, spin);
				}
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
			} else if (cmd == "theme") {  // theme <goblinraid|harpyswarm|champions|bloodfog> [night]
				std::string id, night;
				in >> id >> night;
				for (int t = 0; t < static_cast<int>(gen::kEvents.size()); ++t) {
					if (gen::kEvents[t].id == id) Director::DevTheme(t, night == "night");
				}
			} else if (cmd == "eggstep") {  // eggstep <town index 0-9>: that city's hidden quest one step on
				int t = -1;
				in >> t;
				Eggs::DevStep(t);
			} else if (cmd == "miniboss") {
				Director::DevMiniBoss();
			} else if (cmd == "testkit") {
				Gun::TestKit();
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
			} else if (cmd == "god") {  // god on|off: console god mode set (not toggled), and said in the log
				std::string v;
				in >> v;
				const bool want = v != "off";
				if (want != RE::PlayerCharacter::IsGodMode()) {
					if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>()) {
						if (auto* script = factory->Create()) {
							script->SetCommand("tgm");
							script->CompileAndRun(player);
							delete script;
						}
					}
				}
				logger::info("dev: god {} (now {})", want ? "on" : "off", RE::PlayerCharacter::IsGodMode() ? "on" : "off");
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
				g_camAnim.mode = 0;
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
			} else if (cmd == "campath") {
				// campath x1 y1 z1 yaw1 pitch1 x2 y2 z2 yaw2 pitch2 seconds [ease 1|0]: the free camera glides from one set-up to the other
				CamAnim A;
				int     ease = 1;
				in >> A.a.x >> A.a.y >> A.a.z >> A.yaw0 >> A.pitch0 >> A.b.x >> A.b.y >> A.b.z >> A.yaw1 >> A.pitch1 >> A.dur >> ease;
				A.ease = ease != 0, A.mode = 1, A.t0 = std::chrono::steady_clock::now();
				if (FreeCam()) g_camAnim = A;
				logger::info("dev: campath {:.1f} s", A.dur);
			} else if (cmd == "orbit") {
				// orbit cx cy cz radius height startDeg degPerSec [tiltDeg] [seconds, 0 = until camstop]: the free camera circles a point, looking at it
				CamAnim A;
				in >> A.c.x >> A.c.y >> A.c.z >> A.radius >> A.height >> A.deg0 >> A.degPerSec;
				if (!(in >> A.tilt)) A.tilt = 0;
				if (!(in >> A.dur)) A.dur = 0;
				A.mode = 2, A.t0 = std::chrono::steady_clock::now();
				if (FreeCam()) g_camAnim = A;
				logger::info("dev: orbit r {:.0f} at {:.1f} deg/s", A.radius, A.degPerSec);
			} else if (cmd == "camtrack") {
				// camtrack <ref hex> <x y z | p dz>: the free camera stays put (or rides at the player's head, dz above) and keeps
				// that actor's head in the middle of the frame - a dragon flying at the lens. Logs the distance as it goes
				CamAnim     A;
				std::string hex, w;
				in >> hex >> w;
				A.target = static_cast<RE::FormID>(std::stoul(hex, nullptr, 16));
				if (w == "p") {
					A.onPlayer = true;
					in >> A.height;
				} else {
					A.a.x = std::stof(w);
					in >> A.a.y >> A.a.z;
				}
				A.mode = 3, A.t0 = std::chrono::steady_clock::now();
				if (FreeCam()) g_camAnim = A;
				logger::info("dev: camtrack {:08X}", A.target);
			} else if (cmd == "camstop") {
				g_camAnim.mode = 0;
				logger::info("dev: camstop");
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
			} else if (cmd == "gfxtree") {
				// gfxtree <menu, '_' for spaces> [depth]: a menu's Scaleform tree written out (clips, arrays and their entries'
				// plain members) to SKSE/CurseOfAlduin-gfxtree.txt - (Noah: "organize all the tabs and all of the sliders in
				// each tab in the race menu") RaceMenu's categories and sliders as they are
				std::string menu;
				int depth = 7;
				in >> menu >> depth;
				std::replace(menu.begin(), menu.end(), '_', ' ');
				auto* ui = RE::UI::GetSingleton();
				auto m = ui ? ui->GetMenu(menu) : nullptr;
				RE::GFxValue root;
				if (!m || !m->uiMovie || !m->uiMovie->GetVariable(&root, "_root")) {
					logger::warn("dev: gfxtree: menu '{}' not open", menu);
				} else {
					auto dir = SKSE::log::log_directory();
					std::ofstream out(dir ? *dir / "CurseOfAlduin-gfxtree.txt" : std::filesystem::path("CurseOfAlduin-gfxtree.txt"), std::ios::trunc);
					int lines = 0;
					auto prim = [](const RE::GFxValue& v) -> std::string {
						if (v.IsString()) return std::string("\"") + v.GetString() + "\"";
						if (v.IsNumber()) return fmt::format("{:g}", v.GetNumber());
						if (v.IsBool()) return v.GetBool() ? "true" : "false";
						return {};
					};
					std::function<void(const RE::GFxValue&, const std::string&, int)> walk = [&](const RE::GFxValue& v, const std::string& path, int d) {
						if (lines > 60000 || d > depth) return;
						v.VisitMembers([&](const char* name, const RE::GFxValue& c) {
							if (lines > 60000 || !name) return;
							const std::string p2 = path + "." + name;
							if (c.IsDisplayObject()) {
								out << std::string(d * 2, ' ') << "[clip] " << p2 << "\n", ++lines;
								walk(c, p2, d + 1);
							} else if (c.IsArray()) {
								const auto n = c.GetArraySize();
								out << std::string(d * 2, ' ') << "[array " << n << "] " << p2 << "\n", ++lines;
								for (std::uint32_t i = 0; i < n && i < 600; ++i) {
									RE::GFxValue e;
									if (!c.GetElement(i, &e)) continue;
									std::string row = prim(e);
									if (e.IsObject() && !e.IsDisplayObject()) {
										e.VisitMembers([&](const char* k, const RE::GFxValue& x) {
											if (auto s2 = prim(x); !s2.empty() && k) row += std::string(k) + "=" + s2 + " ";
										});
									} else if (e.IsDisplayObject()) {
										row = "(clip)";
									}
									out << std::string(d * 2 + 2, ' ') << i << ": " << row << "\n", ++lines;
								}
							} else if (c.IsObject() && d < depth) {
								out << std::string(d * 2, ' ') << "[obj] " << p2 << "\n", ++lines;
								walk(c, p2, d + 1);
							} else if (auto s2 = prim(c); !s2.empty()) {
								out << std::string(d * 2, ' ') << p2 << " = " << s2 << "\n", ++lines;
							}
						});
					};
					walk(root, "_root", 0);
					logger::info("dev: gfxtree {} - {} lines written", menu, lines);
				}
			} else if (cmd == "menus") {
				std::string open;
				if (auto* ui = RE::UI::GetSingleton()) {
					for (auto& [name, entry] : ui->menuMap) {
						if (entry.menu && ui->IsMenuOpen(name)) {
							open += fmt::format("{} [flags {:X}], ", name.c_str(), entry.menu->menuFlags.underlying());
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
			} else if (cmd == "class") {  // class <0-3>: that class chosen now (no cost)
				int c = -1;
				in >> c;
				Classes::Choose(c);
			} else if (cmd == "wondergive") {  // wondergive <0-3 class wonder | 4 Alduin's Bane>: into your hands (trailer filming)
				int c = -1;
				in >> c;
				if (c == 4) Echo::DevGiveBane();
				else Classes::DevGiveWonder(c);
			} else if (cmd == "curse") {  // curse <town index> <value>: a city's curse state as is (trailer filming)
				int t = -1, v = 0;
				in >> t >> v;
				Curse::DevSet(t, v);
			} else if (cmd == "equip" && player) {  // equip <hex form id, load-order form> <l|r>: as the pack would
				std::string hex, hand;
				in >> hex >> hand;
				const bool left = hand == "l";
				auto* f = RE::TESForm::LookupByID(static_cast<RE::FormID>(std::stoul(hex, nullptr, 16)));
				auto* b = f ? f->As<RE::TESBoundObject>() : nullptr;
				if (auto* em = RE::ActorEquipManager::GetSingleton(); b && em) em->EquipObject(player, b, nullptr, 1, RE::TESForm::LookupByID<RE::BGSEquipSlot>(left ? 0x013F43 : 0x013F42), false, true, false, true);
				logger::info("dev: equip {} ({}) in the {} hand", hex, b ? b->GetName() : "not found", left ? "left" : "right");
			} else if (cmd == "hands" && player) {
				auto name = [](RE::TESForm* o) { return o ? fmt::format("{} [{:08X}]", o->GetName(), o->GetFormID()) : std::string("empty"); };
				logger::info("dev: hands right {} | left {} | drawn {} | throwing {}", name(player->GetEquippedObject(false)), name(player->GetEquippedObject(true)),
					player->AsActorState()->IsWeaponDrawn(), Gun::Throwing());
			} else if (cmd == "flask") {  // flask <l|r>: the held flask's attack, as the button would
				std::string hand;
				in >> hand;
				logger::info("dev: flask attack ({} hand, held {})", hand == "l" ? "left" : "right", Classes::FlaskInHand(hand == "l"));
				Classes::FlaskAttack(hand == "l");
			} else if (cmd == "wyrm") {  // wyrm <base|play|call> [plugin hex]: the Violet Wyrm's body and call (call: hex 1 = two)
				std::string what, plugin, hex;
				in >> what >> plugin >> hex;
				RE::FormID id = 0;
				try { id = static_cast<RE::FormID>(std::stoul(hex.empty() ? (what == "call" ? plugin : "0") : hex, nullptr, 16)); } catch (...) {}
				Classes::DevWyrm(what, what == "call" ? "" : plugin, id);
			} else if (cmd == "gbeam") {  // gbeam <1|0> [l]: the Gauntlet's attack held / let go
				int on = 0;
				std::string hand;
				in >> on >> hand;
				Classes::DevGauntletHold(on != 0, hand == "l");
				logger::info("dev: gauntlet attack {}", on ? "held" : "let go");
			} else if (cmd == "fx") {  // fx <mesh path under Meshes> <w|c|p> <sideways>
				std::string path, root;
				float side = 0;
				in >> path >> root >> side;
				Gun::DevFx(path, root, side);
			} else if (cmd == "look") {  // look <model> [w|x|h] [scale] [pal|-] [seconds]
				std::string model, mode = "w", pal = "-";
				float scale = 1.0f, secs = 4.0f;
				in >> model >> mode >> scale >> pal >> secs;
				Gun::DevLook(model, mode.empty() ? "w" : mode, scale, pal, secs);
			} else if (cmd == "arequip" && player) {  // arequip <Echo Altar spell index> [l|r]: that spell in hand to cast (the spell-look tests)
				int i = -1;
				std::string hand;
				in >> i >> hand;
				auto* sp = Arcana::Spell(i);
				// (the spell-look tests' alternatives: any spell by its load-order form id, "arequip x1B00186D r")
				if (std::string(in.str()).find(" x") != std::string::npos) {
					std::istringstream again(in.str());
					std::string c2, hex;
					again >> c2 >> hex >> hand;
					if (hex.size() > 1 && hex[0] == 'x') sp = RE::TESForm::LookupByID<RE::SpellItem>(static_cast<RE::FormID>(std::stoul(hex.substr(1), nullptr, 16)));
				}
				if (sp) {
					if (!player->HasSpell(sp)) player->AddSpell(sp), DevAddedSpells().push_back(sp);
					if (auto* em = RE::ActorEquipManager::GetSingleton()) em->EquipSpell(player, sp, RE::TESForm::LookupByID<RE::BGSEquipSlot>(hand == "l" ? 0x013F43 : 0x013F42));
				}
				logger::info("dev: arequip {} -> {} [{:08X}]", i, sp ? sp->GetName() : "none", sp ? sp->GetFormID() : 0);
			} else if (cmd == "fxlist" && player) {  // fxlist: every effect shader and art on you (the blade's glow hunt), and your blade's enchantment
				std::string out;
				RE::ProcessLists::GetSingleton()->ForEachShaderEffect([&](RE::ShaderReferenceEffect* e) {
					if (e && e->target.get().get() == player) out += fmt::format("[shader {} {:08X} worn {}] ", e->effectData ? e->effectData->GetFormEditorID() : "?", e->effectData ? e->effectData->GetFormID() : 0, e->wornObject ? e->wornObject->GetName() : "-");
					return RE::BSContainer::ForEachResult::kContinue;
				});
				RE::ProcessLists::GetSingleton()->ForEachModelEffect([&](RE::ModelReferenceEffect* e) {
					if (e && e->target.get().get() == player) out += fmt::format("[art {:08X} {}] ", e->artObject ? e->artObject->GetFormID() : 0, e->artObject ? e->artObject->model.c_str() : "?");
					return RE::BSContainer::ForEachResult::kContinue;
				});
				auto* w = player->GetEquippedObject(false);
				auto* wp = w ? w->As<RE::TESObjectWEAP>() : nullptr;
				std::string ench = wp && wp->formEnchanting ? fmt::format("{} {:08X}", wp->formEnchanting->GetName(), wp->formEnchanting->GetFormID()) : "none";
				if (auto* inv = player->GetInventoryChanges(); inv && inv->entryList && w) {
					for (auto* e : *inv->entryList) {
						if (!e || e->object != w || !e->extraLists) continue;
						for (auto* x : *e->extraLists) if (x) if (auto* xe = x->GetByType<RE::ExtraEnchantment>(); xe && xe->enchantment) ench += fmt::format(" | extra {} {:08X}", xe->enchantment->GetName(), xe->enchantment->GetFormID());
					}
				}
				logger::info("dev: fxlist - weapon {} enchantment {} | {}", w ? w->GetName() : "none", ench, out);
			} else if (cmd == "arclean" && player) {  // the spells arequip taught taken back
				int n = 0;
				for (auto* sp : DevAddedSpells()) if (sp && player->HasSpell(sp)) player->RemoveSpell(sp), ++n;
				DevAddedSpells().clear();
				logger::info("dev: arclean - {} spells taken back", n);
			} else if (cmd == "fxreload") {
				Gun::DevFxReload();
			} else if (cmd == "swing") {  // swing <0 light|1 heavy>: the weapon in hand's swing effects
				int pw = 0;
				in >> pw;
				Gun::DevSwing(pw);
				logger::info("dev: swing {}", pw ? "heavy" : "light");
			} else if (cmd == "nodes") {
				Gun::DevNodes();
			} else if (cmd == "quit") {  // the game closed to the desktop (so a new build can go in)
				logger::info("dev: quitting to the desktop");
				if (auto* m = RE::Main::GetSingleton()) m->quitGame = true;
			} else if (cmd == "clawrot") {  // clawrot <x> <y> <z> | clawrot off: the claws' turn in first person, in degrees
				std::string a;
				in >> a;
				// clawrot <x> <y> <z> [3]: the first-person turn (or with 3, the third-person one)
				if (a == "off") Classes::DevClawRot(0, 0, 0, false, false);
				else {
					float x = 0, y = 0, z = 0;
					int   v = 1;
					try { x = std::stof(a); } catch (...) {}
					in >> y >> z >> v;
					Classes::DevClawRot(x, y, z, true, v == 3);
				}
			} else if (cmd == "clawform") {  // clawform <hex NewArmoury id> <x> <y> <z>: that one claw's own turn, both views
				std::string id;
				float       x = 0, y = 0, z = 0;
				in >> id >> x >> y >> z;
				try { Classes::DevClawForm(static_cast<RE::FormID>(std::stoul(id, nullptr, 16)), x, y, z); } catch (...) {}
			} else if (cmd == "fly") {  // fly on|off: fly where you look (sprint: faster), hang in the air when you let go
				std::string v;
				in >> v;
				Director::DevFly(v != "off");
			} else if (cmd == "freeloop") {
				// freeloop [seconds] [town index] | freeloop off: (Noah: "run the echo altar being freed animation every 20
				// seconds so I can get a clip of it") the city's freeing - the charge, the burst, the runes - played again and
				// again (the city you're in unless a town is named); off stops it
				static int loopGen = 0;
				std::string a;
				in >> a;
				++loopGen;
				if (a == "off") {
					Shops::DevFreed(-1, false);
					logger::info("dev: freeloop off");
				} else {
					float secs = 20.0f;
					int   town = -1;
					try { if (!a.empty()) secs = std::max(10.0f, std::stof(a)); } catch (...) {}
					in >> town;
					if (town < 0) town = Curse::ZoneTown();
					if (town < 0) {
						logger::info("dev: freeloop - not in a city (name a town index)");
					} else {
						const int mine = loopGen;
						static std::function<void(int, int, float)> again;
						again = [](int a_gen, int a_town, float a_secs) {
							if (a_gen != loopGen) return;
							Shops::FreedPulse(a_town);
							Tasks::Later(a_secs, [a_gen, a_town, a_secs] { if (a_gen == loopGen) Tasks::Add([a_gen, a_town, a_secs] { again(a_gen, a_town, a_secs); }); });
						};
						Shops::DevFreed(town, true);   // (a city not yet freed: its freed look - the rune, the motes - shown while the loop runs)
						logger::info("dev: freeloop - {} freed every {:.0f} s", gen::kTowns[town], secs);
						again(mine, town, secs);
					}
				}
			} else if (cmd == "tablet") {  // tablet <roll|gold|town <n>>: the Tablet of Mysteries rolled now, or moved
				std::string what;
				int t = -1;
				in >> what >> t;
				Tablet::Dev(what, t);
			} else if (cmd == "wonder") {  // wonder <class> <state 0-5>
				int c = -1, st = 0;
				in >> c >> st;
				Classes::DevWonder(c, st);
			} else if (cmd == "spells" && player) {  // spells: what you know and hold (relic checks)
				std::string known;
				for (auto* sp : player->GetActorRuntimeData().addedSpells) {
					if (sp) known += fmt::format("{} [{:08X} cost {:.0f}], ", sp->GetName(), sp->GetFormID(), sp->CalculateMagickaCost(player));
				}
				auto nm = [&](bool a_left) {
					auto* o = player->GetEquippedObject(a_left);
					return o ? fmt::format("{} {:08X}", o->GetName(), o->GetFormID()) : std::string("-");
				};
				logger::info("dev: spells known: {} | right {} | left {} | magicka {:.0f}/{:.0f}", known, nm(false), nm(true),
					player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka), player->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kMagicka));
			} else if (cmd == "sounds" && player) {
				// sounds [range]: every sound marker and looping activator around you - what plays, from which plugin (Noah: the
				// static by Riverwood's river)
				float range = 4000.0f;
				in >> range;
				int n = 0;
				RE::TES::GetSingleton()->ForEachReferenceInRange(player, range, [&](RE::TESObjectREFR* r) {
					auto* base = r ? r->GetBaseObject() : nullptr;
					if (!base) return RE::BSContainer::ForEachResult::kContinue;
					RE::BGSSoundDescriptorForm* d = nullptr;
					if (auto* so = base->As<RE::TESSound>()) d = so->descriptor;
					else if (auto* ac = base->As<RE::TESObjectACTI>()) d = ac->soundLoop;
					if (!d && !base->Is(RE::FormType::Sound)) return RE::BSContainer::ForEachResult::kContinue;
					auto* bf = base->GetFile(0);
					auto* rf = r->GetFile(0);
					logger::info("dev: sound ref {:08X} ({}) base {:08X} '{}' [{}] desc {:08X} [{}] d {:.0f}{}", r->GetFormID(), rf ? rf->GetFilename() : "?",
						base->GetFormID(), base->GetFormEditorID() ? base->GetFormEditorID() : "", bf ? bf->GetFilename() : "?",
						d ? d->GetFormID() : 0, d && d->GetFile(0) ? d->GetFile(0)->GetFilename() : "?", r->GetPosition().GetDistance(player->GetPosition()),
						r->IsDisabled() ? " (disabled)" : "");
					++n;
					return RE::BSContainer::ForEachResult::kContinue;
				});
				logger::info("dev: sounds {} within {:.0f}", n, range);
			} else if (cmd == "floaters" && player) {
				// floaters [range]: trees, rocks and plants round you standing above the ground under them (Noah: "floating rocks
				// and trees and bushes") - each with the plugin that last placed or moved it
				float range = 6000.0f;
				in >> range;
				int n = 0, all = 0;
				std::map<std::string, int> byFile;
				RE::TES::GetSingleton()->ForEachReferenceInRange(player, range, [&](RE::TESObjectREFR* r) {
					auto* base = r ? r->GetBaseObject() : nullptr;
					if (!base || r->IsDisabled() || r->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;
					const auto ft = base->GetFormType();
					if (ft != RE::FormType::Tree && ft != RE::FormType::Static && ft != RE::FormType::Flora && ft != RE::FormType::MovableStatic) return RE::BSContainer::ForEachResult::kContinue;
					auto* bo = base->As<RE::TESBoundObject>();
					if (!bo) return RE::BSContainer::ForEachResult::kContinue;
					const auto pos = r->GetPosition();
					float land = 0;
					if (!RE::TES::GetSingleton()->GetLandHeight(pos, land)) return RE::BSContainer::ForEachResult::kContinue;
					++all;
					const float scale = r->GetScale();
					const float bottom = pos.z + static_cast<float>(bo->boundData.boundMin.z) * scale;
					const float sizeXY = static_cast<float>(std::max(bo->boundData.boundMax.x - bo->boundData.boundMin.x, bo->boundData.boundMax.y - bo->boundData.boundMin.y)) * scale;
					if (bottom - land > 60.0f && std::abs(r->data.angle.x) < 0.3f && std::abs(r->data.angle.y) < 0.3f) {
						auto* last = r->GetDescriptionOwnerFile();
						auto* first = r->GetFile(0);
						const std::string lf = last ? std::string(last->GetFilename()) : "?";
						++byFile[lf];
						if (n++ < 40) logger::info("dev: floater {:08X} {} '{}' {:.0f} above the ground (size {:.0f}) at ({:.0f},{:.0f},{:.0f}) placed by {} last moved by {}", r->GetFormID(),
							ft == RE::FormType::Tree ? "tree" : ft == RE::FormType::Flora ? "plant" : "static", base->GetFormEditorID() ? base->GetFormEditorID() : "", bottom - land, sizeXY, pos.x, pos.y, pos.z,
							first ? first->GetFilename() : "?", lf);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				std::string files;
				for (auto& [f, c] : byFile) files += fmt::format("{} {}, ", f, c);
				logger::info("dev: floaters {} of {} within {:.0f} - by the plugin that last moved them: {}", n, all, range, files);
			} else if (cmd == "hairs" && player) {
				// hairs: the playable hairs Skyrim loaded, by plugin - for men, for women, and for your race (Noah: "I didnt see any
				// of the new hairstyles")
				std::map<std::string, std::array<int, 3>> by;
				auto* race = player->GetRace();
				for (auto* hp : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSHeadPart>()) {
					if (!hp || hp->type.get() != RE::BGSHeadPart::HeadPartType::kHair || !hp->flags.any(RE::BGSHeadPart::Flag::kPlayable)) continue;
					auto* f = hp->GetFile(0);
					auto& row = by[f ? std::string(f->GetFilename()) : "?"];
					if (hp->flags.any(RE::BGSHeadPart::Flag::kMale)) ++row[0];
					if (hp->flags.any(RE::BGSHeadPart::Flag::kFemale)) ++row[1];
					if (race && hp->validRaces && hp->validRaces->HasForm(race)) ++row[2];
				}
				for (auto& [f, r] : by) logger::info("dev: hairs {} - male {} female {} your race ({}) {}", f, r[0], r[1], race ? race->GetFormEditorID() : "?", r[2]);
			} else if (cmd == "near" && player) {
				// near [range]: the trees, rocks and plants nearest you - model, who placed it, who last moved it, its height
				// over the ground (Noah: stand by a floating one)
				float range = 700.0f;
				in >> range;
				struct Row { float d; std::string line; };
				std::vector<Row> rows;
				RE::TES::GetSingleton()->ForEachReferenceInRange(player, range, [&](RE::TESObjectREFR* r) {
					auto* base = r ? r->GetBaseObject() : nullptr;
					if (!base || r == player || r->IsDisabled() || r->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;
					const auto ft = base->GetFormType();
					if (ft != RE::FormType::Tree && ft != RE::FormType::Static && ft != RE::FormType::Flora && ft != RE::FormType::MovableStatic && ft != RE::FormType::Activator) return RE::BSContainer::ForEachResult::kContinue;
					auto* mdl = base->As<RE::TESModel>();
					const auto pos = r->GetPosition();
					float land = 0;
					const bool got = RE::TES::GetSingleton()->GetLandHeight(pos, land);
					auto* last = r->GetDescriptionOwnerFile();
					auto* first = r->GetFile(0);
					auto* bf = base->GetFile(0);
					auto* bl = base->GetDescriptionOwnerFile();
					const float d = pos.GetDistance(player->GetPosition());
					rows.push_back({ d, fmt::format("dev: near {:08X} d {:.0f} base {:08X} [{} / last {}] model '{}' z {:.0f} land {:.0f} ({:+.0f}) scale {:.2f} 3d {} placed by {} last moved by {}", r->GetFormID(), d,
						base->GetFormID(), bf ? bf->GetFilename() : "?", bl ? bl->GetFilename() : "?", mdl ? mdl->GetModel() : "", pos.z, got ? land : 0.0f, got ? pos.z - land : 0.0f, r->GetScale(),
						r->Get3D() != nullptr, first ? first->GetFilename() : "?", last ? last->GetFilename() : "?") });
					return RE::BSContainer::ForEachResult::kContinue;
				});
				std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.d < b.d; });
				for (std::size_t i = 0; i < rows.size() && i < 25; ++i) logger::info("{}", rows[i].line);
				logger::info("dev: near {} within {:.0f}", rows.size(), range);
			} else if (cmd == "hush") {
				// hush <ref hex>: switch one placed sound off (disable) - hush <ref hex> on: back on
				std::string hex, on;
				in >> hex >> on;
				if (auto* r = hex.empty() ? nullptr : RE::TESForm::LookupByID<RE::TESObjectREFR>(static_cast<RE::FormID>(std::stoul(hex, nullptr, 16)))) {
					if (on == "on") r->Enable(false); else r->Disable();
					logger::info("dev: hush {:08X} {}", r->GetFormID(), on == "on" ? "on" : "off");
				}
			} else if (cmd == "foes" && player) {  // foes: the undead round you - their spells, shouts and voice
				int n = 0;
				const auto at = player->GetPosition();
				if (auto* pl = RE::ProcessLists::GetSingleton()) {
					for (auto& h : pl->highActorHandles) {
						auto a = h.get();
						if (!a || a->IsDead() || a.get() == player || a->GetPosition().GetDistance(at) > 6000.0f || n >= 24) continue;
						auto* base = a->GetActorBase();
						std::string sps;
						int shouts = 0;
						if (auto* spl = base ? base->GetSpellList() : nullptr) {
							for (std::uint32_t i = 0; i < spl->numSpells; ++i) {
								if (spl->spells[i]) sps += fmt::format("{}|", spl->spells[i]->GetName());
							}
							shouts = static_cast<int>(spl->numShouts);
						}
						for (auto* sp : a->GetActorRuntimeData().addedSpells) {
							if (sp) sps += fmt::format("+{}|", sp->GetName());
						}
						++n;
						logger::info("dev: foe {} [{:08X}] d {:.0f} hp {:.0f} voice {:.0f} shouts {} spells {} | conf {:.0f} aggr {:.0f} destr {:.0f}", a->GetName(), a->GetFormID(),
							a->GetPosition().GetDistance(at), a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth),
							a->GetActorRuntimeData().voiceTimer, shouts, sps, a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kConfidence),
							a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression), a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kDestruction));
					}
				}
				logger::info("dev: foes {}", n);
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
			// (a second Skyrim left running in the background read the same file and took half the commands) only the
			// game in front answers
			if (DWORD pid = 0; ::GetWindowThreadProcessId(::GetForegroundWindow(), &pid), pid != ::GetCurrentProcessId()) {
				if (static int told = 0; told++ % 200 == 0) logger::info("dev: a command waits - this game isn't the window in front (pid {})", ::GetCurrentProcessId());
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
				} else if (s.rfind("key ", 0) == 0 || s.rfind("type ", 0) == 0 || s.rfind("gfx ", 0) == 0 || s.rfind("gfxtree ", 0) == 0 || s.rfind("menus", 0) == 0 || s.rfind("mouse ", 0) == 0 || s.rfind("hold ", 0) == 0 || s.rfind("keyhold ", 0) == 0 || s.rfind("close ", 0) == 0) {
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

		void TickCamAnim()
		{
			auto& A = g_camAnim;
			if (A.mode == 0) return;
			auto* pc = RE::PlayerCamera::GetSingleton();
			if (!pc || !pc->IsInFreeCameraMode() || !pc->currentState) { A.mode = 0; return; }
			auto*       fc = static_cast<RE::FreeCameraState*>(pc->currentState.get());
			const float el = std::chrono::duration<float>(std::chrono::steady_clock::now() - A.t0).count();
			constexpr float kRad = 0.0174533f;
			if (A.mode == 1) {
				float u = std::clamp(el / std::max(0.05f, A.dur), 0.0f, 1.0f);
				if (A.ease) u = u * u * (3.0f - 2.0f * u);
				fc->translation = A.a + (A.b - A.a) * u;
				float dy = A.yaw1 - A.yaw0;
				while (dy > 180.0f) dy -= 360.0f;
				while (dy < -180.0f) dy += 360.0f;
				fc->rotation.y = (A.yaw0 + dy * u) * kRad;
				fc->rotation.x = (A.pitch0 + (A.pitch1 - A.pitch0) * u) * kRad;
			} else if (A.mode == 3) {
				auto* t = RE::TESForm::LookupByID<RE::Actor>(A.target);
				if (!t || !t->Get3D() || t->IsDisabled()) return;
				if (A.onPlayer) {
					if (auto* p = RE::PlayerCharacter::GetSingleton()) fc->translation = p->GetPosition() + RE::NiPoint3{ 0, 0, A.height };
				} else {
					fc->translation = A.a;
				}
				RE::NiPoint3 h = t->GetPosition();
				if (auto* n = t->Get3D()->GetObjectByName("NPC Head [Head]")) h = n->world.translate;
				else if (auto* n2 = t->Get3D()->GetObjectByName("NPC Head")) h = n2->world.translate;
				else h.z += 300.0f;
				const RE::NiPoint3 d = h - fc->translation;
				if (!(d.Length() > 1.0f)) return;  // (never a NaN aim)
				const float        flat = std::sqrt(d.x * d.x + d.y * d.y);
				fc->rotation.y = std::atan2(d.x, d.y);
				fc->rotation.x = -std::atan2(d.z, std::max(1.0f, flat));
				if (el - A.lastLog > 0.25f) {
					A.lastLog = el;
					logger::info("dev: camtrack t {:.2f} dist {:.0f}", el, d.Length());
				}
				return;
			} else {
				const float deg = A.deg0 + A.degPerSec * el;
				fc->translation = { A.c.x + std::sin(deg * kRad) * A.radius, A.c.y + std::cos(deg * kRad) * A.radius, A.c.z + A.height };
				fc->rotation.y = (deg + 180.0f) * kRad;
				fc->rotation.x = (std::atan2(A.height, std::max(1.0f, A.radius)) / kRad + A.tilt) * kRad;
			}
			if (A.dur > 0 && el > A.dur + (A.mode == 2 ? 0.0f : 30.0f)) A.mode = A.mode == 2 ? 0 : A.mode;  // (an orbit with a length stops; a path holds its end)
		}

		void RunQueuedOnMain()
		{
			FeedKeys();
			TickCamAnim();
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
