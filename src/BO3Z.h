#pragma once
// BO3 Zombies x Skyrim. Every number comes from Generated.h (built from sheets/*.json).
#include "Generated.h"

namespace bo3z
{
	inline constexpr auto kVersion = "0.2.0";

	// Forms read from the player's own Skyrim and from BO3Zombies.esp.
	namespace Forms
	{
		bool                 Resolve();  // after data loaded
		RE::TESBoundObject*  Enemy(std::size_t a_index);
		RE::TESQuest*        IntroQuest();
		bool                 IntroDone();
		std::string          IntroDebug();
		RE::TESGlobal*       Quickstart();
		RE::TESObjectREFR*   RiverwoodMarker();
		RE::TESObjectWEAP*   Gun(int a_gun);
		int                  GunIndexOf(const RE::TESForm* a_form);  // -1 if not one of ours
		RE::AlchemyItem*     PerkPotion(int a_perk);
		int                  PerkIndexOfEffect(RE::FormID a_mgef);    // -1
		RE::TESObjectACTI*   Object(int a_object);
		int                  ObjectIndexOf(const RE::TESForm* a_base); // -1
		RE::TESBoundObject*  Ours(std::uint32_t a_localId);              // any record in our plugin
		RE::SpellItem*       GunSpell(int a_gun);                        // tomes/grimoires: the spell held in the hand
		RE::MagicItem*       Spell(int a_spell);                         // sheet: spells (a WEAP row = its enchantment)
		void                 Play(gen::Sound a_sound);
		void                 PlayAt(gen::Sound a_sound, RE::TESObjectREFR* a_at);  // 3D, follows the reference
		inline gen::Sound    Brew(int a_perk) { return static_cast<gen::Sound>(static_cast<int>(gen::Sound::brew_trollshide) + a_perk); }
	}

	// Plugins.txt: make sure BO3Zombies.esp is enabled (called from DllMain, before the game reads it).
	namespace PluginList
	{
		void Ensure();
	}

	// State kept on the player (sheet: persistence).
	namespace Save
	{
		struct Slot
		{
			int gun{ -1 };
			int clip{ 0 };
			int reserve{ 0 };
			int level{ 0 };  // Atronach Reforge level 0-3
		};
		int  Get(int a_av);
		void Set(int a_av, int a_value);
		Slot GetSlot(int a_slot);
		void SetSlot(int a_slot, const Slot& a_value);
		int  Flags();
		void SetFlags(int a_flags);
		bool Flag(int a_bit);
		void SetFlag(int a_bit, bool a_on);
		int  Crew();  // -1 none
		void SetCrew(int a_crew);
		bool HasPerk(int a_perk);
		void SetPerk(int a_perk, bool a_on);
		int  PerkCount();
		int  PerkBits();
		int  ActiveSlot();
		void SetActiveSlot(int a_slot);
		void Guard();           // every tick: puts the mod's values back if something wiped them (see Forms.cpp)
		void ForgetSnapshot();  // a load or the main menu: the next values are a different game's
		void OnWorldReady();
		inline constexpr int kFlagInit = 0, kFlagReserveV2 = 4, kFlagStarted = 5, kFlagMoved = 11, kPerkShift = 14, kFlagPerksV2 = 23;  // bits: init | crew 1-3 | (old perks 4-10) | moved 11 | slot 12-13 | perks 14-22 | v2 23
	}

	namespace PowerUps
	{
		void Tick(float a_delta);
		void MaybeDrop(const RE::NiPoint3& a_at);
		bool InstaKill();
		int  PointsMult();
		bool FireSale();
		float Timer(int a_kind);
		void  SetTimer(int a_kind, float a_seconds);
	}

	namespace Points
	{
		void Add(int a_points);
		bool Spend(int a_points);
		int  Get();
	}

	namespace Net { struct State; }

	namespace Director
	{
		void             Tick(float a_delta);
		void             Reset();
		bool             IsRoundUndead(RE::Actor* a_actor);
		int              EnemyRow(RE::Actor* a_actor);  // sheet row of one of ours, -1 otherwise
		bool             IsSpared(RE::Actor* a_actor);
		void             Frenzy(RE::Actor* a_actor, float a_seconds);  // Mindbreaker: turns on its own kind  // night truce: townsfolk and followers can't be hurt
		bool             NightNow();  // night hours (the undead are out)
		float            RoundHealth();  // a zombie's full health this round (Skyrim units)
		bool             IsElite(RE::Actor* a_actor);
		bool             IsBoss(RE::Actor* a_actor);
		std::string_view HeadNodeFor(RE::Actor* a_actor);
		void             LogState();
		void             Web(const RE::NiPoint3& a_at, float a_radius, float a_seconds, float a_amount = 60.0f, bool a_frostFx = false);   // Widow's Wine / wand slows
		void             Shock(const RE::NiPoint3& a_at, float a_radius, float a_bo3Damage);  // Electric Cherry
		// event nights (sheet: events) and the nuke
		int              EventNow();          // -1 none, else gen::kEvents index (this night)
		int              EventPointsMult();   // Ash Storm: 2
		float            EventDropMult();
		void             Pause(float a_seconds);  // Nuke: no undead rise for a while
		float            Paused();
		bool             Fighting();          // a night is being fought
		bool             BossAlive();
		void             CallHerald();        // the Herald quest: fog rolls in and Grimmvar rises
		void             CrumbleNear(const RE::NiPoint3& a_at, float a_radius);  // a freed city: its undead fall
		void             NetFill(Net::State& a_state);  // host: the night as the party should see it
		// for the new dead (Undead.cpp): a telegraphed blow on the ground, a hit on the player, one more risen
		void             Hazard(const RE::NiPoint3& a_at, float a_delay, float a_radius, float a_share, std::string_view a_look, bool a_stagger);
		void             HurtPlayer(float a_share, bool a_stagger);
		void             RaiseAt(const RE::NiPoint3& a_at);
		void             DevSpawn(std::string_view a_row, int a_count);  // DevTools: raise that undead in front of you
		void             DevSpawnAt(std::string_view a_row, const RE::NiPoint3& a_at, float a_yawDeg);  // DevTools: ...at a point, facing yaw
		void             DevShowUndead(bool a_show);  // DevTools: hide/show every undead we raised (clean plates for art captures)
		void             EndNight();
		bool             BossPos(RE::NiPoint3& a_at);  // Grimmvar, if he walks
		bool             RaiseChampion(int a_town, std::string_view a_row, std::string_view a_name, float a_healthMult, const RE::NiPoint3& a_at);  // a city's named champion
		bool             ChampionActive(int a_town);  // walking, or slain this night
		bool             ChampionPos(RE::NiPoint3& a_at);          // a city freed: dawn comes now, the next night brings the next round
	}

	// The new dead (Undead.cpp): body language and attacks of our own on the draugr skeleton
	// Sound (Audio.cpp): one-shots with rate limits, keyed loops, undead voices, music
	namespace Audio
	{
		enum class VoiceEvent { kIdle, kAttack, kHurt, kDeath };
		void Ui(gen::Sound a_s, float a_vol = 1.0f);                                   // 2D (menus, your own casts)
		void At(gen::Sound a_s, RE::TESObjectREFR* a_ref, float a_vol = 1.0f);         // 3D, follows the reference
		void AtPoint(gen::Sound a_s, const RE::NiPoint3& a_at, float a_delay = 0.0f, float a_vol = 1.0f);
		void LoopStart(std::uint64_t a_key, gen::Sound a_s, RE::TESObjectREFR* a_owner = nullptr);
		void LoopStop(std::uint64_t a_key, int a_fadeMs = 200);
		bool Looping(std::uint64_t a_key);
		void Voice(RE::Actor* a_actor, VoiceEvent a_ev);
		void Flesh(RE::Actor* a_actor, bool a_death);  // the wet hit of a spell landing (rate-limited per body), the fall when it dies
		void InstallSteps();
		std::string Stats();
		RE::BGSSoundDescriptorForm* Descriptor(gen::Sound a_s);                            // our undead's footsteps, on their animation's footfalls
		void Tick(float a_delta);
		void StopAll();
		void BossMusic(bool a_on);
		void FreedMusic();
		void IntroMusic(bool a_on);  // the intro's music (our MUSC: the music slider applies, the hold's music waits)
		void SetMenuMusic();
		void MenuUiSounds(bool a_on);  // the main menu's own focus/accept/back sounds play ours while it is up
		inline constexpr std::uint64_t kLoopCharge = 1, kLoopRite = 2, kLoopNight = 3, kLoopMenu = 4;
	}

	namespace Undead
	{
		enum class Type { Shambler, Runner, Crawler, Wailer, Bloater, Stalker, Reaper, Necro, Wyrm };
		enum Cue { kCueNone = 0, kCueRoar, kCueScream, kCueGrab, kCueSwipeL, kCueSwipeR, kCueCrouch, kCueChannel };
		void  Assign(RE::Actor* a_actor, Type a_type);
		void  Forget(RE::Actor* a_actor);
		void  CueFor(RE::Actor* a_actor, int a_cue, float a_seconds);
		Type  Roll(int a_round);         // which of the risen dead this one is
		float SpeedMult(Type a_type);
		float HealthMult(Type a_type);
		void  Tick(RE::PlayerCharacter* a_player, float a_delta);
		void  Reset();
	}

	// Co-op, our own layer over Steam (Net.cpp): a friends-only lobby, the host's zombies game sent to the party
	namespace Net
	{
#pragma pack(push, 1)
		struct State  // host -> everyone, 4 times a second
		{
			std::uint8_t  type, version, phase;  // phase: 0 day, 1 dusk (countdown), 2 fighting
			std::int8_t   event;                 // gen::kEvents index, -1 none
			std::int32_t  round;
			float         countdown;
			float         bossHp;                // 0..1, -1 no boss
			std::uint8_t  bossEnraged, pad[3];
			float         pu[4];                 // power-up seconds left: insta-kill, double points, -, fire sale
			float         nuke;                  // seconds the dead still lie low
			std::int32_t  boxAv;                 // the Coffer's town and spins (save AV)
			std::int32_t  curse[10];             // the Dirge in every city
		};
#pragma pack(pop)
		struct FriendGame { std::uint64_t id, lobby; std::string name; };
		bool        Available();     // Steam is there
		void        PatchTogether(); // once at start: Skyrim Together's menu follows the party
		void        Tick(float a_delta);
		bool        InLobby();
		bool        IsHost();
		bool        IsClient();
		bool        Fresh();         // a client with recent word from the host
		const State& Last();
		std::string Status();
		std::string HostName();
		int         Members();       // others in the party
		void        HostLobby();     // open a party and the Steam invite dialog
		void        InviteFriends();
		void        JoinLobby(std::uint64_t a_lobby);
		void        LeaveLobby();
		std::vector<FriendGame> FriendsInParties();  // friends in Skyrim with a party open
		void        PowerUpTaken(int a_kind);
		std::vector<int> TakeRemotePowerUps();
	}

	// Co-op with Skyrim Together (UndyingNights.ini: Role=host|client|solo)
	namespace Coop
	{
		enum class Role { Solo, Host, Client };
		Role Get();
		bool Client();          // a friend's game: the host raises the dead
		bool TogetherLoaded();  // Skyrim Together runs the clock
	}

	// The Undying Dirge (sheet: curse): four night quests per city; a freed city is a sanctuary.
	namespace Curse
	{
		void  Tick(float a_delta);
		void  Reset();
		void  OnUndeadDeath(RE::Actor* a_actor);
		void  OnBossSlain();
		void  OnChampionSlain(int a_town);  // a city's champion fell (Director)
		bool  SafeHere(RE::PlayerCharacter* a_player);  // in a freed city: no undead rise here
		float SpawnBoost();                              // holding a shrine: the night pushes harder
		int   ZoneTown();                                // city the player is in (-1)
		int   RelicOf(int a_town);                       // the city's relic weapon (gun index, -1)
		void  ClaimRelic(int a_town);                    // learn a freed city's Thu'um (the shrine's menu)
		int   ClaimableShout(int a_town);                // the freed city's Thu'um not yet learned (-1)
		int   Raw(int a_town);                           // the city's progress value (step*1000 + progress)
		void  SetRaw(int a_town, int a_value);
	}

	namespace Intro
	{
		void OnMainMenu();  // the next new game moves its character again
		void Install();                 // data loaded: set MQQuickstart
		void Tick();
		bool CrewScreenOpen();
		bool OnButton(const RE::ButtonEvent* a_event);  // true = consumed
	}

	namespace Gun
	{
		void                  Tick(float a_delta);
		void                  Reset();
		bool                  OnButton(const RE::ButtonEvent* a_event);
		std::pair<bool, bool> RecentHit(RE::FormID a_actor);  // {gun kill, headshot}
		bool                  Give(int a_gun);                // buy/box: put in a slot, equip it
		bool                  KnowsShout(int a_shout);        // the cities' shouts (sheet: shouts)
		void                  LearnShout(int a_shout);
		int                   ShoutOfSpell(RE::FormID a_spell, int& a_level);
		void                  Shout(int a_shout, int a_level);
		bool                  Owns(int a_gun);
		void                  RefillAmmo(int a_gun);
		bool                  Reforge();      // Atronach Reforge on the weapon in hand
		int                   ReforgeCost();  // -1 no weapon, 0 maxed
		int                   SlotCount();
		float                 AdsBlend();                     // 0..1, for movement and HUD
		void                  FxBlast(std::string_view a_name, const RE::NiPoint3& a_at, float a_delay = 0.0f);  // a no-damage Skyrim explosion (look only)
	}

	namespace Shops
	{
		void Tick(float a_delta);
		void Reset();
		void OnActivate(RE::TESObjectREFR* a_ref);
		bool OnButton(const RE::ButtonEvent* a_event);  // the shrine menu's keys (true = used)
		bool MenuEats(RE::InputEvent* a_event);          // Skyrim's own handlers must not see this key (the shrine menu has it)
		std::string BoxTown();
		bool        StandFrame(int a_town, RE::NiPoint3& a_center, float& a_yaw, RE::TESWorldSpace*& a_world);  // true: the station is placed
		RE::TESObjectREFR* Placed(int a_town, int a_kind);  // the town's machine of that object kind (6 = relic shrine), if placed
		int         TownOfRef(RE::TESObjectREFR* a_ref);
		bool        StationTarget(int a_town, RE::NiPoint3& a_at);  // where to head for the town's station, in your current world (a walled city from outside: its gate)
		bool        Discovered(int a_town);
	}

	namespace Perks
	{
		void OnEffect(RE::FormID a_mgef);
		void OnZombieHit(RE::Actor* a_zombie);  // a round zombie hit the player
		void OnCast(int a_shots);  // Stormcrown
		void Tick();
		float ManaMult();          // magicka cost (tomes)
		float ChargeMult();        // Quicksilver: charge time and cooldown of signature moves (x)
		float FireDelayMult();
		float DamageMult();
		float HeadMult();
	}

	namespace Movement
	{
		void OnButton(const RE::ButtonEvent* a_event);  // crouch/jump keys (Skyrim ignores crouch while sprinting)
		void Tick(float a_delta);
		void Reset();
	}

	namespace Hud
	{
		struct State
		{
			bool        inGame{ false };
			int         round{ 0 };
			bool        nightActive{ false };
			float       puInsta{ 0 }, puDouble{ 0 }, puFireSale{ 0 };  // power-up seconds left  // a night (round) is being fought; otherwise round = nights survived
			int         points{ 0 };
			bool        gunOut{ false };
			std::string gunName;
			int         gunLevel{ 0 };
			int         clip{ 0 };
			int         reserve{ 0 };
			bool        reloading{ false };
			int         gunKind{ 0 };     // 0 wand (infinite), 1 tome, 2 grimoire (magicka)
			float       manaCost{ 0 };    // per shot
			std::string gunElement;       // wand ability / element line under the name
			std::uint32_t gunColor{ 0 };  // element colour RRGGBB
			float       charge{ 0 };      // 0..1 while holding fire
			float       chargeCd{ 0 };    // 1..0 cooldown of the signature move
			std::string chargeName;
			float       ads{ 0 };
			std::string banner;
			float       bannerTime{ 0 };
			int         lastGain{ 0 };
			float       gainTime{ 0 };
			float       hitTime{ 0 };
			bool        hitKill{ false };
			int         perks{ 0 };      // bitmask
			int         crew{ -1 };
			bool        crewScreen{ false };
			int         crewHighlight{ 0 };
			float       countdown{ 0 };  // seconds until the next night starts (0 = none)
			float       hp{ 0 }, hpMax{ 0 }, mp{ 0 }, mpMax{ 0 }, sp{ 0 }, spMax{ 0 };
			float       hurtTime{ 0 };   // red vignette after taking a hit
			bool        perkMenu{ false };
			std::string bossName;  // empty = no boss
			float       bossHp{ 0 };
			bool        bossEnraged{ false };
			int         perkSel{ 0 };
			// the shrine: its roll (0 idle, 1 charging, 2 spinning, 3 offering), progress 0..1, the spell shown, the price; a Thu'um to learn there
			int         rollPhase{ 0 }, rollGun{ -1 }, rollCost{ 0 }, shrineShout{ -1 };
			float       rollT{ 0 };
			int         nightEvent{ -1 };   // gen::kEvents index while it runs
			std::string questCity, questTitle, questGoal;  // the city's current curse quest (empty = none)
			float       questFrac{ 0 };
			int         questStep{ 0 };
			std::string sanctuary;         // in a freed city at night: its name
			std::string loreTitle, loreText;
			float       loreTime{ 0 }, loreTotal{ 0 };
			float       wardFrac{ 0 };     // kindling a ward-fire
			float       nukeLeft{ 0 }, nukeFlash{ 0 };
			std::string questNote;        // quest news in the tracker
			// the quest's objective marker: where on screen (0..1), in front of you or not, how far (units), what
			// floating health bars over the wounded undead near you (screen x, y 0..1, health 0..1, a name for the great ones)
			struct EnemyBar { float x, y, frac, scale; std::string name; std::uint32_t color; std::uint32_t id{ 0 }; int rank{ 0 }; };  // rank 0 risen, 1 elite / great, 2 champion, 3 another foe
			std::vector<EnemyBar> enemyBars;
			// the ritual circle (Circle / Hold rites): ring points on screen (x, y 0..1, depth), progress, reset flash
			bool        ringOn{ false }, ringHold{ false };
			std::vector<std::array<float, 3>> ringPts;
			float       ringFrac{ 0 }, ringFlash{ 0 };
			int         ringSeconds{ 0 }, ringInside{ 0 };
			bool        objOn{ false }, objFront{ false };
			float       objX{ 0 }, objY{ 0 }, objDist{ 0 };
			std::string objLabel;
			std::uint32_t objColor{ 0xD6B260 };
			float       questNoteTime{ 0 };
			// weapon slots (2, or 3 with the brew): the weapon in each (gen::kGuns index, -1 empty), its Reforge level
			int                slotCount{ 2 };
			std::array<int, 3> slotGun{ -1, -1, -1 };
			std::array<int, 3> slotLevel{ 0, 0, 0 };
			int                activeSlot{ -1 };
			int                gunIndex{ -1 };
		};
		State&      Edit();
		std::mutex& Lock();
		void        Banner(std::string a_text, float a_seconds = 4.0f);
		void        Lore(std::string a_title, std::string a_text, float a_seconds = 14.0f);  // a scroll of the Dirge
		void        Install();
		void        OnClick();  // a left click (menus: the party panel's buttons)
		void        IntroStart();             // the Undying Nights intro film (once, for a new character)
		bool        IntroActive();
		void        IntroSkipHeld(bool a_held);
	}

	namespace DevTools
	{
		void PollFromRender();
		void CaptureIfRequested(ID3D11DeviceContext* a_context, IDXGISwapChain* a_swap);
		void RunQueuedOnMain();
		void RunQueuedInGame();
		bool HudOff();  // DevTools 'hud 0': our overlay draws nothing (art captures)
	}

	namespace Events
	{
		void InstallMenuWatch();
	}
}
