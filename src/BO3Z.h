#pragma once
// BO3 Zombies x Skyrim. Every number comes from Generated.h (built from sheets/*.json).
#include "Generated.h"

namespace bo3z
{
	inline constexpr auto kVersion = "0.2.0";

	// Forms read from the player's own Skyrim and from CurseOfAlduin.esp.
	namespace Forms
	{
		bool                 Resolve();  // after data loaded
		RE::TESBoundObject*  Enemy(std::size_t a_index);
		RE::TESQuest*        IntroQuest();
		std::array<RE::TESQuest*, 3> MQ102();  // MQ102A (Hadvar), MQ102B (Ralof), MQ102
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
		inline gen::Sound    Brew(int a_perk) { return static_cast<gen::Sound>(static_cast<int>(gen::Sound::brew_trollshide) + a_perk % 9); }  // (nine jingles: the later perks share them)
	}

	// Plugins.txt: make sure CurseOfAlduin.esp is enabled (called from DllMain, before the game reads it).
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
		std::uint32_t PerkBits();
		bool PerkBit(int a_perk);  // the old flags' bit (the first nine perks)
		int  ActiveSlot();
		void SetActiveSlot(int a_slot);
		void Guard();           // every tick: puts the mod's values back if something wiped them (see Forms.cpp)
		void ForgetSnapshot();  // a load or the main menu: the next values are a different game's
		void OnWorldReady();
		void InstallCoSave();  // SKSE serialization: the values live in the co-save
		int  PerkStacks(int a_perk);
		int  StaminaBonus();  // extra stamina already given to this character
		void SetStaminaBonus(int a_n);  // blessings stack: how many of this one
		void SetPerkStacks(int a_perk, int a_n);
		int  DragonBond();               // the glider's dragon: 0 none, 1-3 the Echo Altar's bonds (kept for the whole game)
		void SetDragonBond(int a_bond);
		inline constexpr int kFlagInit = 0, kFlagReserveV2 = 4, kFlagStarted = 5, kFlagMoved = 11, kPerkShift = 14, kFlagPerksV2 = 23, kOldPerkBits = 9;  // bits: init | crew 1-3 | (old perks 4-10) | moved 11 | slot 12-13 | perks 14-22 | v2 23
	}

	namespace PowerUps
	{
		void Tick(float a_delta);
		void MaybeDrop(const RE::NiPoint3& a_at);
		bool InstaKill();
		float PointsMult();
		bool FireSale();
		float Timer(int a_kind);
		void  SetTimer(int a_kind, float a_seconds);
	}

	namespace Points
	{
		void Add(int a_points);
		void Refund(int a_points);
		bool Spend(int a_points);
		int  Get();
	}

	namespace Net { struct State; }

	namespace Director
	{
		void             Tick(float a_delta);
		void             Reset();
		bool             IsRoundUndead(RE::Actor* a_actor);
		float            HarmMult(RE::Actor* a_actor);
		bool  HarpyNear(const RE::NiPoint3& a_at, float a_r);  // a living harpy this close (the per-hit cap)
		std::vector<RE::ActorHandle> AliveHandles();
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
		bool             IsElite(RE::Actor* a_actor);
		bool             IsBoss(RE::Actor* a_actor);
		void             Noise(float a_amount);  // the night hears you (spells bursting, shouts, fights): enough draws a Volatile
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
		void             SkipToDawn(const char* a_banner);  // the clock jumps to first light (the night counts as survived)
		bool             MayRest(std::string& a_why);  // waiting / sleeping: not at night, and only the host's clock moves
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
		void Voice(std::optional<gen::Sound> a_s);                                     // the intro narrator (one line at a time; nullopt stops it)
		void At(gen::Sound a_s, RE::TESObjectREFR* a_ref, float a_vol = 1.0f);         // 3D, follows the reference
		void AtPoint(gen::Sound a_s, const RE::NiPoint3& a_at, float a_delay = 0.0f, float a_vol = 1.0f);
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
		void Tick(float a_delta);
		void StopAll();
		void BossMusic(bool a_on);
		void FreedMusic();
		void IntroMusic(bool a_on);  // the intro's music (our MUSC: the music slider applies, the hold's music waits)
		void FilmQuiet(bool a_on);   // Skyrim's own sound (world, voices, its music) silent while the intro film plays
		void SetMenuMusic();
		void MenuUiSounds(bool a_on);  // the main menu's own focus/accept/back sounds play ours while it is up
		inline constexpr std::uint64_t kLoopCharge = 1, kLoopRite = 2, kLoopNight = 3, kLoopMenu = 4, kLoopShrine = 5;
	}

	namespace Undead
	{
		enum class Type { Shambler, Runner, Crawler, Wailer, Bloater, Stalker, Reaper, Necro, Wyrm };
		enum Cue { kCueNone = 0, kCueRoar, kCueScream, kCueGrab, kCueSwipeL, kCueSwipeR, kCueCrouch, kCueChannel };
		void  Assign(RE::Actor* a_actor, Type a_type);
		void  Forget(RE::Actor* a_actor);
		void  CueFor(RE::Actor* a_actor, int a_cue, float a_seconds);
		void  SetEyes(RE::Actor* a_actor, std::string_view a_id);  // its glowing eyes (meshes/CurseOfAlduin/eyes/<id>_glow.nif)
		Type  Roll(int a_round);         // which of the risen dead this one is
		float SpeedMult(Type a_type);
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
			float         pu[4];                 // power-up seconds left: insta-kill, double points, -, fire sale
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
		void        PowerUpTaken(int a_kind);
		void        AltarBusy(int a_town, bool a_on);  // our roll holds that altar (one roller at a time)
		void        EggDone(int a_town);  // a hidden quest finished: the party near that altar shares it
		std::vector<int> TakeRemotePowerUps();
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
		bool BarrowJarlsOpen();   // after Bleak Falls Barrow
		bool ElitesOpen();        // after Dragon Rising
		bool AlduinFallen();      // after Sovngarde
		bool GrimmvarOpen();      // after Alduin's Bane: the Undying Wyrm can be hunted
	}

	namespace Classes
	{
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
		struct Prize { std::string name; std::function<void()> give; };
		bool             RollGear(int a_cls, Prize* a_out = nullptr);      // a Spellsword weapon or shield / a Ranger's crossbow from the altar (material by freed cities)
		void             OnKill(bool a_bySummon, RE::Actor* a_dying);  // you (or your summon) killed a foe: what you hold trains, the wonder trial counts
		void             NoteHit(RE::Actor* a_target, RE::FormID a_source, bool a_power);  // your blow (what killed it; the wonders' on-hit)
		bool             OnCast(RE::FormID a_spell);   // the Tome of the Six Fires
		void             NoteCast(RE::FormID a_spell); // a summon spell: called up here if Skyrim's own didn't come
		bool             IsOwnSummon(RE::Actor* a_actor);
		void             TickFast(float a_delta);      // where the Subsuming Vortex's arrows come down
		int              RestockWonder();              // the Vortex's arrows topped up (quivers, Max Ammo)
		bool             WonderEarned();               // any class's wonder won (the Three Words follow)
		std::string      WonderLine();                 // the altar's line: your class wonder's trial
		std::vector<std::string> Progress(int& a_cls);  // the class guide's live page (paragraphs, <b> kept) and the class it is for
		void             ImbueWeapon();                // the Spellsword's Imbue: the held spell-weapon one material up, its element kept
		void             OnBlock(RE::Actor* a_attacker, bool a_spell);  // a blow blocked with a spellshield: reflect / stagger
		bool             RollHero(Prize* a_out = nullptr);                   // the Summoner's late Legendaries: a hero or an army (needs a Legendary summon)
		bool             RollSupport(Prize* a_out = nullptr);                // Destruction's altar side-pick: Ash Cloud, Cinder Storm, Charged Nova or Symmetry
		std::string      BoltLine();                   // the Ranger's crossbow mastery, for the altar ('' other classes)
		void             NextOutfit();                 // the altar's outfit picker: the next outfit (any class), worn
		std::string      OutfitName();                 // the one worn from the picker ('' none yet)
		int              Mastery(int a_index);     // kills its ladder has earned toward the next rung
		int              BowMastery(int a_bow);    // an altar bow's kills toward its next tier
		int              BowMasteryNeed(int a_bow);
		void             SpendBowMastery(int a_bow);
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
		void               Forget(int a_index);          // gone for good: unequipped, out of the Magic menu, reforging lost
		void               Replace(int a_old, int a_new);  // the new spell in the old one's place (and hands)
		RE::SpellItem*     Spell(int a_index);
		int                IndexOf(const RE::TESForm* a_form);
		int                IndexOfEffect(RE::FormID a_effect);  // the Coffer spell a magic effect belongs to (-1: none)
		bool               Damages(int a_index);         // one of its own effects takes health
		bool               Known(int a_index);
		int                Roll();                       // a spell you don't know yet (-1: you know them all)
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
		int                NextRung(int a_index);        // the same ladder one rung up (-1: the top)
		std::string_view   LadderName(int a_index);
		bool               Concentration(int a_index);
		std::uint32_t      ElementColor(int a_element);
		std::string_view   ElementName(int a_element);
		int                Elements();
		std::array<int, 2> Hands();                      // the Coffer spell in each hand (0 left, 1 right; -1 none)
		int                Level(int a_index);           // reforged 0..gen::kForgeMax
		void               SetLevel(int a_index, int a_level);
		std::vector<std::uint8_t> Levels();               // for the co-save
		void               SetLevels(const std::vector<std::uint8_t>& a_levels);
	}

	// The Echo Altar's bows (sheet: bows; src/Bows.inl): one carried at a time, its shots the element's spell
	namespace Bows
	{
		inline constexpr int kIdBase = 2000;   // HUD ids: a bow shows as kIdBase + its index
		int                       Count();
		std::string_view          Name(int a_i);
		std::uint32_t             Color(int a_i);
		std::string_view          Element(int a_i);   // its emblem (bw_<element>.png)
		int                       Level(int a_i);     // reforged 0..gen::kForgeMax
		std::string_view          TierName(int a_i, int a_tier);  // its attack at a reforge tier (0 as rolled .. 3)
		int                       Held();             // the bow in your hands (-1)
		int                       Carried();          // the bow you carry (-1)
		int                       IndexOf(const RE::TESForm* a_form);
		void                      Give(int a_i);      // in hand, the old one given back
		int                       Roll();             // one you don't carry, of the tiers the altars allow (-1)
		std::vector<std::uint8_t> Levels();           // for the co-save
		void                      SetLevels(const std::vector<std::uint8_t>& a_levels);
		bool                      IsStrikeSpell(const RE::MagicItem* a_item);
		int                       Restock();          // every quiver full (bow arrows, crossbow bolts): Max Ammo, the altar's quiver  // the mods' spells our bows and the Storm Staff loose (they never hurt you)
	}

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
		bool        Shelter();                    // townsfolk go indoors at night
		bool        Blood();                      // headshot blood bursts (MCM: Effects)
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
		void OnCast(RE::FormID a_spell);  // a spell you cast: one of your equipped shout's words starts its cooldown
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
		void Tick(float a_delta);                      // game thread
		bool OnButton(const RE::ButtonEvent* a_b);     // Activate held (revive); arrows switch whom you watch
		Life State();
		View Now();
		Out  Outgoing();                               // Net: what to tell the party
		void Heard(std::uint64_t a_from, const std::string& a_name, std::uint8_t a_life, const RE::NiPoint3& a_at, float a_left, std::int8_t a_wonder = -1, std::int8_t a_cls = -1, std::uint32_t a_credit = 0);
		int  FriendsShort(int a_town, int a_need, std::string* a_names = nullptr);  // (co-op altars) friends in the party with fewer than a_need own trials there
		int  FriendCount();                                // other players in the party now
		std::string WonderHolder(int a_wonder);         // the friend in the party holding that wonder weapon ('' none)
		void        SetWonder(int a_wonder);            // ours (-1 none): told to the party
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
		void  Tick(float a_delta);
		void  Reset();
		void  OnUndeadDeath(RE::Actor* a_actor);
		void  OnBossSlain();
		void  OnWave(bool a_cleared);       // the night's waves: one begins (false) or is cleared (true)
		void  OnChampionSlain(int a_town);  // a city's champion fell (Director)
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
		int   RelicOf(int a_town);                       // the city's relic weapon (gun index, -1)
		void  ClaimRelic(int a_town);                    // learn a freed city's Thu'um (the shrine's menu)
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
		void Tick(float a_delta);
		void Reset();
		void OnUndeadDeath(RE::Actor* a_actor);                // a kill near a barrow that wants the dead
		bool OnActivate(RE::TESObjectREFR* a_ref);             // the Reliquary's doors and pedestals (true: handled)
		bool StoneWaits(int a_town);                           // a freed city whose Echo Stone still holds its page
		bool Target(RE::PlayerCharacter* a_player, RE::NiPoint3& a_at, std::string& a_label);  // where the story wants you
		bool Strip(std::string& a_title, std::string& a_goal, float& a_frac);                  // a hunt under way (under the compass)
		void Allies(int a_kind, int a_count, float a_seconds, const RE::NiPoint3& a_at);       // the wonders' summons: 0 the barrow's bone warriors, 1 the Queen's spirit-wolves
		bool IsAlly(RE::Actor* a_actor);
		int  WonderHeld();                                     // the wonder weapon in your hands (-1)
		bool WordSilenced(int a_w);                            // Stage 4: 0 SLEN (flesh remade), 1 TIID (borrowed time), 2 VO (the curse undone)
		bool CurseBroken();                                    // all three words silenced (Stage 5's rewards open)
		void OnBearerSlain();                                  // a word's bearer fell (Director's champion with no city)
		bool AwardNextPart();                                  // the mini-boss's drop: the next missing wonder part (false: all yours)
		std::vector<RE::ActorHandle> Summon(std::string_view a_plugin, RE::FormID a_id, int a_count, const RE::NiPoint3& a_at);  // allies for the night (the hidden quests' summons)
		bool BoneDragon();                                     // Stage 5: the glider's dragon wears Alduin's bones
		bool EndlessNight();                                   // Stage 5: the nights don't end while the brazier burns
		int  NewGamePlus();                                    // Stage 5: cycles begun again at the Seat of the Awakened (0 = the first)
		bool WearsRobes();                                     // Stage 5: the Robes of the First Dragonborn are on
	}

	namespace Intro
	{
		void OnMainMenu();  // the next new game moves its character again
		bool NewGameThisSession();  // a new game was started from the main menu and its character made (not Continue / Load)
		void Install();                 // data loaded: set MQQuickstart
		void Tick();
		bool CrewScreenOpen();
		bool OnButton(const RE::ButtonEvent* a_event);  // true = consumed
		void OnRaceMenu(bool a_opening);  // the character creator opened or closed (whoever opened it)
	}

	namespace Gun
	{
		void                  Tick(float a_delta);
		void                  Reset();
		void                  TestKit();  // dev: every mod's attack spell (named by element), every bow and shout, the glider, 150,000 septims
		bool                  TestKitOn();         // a test character (UN_TestKit): the glider unlocked, its spells named by element
		void                  TestKitIfFlagged();  // a new character: the kit, if CurseOfAlduin-testkit.flag is beside the log
		void                  EyeBeam();  // Winterhold's hidden quest: a beam of the Eye's light through the dead ahead
		bool                  OnButton(const RE::ButtonEvent* a_event);
		std::pair<bool, bool> RecentHit(RE::FormID a_actor);  // {gun kill, headshot}
		bool                  Give(int a_gun);                // buy/box: put in a slot, equip it
		int                   LevelOf(int a_gun);             // its reforge level (-1: not carried)
		bool                  OnStaffHit(RE::Actor* a_target, RE::FormID a_source);  // a Falmer staff's own bolt struck it
		void                  OnStaffCast(RE::FormID a_spell);
		void                  SetLevelOf(int a_gun, int a_level);
		int                   TakeAway(int a_gun);            // out of its slot and your hands (its level returned; -1 not carried)
		bool                  IsStaffWonder(int a_gun);       // one of the four Falmer staffs
		void                  Console(const std::string& a_cmd);  // a console command, run as the player
		bool                  KnowsShout(int a_shout);        // the cities' shouts (sheet: shouts)
		void                  LearnShout(int a_shout);
		int                   ShoutOfSpell(RE::FormID a_spell, int& a_level);
		void                  StormHit(RE::Actor* a_target);
		void                  FangHit(RE::Actor* a_target, bool a_power);  // Alduin's Fang struck it (Stage 5)   // a blow you landed: Stormblast's charge goes off on it
		void                  Shout(int a_shout, int a_level);
		bool                  Owns(int a_gun);
		void                  RefillAmmo(int a_gun);
		bool                  Reforge();      // Atronach Reforge on the weapon in hand
		int                   ReforgeCost();  // -1 no weapon, 0 maxed
		int                   SlotCount();
		float                 AdsBlend();                     // 0..1, for movement and HUD
		void                  FxBlast(std::string_view a_name, const RE::NiPoint3& a_at, float a_delay = 0.0f);  // a no-damage Skyrim explosion (look only)
		void                  AreaHurt(const RE::NiPoint3& a_at, float a_radius, float a_share, std::string_view a_blast);  // the night's dead round a point lose a share of a whole zombie's health
		void                  BlackHole(const RE::NiPoint3& a_at, bool a_legendary);  // the Subsuming Vortex: a purple singularity pulls the dead in, then bursts
		void                  DevCast(const std::string& a_mode);  // dev: 'cast tap|full|ult|orb|hand' with a spell in hand
	}

	namespace Shops
	{
		inline constexpr int kReelPass = 40;          // spells the roll's reel runs past before it stops on the one won
		float ReelPos(float a_k);                      // where the reel stands at spin progress 0..1 (eased to a stop)
		int   ReelSpell(int a_target, float a_pos, int a_slot);  // the spell in a slot of the reel (0 = the front)
		void  BeforeLoad();  // a save is loading over this world: our hand-made map-marker records come off first
		std::vector<std::uint8_t> RollState();                    // each altar's rolls tonight (the co-save)
		void  SetRollState(const std::vector<std::uint8_t>& a_s);
		void  HeardBusy(int a_town, const std::string& a_who, bool a_on);  // a friend's roll holds (or frees) that altar
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
		int         TownOfRef(RE::TESObjectREFR* a_ref);
		bool        StationTarget(int a_town, RE::NiPoint3& a_at);  // where to head for the town's station, in your current world (a walled city from outside: its gate)
		bool        Discovered(int a_town);
	}

	namespace Perks
	{
		void OnEffect(RE::FormID a_mgef);
		bool Grant(int a_perk);    // bought: applied at once (one at a time)
		void LoseAll();            // death takes them all
		void OnZombieHit(RE::Actor* a_zombie);  // a round zombie hit the player
		float SinceZombieHit(RE::FormID a_zombie);  // seconds since this one's blow last landed (large: never)
		void OnCast(int a_shots);  // Stormcrown
		void Tick();
		bool SelfRevive();         // Quick Revive held: a killing blow is a fall instead
		void SelfHit();            // your own spell hit you: what it took is given back
		void RearmAfterLoad();     // a moment after a load: the first-person hands rebuilt
		void DamagePlayer(float a_amount);
		void Dodged(float a_seconds);  // a dodge: the night's blows miss you this long  // the night's blows on the player (a killing one caught by the tonic / the party's fall)
		float ManaMult();          // magicka cost (tomes)
		float RegenMult();  // magicka returns faster (Hawkeye, stacked)
		float ChargeMult();        // Quicksilver: charge time and cooldown of signature moves (x)
		float FireDelayMult();
		float DamageMult();
		float HeadMult();
		std::vector<int> Menu();          // the altar's perks for your class: the shared ones, then your class's
		int   MenuAt(int a_pos);
		bool  Live(int a_perk);           // carried, and (a class perk) you are of its class
		float DrawSpeedMult();            // Eagle-Eye Draught
		float SwingSpeedMult();           // Steel-Wind Ale
		float WardMult();                 // Dawnwatch Tonic (bow or crossbow in hand)
		bool  SummonMight();              // Bonebinder's Brew
		void  OnClassKill(int a_how, RE::Actor* a_dying);  // 0 summon, 1 Destruction spell, 2 bow/crossbow, 3 melee
		void  OnMeleeHit();               // Bladesong Mead
	}

	namespace Fantasy
	{
		void Inject();  // the High Fantasy Pack's creatures into Skyrim's wilderness and bandit encounters (data loaded)
	}

	namespace Movement
	{
		bool Dodging();  // a dodge roll under way
		bool Gliding();  // on the dragon glider (or just landed from a dive)

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
		void  OnHurt();                          // (the Nightingale's coins)
		void  OnUndeadDeath(RE::Actor* a_actor); // (marked ground; Arkay's Mercy)
		void  OnBossSlain(int a_town);           // its named foe fell (Director's champion, town -10 - city)
		bool  OnSpellCast(RE::FormID a_spell);   // a summon's power, the Eye's beam (true: ours)
		void  Heard(int a_town);                 // a friend finished that city's quest
		std::vector<std::string> Whispers(int a_town);  // the altar's hints: a line more for each step found
		float DamageMult();                      // Vaermina's Gift
		bool  ShadowStep();                      // the Nightingale's Debt: a dodge hides you
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
			std::string waveTheme;          // a themed wave's name ("GOBLIN RAID"), else empty
			std::uint32_t waveThemeColor{ 0 };
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
			float       headBurstT{ 0 };  // a headshot kill's white flash at the crosshair
			float       lightningT{ 0 }, lightningI{ 0 };  // the sky's flash (seconds left, strength)
			bool        eggDark{ false };                 // a hidden quest's dark (Riften's, Dawnstar's dream)
			std::vector<std::string> whispers;            // the altar's hidden quest: its whispers (empty: none yet)
			float       nightProgress{ -1 };              // 0..1 through the night's waves (-1: not fighting)
			std::string styleTag;         // a style bonus ("+40 MULTI-KILL") by the septims
			float       styleTime{ 0 };
			std::uint32_t perks{ 0 };    // bitmask
			std::array<int, 32> perkStacks{};  // how many of each blessing (they stack)
			int         crew{ -1 };
			bool        crewScreen{ false };
			int         crewHighlight{ 0 };
			float       countdown{ 0 };  // seconds until the next night starts (0 = none)
			float       hp{ 0 }, hpMax{ 0 }, mp{ 0 }, mpMax{ 0 }, sp{ 0 }, spMax{ 0 };
			float       hurtTime{ 0 };   // red vignette after taking a hit
			float       noise{ 0 };      // how loud you've been tonight (0..1: full, a Volatile hears you)
			bool        perkMenu{ false };
			std::string bossName;  // empty = no boss
			float       bossHp{ 0 };
			bool        bossEnraged{ false };
			int         perkSel{ 0 };
			int         altarFocus{ 0 }, chooseFocus{ 0 };
			// the roll's beams: from each column's rune stone into the heart stone (screen 0..1), how many are lit (0..6)
			std::array<float, 12> beamFrom{};
			float       beamToX{ 0 }, beamToY{ 0 };
			int         beamLit{ 0 };
			float       beamGrow{ 0 };  // how far the newest beam has reached from its stone toward the heart (0..1)  // the altar's focused row (Shops Row) and, a third spell won, the choice in focus
			// the shrine: its roll (0 idle, 1 charging, 2 spinning, 3 offering), progress 0..1, the spell shown, the price; a Thu'um to learn there
			int         rollPhase{ 0 }, rollGun{ -1 }, rollCost{ 0 }, shrineShout{ -1 };
			int         dragonBond{ 0 };   // the glider's dragon: its bond (0-3)
			bool        bondLocked{ false };
			std::vector<std::array<float, 3>> motes;  // (Noah: the dead give up their essence) screen x, y (0-1) where a kill fell, its age in seconds
			int         freedCount{ 0 }, forgeAt{ 3 }, legendAt{ 6 };  // Echo Altars freed; the reforge and the Legendary roll open at these
			// the altar's trials (the city still held): which are done, the one under way, the controller's pick, each with tonight's numbers
			bool        chooseOn{ false };   // the altar: a third spell won - which of your two does it replace?
			int         chooseNew{ -1 }, chooseA{ -1 }, chooseB{ -1 }, chooseRefund{ 0 };  // (Arcana indices)
			bool        trialsOn{ false };
			int         trialDone{ 0 }, trialActive{ 0 }, trialSel{ 0 };
			std::array<std::string, 5> trialDesc;
			std::string trialCity, trialBoss, trialBossLine;
			int         bowHeld{ -1 };    // the altar's bow in hand (Bows index), and how far it is drawn
			float       bowDraw{ 0 };
			bool        rollBow{ false };  // the altar's roll is the bow wheel
			std::string rollPrize;  // a class row's win, landed (its name over the altar in place of a spell's)
			int         bowRollCost{ 0 }, bowCarried{ -1 };
			float       questTimer{ -1 };  // the trial under way: seconds left on its clock (-1 none)
			bool        questWarn{ false };  // ...and it is in danger (time short, health low)
			int         rollTarget{ -1 };              // the spell the roll will land on (Arcana index), for the reel
			float       rollT{ 0 };
			float       pulseT{ 0 }, pulseX{ 0.5f }, pulseY{ 0.5f };  // a freed shrine's wave (seconds left, its screen spot)
			bool        pulseOn{ false };
			bool        rollOnScreen{ false };          // the shrine's crown is in view: the roll is drawn over it
			float       rollScreenX{ 0 }, rollScreenY{ 0 }, rollDist{ 0 };
			int         nightEvent{ -1 };   // gen::kEvents index while it runs
			std::string questCity, questTitle, questGoal;  // the city's current curse quest (empty = none)
			float       questFrac{ 0 };
			int         questStep{ 0 };
			std::string sanctuary;         // in a freed city at night: its name
			std::string loreTitle, loreText;
			float       loreTime{ 0 }, loreTotal{ 0 };
			// (Noah: no Word Walls) the altar's heart-stone teaching its city's Thu'um: the three words rise from its runes one by one
			std::string revealW[3], revealT[3], revealName;
			bool        skipHint{ false };  // Helgen can be skipped now: its line shows (Intro)
			float       skipFrac{ 0 };      // how far the hold has come
			float       revealTime{ 0 };
			std::uint32_t revealColor{ 0 };
			float       wardFrac{ 0 };     // kindling a ward-fire
			float       nukeLeft{ 0 }, nukeFlash{ 0 };
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
			std::string lootTitle, lootNote;
			std::vector<LootItem> loot;
			int         lootSel{ 0 };
			std::uint32_t objColor{ 0xD6B260 };
			float       questNoteTime{ 0 };
			// weapon slots (2, or 3 with the brew): the weapon in each (gen::kGuns index, -1 empty), its Reforge level
			int                slotCount{ 2 };
			std::array<int, 3> slotGun{ -1, -1, -1 };
			std::array<int, 3> slotLevel{ 0, 0, 0 };
			int                activeSlot{ -1 };
			int                gunIndex{ -1 };
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
		void        Essence(const RE::NiPoint3& a_at);  // a kill's essence flies from the body to your count
		void        Lore(std::string a_title, std::string a_text, float a_seconds = 14.0f);
		void        WordReveal(int a_shout);
		bool        PadLast();                                // the last input was a controller (prompts follow it)
		void        NotePad();                                // a stick moved: the player is on a controller
		std::string PadWord(std::string_view a_xbox);         // a pad button's name as the player's pad prints it (A -> Cross with PlayStation icons)  // the altar's runes give up a shout's three words (Curse::ClaimRelic)  // a scroll of the Curse
		void        Install();
		void        OnClick();  // a left click (menus: the party panel's buttons)
		void        OnMouseMove(float a_dx, float a_dy);  // raw mouse movement (the menu cursor never sticks)
		void        OnMenuButton(const RE::ButtonEvent* a_b);  // main menu: pad / arrow keys over our words
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
}
