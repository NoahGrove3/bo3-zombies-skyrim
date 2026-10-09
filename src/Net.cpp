#include "BO3Z.h"

// Co-op, our own layer (Skyrim Together moves the players; this carries the zombies game).
//
// Lobby: Steam friends-only lobby. The host makes it from the main menu (or in game) and invites through the
// Steam overlay; a friend accepts the invite (or picks the host from the "friends playing" list) and joins. The
// lobby owner is the host. No IP addresses, no party file.
//
// Messages: Steam's ISteamNetworkingMessages, peer to peer through Steam's relays, on channel 7.
//   host -> everyone, 4 times a second: the night (phase, round, countdown, event), the boss bar, the Coffer's
//           town, power-up timers and the Nuke, and the Undying Dirge's progress in every city
//   client -> host, once a second: its Dirge progress (the larger of the two wins, so anyone's quest work counts)
//   anyone -> everyone: a power-up taken (the host relays a client's)
// Everything goes through steam_api64.dll's flat C API, loaded at run time (Skyrim ships it and runs its callbacks).
namespace bo3z::Net
{
	namespace
	{
		using u64 = std::uint64_t;
		constexpr int kChannel = 7;
		constexpr std::uint8_t kVersion = 1;
		enum : std::uint8_t { kHello = 1, kState = 2, kPowerUp = 3, kCurse = 4 };

		// ---- the flat API ----
		struct Identity  // SteamNetworkingIdentity (136 bytes)
		{
			std::int32_t type{ 16 };  // k_ESteamNetworkingIdentityType_SteamID
			std::int32_t size{ 8 };
			u64          steamId{ 0 };
			char         pad[120]{};
		};
		static_assert(sizeof(Identity) == 136);
		struct Message  // SteamNetworkingMessage_t (start of it)
		{
			void*        data;
			std::int32_t size;
			std::uint32_t conn;
			Identity     peer;
		};
		struct FriendGameInfo
		{
			u64           gameId;
			std::uint32_t ip;
			std::uint16_t port, queryPort;
			u64           lobby;
		};
		static_assert(sizeof(FriendGameInfo) == 24);

		struct Api
		{
			bool ok{ false };
			void *mm{}, *fr{}, *nm{}, *us{}, *ut{};
			u64 (*GetSteamID)(void*){};
			u64 (*CreateLobby)(void*, int, int){};
			u64 (*JoinLobby)(void*, u64){};
			void (*LeaveLobby)(void*, u64){};
			u64 (*GetLobbyOwner)(void*, u64){};
			int (*GetNumLobbyMembers)(void*, u64){};
			u64 (*GetLobbyMemberByIndex)(void*, u64, int){};
			bool (*SetLobbyData)(void*, u64, const char*, const char*){};
			bool (*SetLobbyJoinable)(void*, u64, bool){};
			void (*InviteDialog)(void*, u64){};
			int (*GetFriendCount)(void*, int){};
			u64 (*GetFriendByIndex)(void*, int, int){};
			bool (*GetFriendGamePlayed)(void*, u64, FriendGameInfo*){};
			const char* (*GetFriendPersonaName)(void*, u64){};
			bool (*IsAPICallCompleted)(void*, u64, bool*){};
			bool (*GetAPICallResult)(void*, u64, void*, int, int, bool*){};
			int (*SendMessageToUser)(void*, const Identity&, const void*, std::uint32_t, int, int){};
			int (*ReceiveMessagesOnChannel)(void*, int, Message**, int){};
			bool (*AcceptSessionWithUser)(void*, const Identity&){};
			void (*Release)(Message*){};
			void (*RegisterCallback)(void*, int){};
		} A;

		template <class F>
		bool Load(HMODULE h, F& f, const char* name)
		{
			f = reinterpret_cast<F>(::GetProcAddress(h, name));
			if (!f) {
				logger::warn("net: steam_api64 has no {}", name);
			}
			return f != nullptr;
		}

		bool Init()
		{
			static bool tried = false;
			if (tried) {
				return A.ok;
			}
			tried = true;
			HMODULE h = ::GetModuleHandleW(L"steam_api64.dll");
			if (!h) {
				logger::warn("net: steam_api64.dll is not loaded (not the Steam version?): co-op lobby off");
				return false;
			}
			void* (*mm)() = nullptr; void* (*fr)() = nullptr; void* (*nm)() = nullptr; void* (*us)() = nullptr; void* (*ut)() = nullptr;
			bool ok = Load(h, mm, "SteamAPI_SteamMatchmaking_v009") & Load(h, fr, "SteamAPI_SteamFriends_v017") &
			          Load(h, nm, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002") & Load(h, us, "SteamAPI_SteamUser_v021") & Load(h, ut, "SteamAPI_SteamUtils_v010");
			ok &= Load(h, A.GetSteamID, "SteamAPI_ISteamUser_GetSteamID") & Load(h, A.CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby") &
			      Load(h, A.JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby") & Load(h, A.LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby") &
			      Load(h, A.GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner") & Load(h, A.GetNumLobbyMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers") &
			      Load(h, A.GetLobbyMemberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex") & Load(h, A.SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData") &
			      Load(h, A.SetLobbyJoinable, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable") &
			      Load(h, A.InviteDialog, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog") & Load(h, A.GetFriendCount, "SteamAPI_ISteamFriends_GetFriendCount") &
			      Load(h, A.GetFriendByIndex, "SteamAPI_ISteamFriends_GetFriendByIndex") & Load(h, A.GetFriendGamePlayed, "SteamAPI_ISteamFriends_GetFriendGamePlayed") &
			      Load(h, A.GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName") & Load(h, A.IsAPICallCompleted, "SteamAPI_ISteamUtils_IsAPICallCompleted") &
			      Load(h, A.GetAPICallResult, "SteamAPI_ISteamUtils_GetAPICallResult") & Load(h, A.SendMessageToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser") &
			      Load(h, A.ReceiveMessagesOnChannel, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel") &
			      Load(h, A.AcceptSessionWithUser, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser") & Load(h, A.Release, "SteamAPI_SteamNetworkingMessage_t_Release") &
			      Load(h, A.RegisterCallback, "SteamAPI_RegisterCallback");
			if (!ok) {
				return false;
			}
			A.mm = mm(), A.fr = fr(), A.nm = nm(), A.us = us(), A.ut = ut();
			A.ok = A.mm && A.fr && A.nm && A.us && A.ut;
			logger::info("net: Steam {} (you are {:016X})", A.ok ? "ready" : "interfaces missing", A.ok ? A.GetSteamID(A.us) : 0);
			return A.ok;
		}

		// ---- a Steam callback object (same layout as Steamworks' CCallbackBase) ----
		class CallbackBase
		{
		public:
			virtual void Run(void* a_param) = 0;
			virtual void Run(void* a_param, bool a_ioFailure, u64 a_call) = 0;
			int          GetICallback() { return m_iCallback; }
			virtual int  GetCallbackSizeBytes() = 0;

		protected:
			std::uint8_t m_nCallbackFlags{ 0 };
			int          m_iCallback{ 0 };
		};

		// state
		u64         g_me = 0, g_lobby = 0, g_owner = 0;
		u64         g_pendingCreate = 0, g_pendingJoin = 0;
		u64         g_joinWanted = 0;    // an invite accepted / a friend picked: join when we can
		std::string g_status = "Not in a lobby";
		std::vector<u64> g_members;
		float       g_memberTimer = 0, g_sendTimer = 0, g_curseTimer = 0;
		std::chrono::steady_clock::time_point g_lastState{};
		State       g_last{};
		bool        g_haveState = false;
		std::vector<int> g_powerIn;   // power-ups taken elsewhere, applied on the main thread

		struct JoinRequested final : CallbackBase  // GameLobbyJoinRequested_t (333): an invite accepted in the overlay
		{
			void Run(void* a_param) override
			{
				const auto* p = static_cast<const u64*>(a_param);
				g_joinWanted = p[0];
				logger::info("net: invite accepted (lobby {:016X}, from {:016X})", p[0], p[1]);
			}
			void Run(void* a_param, bool, u64) override { Run(a_param); }
			int  GetCallbackSizeBytes() override { return 16; }
			void Register()
			{
				m_iCallback = 333;
				A.RegisterCallback(this, 333);
			}
		} g_joinCb;

		Identity Id(u64 a_id)
		{
			Identity i;
			i.steamId = a_id;
			return i;
		}

		void Send(u64 a_to, const void* a_data, std::uint32_t a_size, bool a_reliable)
		{
			if (a_to && a_to != g_me) {
				A.SendMessageToUser(A.nm, Id(a_to), a_data, a_size, a_reliable ? 8 : 0, kChannel);  // 8 = reliable, 0 = unreliable
			}
		}

		void ToAll(const void* a_data, std::uint32_t a_size, bool a_reliable)
		{
			for (u64 m : g_members) {
				Send(m, a_data, a_size, a_reliable);
			}
		}

		void RefreshMembers()
		{
			g_members.clear();
			if (!g_lobby) {
				return;
			}
			const int n = A.GetNumLobbyMembers(A.mm, g_lobby);
			for (int i = 0; i < n; ++i) {
				const u64 m = A.GetLobbyMemberByIndex(A.mm, g_lobby, i);
				if (m && m != g_me) {
					g_members.push_back(m);
					A.AcceptSessionWithUser(A.nm, Id(m));  // their messages are welcome
				}
			}
			const u64 owner = A.GetLobbyOwner(A.mm, g_lobby);
			if (owner && owner != g_owner) {
				g_owner = owner;
				logger::info("net: lobby {:016X} host is {}", g_lobby, owner == g_me ? "you" : A.GetFriendPersonaName(A.fr, owner));
			}
		}

		void Entered(u64 a_lobby, bool a_host)
		{
			g_lobby = a_lobby;
			g_owner = a_host ? g_me : A.GetLobbyOwner(A.mm, a_lobby);
			RefreshMembers();
			g_status = a_host ? "Hosting - invite your friends" : std::string("In ") + A.GetFriendPersonaName(A.fr, g_owner) + "'s party";
			if (!a_host) {
				const std::uint8_t hello[2]{ kHello, kVersion };
				Send(g_owner, hello, 2, true);
			}
			Hud::Banner(a_host ? "Co-op party open: invite friends from the Steam overlay" : "Joined the party - the host runs the nights", 4.0f);
			logger::info("net: {} lobby {:016X} ({} others)", a_host ? "hosting" : "joined", a_lobby, g_members.size());
		}

		void PollCalls()
		{
			bool failed = false;
			if (g_pendingCreate && A.IsAPICallCompleted(A.ut, g_pendingCreate, &failed)) {
				struct { std::int32_t result; std::uint32_t pad; u64 lobby; } r{};  // LobbyCreated_t (513)
				const bool got = A.GetAPICallResult(A.ut, g_pendingCreate, &r, sizeof(r), 513, &failed);
				g_pendingCreate = 0;
				if (got && !failed && r.result == 1 && r.lobby) {
					A.SetLobbyData(A.mm, r.lobby, "undying_nights", std::to_string(kVersion).c_str());
					A.SetLobbyJoinable(A.mm, r.lobby, true);
					Entered(r.lobby, true);
					A.InviteDialog(A.fr, r.lobby);
				} else {
					g_status = "Couldn't open a party (Steam said no)";
					logger::warn("net: CreateLobby failed (result {})", r.result);
				}
			}
			if (g_pendingJoin && A.IsAPICallCompleted(A.ut, g_pendingJoin, &failed)) {
				struct { u64 lobby; std::uint32_t perms; bool locked; std::uint32_t response; } r{};  // LobbyEnter_t (504)
				const bool got = A.GetAPICallResult(A.ut, g_pendingJoin, &r, sizeof(r), 504, &failed);
				g_pendingJoin = 0;
				if (got && !failed && r.response == 1) {
					Entered(r.lobby, false);
				} else {
					g_status = "Couldn't join that party";
					logger::warn("net: JoinLobby failed (response {})", r.response);
				}
			}
		}

		void Receive()
		{
			Message* msgs[32];
			const int n = A.ReceiveMessagesOnChannel(A.nm, kChannel, msgs, 32);
			for (int i = 0; i < n; ++i) {
				const auto* d = static_cast<const std::uint8_t*>(msgs[i]->data);
				const int   sz = msgs[i]->size;
				const u64   from = msgs[i]->peer.steamId;
				if (sz >= 1) {
					switch (d[0]) {
					case kHello:
						logger::info("net: {} joined the night", A.GetFriendPersonaName(A.fr, from));
						Hud::Banner(std::string(A.GetFriendPersonaName(A.fr, from)) + " joined the party", 3.0f);
						break;
					case kState:
						if (sz >= static_cast<int>(sizeof(State)) && from == g_owner && g_owner != g_me) {
							std::memcpy(&g_last, d, sizeof(State));
							g_haveState = true;
							g_lastState = std::chrono::steady_clock::now();
						}
						break;
					case kPowerUp:
						if (sz >= 2) {
							g_powerIn.push_back(d[1]);
							if (IsHost()) {  // relay a friend's grab to the others
								for (u64 m : g_members) {
									if (m != from) {
										Send(m, d, 2, true);
									}
								}
							}
						}
						break;
					case kCurse:
						if (IsHost() && sz >= 1 + 4 * static_cast<int>(gen::kCurseCities.size())) {
							for (std::size_t t = 0; t < gen::kCurseCities.size(); ++t) {
								std::int32_t v;
								std::memcpy(&v, d + 1 + 4 * t, 4);
								if (v > Curse::Raw(static_cast<int>(t))) {
									Curse::SetRaw(static_cast<int>(t), v);  // a friend's quest work counts for everyone
								}
							}
						}
						break;
					default: break;
					}
				}
				A.Release(msgs[i]);
			}
		}
	}

	bool Available() { return Init(); }
	bool InLobby() { return g_lobby != 0; }
	bool IsHost() { return g_lobby && g_owner == g_me; }
	bool IsClient() { return g_lobby && g_owner && g_owner != g_me; }
	bool Fresh() { return IsClient() && g_haveState && std::chrono::steady_clock::now() - g_lastState < std::chrono::seconds(3); }
	const State& Last() { return g_last; }
	std::string Status() { return g_status; }
	int Members() { return static_cast<int>(g_members.size()); }
	std::string HostName() { return g_owner && A.ok ? A.GetFriendPersonaName(A.fr, g_owner) : ""; }

	void HostLobby()
	{
		if (!Init() || g_pendingCreate) {
			return;
		}
		if (g_lobby) {
			A.InviteDialog(A.fr, g_lobby);
			return;
		}
		g_pendingCreate = A.CreateLobby(A.mm, 1, 8);  // friends only, up to 8
		g_status = "Opening a party...";
		logger::info("net: creating a lobby");
	}
	void InviteFriends()
	{
		if (Init() && g_lobby) {
			A.InviteDialog(A.fr, g_lobby);
		}
	}
	void JoinLobby(std::uint64_t a_lobby)
	{
		if (!Init() || !a_lobby || g_pendingJoin) {
			return;
		}
		if (g_lobby) {
			LeaveLobby();
		}
		g_pendingJoin = A.JoinLobby(A.mm, a_lobby);
		g_status = "Joining...";
		logger::info("net: joining lobby {:016X}", a_lobby);
	}
	void LeaveLobby()
	{
		if (A.ok && g_lobby) {
			A.LeaveLobby(A.mm, g_lobby);
			logger::info("net: left lobby {:016X}", g_lobby);
		}
		g_lobby = g_owner = 0;
		g_members.clear();
		g_haveState = false;
		g_status = "Not in a lobby";
	}

	std::vector<FriendGame> FriendsInParties()
	{
		std::vector<FriendGame> out;
		if (!Init()) {
			return out;
		}
		const int n = A.GetFriendCount(A.fr, 4);  // k_EFriendFlagImmediate
		for (int i = 0; i < n; ++i) {
			const u64      f = A.GetFriendByIndex(A.fr, i, 4);
			FriendGameInfo g{};
			if (A.GetFriendGamePlayed(A.fr, f, &g) && (g.gameId & 0xFFFFFF) == 489830 && g.lobby) {  // Skyrim Special Edition, in a lobby
				out.push_back({ f, g.lobby, A.GetFriendPersonaName(A.fr, f) });
			}
		}
		return out;
	}

	void PowerUpTaken(int a_kind)
	{
		if (!g_lobby) {
			return;
		}
		const std::uint8_t m[2]{ kPowerUp, static_cast<std::uint8_t>(a_kind) };
		if (IsHost()) {
			ToAll(m, 2, true);
		} else {
			Send(g_owner, m, 2, true);
		}
	}

	std::vector<int> TakeRemotePowerUps()
	{
		std::vector<int> out;
		out.swap(g_powerIn);
		return out;
	}

	// Main thread, every frame (menus too: the lobby works from the main menu).
	void Tick(float a_delta)
	{
		if (!Init()) {
			return;
		}
		static bool once = false;
		if (!once) {
			once = true;
			g_me = A.GetSteamID(A.us);
			g_joinCb.Register();
			// launched from an invite while the game was closed: "+connect_lobby <id>" on the command line
			std::wstring cmd = ::GetCommandLineW();
			if (const auto p = cmd.find(L"+connect_lobby"); p != std::wstring::npos) {
				g_joinWanted = std::wcstoull(cmd.c_str() + p + 14, nullptr, 10);
				logger::info("net: started from an invite (lobby {:016X})", g_joinWanted);
			}
		}
		if (g_joinWanted) {
			const u64 l = g_joinWanted;
			g_joinWanted = 0;
			JoinLobby(l);
		}
		PollCalls();
		if (!g_lobby) {
			return;
		}
		if ((g_memberTimer -= a_delta) <= 0) {
			g_memberTimer = 1.0f;
			RefreshMembers();
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (!IsHost() && pl && pl->Is3DLoaded()) {  // our Dirge progress to the host (the larger counts)
				std::vector<std::uint8_t> m(1 + 4 * gen::kCurseCities.size());
				m[0] = kCurse;
				for (std::size_t t = 0; t < gen::kCurseCities.size(); ++t) {
					const std::int32_t v = Curse::Raw(static_cast<int>(t));
					std::memcpy(m.data() + 1 + 4 * t, &v, 4);
				}
				Send(g_owner, m.data(), static_cast<std::uint32_t>(m.size()), true);
			}
		}
		Receive();
		auto* player = RE::PlayerCharacter::GetSingleton();
		const bool inGame = player && player->Is3DLoaded() && !RE::UI::GetSingleton()->IsMenuOpen(RE::MainMenu::MENU_NAME);
		if (IsHost() && inGame && (g_sendTimer -= a_delta) <= 0) {
			g_sendTimer = 0.25f;
			State s{};
			s.type = kState;
			s.version = kVersion;
			Director::NetFill(s);
			for (std::size_t t = 0; t < gen::kCurseCities.size() && t < std::size(s.curse); ++t) {
				s.curse[t] = Curse::Raw(static_cast<int>(t));
			}
			s.boxAv = Save::Get(gen::kAV_box);
			ToAll(&s, sizeof(s), false);
		}
	}
}
