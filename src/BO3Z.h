#pragma once
// BO3 Zombies x Skyrim. Every number comes from Generated.h (built from sheets/*.json).
#include "Generated.h"

namespace bo3z
{
	inline constexpr auto kVersion = "0.2.0";

	// Forms read from the player's own Skyrim and from CurseOfAlduin.esp.
	namespace Forms
	{
		// (Noah: "the magic tied to the bow and the sword keeps firing until I'm out of mana") a spell cast once from a
		// ref's instant caster - a concentration spell (Flames, a torrent) is let go of a moment later instead of being held
		void                 CastOnce(RE::TESObjectREFR* a_from, RE::SpellItem* a_spell, RE::TESObjectREFR* a_target, float a_strength = 1.0f, RE::Actor* a_blame = nullptr);
		bool                 Resolve();  // after data loaded
		RE::TESBoundObject*  Enemy(std::size_t a_index);
		RE::TESQuest*        IntroQuest();
		std::array<RE::TESQuest*, 3> MQ102();  // MQ102A (Hadvar), MQ102B (Ralof), MQ102
		bool                 IntroDone();
		std::string          IntroDebug();
		RE::TESGlobal*       Quickstart();
		RE::TESObjectREFR*   RiverwoodMarker();
		RE::AlchemyItem*     PerkPotion(int a_perk);
		int                  PerkIndexOfEffect(RE::FormID a_mgef);    // -1
		RE::TESObjectACTI*   Object(int a_object);
		int                  ObjectIndexOf(const RE::TESForm* a_base); // -1
		RE::TESBoundObject*  Ours(std::uint32_t a_localId);              // any record in our plugin
		void                 Give(RE::Actor* a_to, RE::TESBoundObject* a_obj, int a_count);  // into the pack as a pickup is (the HUD's item feed shows it)
		RE::MagicItem*       Spell(int a_spell);                         // sheet: spells (a WEAP row = its enchantment)
		void                 Play(gen::Sound a_sound);
		void                 PlayAt(gen::Sound a_sound, RE::TESObjectREFR* a_at);  // 3D, follows the reference
		inline gen::Sound    Brew(int a_perk) { return static_cast<gen::Sound>(static_cast<int>(gen::Sound::brew_trollshide) + a_perk % 9); }  // (nine jingles: the later perks share them)
	}

	// Plugins.txt: make sure CurseOfAlduin.esp is enabled (called from DllMain, before the game reads it).
	namespace Display
	{
		void FitScreen(bool a_memory);   // (a_memory: SKSE's loaded - the game's settings in memory too)   // SkyrimPrefs.ini's size to the screen's own resolution, before the window's made
	}
	namespace PluginList
	{
		void Ensure();
	}

	// State kept on the player (sheet: persistence).
	namespace Save
	{
		int  Get(int a_av);
		void Set(int a_av, int a_value);
		int  Flags();
		void SetFlags(int a_flags);
		bool Flag(int a_bit);
		void SetFlag(int a_bit, bool a_on);
		bool HasPerk(int a_perk);
		void SetPerk(int a_perk, bool a_on);
		int  PerkCount();
		std::uint32_t PerkBits();
		bool PerkBit(int a_perk);  // the old flags' bit (the first nine perks)
		void Guard();           // every tick: puts the mod's values back if something wiped them (see Forms.cpp)
		void ForgetSnapshot();  // a load or the main menu: the next values are a different game's
		void OnWorldReady();
		float SinceWorldReady();  // seconds since the save came up (huge before the first)
		void InstallCoSave();  // SKSE serialization: the values live in the co-save
		int  PerkStacks(int a_perk);
		int  StaminaBonus();  // extra stamina already given to this character
		void SetStaminaBonus(int a_n);  // blessings stack: how many of this one
		void SetPerkStacks(int a_perk, int a_n);
		int  DragonBond();               // the glider's dragon: 0 none, 1-3 the Echo Altar's bonds (kept for the whole game)
		void SetDragonBond(int a_bond);
		inline constexpr int kFlagInit = 0, kFlagReserveV2 = 4, kFlagStarted = 5, kFlagMoved = 11, kPerkShift = 14, kFlagPerksV2 = 23, kOldPerkBits = 9;  // bits: init | (the old crew 1-3) | (old perks 4-10) | moved 11 | (the old weapon slot 12-13) | perks 14-22 | v2 23
	}

	namespace Points
	{
		void Add(int a_points);
		void Refund(int a_points);
		bool Spend(int a_points);
		int  Get();
	}

	namespace Net { struct State; }
	namespace Hud { struct State; }

	// (the audit: inventories changed with the inventory open crash Skyrim - and with Skyrim Souls the world goes on while
	// it is) any menu that shows items or spells, or the game paused: no adding, removing or equipping now
	inline bool ItemMenusOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) return true;
		for (const auto& n : { RE::InventoryMenu::MENU_NAME, RE::ContainerMenu::MENU_NAME, RE::BarterMenu::MENU_NAME, RE::GiftMenu::MENU_NAME,
				 RE::FavoritesMenu::MENU_NAME, RE::MagicMenu::MENU_NAME, RE::LoadingMenu::MENU_NAME }) {
			if (ui->IsMenuOpen(n)) return true;
		}
		return false;
	}

	// (Noah: "each weapon, spell, bow, crossbow - anything from the Echo Altar - should have the same base damage per
	// upgrade: 10, then 25 reforged, then 50, then 65, and so on") the one curve every altar item grows by: what it does
	// unforged, times this for the upgrades it has had (a spell's rungs and Ascends, a bow's or crossbow's reforges, a
	// summon family's rungs and Ascends, a spell-weapon's steel)
	// (the audit: 12000 here, 12,000 in the guides) a count as people write it
	inline std::string Num(long long a_v)
	{
		std::string s = std::to_string(a_v < 0 ? -a_v : a_v);
		for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
		return (a_v < 0 ? "-" : "") + s;
	}
	// (Noah: "the wonders should have more damage than max upgrade by 2x") the class wonders hit on the curve's 30th step -
	// about twice an item at its highest tier (13: 5.2x -> 10.3x); 31 once the three dragons fall
	inline constexpr int kWonderUps = 30;
	// (Noah: "each spell the Curse of Alduin uses should have the same base level damage and spell cost") one cost for every
	// spell at a tier: 25 magicka at the base, 9 more each step up, never past 150
	inline int TierSpellCost(int a_ups) { return std::min(150, 25 + 9 * std::max(0, a_ups)); }
	// (Noah: "all the spells' costs are very very high - the max for the strongest spells should be 150") a spell's magicka on
	// one gentle curve: cheap spells as they are, dearer ones drawn in, nothing above 150
	inline int CapSpellCost(float a_cost)
	{
		if (a_cost <= 50.0f) return std::max(1, static_cast<int>(std::lround(a_cost)));
		return static_cast<int>(std::lround(std::min(150.0f, 50.0f + (a_cost - 50.0f) * 0.19f)));
	}
	// (the crash of 2 Nov: a straggler moved under the ground - disabled for its move - was sent into combat: StartCombat on an
	// actor with no 3D / process reads null) combat started only on an actor that is really there
	inline void SafeCombat(RE::Actor* a_actor, RE::Actor* a_target)
	{
		if (!a_actor || !a_target || a_actor->IsDisabled() || a_target->IsDisabled() || !a_actor->Is3DLoaded() || a_actor->IsDead()) return;
		auto* proc = a_actor->GetActorRuntimeData().currentProcess;
		if (!proc || !proc->middleHigh || !a_actor->GetParentCell()) return;
		a_actor->StartCombat(a_target);
	}
	inline void SafeCombat(const RE::NiPointer<RE::Actor>& a_actor, RE::Actor* a_target) { SafeCombat(a_actor.get(), a_target); }
	inline void SafeCombat(RE::Actor* a_actor, const RE::NiPointer<RE::Actor>& a_target) { SafeCombat(a_actor, a_target.get()); }
	inline void SafeCombat(const RE::NiPointer<RE::Actor>& a_actor, const RE::NiPointer<RE::Actor>& a_target) { SafeCombat(a_actor.get(), a_target.get()); }
	inline float UpgradeMult(int a_upgrades)
	{
		// (Noah: "I was pretty much unstoppable by night three - even with just one upgrade on the spell I was obliterating
		// everything ... the upgrades need to be more balanced") a gentler curve: Tier I 1.3x, II 1.7x, III 2.2x, +0.3 an Ascend
		static constexpr float kCurve[4] = { 1.0f, 1.3f, 1.7f, 2.2f };
		return a_upgrades <= 0 ? 1.0f : a_upgrades <= 3 ? kCurve[a_upgrades] : 2.2f + 0.3f * static_cast<float>(a_upgrades - 3);
	}

	namespace Director
	{
		void LearnDance(bool a_announce);  // Dance of Death taught (Solitude freed, or the test kit)
		void QuietWind();                   // (data loaded) the ambient wind 12 dB down
		void ShoutLooks();                  // (data loaded) our shouts shown in the Magic menu as Skyrim's are
		void             Tick(float a_delta);
		bool             Scouting();
		bool             Rising(RE::Actor* a_actor);    // still climbing out of the ground (not to be fought yet)                     // (Noah) scout mode: flying, unhurt, no undead - arrows mark spawn points
		void             ScoutMark(const RE::NiPoint3& a_at);  // an arrow of the player's came to rest here
		void             ScoutKey(const RE::ButtonEvent* a_b);  // F10 scout mode on / off; while scouting B marks where you look, N puts the city's altar where you stand
		void             Reset();
		bool             IsRoundUndead(RE::Actor* a_actor);
		float            HarmMult(RE::Actor* a_actor);
		bool  HarpyNear(const RE::NiPoint3& a_at, float a_r);  // a living harpy this close (the per-hit cap)
		std::vector<RE::ActorHandle> AliveHandles();
		bool             PlayerSide(RE::Actor* a, RE::PlayerCharacter* a_player);  // the player, a friend, a follower, an Echo ally or a summon of yours
		void             RestartWave();   // (the BO3 fall, solo) the wave's dead gone, the wave begins again
		void             SlowTime(float a_realSeconds, float a_mult);  // the world slowed for a beat (solo only)
		void             NormalTime();
		float            TakenMult(RE::Actor* a_actor);  // how much harder one of ours is hurt (a dazed Barrow Champion 2, a berserk Skeleton Mutant 1.25)
		void             OnDying(RE::Actor* a_actor);    // any death: a champion or mini-boss pays out at once  // how hard this one of the night's dead hits you, its spells too (1 = as made)
		void             HeadlongCombat();  // data loaded: the undead charge straight in
		std::vector<RE::FormID> Sheltered();                       // townsfolk sent indoors for the night (co-save 'SHLT')
		void             Unshelter(const std::vector<RE::FormID>& a_ids);  // a save loaded: everyone back out
		int              EnemyRow(RE::Actor* a_actor);  // sheet row of one of ours, -1 otherwise
		bool             IsSpared(RE::Actor* a_actor);
		bool             IsFoe(RE::Actor* a_actor);  // the night's undead, or anything of Skyrim's out to hurt you (bandits, wolves, trolls...) - never townsfolk, followers or friends
		void             Frenzy(RE::Actor* a_actor, float a_seconds);  // Mindbreaker: turns on its own kind  // night truce: townsfolk and followers can't be hurt
		bool             NightNow();  // night hours (the undead are out)
		float            RoundHealth();  // a zombie's full health this round (Skyrim units)
		float            KillEssenceScale();  // (the essence economy) a kill's essence after night 10: tapering as the dead multiply
		float            PriceScale();        // the repeat buys' prices: 3% more a night after night 10
		RE::TESFaction*  UndeadFactionForm();  // the Curse's own faction (the night's dead)
		bool             IsElite(RE::Actor* a_actor);
		bool             IsBoss(RE::Actor* a_actor);
		bool             IsGreat(RE::Actor* a_actor);  // the boss, the champion of the moment, or any dragon
		struct RestView
		{
			bool  open = false, night = false, canSkip = false, armed = false, waiting = false, stopsAtDusk = false;
			int   hours = 1, nightNo = 1;
			float now = 12, target = 12, dusk = 20, dawn = 5.6f, rise = 20.4f;
		};
		void             OpenRest();                       // the Curse's rest screen (in place of Skyrim's wait menu)
		bool             RestOpen();
		bool             WaitKey(RE::InputEvent* a_event);  // the Wait key: the rest screen (true: taken)
		bool             RestEats(RE::InputEvent* a_event);
		bool             BedEats(RE::InputEvent* a_event);  // Activate on a bed: ours (you lie down in it, the rest screen, you get up)
		bool             RestButton(const RE::ButtonEvent* a_b);  // its keys (true: taken)
		RestView         RestNow();
		void             QueueZombieHit(RE::Actor* a_zombie, float a_seconds);  // its blow lands when its swing does
		void             Noise(float a_amount);  // the night hears you (spells bursting, shouts, fights): enough draws a Volatile
		bool             DanceOfDeath(RE::FormID a_spell);  // (Noah) the Dance Of Death spoken: the dead near you dance (true: it was that shout)
		std::string_view HeadNodeFor(RE::Actor* a_actor);
		void             LogState();
		void             Web(const RE::NiPoint3& a_at, float a_radius, float a_seconds, float a_amount = 60.0f, bool a_frostFx = false);   // Widow's Wine / wand slows
		void             Shock(const RE::NiPoint3& a_at, float a_radius, float a_bo3Damage);  // Electric Cherry
		// event nights (sheet: events) and the nuke
		int              EventNow();          // -1 none, else gen::kEvents index (this night)
		float            EventPointsMult();   // an event night: 2; a themed wave: its own (Blood Fog 1.5, Champions 2)
		int              ThemeNow();          // the theme being fought (event night or themed wave), -1 none
		void             DevTheme(int a_theme, bool a_night);  // dev: the next wave themed (or tonight an event night)
		void             DevMiniBoss();
		void             DevFly(bool a_on);   // the scout's flight alone, for placing things (dev: fly on|off)
		float            EventDropMult();
		void             Pause(float a_seconds);  // Nuke: no undead rise for a while
		float            Paused();
		bool             Fighting();          // a night is being fought
		bool             BossAlive();
		void             CallHerald();        // the Herald quest: fog rolls in and Grimmvar rises
		void             CrumbleNear(const RE::NiPoint3& a_at, float a_radius);  // a freed city: its undead fall
		void             LiftWave(const RE::NiPoint3& a_from);
		void             UndeadNear(const RE::NiPoint3& a_from, float a_range, std::vector<std::array<float, 4>>& a_out);  // (dx, dy, dist, 1 boss/champion)  // the curse lifted: a wave of light from the shrine strikes the dead down
		void             NetFill(Net::State& a_state);  // host: the night as the party should see it
		// for the new dead (Undead.cpp): a telegraphed blow on the ground, a hit on the player, one more risen
		void             Hazard(const RE::NiPoint3& a_at, float a_delay, float a_radius, float a_share, std::string_view a_look, bool a_stagger);
		void             HurtPlayer(float a_share, bool a_stagger);
		void             RaiseAt(const RE::NiPoint3& a_at);
		RE::ActorHandle  RaiseTrial(const RE::NiPoint3& a_at, bool a_elite);  // a trial's wave: one more of tonight's dead (or an elite)
		void             DevSpawn(std::string_view a_row, int a_count);  // DevTools: raise that undead in front of you
		void             DevSpawnAt(std::string_view a_row, const RE::NiPoint3& a_at, float a_yawDeg);  // DevTools: ...at a point, facing yaw
		void             DevShowUndead(bool a_show);  // DevTools: hide/show every undead we raised (clean plates for art captures)
		void             EndNight();
		void             SkipToDawn(const char* a_banner, bool a_lapse = true);  // first light: a sunrise time-lapse (a_lapse false: the clock jumps)  // the clock jumps to first light (the night counts as survived)
		bool             MayRest(std::string& a_why);  // waiting / sleeping: not at night, and only the host's clock moves
		bool             BossPos(RE::NiPoint3& a_at);  // Grimmvar, if he walks
		bool             RaiseChampion(int a_town, std::string_view a_row, std::string_view a_name, float a_healthMult, const RE::NiPoint3& a_at);  // a city's named champion
		bool             ChampionActive(int a_town);  // walking, or slain this night
		bool             PriestHold();                // the night's last wave is down and its Dragon Priest is still to come (the night waits for it)
		bool             ChampionPos(RE::NiPoint3& a_at);          // a city freed: dawn comes now, the next night brings the next round
		bool             ChampionAlive();  // a champion (a city's, a dragon's) is up and alive
	}

	// The new dead (Undead.cpp): body language and attacks of our own on the draugr skeleton
	// Sound (Audio.cpp): one-shots with rate limits, keyed loops, undead voices, music
	namespace Audio
	{
		enum class VoiceEvent { kIdle, kAttack, kHurt, kDeath };
		void Ui(gen::Sound a_s, float a_vol = 1.0f);                                   // 2D (menus, your own casts)
		void Voice(std::optional<gen::Sound> a_s);                                     // the intro narrator (one line at a time; nullopt stops it)
		void At(gen::Sound a_s, RE::TESObjectREFR* a_ref, float a_vol = 1.0f);         // 3D, follows the reference
		void AtPoint(gen::Sound a_s, const RE::NiPoint3& a_at, float a_delay = 0.0f, float a_vol = 1.0f);
		void Mine(gen::Sound a_s, const RE::NiPoint3& a_at, float a_vol = 1.0f);         // your own sound at a point (never held back by the horde's budget)
		void LoopStart(std::uint64_t a_key, gen::Sound a_s, RE::TESObjectREFR* a_owner = nullptr);
		void LoopStop(std::uint64_t a_key, int a_fadeMs = 200);
		void LoopVolume(std::uint64_t a_key, float a_vol);  // a playing loop's loudness (0..1)
		bool Looping(std::uint64_t a_key);
		void LoopAt(std::uint64_t a_key, gen::Sound a_s, const RE::NiPoint3& a_at, float a_vol = 1.0f);  // a moving loop (a bolt's flight)
		void LoopMove(std::uint64_t a_key, const RE::NiPoint3& a_at);
		void Voice(RE::Actor* a_actor, VoiceEvent a_ev);
		void Flesh(RE::Actor* a_actor, bool a_death);
		void Game(RE::FormID a_sndr, RE::TESObjectREFR* a_at, float a_vol = 1.0f);  // one of Skyrim's own sounds (SNDR id)  // the wet hit of a spell landing (rate-limited per body), the fall when it dies
		void InstallSteps();
		std::string Stats();
		RE::BGSSoundDescriptorForm* Descriptor(gen::Sound a_s);                            // our undead's footsteps, on their animation's footfalls
		void Duck(float a_seconds);  // the music 9 dB down for a moment (a wave, a boss, your fall)
		void Tick(float a_delta);
		void StopAll();
		void BossMusic(bool a_on);
		void FreedMusic();
		void NightMusic(bool a_on);  // the night's song, dusk to dawn
		void IntroMusic(bool a_on);  // the intro's music (our MUSC: the music slider applies, the hold's music waits)
		void FilmQuiet(bool a_on);   // Skyrim's own sound (world, voices, its music) silent while the intro film plays
		void SetMenuMusic();
		void MenuUiSounds(bool a_on);  // the main menu's own focus/accept/back sounds play ours while it is up
		inline constexpr std::uint64_t kLoopCharge = 1, kLoopRite = 2, kLoopNight = 3, kLoopMenu = 4, kLoopShrine = 5, kLoopSpell = 6;  // (kLoopSpell: Spellcraft's own hum - the gun pose stopped kLoopCharge every frame under it)
	}

	namespace Undead
	{
		void InstallHooks();  // the animation graph's update, hooked where it begins (once, at the main menu)
		enum class Type { Shambler, Runner, Crawler, Wailer, Bloater, Stalker, Reaper, Necro, Wyrm };
		enum Cue { kCueNone = 0, kCueRoar, kCueScream, kCueGrab, kCueSwipeL, kCueSwipeR, kCueCrouch, kCueChannel, kCueStandUp, kCueCast, kCueLeap };
		bool  HasClips(RE::Actor* a_actor);    // moves by the Mixamo clips (Data/SKSE/Plugins/CurseOfAlduin/anims)
		float StandUpSeconds();                 // the stand-up clip's length as it plays
		void  OnHit(RE::Actor* a_actor);       // a hit landed on it: its flinch (now and then)
		void  SetVolatile(RE::Actor* a_actor); // the Volatile: its own fast claws, and it pounces
		void  Assign(RE::Actor* a_actor, Type a_type);
		void  Forget(RE::Actor* a_actor);
		void  CueFor(RE::Actor* a_actor, int a_cue, float a_seconds);
		void  SetEyes(RE::Actor* a_actor, std::string_view a_id);  // its glowing eyes (meshes/CurseOfAlduin/eyes/<id>_glow.nif), and its row's own trick (the Risen Dead turn, the Bride leaps, the Volatile is armoured)
		bool  IsBrute(RE::Actor* a_actor);    // the Volatile's armoured hide
		bool  Dance(RE::Actor* a_actor, float a_seconds);  // (the Dance Of Death) it dances, mind stopped, for a_seconds
		bool  IsDancing(RE::Actor* a_actor);  // (its blows land half again as hard)
		Type  Roll(int a_round);         // which of the risen dead this one is
		float SpeedMult(Type a_type);
		float HitDelay(RE::Actor* a_actor);   // seconds until its swing's claws meet you (0: none coming)
		void  FreshStep(RE::Actor* a_actor);  // its first steps (out of a portal) walked before it takes its own gait
		void  SetPace(RE::Actor* a_actor, float a_scale);  // its round's pace (1 = round one): walker, runner or sprinter, its speed held at its gait's clip
		float HealthMult(Type a_type);
		float DamageMult(Type a_type);
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
			float         nuke;                  // seconds the dead still lie low
			std::int32_t  boxAv;                 // the Coffer's town and spins (save AV)
			std::int32_t  curse[10];             // the Curse in every city
			std::uint32_t weather;               // the host's sky (form id): everyone sees the same rain
			std::uint32_t weatherLocal;          // the same, by its plugin (load orders differ) ...
			char          weatherPlugin[48];
			std::uint8_t  weatherClass;          // ... and its kind (pleasant / cloudy / rain / snow) if that plugin isn't here
			float         daysPassed;            // the host's clock (GameDaysPassed): it moves everyone's
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
		bool        OverlayBusy();   // the Steam overlay is up (or just closed): clicks are its own
		void        JoinLobby(std::uint64_t a_lobby);
		void        LeaveLobby();
		std::vector<FriendGame> FriendsInParties();  // friends in Skyrim with a party open
		void        TellClass(int a_cls);   // (the altar rework) your class, for the party's auras
		void        TellWord(int a_w);      // (host) a word silenced: the party's games silence it too
		void        EggDone(int a_town);  // a hidden quest finished: the party near that altar shares it
		void        PriestSlain(int a_town);  // (host) a city's Dragon Priest fell: every game in the party receives his weapon
	}

	// The Coffer's spells: 150 spells from the spell mods, handed out by the shrine (Arcana.cpp, src/CofferSpells.h)
	// The four classes (the classes overhaul): 0 Summoner, 1 Destruction, 2 Ranger, 3 Spellsword (coffer::Class)
	// The Curse follows the main story (src/Story.cpp)
	namespace Story
	{
		int  Stage();             // 0 Helgen .. 7 Alduin fallen in Sovngarde
		void Reset();             // a load / a new game
		bool SceneHold();         // a set piece of the main quest is playing out here: no night begins
		bool TrialsOpen(int a_town, std::string* a_why = nullptr);
		std::string_view CityQuestName(int a_town);  // the quest that wakes a city's Echo Altar (empty: Riverwood)
		bool BarrowJarlsOpen();   // after Bleak Falls Barrow
		bool ElitesOpen();        // after Dragon Rising
		bool AlduinFallen();      // after Sovngarde
		bool GrimmvarOpen();      // after Alduin's Bane: the Undying Wyrm can be hunted
	}

	namespace Classes
	{
		void TickItemCard();   // (Noah: the swords' item cards "way too long" - each tier's card tells only what it is and what that tier does; the charge bar hidden)
		int              Get();            // -1 none chosen yet
		RE::TESObjectARMO* Underwear();    // (Noah: no bare body) Underwear.esp's bra and underwear
		void             DressUnder(bool a_anySex = false);  // nothing on the body slot: the underwear goes on (women; anyone with a_anySex)
		bool             Open();           // the class picker is up
		int              Selected();
		bool             Switching();      // the picker was opened at an altar (a switch, for septims)
		std::string_view Name(int a_cls);
		int              SwitchCost();
		void             OpenPicker(bool a_switch);
		void             Choose(int a_cls);
		void             Hover(int a_cls);
		void             OnNewCharacter();
		void             OnWorldReady();
		bool             OnButton(const RE::ButtonEvent* a_b);
		bool             MenuEats(RE::InputEvent* a_event);
		std::vector<std::uint8_t> State();  // co-save 'CLAS'
		void             SetState(const std::vector<std::uint8_t>& a_v);
		void             Revert();
		void             Tick();                   // the Summoner's summon cap, once a second
		// (Noah: the altar's rows keep their roll) what a class row won, given when it is taken off the altar after the roll
		// a class row's win: its name, how it is given, and what the altar's reel turns through before landing on it (reel[at])
		void             OnKill(bool a_bySummon, RE::Actor* a_dying, RE::Actor* a_killer = nullptr);  // you (or your summon) killed a foe: what you hold trains, the wonder trial counts
		void             NoteHit(RE::Actor* a_target, RE::FormID a_source, bool a_power);  // your blow (what killed it; the wonders' on-hit)
		bool             OnCast(RE::FormID a_spell);   // the Tome of the Six Fires
		void             NoteCast(RE::FormID a_spell); // a summon spell: called up here if Skyrim's own didn't come
		bool             IsOwnSummon(RE::Actor* a_actor);
		int              SummonFamily(RE::Actor* a_actor);  // the Summoner spell (kSpells index) that called it up (-1)
		float            BladeShare(const RE::TESObjectWEAP* a_w);  // one light blow of a blade: its share of one of the night's dead, at its tiers (before the night's power)
		int              AltarGearUpgrades(const RE::TESForm* a_w);  // an altar spell-weapon's steel as upgrades (0..3), -1 when it isn't altar gear
		int              BladeSpell();  // the Spellsword spell your blade carries (in a hand, or the last held with a two-handed blade; -1)
		void             RiftHit(RE::Actor* a_target);     // (the Summoner's Riftblade) every fifth strike: a Flame Atronach for 10 s
		bool             IsRiftblade(RE::FormID a_source);
		bool             IsRiftSummon(RE::Actor* a_actor);  // one of the Riftblade's atronachs
		// (the altar rework: Kit.cpp) the pieces a class's kit is made of
		void             GiveGear(RE::TESForm* a_form, int a_count, bool a_equip);
		void             LearnSpell(RE::SpellItem* a_sp);  // taught and kept with your class (it goes with it when you switch)
		void             QueueThrall(RE::Actor* a_struck, int a_level);  // should it fall now, it rises as your thrall
		bool             KitGiven(int a_cls);         // that class's starting kit has been handed over
		RE::TESObjectWEAP* Riftblade();
		RE::TESBoundObject* FlaskItem(int a_el);  // the Alchemist's flasks, items in the pack (0 fire 1 frost 2 shock 3 acid 4 holy water 5 bloodbloom 6 the Philosopher's Flask)
		void               GiveFlask(int a_el);   // put in your pack
		bool               ArcaneBladeHit(RE::Actor* a_target, RE::SpellItem* a_spell);   // the Spellsword's arcane rung on a blade hit (false: not one)
		bool               Gilded(RE::Actor* a_a);    // turned to gold by the Philosopher's Flask (held fast, every blow twice as hard)
		bool               FlaskInHand(bool a_left);  // a flask held, drawn, in that hand
		int                FlaskElementInHand(bool a_left);  // which flask that hand holds (drawn or not): 0-6, -1 none
		void               FlaskAttack(bool a_left);  // its throw
		void               FlaskAttackUp(bool a_left);  // the attack let go (the Mutagen Gauntlet's beam stops)
		void               DevWyrm(std::string_view a_what, std::string_view a_plugin, RE::FormID a_id);   // (dev: wyrm base|play|call)
		void               DevGauntletHold(bool a_on, bool a_left);  // (dev: gbeam) the Gauntlet's attack held down / let go
		// (Noah: "make the mutagen staff just like a quarter staff ... holding block should fire the beam and heavy attack should
		// throw flasks cycling through each flask") Animated Armoury's Dwarven Quarterstaff's look and swings; block held: a 2 s
		// piercing beam, the next vial's element each; a power attack throws the next flask
		void               DevClawRot(float a_x, float a_y, float a_z, bool a_on, bool a_third);
		void               SlayerLook();   // (data loaded) the Dragon Slayer as Animated Armoury's Glass Katana
		void               DevClawForm(RE::FormID a_local, float a_x, float a_y, float a_z);  // one claw's own turn (its NewArmoury id)
		void               WandButton(bool a_down);   // the attack pressed / let go with the Carian Wand drawn
		bool               WandInHand();
		void               WandReady();   // (data loaded) the Carian Wand carries its cast at every tier   // (dev: clawrot) the claws' turn in first person
		// (Noah: the Codex redesign - "progress first") the class book's live page: your nights, cities, Essence, wonder and
		// kit, read once a second on the game's thread (Tick) and drawn by the Codex
		struct CodexKit { std::string name, info; int tier{ 0 }, asc{ 0 }, cost{ -1 }; bool can{ false }, carried{ true }; std::uint32_t color{ 0xC8B48C }; };
		struct CodexSnap { int cls{ -1 }, nights{ 0 }, freed{ 0 }, points{ 0 }; std::string wonder; std::vector<CodexKit> kit; };
		CodexSnap          Codex();
		void               MutagenLook();                        // (data loaded: the look taken)
		bool               MutagenInHand();                      // the Mutagen Staff drawn in your hands
		void               MutagenBlock(bool a_down);            // the block button pressed / let go with it drawn
		void               MutagenPower();                       // a power attack swung with it
		void               MaceSwing(bool a_power);  // a swing of a class sword (any swing: hit or miss) - its tier's arc, burst and Tier III
		void               SlayerSwing(bool a_power);  // the Dragon Slayer's swing: a charged one spends its dragon embers (Dragonbreaker)
		void               SlayerKill();              // a kill by the Dragon Slayer: an ember
		int                SlayerEmbers();
		RE::TESObjectWEAP* CarianBlade();             // Destruction's Carian Wand
		RE::TESObjectWEAP* ClassWeapon(int a_cls);    // a class's weapon (0 Summoner .. 3 Spellsword)
		int                SwordClass(const RE::TESForm* a_w);   // whose class sword (0 Summoner, 1 Destruction, 2 Alchemist, 3 Spellsword; -1 none)
		bool                 AdoptLook(RE::TESObjectWEAP* a_w, const RE::TESObjectWEAP* a_src);   // an Animated Armoury weapon's look and feel on ours
		void                 NameSetPieces();   // the class sets' pieces named for their class ("Alchemist Set Hood")
		bool                 AdoptTierLook(RE::TESObjectWEAP* a_w, int a_tier);   // a class weapon's material for its tier
		void                 WhipEquipped(RE::Actor* a_a, bool a_left);   // a whip's chain told it's in hand
		RE::EnchantmentItem* BladeLook(const RE::TESObjectWEAP* a_w, int a_tier);   // a sword's look at a tier (nullptr: bare)
		void               TickShadowflame(float a_delta);
		bool               FlaskSwingBlocked(bool a_left);  // a flask in that hand whose next throw isn't ready (its swing held back)
		RE::TESObjectWEAP* AlchemistKnife();
		std::string        ArmourSetName(int a_cls, int a_tier);   // (the class armour sets) its name at that tier
		bool               ArmourCarried(int a_cls, int a_tier);   // a piece of it in your pack
		void               GiveArmourSet(int a_cls, int a_tier, bool a_wear);  // handed over (the Battle-mage's earlier look taken back)
		void               TickArmour();                           // its pieces' armour, light and weightless; the Ranger's stride
		RE::TESObjectWEAP* SpellswordBlade();         // the Spellsword's one-handed sword
		int              TwoHanderCount();            // the Spellsword's relic two-handers
		RE::TESObjectWEAP* TwoHander(int a_i);
		void             TickFast(float a_delta);      // the other spells' costs on the one curve
		bool             WonderEarned();               // any class's wonder won
		int              WonderState(int a_cls);       // 0 shut, 1-3 its secret under way, 4 taken up, 5 won and waiting in the Reliquary
		std::string_view WonderName(int a_cls);
		int              WonderMask();                 // bit per class: its wonder won (waiting in the Reliquary or taken up)
		void             DevWonder(int a_cls, int a_st);  // dev: set a wonder's state (5 = won now, with the call)
		void             KitWonders();                    // the test kit: the Staff of Magnus and the Philosopher's Flask
		void             DevGiveWonder(int a_cls);        // dev (trailer filming): the class's wonder straight into your hands, no Reliquary
		bool             TakeWonder(int a_cls);        // (the Reliquary's pedestal) a waiting wonder taken up - or a lost one given again
		bool             WonderOpen();                 // your class's secret quest is open (six cities freed) and the wonder not yet won
		void             EarnWonder(int a_cls);        // (Eggs) the quest done: the wonder
		std::string      WonderLine();                 // the altar's line: your class wonder's trial
		std::vector<std::string> Progress(int& a_cls);  // the class guide's live page (paragraphs, <b> kept) and the class it is for
		void             NextOutfit();                 // the altar's outfit picker: the next outfit (any class), worn
		std::string      OutfitName();                 // the one worn from the picker ('' none yet)
		int              Mastery(int a_index);     // kills its ladder has earned toward the next rung
		int              MasteryNeed(int a_index);
		void             SpendMastery(int a_index);
	}

	namespace Arcana
	{
		inline constexpr int kIdBase = 1000;   // HUD ids: a Coffer spell shows as kIdBase + its index (weapons keep 0..)
		void               Load();                       // data loaded: find them, set their costs and charge
		int                Count();
		int                SchoolOf(int a_index);        // 0 destruction, 1 conjuration, 2 restoration, 3 alteration, 4 illusion
		float              Power();                      // the spells', bows' and shouts' power for the night (0.5 .. 1.5)
		void               RefreshPower();               // a new night (or a save loaded): the power follows
		std::vector<int>   Carried();                    // the altar's spells you carry (in hand first) - two at most
		std::vector<int>   Carried(int a_cls);           // the same, counting only that class's (-1: every one)
		std::size_t        CarryCap();                   // spells you may carry: 2, the Summoner 3
		bool               Rollable(int a_index);           // the altar still rolls it (the Spellsword: touches only)
		void               Forget(int a_index);          // gone for good: unequipped, out of the Magic menu, reforging lost
		void               Replace(int a_old, int a_new);  // the new spell in the old one's place (and hands)
		RE::SpellItem*     Spell(int a_index);
		int                IndexOf(const RE::TESForm* a_form);
		int                IndexOfEffect(RE::FormID a_effect);  // the Coffer spell a magic effect belongs to (-1: none)
		bool               Damages(int a_index);         // one of its own effects takes health
		bool               Known(int a_index);
		int                Next(int a_after);            // the next in the pool (the roll flickers through them)
		void               Give(int a_index, bool a_equip);
		void               GiveStarter();
		std::string_view   Name(int a_index);
		int                ElementOf(int a_index);
		int                CostOf(int a_index);
		int                TierOf(int a_index);          // 0 common .. 3 the strongest (3 = a ladder's last rung: Legendary)
		int                ClassOf(int a_index);         // coffer::Class
		int                RungOf(int a_index);          // 1 = what the altar rolls
		int                Rungs(int a_index);           // how many rungs its ladder has
		int                Ups(int a_index);             // (the altar rework) its tier on the one curve: its rung's, plus its steps on it
		int                TierOfRung(int a_index);      // its rung's tier alone (4 rungs 0..3, 3 rungs 0 2 3, 2 rungs 0 3)
		int                NextRung(int a_index);        // the same ladder one rung up (-1: the top)
		std::string_view   LadderName(int a_index);
		bool               Concentration(int a_index);
		std::uint32_t      ElementColor(int a_element);
		std::string_view   ElementName(int a_element);
		int                Elements();
		std::array<int, 2> Hands();                      // the Coffer spell in each hand (0 left, 1 right; -1 none)
		int                Level(int a_index);           // reforged 0..gen::kForgeMax
		void               SetLevel(int a_index, int a_level);
		void               ReapplyAll();                 // every spell's power and cost set again (a party aura came or went)
		std::vector<std::uint8_t> Levels();               // for the co-save
		void               SetLevels(const std::vector<std::uint8_t>& a_levels);
	}

	// The Echo Altar's bows (sheet: bows; src/Bows.inl): one carried at a time, its shots the element's spell
	// The player's settings (CurseOfAlduin.ini [Settings]; the panel in the System menu)
	namespace Settings
	{
		inline constexpr float kFovMin = 60.0f, kFovMax = 130.0f;
		inline constexpr int   kKeys = 7;          // the shrine's keys: roll, reforge, perk, shout, leave, bond the dragon, call a bow
		float       FovFirst();
		float       FovThird();
		void        SetFov(bool a_first, float a_v);
		inline constexpr float kBrightMin = 0.5f, kBrightMax = 2.0f;
		float       Brightness();                 // the scene's brightness (1 = as Skyrim made it)
		void        SetBrightness(float a_v);
		inline constexpr float kAdsSensMin = 0.1f, kAdsSensMax = 2.0f;
		float       AdsSensitivity();             // (Noah) the look's speed aiming down sights, times Skyrim's own (1 = the same)
		void        SetAdsSensitivity(float a_v);
		bool        Shelter();                    // townsfolk go indoors at night
		bool        Blood();                      // headshot blood bursts (MCM: Effects)
		bool        Scout();                      // (Noah) scout mode, for laying out each city's spawn points (MCM: 6. Scout, or F10)
		void        SetScout(bool a_on);
		float       FogStrength();                // the night's fog, 0 .. 1.5
		bool        ScreenShake();
		bool        ReduceFlashes();              // softer lightning, fewer white flashes
		bool        PlayStationIcons();           // controller prompts drawn as PlayStation buttons (a PS5 pad looks like an Xbox one to Skyrim)
		bool        DragonBankReverse();          // the dragon leans out of its turns instead of into them (Noah's choice)
		bool        DragonInvertDive();           // forward pulls up, back dives
		float       CorpseSeconds();
		float MeleeSpeed();     // melee weapons' swing speed (1.0 - 2.0)
		float DrawSpeed();      // bows' draw and crossbows' reload speed (1.0 - 2.0)
		int         Difficulty();                 // Skyrim's difficulty, 0 Novice .. 5 Legendary (the trials follow it)
		bool        FollowDifficulty();           // the Mod Configuration menu's 'Follow Skyrim's difficulty'
		float       UndeadHealthMult();           // Skyrim's difficulty (if followed) x the MCM's own
		float       UndeadDamageMult();
		int         Key(int a_action);
		std::string KeyName(int a_action);
		void        Tick();                       // re-read the MCM's file when it changes
		void        ApplyMods();                  // the Other mods page into those mods' own files (game start)
	}

	// Co-op life and death: down, revive, spectate, and the party wipe (Party.cpp)
	namespace Compass
	{
		void Tick(float a_delta);  // game thread: the heading and the marks round you, for the HUD's compass
		void Heading();            // game thread, every frame: just the heading
		void OnCast(RE::FormID a_spell);  // a spell you cast: one of your equipped shout's words starts its cooldown
		void ClearShout();                // (Thu'um Unbound, Refill Stats) the shout ready again
	}

	// (Noah: everything balanced by Skyrim's own difficulty setting) what the mod scripts itself - bows, staffs, shouts,
	// Alduin's Bane, the altar's spells' cost, the night's blows - follows the game's difficulty the way Skyrim's own
	// damage does, gentler at the ends (src/Gun.cpp)
	namespace Difficulty
	{
		int   Level();  // 0 Novice .. 5 Legendary
		float Dealt();  // your damage: 1.4 .. 0.7
		float Taken();  // damage to you: 0.5 .. 2.0
		float Mana();   // magicka costs: 0.8 .. 1.25
		int   Freed();  // Echo Altars freed (checked every few seconds)
		float FoeToughness();  // Skyrim's own foes against your class's weapons: 1 .. 2.2, more with each altar freed
		void  TickFoeDamage();  // Skyrim's damage-to-you settings raised with your progress
	}

	namespace Party
	{
		void  ReviveAt(const RE::NiPoint3& a_at, float a_radius);  // (an upgraded staff's bolt) a fallen friend there gets up
		float FriendDistance(const RE::NiPoint3& a_at);  // the nearest friend in the party (1e9 alone)
		bool  IsFriend(RE::Actor* a_actor);              // another player's figure (Skyrim Together)
		bool  SoloFallen();                              // alone and on the death screen
		bool  HoldsDeath();                              // the player falls instead of dying (a killing blow is turned aside)
		inline constexpr std::uint32_t kGhostRoll = 1, kGhostFall = 2, kGhostShadow = 4;
		void  Ghost(std::uint32_t a_why, bool a_on);     // untouchable while any reason holds (setghost, counted)
		enum class Life : std::uint8_t { Alive = 0, Down = 1, Dead = 2 };
		struct Fallen { std::string name; float left, dist; };
		struct View
		{
			Life                life{ Life::Alive };
			float               left{ 0 };        // down: seconds to bleed out
			std::string         watching;         // dead: whom you watch
			float               reviveFrac{ -1 };  // reviving a friend: 0..1 (-1 none near)
			std::string         reviveName;
			std::vector<Fallen> fallen;           // friends down right now
			struct Mark { std::string name; float x, y, dist; bool front; Life life; float hp{ 1.0f }; float ang{ 0 }; int cls{ -1 }; };  // cls: their class  // hp: 0..1 from their figure
			std::vector<Mark>   marks;            // where each friend is (screen 0..1)
		};
		struct Out { std::uint8_t life; RE::NiPoint3 at; float left; std::uint64_t revive; std::int8_t wonder; std::int8_t cls{ -1 }; };
		void OnLoad();                                 // a save is loading: the fall's screen and hold let go
		void Tick(float a_delta);                      // game thread
		bool OnButton(const RE::ButtonEvent* a_b);     // Activate held (revive); arrows switch whom you watch
		Life State();
		View Now();
		Out  Outgoing();                               // Net: what to tell the party
		void Heard(std::uint64_t a_from, const std::string& a_name, std::uint8_t a_life, const RE::NiPoint3& a_at, float a_left, std::int8_t a_wonder = -1, std::int8_t a_cls = -1, std::uint32_t a_credit = 0);
		int  FriendsShort(int a_town, int a_need, std::string* a_names = nullptr);  // (co-op altars) friends in the party with fewer than a_need own trials there
		int  FriendCount();                                // other players in the party now
		std::string WonderHolder(int a_wonder);         // (retired: always '')
		void        SetWonder(int a_mask);              // the class wonders we have won (bits): told to the party
		int         PartyWonders();                     // the class wonders our friends have won (bits, pooled while this game is played)
		void        ResetWonders();                     // (a load or a new game: the pool starts empty)
		void Revived(std::uint64_t a_by);
		void Forget(std::uint64_t a_who);
		// The party's save points: every dawn (and the dawn a freed city brings) each game saves itself a few seconds
		// later, all at the same moment (the host's night ends on every game at once); a wiped party loads exactly that
		void DawnSave(int a_night);
		void CityFreed(int a_town);  // the coming dawn's save is named for the freed city
	}

	// Co-op with Skyrim Together (CurseOfAlduin.ini: Role=host|client|solo)
	namespace Coop
	{
		enum class Role { Solo, Host, Client };
		Role Get();
		bool Client();          // a friend's game: the host raises the dead
		bool TogetherLoaded();  // Skyrim Together runs the clock
	}

	// Alduin's Curse (sheet: curse): four night quests per city; a freed city is a sanctuary.
	namespace Curse
	{
		void  DevSet(int a_town, int a_value);  // dev (trailer filming): a city's curse value as is (1010000 = Ward under way, 3760000 = its Dragon Priest walks)
		void  Tick(float a_delta);
		void  Reset();
		void  OnUndeadDeath(RE::Actor* a_actor);
		void  OnBossSlain();
		void  OnWave(bool a_cleared);       // the night's waves: one begins (false) or is cleared (true)
		void  OnChampionSlain(int a_town);  // a city's champion fell (Director)
		bool  PriestWaits();        // here, a city whose trials are done waits for its Dragon Priest (the night's last foe)
		void  OnNightCleared();     // the night's last wave fell: a city whose Dragon Priest fell tonight is freed now
		void  ForgetPendingLift();  // a load: no Jarl's fall is waiting
		void  MigrateRites();               // a save from the four-rite cities: freed stays freed, the rest begin the trials
		void  NewGamePlus();                // Stage 5, the Seat of the Awakened: every city back under the curse, the nights start over
		bool  Freed(int a_town);            // its Echo Altar is awake (three trials and its Dragon Priest)
		std::uint32_t CreditBits();          // (co-op) your own trials per city, 3 bits a city: told to the party
		std::vector<std::uint8_t> MineState();               // the co-save: which trials you did yourself, per city
		void                      SetMineState(const std::vector<std::uint8_t>& a_s);
		bool  SafeHere(RE::PlayerCharacter* a_player);  // in a freed city: no undead rise here
		float SpawnBoost();                              // holding a shrine: the night pushes harder
		int   ZoneTown();                                // city the player is in (-1)
		int   ClaimableShout(int a_town);                // the freed city's Thu'um not yet learned (-1)
		int   Raw(int a_town);                           // the city's progress value (1000000 + done*100000 + active*10000 + progress; 9000000 freed)
		void  SetRaw(int a_town, int a_value);
		int   Steps(int a_town);                         // trials done (0-3), 4 when freed
		void  StartTrial(int a_town, int a_trial);       // the altar's menu: take up one of the five trials (gen::kTrials)
		void  NoteHit(RE::FormID a_target, bool a_melee);  // the player's last blow on one of the dead (Trial of Steel)
		void  OnClassKill(int a_how, int a_element, float a_dist);  // the class trial: 0 summon, 1 Destruction spell, 2 bow/crossbow, 3 melee
		std::string TrialTitle(int a_trial);             // its name (the class trial's follows your class)
		struct AltarView
		{
			bool                       on{ false };  // the city is still held (its trials are offered)
			int                        done{ 0 }, active{ 0 };  // bits of trials done; 0 none, 1-5 a trial, 6 the Dragon Priest
			std::array<std::string, 5> desc;         // each trial with tonight's numbers
			std::string                boss, bossLine;
		};
		AltarView Altar(int a_town);
	}

	// Stage 2 (src/Echo.cpp): the Echo Stones, the wonder weapons' parts, the Reliquary under High Hrothgar
	namespace Echo
	{
		void DevGiveBane();  // dev (trailer filming): Alduin's Bane into your hands
		void Tick(float a_delta);
		void HeardWords(int a_mask, bool a_live);  // (Net) the host's silenced words: dragons always, the Echo only as it falls
		int  DragonMask();                         // the three dragons slain (bits 0-2)
		bool SiegeActive();                        // a dragon's (or the Echo's) siege under way
		bool EchoAwake();                          // the Echo of Alduin waits on the Throat (not slain this cycle)
		void Reset();
		void OnUndeadDeath(RE::Actor* a_actor);                // a kill near a barrow that wants the dead
		bool OnActivate(RE::TESObjectREFR* a_ref);             // the Reliquary's doors and pedestals (true: handled)
		bool OnButton(const RE::ButtonEvent* a_b);             // High Hrothgar's hall: taking a floating wonder / the Bane, the pillar's menu (true: handled)
		bool Eats(RE::InputEvent* a_event);                    // those keys kept from Skyrim (no swing, no Activate prompt of its own)
		bool StoneWaits(int a_town);                           // a freed city whose Echo Stone still holds its page
		bool Target(RE::PlayerCharacter* a_player, RE::NiPoint3& a_at, std::string& a_label);  // where the story wants you
		bool Strip(std::string& a_title, std::string& a_goal, float& a_frac);                  // a hunt under way (under the compass)
		void Allies(int a_kind, int a_count, float a_seconds, const RE::NiPoint3& a_at);       // the wonders' summons: 0 the barrow's bone warriors, 1 the Queen's spirit-wolves
		bool IsAlly(RE::Actor* a_actor);
		bool WordSilenced(int a_w);                            // a word's dragon slain: 0 SLEN (flesh remade), 1 TIID (borrowed time), 2 VO (the curse undone); 3 the Echo of Alduin
		bool DragonsSlain();                                   // Act III done: the three dragons dead (the wonders' Legendary form)
		bool WondersAll();                                     // Act III open: all four class wonders won (yours, or pooled with your party)
		bool CurseBroken();                                    // the Echo of Alduin slain this cycle: the curse's nights are over (unless the Brazier burns)
		bool HallOpen();                                       // the Hall of the Awakened's rewards (once the Echo has fallen in any cycle)
		void OnBearerSlain();                                  // a word's dragon (or the Echo) fell (Director's champion with no city)
		bool AwardNextPart();                                  // (retired: the parts are gone - always false)
		std::vector<RE::ActorHandle> Summon(std::string_view a_plugin, RE::FormID a_id, int a_count, const RE::NiPoint3& a_at);  // allies for the night (the hidden quests' summons)
		void Thralls(RE::TESNPC* a_npc, int a_count, float a_seconds, const RE::NiPoint3& a_at, int a_max);  // (Alduin's Bane, the Summoner) raised for a while, a_max at once
		bool BoneDragon();                                     // the Hall: the glider's dragon wears Alduin's bones
		bool EndlessNight();                                   // the Hall: the curse's nights never end while the brazier burns
		int  NewGamePlus();                                    // cycles begun again at the Seat of the Awakened (0 = the first)
		float SetDamageMult();                                 // the First Dragonborn's Regalia worn: 1.25
		bool WearsRobes();                                     // the First Dragonborn's Regalia (its robes) are on
	}

	namespace Intro
	{
		void OnMainMenu();  // the next new game moves its character again
		bool FreshGame();  // this is a new game's new character (seen starting since the main menu, or level 1 on its first days)
		bool NewGameThisSession();  // a new game was started from the main menu and its character made (not Continue / Load)
		void Install();                 // data loaded: set MQQuickstart
		void Tick();
		bool OnButton(const RE::ButtonEvent* a_event);  // true = consumed
		void OnRaceMenu(bool a_opening);  // the character creator opened or closed (whoever opened it)
	}

	namespace Gun
	{
		RE::NiPointer<RE::NiAVObject> LoadModel(const std::string& a_path);   // one of our models (a fresh copy), by its path under Meshes
		std::vector<RE::Actor*> FoesNear(const RE::NiPoint3& a_at, float a_radius);   // the night's dead (and your other foes) round a point
		void                  HitShare(RE::Actor* a_target, float a_share);           // a blow of a share of one of the night's dead's health
		void                  Freeze(RE::Actor* a_target, float a_seconds);           // held fast (frozen solid, rooted)
		void                  Singularity(const RE::NiPoint3& a_at, float a_radius, float a_crush, float a_share, bool a_dark);   // draws the dead in for a_crush seconds, then bursts (dark: the void's look; else light's)
		void                  Tick(float a_delta);
		void                  Reset();
		void                  TestKit();  // dev: every mod's attack spell (named by element), every bow and shout, the glider, 150,000 septims
		bool                  TestKitOn();         // a test character (UN_TestKit): the glider unlocked, its spells named by element
		void                  TestKitIfFlagged();  // a new character: the kit, if CurseOfAlduin-testkit.flag is beside the log
		void                  EyeBeam();  // Winterhold's hidden quest: a beam of the Eye's light through the dead ahead
		bool                  OnButton(const RE::ButtonEvent* a_event);
		std::pair<bool, bool> RecentHit(RE::FormID a_actor);  // {a blow of ours, headshot}
		void                  Console(const std::string& a_cmd);  // a console command, run as the player
		bool                  KnowsShout(int a_shout);        // the cities' shouts (sheet: shouts)
		void                  LearnShout(int a_shout);
		int                   ShoutOfSpell(RE::FormID a_spell, int& a_level);
		void                  StormHit(RE::Actor* a_target);
		void                  PriestHit(RE::Actor* a_target, RE::FormID a_source, bool a_power);  // a Dragon Priest's weapon struck it: its theme, and on a power attack (a bow's sixth arrow) its signature
		void                  FangHit(RE::Actor* a_target, bool a_power);  // Alduin's Fang struck it (Stage 5)   // a blow you landed: Stormblast's charge goes off on it
		void                  Shout(int a_shout, int a_level);
		bool                  Reforge();      // the class spell in hand climbs its ladder (Reforge, Bind Pact, Imbue)
		int                   ReforgeCost();  // -1 no class spell in hand, 0 at the top
		void                  FxBlast(std::string_view a_name, const RE::NiPoint3& a_at, float a_delay = 0.0f);  // a no-damage Skyrim explosion (look only)
		void                  FxFrom(RE::BGSExplosion* a_src, const RE::NiPoint3& a_at, float a_scale);  // any mod's burst, quieted (look only), at a_scale
		RE::BGSExplosion*     Quiet(RE::BGSExplosion* a_src);  // that burst's quiet copy (no damage, no knockdown, no tint)
		void                  AreaHurt(const RE::NiPoint3& a_at, float a_radius, float a_share, std::string_view a_blast);
		void                  Burn(RE::Actor* a_target, float a_perSecond, float a_seconds, int a_fec = 0);  // damage over time (a_fec: the element its kill dies in - 0 fire, 3 poison, -1 none)
		// (the class swords) a crescent flying out from a swing, cutting through every one of the dead it passes (share: of one
		// of the night's dead's health); its angle, where it flies; and the Summoner's circling blades of light
		struct SlashSpec { std::string model; RE::NiPoint3 from, dir; float roll = 0, speed = 1700, range = 1500, radius = 110, share = 0.3f, scale = 1.0f, damage = 0; bool sideways = false, stopOnHit = false, bolt = false;  /* bolt: a spell bolt - its full size the whole way (no crescent's growing) */ float flingKill = 0; int fec = -1;  /* the element its kills die by (FEC) */ std::function<void(RE::Actor*, bool)> onHit; };  // (flingKill: a blow that kills throws the body that hard)
		void                  Slash(const SlashSpec& a_s);
		RE::NiPoint3          SwingDir();
		RE::NiPoint3          SwingFrom();  // where a swing's slash starts: ahead of your eyes (first person), your chest (third)
		float                 SwingRoll(const RE::NiPoint3& a_dir);
		void                  Halo(int a_count, const std::string& a_model, float a_scale, float a_period, std::function<void(RE::Actor*)> a_strike);
		void                  BlastSound(std::string_view a_name, const RE::NiPoint3& a_at, float a_vol = 1.0f);  // a Skyrim spell explosion's own impact sound
		void                  Meteor(const RE::NiPoint3& a_at, bool a_gold, float a_scale, std::function<void(const RE::NiPoint3&)> a_land);
		void                  MeteorAt(RE::Actor* a_target, bool a_gold, float a_scale, std::function<void(const RE::NiPoint3&)> a_land);   // a fireball from the sky chasing one of the dead
		void                  Grow(const std::string& a_model, const RE::NiPoint3& a_at, float a_yaw, float a_s0, float a_s1, float a_grow, float a_life, float a_fadeFrom);  // a shape of light set on the ground: grown, held, faded
		void                  MeteorImpact(const RE::NiPoint3& a_at, bool a_gold, float a_size);  // a meteor's landing: flash, shockwave ring, burning scorch (no damage)
		void                  Ring(const char* a_colour, const RE::NiPoint3& a_at, float a_r0, float a_r1, float a_life);  // a shockwave ring growing across the ground (ring_<colour>.nif)
		void                  Knock(RE::Actor* a_a, float a_mag);  // a stagger
		void                  FireDeath(RE::Actor* a_a, int a_elem);  // the element's touch laid on one about to die, so FEC gives it that death (0 fire)
		int                   FecElem(int a_elem);  // the element the blows struck from now on kill with (FEC's death effects: 0 fire 1 frost 2 shock ...); returns the one before
		struct CycloneSpec
		{
			std::string model; RE::NiPoint3 from, fwd; float dist, life, share, size = 1.0f; int fec = 0; bool dragons = false; bool noPull = false;   /* (a blizzard: it stands, it doesn't drag the dead in) */
			std::function<void(RE::Actor*)> touch; std::function<void(const RE::NiPoint3&)> burst;
			bool follow = false, push = false;   /* (Alduin's Bane: it spins on you, and throws the dead out rather than drawing them in) */
		};
		void                  ElemCyclone(CycloneSpec a_s);
		// (Noah: the Mutagen Gauntlet "shoot a beam coming from the palm/glass ball while holding attack") its state, set by
		// Classes every frame for the glove's look (the vials' levels, which one pours, the orb's colour) and the arm's pose
		struct GauntletState { float fill[6]{ 1, 1, 1, 1, 1, 1 }; int active = -1; int phase = 0; float t = 0; bool left = false; };   // phase: 0 idle, 1 the charge's gesture, 2 the beam
		void                  SetGauntlet(const GauntletState& a_s);
		bool                  GauntletBeam(int a_el, float a_width, RE::NiPoint3& a_from, RE::NiPoint3& a_to);   // the beam laid from the palm's orb to where you aim (false: no glove in sight)
		bool                  StaffBeam(int a_el, float a_width, const RE::NiPoint3& a_from, RE::NiPoint3& a_to);   // a beam drawn from a point to where you aim
		void                  GauntletBeamOff();
		std::vector<RE::Actor*> LineHurt(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_radius, float a_share);   // every one of the dead along a line struck (share of one's health); those struck
		bool                  SkyBeam(const RE::NiPoint3& a_at, float a_secs);   // Mysticism's Sunbeam down out of the sky onto a point (false: not installed - the sunlit pillar instead)
		void                  FxBlastSized(std::string_view a_name, const RE::NiPoint3& a_at, float a_scale);   // one of Skyrim's explosions at a size
		void                  QueueFx(std::string a_path, const RE::NiPoint3& a_at, float a_scale, float a_delay, float a_life, bool a_flat);   // one of our effect models (or its effects-map stand-ins) set off at a point   // a cyclone of an element carried along the ground (FireCyclone's, any look)
		void                  FireCyclone(const RE::NiPoint3& a_from, const RE::NiPoint3& a_fwd, float a_dist, float a_life, float a_share, std::function<void(RE::Actor*)> a_touch, std::function<void(const RE::NiPoint3&)> a_burst);  // a tornado of violet fire carried along the ground
		void                  LightningArc(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_delay);  // a bolt of lightning between two points  // a meteor of ours (tools/meteorfx.py) falling from high above onto a point
		void                  PurpleFire(const RE::NiPoint3& a_at, float a_scale, float a_delay, float a_life);
		void                  Motes(int a_elem, const RE::NiPoint3& a_at, float a_scale, float a_life);  // fine motes rising off the ground over an area (the ElemBurst elements)
		bool                  PlayerPowerAttack();   // your swing now a power attack (its attack data; the hit's own flag never comes with Precision)
		void                  ElemBurst(int a_elem, const RE::NiPoint3& a_at, float a_scale, float a_delay, float a_life);  // an element's impact burst (0 fire 1 frost 2 shock 3 wind 4 sun 5 blood 6 void), here a moment  // the Horn's dragon's violet flame, here a moment  // the night's dead round a point lose a share of a whole zombie's health
		void                  Puddle(int a_elem, const RE::NiPoint3& a_at, float a_scale, float a_life);  // a pool of an element on the ground (0 fire 1 frost 2 shock 3 acid 4 sun 5 blood)
		void                  SetLightOrb(bool a_on);
		void                  DevFx(const std::string& a_path, const std::string& a_root, float a_side);
		void                  DevNodes();
		void                  DevLook(const std::string& a_model, const std::string& a_mode, float a_scale, const std::string& a_pal, float a_secs);
		void                  DevFxReload();
		void                  TintGold(RE::NiAVObject* a_root);   // every effect shader under a node in gold (once)
		void                  TintPal(RE::NiAVObject* a_root, const std::string& a_pal, bool a_hideFlames);   // ... in any palette
		void                  DevSwing(int a_power);
		void                  TickLooks(float a_delta);
		void                  StartThrow(bool a_left, std::function<void()> a_release);  // the underhand throw on that arm; a_release at the top of the swing
		bool                  Throwing();  // the Light Orb over your head, lit or put away
		void                  Lob(const std::string& a_model, const RE::NiPoint3& a_from, const RE::NiPoint3& a_vel, float a_scale, std::function<void(const RE::NiPoint3&)> a_land, float a_spin = 12.0f, float a_grav = 900.0f);  // (a_spin 0: it flies nose first - a meteor)  // a thrown thing flown on its arc; a_land where it comes down
		void                  Hazard(const std::string& a_model, const RE::NiPoint3& a_at, float a_scale, float a_life, float a_yaw = 0.0f);  // a model of light lingering a_life seconds where it's set (drawn as the flasks' ground fires are)
		RE::ObjectRefHandle   PlaceAt(RE::TESBoundObject* a_obj, const RE::NiPoint3& a_at, float a_yaw);  // a placed object made at the point itself (its 3D drawn there, not made at the player and moved)
	}

	namespace ArmorForge
	{
		void Build();  // the class robes built from Black Mage Armor SE's files on this PC (once per recipe)
		void BuildHipBlades();  // the four class swords' models, sheathed at the hip (from Light Greatswords' files on this PC)
	}

	// (Noah: the mystery box) the Tablet of Mysteries: one freed city at a time; its roll animated on its model (Tablet.cpp)
	namespace Tablet
	{
		bool StarWandInHand();      // the Wandering Star (a wand now) drawn in your right hand
		void StarButton(bool a_down);   // its attack held or let go (Gun's buttons)
		void AdoptLooks();   // Tablet weapons that wear another mod's weapon (the Eclipse Edge: the Glass Pike)
		int  Town();                                   // where it stands (-1: no city freed yet)
		int  Price();                                  // a spin (or a repair) now
		void OnActivate(RE::TESObjectREFR* a_ref);
		void Tick(float a_delta);
		std::vector<std::uint8_t> State();             // (saved with the altars' rolls)
		void SetState(const std::uint8_t* a_v, std::size_t a_n);
		void Dev(std::string_view a_what, int a_town);
		// the donated weapon you carry
		void  OnHit(RE::FormID a_source);              // your blow landed (melee and fists: a use)
		void  OnCast(RE::FormID a_spell);              // your cast (staffs and the Codex: a use)
		void  OnFell();                                // (lost if you fall)
		void  DevKit();                                // (the test kit) every donated weapon, at its full uses and never wearing
		void  OnStrike(RE::Actor* a_target, RE::FormID a_source, bool a_power, bool a_arrow);   // its power on a blow or an arrow
		float TakenMult(RE::Actor* a_actor);           // (the Saltrice Sickle's salt: 30% more taken)
		bool  IsTablet(const RE::TESForm* a_w);        // the donated weapon you carry?
		bool  IsTabletSpell(const RE::TESForm* a_f);   // its spell (a staff's enchantment, the Codex)?
		float Share();                                 // its blow's share of the night's health: one or two tiers over your sword
		float Wear();                                  // its uses left 0..1 (-1: none carried)
	}

	namespace Shops
	{
		void  BeforeLoad();  // a save is loading over this world: our hand-made map-marker records come off first
		std::vector<std::uint8_t> RollState();                    // each altar's rolls tonight (the co-save)
		void  SetRollState(const std::vector<std::uint8_t>& a_s);
		void Tick(float a_delta);
		void Reset();
		void OnActivate(RE::TESObjectREFR* a_ref);
		bool OnButton(const RE::ButtonEvent* a_event);  // the shrine menu's keys (true = used)
		void DevAct(int a_action);
		bool MenuEats(RE::InputEvent* a_event);          // Skyrim's own handlers must not see this key (the shrine menu has it)
		std::string BoxTown();
		bool        StandFrame(int a_town, RE::NiPoint3& a_center, float& a_yaw, RE::TESWorldSpace*& a_world);  // true: the station is placed
		RE::TESObjectREFR* Placed(int a_town, int a_kind);
		void               FreedPulse(int a_town);   // the city's last rite: the shrine sends out a wave of light and sound  // the town's machine of that object kind (6 = relic shrine), if placed
		void                DevFreed(int a_town, bool a_on);   // (dev freeloop) a city's freed look shown though it isn't freed (-1: none)
		int         TownOfRef(RE::TESObjectREFR* a_ref);
		bool        StationTarget(int a_town, RE::NiPoint3& a_at);  // where to head for the town's station, in your current world (a walled city from outside: its gate)
		bool        Discovered(int a_town);
	}

	namespace Perks
	{
		void OnEffect(RE::FormID a_mgef);
		bool Grant(int a_perk);    // bought: applied at once (one of each)
		bool Refund(int a_perk);   // given back at the altar: off you, half its price back
		int  RefundOtherClasses(int a_keep);  // (a class switch) the other classes' brews off you, half their price back; the shared ones stay
		std::vector<std::uint8_t> TakeAll();                         // (a class left) its brews off you
		void                      GiveBack(const std::vector<std::uint8_t>& a_stacks);  // (a class taken up again) back on
		void OnZombieHit(RE::Actor* a_zombie);  // a round zombie hit the player
		float SinceZombieHit(RE::FormID a_zombie);  // seconds since this one's blow last landed (large: never)
		void Tick();
		bool SelfRevive();         // just risen: a killing blow is caught for a moment
		void SelfHit();            // your own spell hit you: what it took is given back
		void RearmAfterLoad();     // a moment after a load: the first-person hands rebuilt
		void DamagePlayer(float a_amount);
		void Dodged(float a_seconds);  // a dodge: the night's blows miss you this long
		float ManaMult();          // magicka cost
		float RegenMult();         // magicka returns faster (Mana Font Draught)
		std::vector<int> Menu();          // the altar's brews for your class: your class's, then the shared ones
		int   MenuAt(int a_pos);
		bool  Live(int a_perk);           // carried, and (a class brew) you are of its class
		float FlaskCostMult();            // (the Alchemist) Quickhand Elixir
		float FlaskRadiusMult();          // (the Alchemist) Volatile Tincture
		bool  SummonMight();              // Grave Fury Wine
		float SummonMightMult();
		float BladeSpellMult();           // Spellbinder's Draught: the blade's spell harder
		float KillEssenceMult(RE::Actor* a_dying);  // Gilded Tongue Tonic, Bounty Hunter's Ale
		float MiniBossEssenceMult();      // Bounty Hunter's Ale
		void  Catalyze(RE::Actor* a_target);   // Catalyst Philter: a flask's burst caught it
		float TakenMult(RE::Actor* a_target);  // ... and it takes more for a while
		void  OnSummonDied(RE::Actor* a_summon);  // Bone Burst Brew
		void  OnDestructionCast(RE::SpellItem* a_spell);  // Overflow Tea
		void  OnClassKill(int a_how, RE::Actor* a_dying);  // 0 summon, 1 Destruction spell, 3 melee
		void  OnMeleeHit();               // Bladesong Mead
	}

	namespace Fantasy
	{
		void Inject();  // the High Fantasy Pack's creatures into Skyrim's wilderness and bandit encounters (data loaded)
		void TameSpellSounds();  // every magic effect sound quieter (the drawn / readied drones most)
		void HushAltarEchoes();  // the altar's spells: their effects' doubled sounds silenced (after Arcana::Load)
	}

	namespace Movement
	{
		bool Dodging();  // a dodge roll under way
		bool Gliding();  // on the dragon glider (or just landed from a dive)
		RE::NiMatrix3 SlerpRot(const RE::NiMatrix3& a_from, const RE::NiMatrix3& a_to, float a_t);

		void OnButton(const RE::ButtonEvent* a_event);  // crouch/jump keys (Skyrim ignores crouch while sprinting)
		void OnStick(const RE::ThumbstickEvent* a_event);  // the left stick (the dragon glide's steering)
		void Tick(float a_delta);
		void Reset();
		bool Riding();  // on a horse
		void PoseGlideArms(RE::PlayerCharacter* a_player);
		void SwingBody(RE::PlayerCharacter* a_player);
		void DevChase(bool a_on, float a_dist, float a_yaw, float a_pitch, float a_height, float a_spin);  // dev: a camera following you  // hanging from the dragon's harness, your body swings
		// the Echo Altar's dragon bonds: I Wind-Rider, II Talon Strike, III Alduin's Thrall (kept for the whole game)
		inline constexpr int kBondCost[3] = { 5000, 9000, 14000 };
		void BuyBond(RE::TESObjectREFR* a_shrine);
		bool BondLocked();  // the bonds wait for Whiterun's Echo Altar to be won
		void DevBreath(int a_element);  // dev: Bond III breath (0 fire, 1 frost, 2 lightning)
		void DevFlap();
		void DevBreathProj(int a_element, RE::FormID a_proj, float a_scale, float a_beamSign);
		void DevTakeOff();  // dev: Bond II's Updraft shout
		bool OnSpellCast(RE::FormID a_spell);  // the Updraft shout spoken: true if it was
		void TestKit();     // dev: Dragon's Updraft and all three bonds
		void DevUnlockGlider();  // dev: the glider unlocked on this computer (a flag file) and Dragon's Updraft
		void GiveUpdraft();
		bool DevGliderFlag();
		void DevFlyOff();   // dev: the fly-off dragon, in front of you  // after the animation: arms down until the glider's dragon reaches you
	}

	// Quick loot in the game's style (src/Loot.cpp): a body's or a container's things beside the crosshair
	namespace Loot
	{
		void Tick(float a_delta);
		bool Eats(RE::InputEvent* a_event);           // Skyrim's own handlers must not see this key (the loot list has it)
		bool OnButton(const RE::ButtonEvent* a_event);
	}

	// (Noah) the hidden quests, one per Echo Altar (src/Eggs.cpp, sheet: eggs)
	namespace Eggs
	{
		void  Tick(float a_delta);
		void  OnHurt();                          // (a coin step: taken unhurt)
		void  OnUndeadDeath(RE::Actor* a_actor); // (marked ground)
		void  OnClassKill();                     // (Classes) a kill your class's own way
		bool  WantsDead();                       // a secret's step in this freed city wants the dead about (they don't crumble)
		void  OnBossSlain(int a_town);           // its named foe fell (Director's champion, town -10 - city)
		bool  OnSpellCast(RE::FormID a_spell);   // (retired: false)
		void  Heard(int a_town);                 // (retired)
		void  Reset();                           // a save loaded or a new game: the live step forgotten
		std::vector<std::string> Whispers(int a_town);  // the altar's hints: a line more for each step found
		std::string Line();                      // your class's quest, for the guide
		float DamageMult();                      // (retired: 1)
		bool  ShadowStep();                      // (retired: false)
		void  DevStep(int a_town);
	}

	// (Noah) the night's atmosphere (src/Atmos.cpp)
	namespace Atmos
	{
		void Lightning(float a_intensity);  // a flash over the sky, thunder after it (0..1)
		void Tick(float a_delta);
	}

	// (Noah) the night's score, records and style points (src/Stats.cpp)
	namespace Stats
	{
		struct Line { int kills = 0, heads = 0, best = 0, septims = 0, revives = 0, style = 0, waves = 0, minis = 0, night = 0; };
		struct Records { int bestNight = 0, kills = 0, heads = 0, nights = 0, minis = 0, bestStreak = 0, quests = 0, wonders = 0; };
		struct Friend { std::string name; Line line; };
		enum Kind { kMiniBoss, kRevive, kWave, kQuest, kWonder };
		void OnKill(RE::Actor* a_dying, bool a_gun, bool a_head, bool a_melee, bool a_bow, float a_dist);
		void Style(std::string_view a_tag, int a_points);  // a style bonus: the points and the tag by the septims
		void OnHurt();                                     // the streak ends
		void OnEarned(int a_septims);
		void Note(Kind a_kind);
		void NightBegins(int a_night);
		void NightEnds(int a_night);
		Line Tonight();
		Line LastNight();
		Records Lifetime();
		void Heard(std::uint64_t a_from, const std::string& a_name, const Line& a_line);
		std::vector<Friend> Party();
	}

	namespace Tasks
	{
		void Add(std::function<void()> a_task);    // on the main thread (SKSE's queue, or ours when loaded as the .asi)
		void AddUI(std::function<void()> a_task);
		void Drain();                              // ours: from the Present hook
		void Later(float a_seconds, std::function<void()> a_task);  // on the main thread, after a while (one timer thread for all)
	}

	namespace Watchdog
	{
		void Beat();   // a frame was drawn (from Present)
		void WorldLoaded();   // a load finished: the player should update soon
		void PlayerTicked();  // it did
		void Start(bool a_onMainThread);  // the thread that logs where the game is stuck when frames stop
	}

	namespace Hud
	{
		// (Noah: Edge UI is the game's UI) our compass, vitals, equip cards, health and boss bars, quest list and markers,
		// crosshair and party panel stand down: Skyrim's own HUD (Edge UI, TrueHUD) shows them
		inline constexpr bool kEdgeHud = true;
		inline constexpr bool kOwnBars = true;  // (Noah: our own health, stamina and magicka plate instead of Edge UI's bars - with the shout's cooldown round it)
		struct State
		{
			bool        inGame{ false };
			// the nearest shrine's orb on screen (x, y 0..1; r its radius in screen heights): its flame aura is drawn there
			bool        orbOn{ false };
			float       orbX{ 0 }, orbY{ 0 }, orbR{ 0 }, orbFade{ 0 };
			bool        orbAim{ false };  // the orb is in the crosshair: Activate opens the shrine
			int         round{ 0 };
			bool        nightActive{ false };
			int         wave{ 0 }, waves{ 0 }, waveLeft{ 0 }, waveSize{ 0 };  // (Noah: BO3 waves) this night's wave, how many, the dead still to kill in it
			float       waveRest{ 0 };    // seconds until the next wave rises
			std::vector<std::array<float, 4>> lastDead;  // (the round flow) a wave's last three: screen x, y, in front (1/0), distance
			std::string waveTheme;          // a themed wave's name ("GOBLIN RAID"), else empty
			std::uint32_t waveThemeColor{ 0 };
			// a night (round) is being fought; otherwise round = nights survived
			int         points{ 0 };
			std::string banner;
			float       bannerTime{ 0 };
			float       bannerTotal{ 0 };
			std::string sayWho, sayLine;  // (a townsperson speaks: a subtitle)
			float       sayTime{ 0 };
			float       dread{ 0 };       // (surrounded: 0..1, the screen's edges darken a little)
			int         lastGain{ 0 };
			float       gainTime{ 0 };
			float       hitTime{ 0 };
			bool        hitKill{ false };
			bool        hitHead{ false };  // (hit feedback) a headshot: the marker in gold
			float       hurtDirT{ 0 }, hurtDir{ 0 };  // (hit feedback) a blow from where you can't see: its side (radians from your facing), and its fade
			float       kickT{ 0 };                   // (hit feedback) a big blow: the screen's edge flinches
			// (Noah: "BO3 death instead of reloading a save") the fall's screen: seconds since you fell (-1 none), the night as it
			// stood, and how near you are to rising (0..1)
			float       deathT{ -1 };
			int         deathNight{ 0 }, deathWave{ 0 }, deathWaves{ 0 }, deathKills{ 0 }, deathHeads{ 0 }, deathBest{ 0 }, deathSeptims{ 0 }, deathPerks{ 0 };
			float       deathRise{ 0 };
			bool        deathReady{ false };  // (a press rises now)
			bool        deathLoading{ false };  // (your last save is being loaded)
			float       headBurstT{ 0 };  // a headshot kill's white flash at the crosshair
			float       lightningT{ 0 }, lightningI{ 0 };  // the sky's flash (seconds left, strength)
			bool        eggDark{ false };                 // a hidden quest's dark (Riften's, Dawnstar's dream)
			std::vector<std::string> whispers;            // the altar's hidden quest: its whispers (empty: none yet)
			float       nightProgress{ -1 };              // 0..1 through the night's waves (-1: not fighting)
			std::string styleTag;         // a style bonus ("+40 MULTI-KILL") by the septims
			float       styleTime{ 0 };
			std::uint32_t perks{ 0 };    // bitmask
			std::array<int, 32> perkStacks{};  // how many of each blessing (they stack)
			float       countdown{ 0 };  // seconds until the next night starts (0 = none)
			float       hp{ 0 }, hpMax{ 0 }, mp{ 0 }, mpMax{ 0 }, sp{ 0 }, spMax{ 0 };
			float       hurtTime{ 0 };   // red vignette after taking a hit
			float       noise{ 0 };      // how loud you've been tonight (0..1: full, a Volatile hears you)
			bool        perkMenu{ false };
			std::string bossName;  // empty = no boss
			float       bossHp{ 0 };
			bool        bossEnraged{ false };
			int         perkSel{ 0 };
			int         altarFocus{ 0 };   // the altar's focused row (Shops Row)
			int         altarOpen{ 210 };  // the Echo Altar's open section (210 kit, 211 Armory, 212 perks, 213 Thu'um and dragon, 214 trials, 215 whispers; 0 none)
			int         shrineShout{ -1 };   // a Thu'um to learn at the altar
			// (the altar rework) your kit and the Armory (Kit.cpp)
			struct KitLine { int id{ 0 }; std::string name, info; int tier{ 0 }, asc{ 0 }, cost{ -1 }; bool can{ false }, carried{ true }; std::uint32_t color{ 0xC8B48C }; };
			std::vector<KitLine> kitLines, armoryLines;
			float                armorBonus{ 0 };   // the class's armour from its upgrades (0..0.3)
			int         dragonBond{ 0 };   // the glider's dragon: its bond (0-3)
			bool        bondLocked{ false };
			std::vector<std::array<float, 3>> motes;  // (Noah: the dead give up their essence) screen x, y (0-1) where a kill fell, its age in seconds
			int         freedCount{ 0 }, forgeAt{ 3 };  // Echo Altars freed; the reforge opens at this many
			// the altar's trials (the city still held): which are done, the one under way, the controller's pick, each with tonight's numbers
			bool        trialsOn{ false };
			bool        altarFreed{ false };  // (Noah: "Curse Lifted") this altar's city is freed
			std::vector<std::array<float, 3>> freedAltars;  // the freed cities' Echo Altar map icons (world x, y, z): gold on the world map
			bool                  tabletOn{ false };   // the Tablet of Mysteries: where it stands (its map icon)
			std::array<float, 3>  tabletAt{};
			float                 tabletWear{ -1.0f }; // the donated weapon's uses left 0..1 (-1: none) - its durability bar
			std::string           tabletName;
			int         trialDone{ 0 }, trialActive{ 0 }, trialSel{ 0 };
			std::array<std::string, 5> trialDesc;
			std::string trialCity, trialBoss, trialBossLine, trialLock;  // (trialLock: why the altar's trials are shut)
			float       questTimer{ -1 };  // the trial under way: seconds left on its clock (-1 none)
			bool        questWarn{ false };  // ...and it is in danger (time short, health low)
			float       pulseT{ 0 }, pulseX{ 0.5f }, pulseY{ 0.5f };  // a freed shrine's wave (seconds left, its screen spot)
			bool        pulseOn{ false };
			int         nightEvent{ -1 };   // gen::kEvents index while it runs
			std::string questCity, questTitle, questGoal;  // the city's current curse quest (empty = none)
			float       questFrac{ 0 };
			int         questStep{ 0 };
			std::string sanctuary;         // in a freed city at night: its name
			std::string loreTitle, loreText;
			std::string slainTitle, slainLine;  // (Noah) a Dragon Priest slain: "<name> has been slain" / "<weapon> received"
			float       slainTime{ 0 }, slainTotal{ 0 };
			float       loreTime{ 0 }, loreTotal{ 0 };
			// (Noah: no Word Walls) the altar's heart-stone teaching its city's Thu'um: the three words rise from its runes one by one
			std::string revealW[3], revealT[3], revealName;
			bool        skipHint{ false };  // Helgen can be skipped now: its line shows (Intro)
			float       skipFrac{ 0 };      // how far the hold has come
			float       revealTime{ 0 };
			std::uint32_t revealColor{ 0 };
			float       wardFrac{ 0 };     // kindling a ward-fire
			std::string questNote;        // quest news in the tracker
			// the quest's objective marker: where on screen (0..1), in front of you or not, how far (units), what
			// floating health bars over the wounded undead near you (screen x, y 0..1, health 0..1, a name for the great ones)
			struct EnemyBar { float x, y, frac, scale; std::string name; std::uint32_t color; std::uint32_t id{ 0 }; int rank{ 0 }; int fury{ 0 }; };  // rank 0 risen, 1 elite / great, 2 champion, 3 another foe; fury: the fire round it (0 none, 1 special, 2 elite, 3 champion)
			std::vector<EnemyBar> enemyBars;
			// the ritual circle (Circle / Hold rites): ring points on screen (x, y 0..1, depth), progress, reset flash
			bool        ringOn{ false }, ringHold{ false };
			bool        ringLabelOn{ false };  // where its words stand on the screen: over the altar, fixed (not chasing the ring)
			float       ringLabelX{ 0 }, ringLabelY{ 0 };
			std::vector<std::array<float, 3>> ringPts;
			float       ringFrac{ 0 }, ringFlash{ 0 };
			int         ringSeconds{ 0 }, ringInside{ 0 };
			bool        objOn{ false }, objFront{ false };
			float       objX{ 0 }, objY{ 0 }, objDist{ 0 };
			std::string objLabel;
			float       objAng{ 0 };  // the objective's bearing from you (radians, 0 north, clockwise)
			// the compass (The Curse of Alduin's own, over Skyrim's hidden one): which way you face, and what lies around you
			struct CompassMark { float ang, dist; int kind; std::string label; int icon = -1; bool known = true; };  // icon: the map marker type (its map icon)  // kind: 1 rite, 2 shrine, 3 city, 4 town, 5 dungeon, 6 place, 7 friend, 8 undead, 9 boss
			bool        compassOn{ false };
			float       heading{ 0 };
			float       shoutCd{ 0 };     // your shout's cooldown still to run (1 just shouted .. 0 ready), on the shout card
			float       shoutReady{ 0 };  // a flash on it the moment it is ready again (1 .. 0)
			float       shoutLeft{ 0 };   // ... in seconds
			std::string shoutName;        // the shout (or power) you have equipped - the shout card under the spell dock
			std::vector<CompassMark> compass;
			struct QuestMark { float x, y, dist; bool front; std::string label; };  // a quest's target on the screen (0..1), drawn in our style
			std::vector<QuestMark> qmarks;
			// the quests chosen in the journal (Skyrim's and the Echo Altar's rites alike), top left: name and open goals
			struct Tracked { std::string title; std::vector<std::string> goals; };
			std::vector<Tracked> tracked;
			// sneaking (our own detection meter, in place of Skyrim's eye): how aware the most watchful one is, and who
			struct Watcher { float ang, aware; bool seen; };  // ang: bearing from where you look (radians); aware 0..1
			bool        sneaking{ false };
			float       aware{ 0 };       // 0 hidden .. 1 seen
			bool        seen{ false };
			std::vector<Watcher> watchers;
			struct LootItem { std::string name; int count, value; float weight; int kind; };  // kind: 0 misc, 1 gold, 2 weapon, 3 armor, 4 potion, 5 ingredient, 6 book, 7 scroll, 8 ammo, 9 soul gem, 10 key
			bool        lootOn{ false };
			bool        ownAim{ false };   // something of ours under the crosshair: Skyrim's own prompt hidden
			// (Noah: High Hrothgar's hall - the wonders and Alduin's Bane floating to be taken up, the fiery pillar's menu)
			std::string hallAim;            // the floating thing in your sights: what Activate / left click does ("" none)
			struct HallRow { std::string title, desc, state; bool live; };
			bool        hallMenu{ false };
			std::string hallTitle;
			std::vector<HallRow> hallRows;  // the last is Leave
			int         hallFocus{ 0 };
			std::string lootTitle, lootNote;
			std::vector<LootItem> loot;
			int         lootSel{ 0 };
			std::uint32_t objColor{ 0xD6B260 };
			float       questNoteTime{ 0 };
			// (Noah: Skyrim's own and other mods' weapons and spells shown too) what is in each hand when it is not ours:
			// kind 0 none, 1 weapon, 2 bow / crossbow, 3 spell, 4 shield, 5 staff, 6 torch or other; its name, and its damage
			// (weapons) or magicka cost (spells); both hands the same two-handed thing: shown once
			std::array<int, 2>         skyKind{ 0, 0 };
			std::array<std::string, 2> skyName;
			std::array<int, 2>         skyValue{ 0, 0 };
			std::array<std::uint32_t, 2> skyColor{ 0, 0 };
			std::array<int, 2> handSpell{ -1, -1 };   // the Coffer spell in each hand (Arcana index; 0 left, 1 right)
			std::array<int, 2> handCost{ 0, 0 };      // what each costs you now (magicka; a second, for a channelled one)
		};
		State&      Edit();
		std::mutex& Lock();
		void        Banner(std::string a_text, float a_seconds = 4.0f);
		void        QuestBanner(std::string a_text, float a_seconds);  // a quest's line across the screen (shown even with the other banners off)
		void        ShoutLearned(std::string_view a_name, std::string_view a_words);  // the shout-learned pop-up (the one banner still shown)
		void        Say(std::string a_who, std::string a_line, float a_seconds = 4.5f);  // a townsperson's words, as a subtitle
		void        Essence(const RE::NiPoint3& a_at);  // a kill's essence flies from the body to your count
		void        Lore(std::string a_title, std::string a_text, float a_seconds = 14.0f);
		void        Slain(std::string a_title, std::string a_line, float a_seconds = 7.0f);  // a Dragon Priest's fall and his weapon received (never switched off)
		void        SetPopups(bool a_on);  // the one-time cards on or off (kept across restarts)
		void        ClearMessages();  // the Banner, the Lore card and their queues emptied (a load)
		void        WordReveal(int a_shout);
		bool        PadLast();                                // the last input was a controller (prompts follow it)
		void        NotePad();                                // a stick moved: the player is on a controller
		std::string PadWord(std::string_view a_xbox);         // a pad button's name as the player's pad prints it (A -> Cross with PlayStation icons)  // the altar's runes give up a shout's three words (Curse::ClaimRelic)  // a scroll of the Curse
		void        Install();
		void        OnClick();  // a left click (menus: the party panel's buttons)
		void        OnMouseMove(float a_dx, float a_dy);  // raw mouse movement (the menu cursor never sticks)
		void        OnMenuButton(const RE::ButtonEvent* a_b);
		void        InstallInputGate();
		void        OnLeftStick(float a_x, float a_y);  // the world map: the left stick moves the cursor  // main menu: pad / arrow keys over our words
		void        DevMenuPick(const std::string& a_text);  // dev: press a main-menu word
		void        IntroStart();             // the Curse of Alduin intro film (once, for a new character)
		bool        IntroActive();
		bool        IntroBusy();
		void        DevMain(int a_index);              // DevTools: press a main-list entry
		void        DevParty(const std::string& a_what);  // DevTools: host / invite / leave  // the film, or the black before or after it (a new character's arrival)
		void IntroVeil(float a_seconds, bool a_instant = false);  // black over the game for a while (a new character's trip to Riverwood, before the film)
		void AfterCharacterMade();  // the race menu closed on a new character: a moment of black, then the game, and the first save
		void        IntroSkipHeld(bool a_held);
	}

	namespace DevTools
	{
		void PollFromRender();
		void CaptureIfRequested(ID3D11DeviceContext* a_context, IDXGISwapChain* a_swap);
		void RunQueuedOnMain();
		void RunQueuedInGame();
		bool HudOff();  // DevTools 'hud 0': our overlay draws nothing (art captures)
		void PressKey(RE::INPUT_DEVICE a_device, std::uint32_t a_id, float a_hold);  // through Skyrim's input queue, as if pressed
	}

	namespace Events
	{
		void InstallMenuWatch();
		void InstallLoadGuard();  // (a save holding an extra twice no longer crashes as it loads)
	}

	// (Noah: "each class has its set starting gear that can be scaled as the player progresses") the altar rework: your
	// class's kit and Armory, every item on one tier curve (Base, I, II, III Legendary, then Ascends), gated by cities freed;
	// your armour from your upgrades (Kit.cpp)
	namespace Kit
	{
		void  OnKitGiven(int a_cls);              // a class's starting kit handed over: those items are owned
		void  Fill(Hud::State& a_h);              // the altar's lines (under the Hud lock)
		std::vector<int> Rows();                  // the altar's rows for the kit (their ids, in order: kit, Armory)
		bool  Select(int a_id, RE::TESObjectREFR* a_altar);  // a kit row taken: upgrade (or take it out of the chest); an Armory row: buy
		void  OnPriestSlain();                    // a Dragon Priest fell: 3,000 Essence
		int   Tier(int a_item);                   // its upgrades (0 Base .. 3 Legendary, then Ascends); -1 not owned
		int   TierOfWeapon(const RE::TESForm* a_w);   // a kit weapon's upgrades (-1: not one)
		void  SyncWeapons(bool a_reequip);            // the kit's weapons as their tiers make them (names, damage, looks)
		int   FlaskTier(int a_el);  // an Alchemist's flask's tier (-1: not owned)
		int   MadTier();     // the Madness Sword drawn in your hand: its tier (-1: not drawn)
		int   RiftTier();                         // the Riftblade's upgrades (0..)
		int   ArmourTier(int a_cls);              // (the class armour sets) the set's upgrades (0..), -1 not owned
		int   ArmourRating(int a_cls);            // 10, and 10 more for each of its upgrades
		float ArmorResist();                      // 0 .. 0.20 of the night's blows taken off (5% a set tier)
		std::string MiniBossSpoils(int a_night);  // a mini-boss's spoils by where your class stands: an open Armory item, else a free upgrade, else essence
		float ArmorAscendMult();                  // 1 + 5% for each Ascend of the class's armor set (on all your side deals the dead)
		float AuraCostMult();                     // (co-op) a Destruction mage in the party: spells cost less
		float AuraMeleeTaken();                   // (co-op) a Spellsword in the party: blows land softer
		bool  PartyHas(int a_cls);                // another player in the party plays that class
		void  HeardClass(std::uint64_t a_from, int a_cls);
		void  OnPlayerHit(RE::Actor* a_target, const RE::TESForm* a_source);  // your blow landed (the Spellsword set's magicka)
		void  Tick(float a_delta);
		std::vector<std::uint8_t> State();        // co-save 'KIT1'
		void  SetState(const std::vector<std::uint8_t>& a_v);
		void  Revert();
		void  DevAll();                          // dev: every class's whole kit at its highest tier
		void  DevBase();                         // dev: every class's whole kit owned, at Base
		void  DevTestKit();                      // the test kit: every class's whole kit at Tier III
		bool  Loaded();                           // this save carries the rework's record
		void PriestDrop(int a_town);  // (Noah: "all players receive the weapon") a city's Dragon Priest slain: his weapon to you, and the card
	}
}
