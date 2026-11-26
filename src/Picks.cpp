#include "CurseOfAlduin.h"
#include <mutex>
#include "WorkshopPicks.h"

// (Noah, 2026-11-26: "make the volume on all of the weapon sound effects 20% ... I assigned all the sound effects I want to use
// for the weapons. Can you finalize these?" - then "all my picks at once" for the in-game test) what he chose in the workshop doc,
// played in the game: each ability's own sound (Skyrim's, the DLCs', Apocalypse's, Mysticism's or ours) at 20%, Skyrim's own
// weapon sounds at 20% for every weapon, his hit feel by weight, a power attack's kill thrown, and the trials' new sounds.
namespace coa::Picks
{
	namespace
	{
		RE::BGSSoundDescriptorForm* Form(const Snd& a_s)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh && a_s.plugin ? dh->LookupForm<RE::BGSSoundDescriptorForm>(a_s.id, a_s.plugin) : nullptr;
		}
		// one sound where it happens: carried on you, softer the further off it went off (as Gun::BlastSound)
		void PlayOne(const Snd& a_s, const RE::NiPoint3& a_at, float a_vol)
		{
			auto go = [s = a_s, at = a_at, a_vol]() {
				auto* p = RE::PlayerCharacter::GetSingleton();
				if (!p) return;
				const float d = p->GetPosition().GetDistance(at);
				const float v = std::clamp(1.0f - (d - 350.0f) / 2400.0f, 0.25f, 1.0f) * s.gain * a_vol;
				if (s.ours >= 0) {
					Audio::Mine(static_cast<gen::Sound>(s.ours), at, std::min(1.0f, v));
				} else if (auto* f = Form(s)) {
					Audio::Game(f->GetFormID(), p, std::min(1.0f, v));
				}
			};
			if (a_s.delay > 0.01f) Tasks::Later(a_s.delay, go);
			else go();
		}
		const CardSnd* Find(std::string_view a_card, std::string_view a_slot)
		{
			for (const auto& c : kCardSnd) if (a_card == c.card && a_slot == c.slot) return &c;
			return nullptr;
		}
		const char* CardOf(std::string_view a_owner)
		{
			for (const auto& [o, c] : kOwnerCard) if (a_owner == o) return c;
			return nullptr;
		}
		// a moment's sound once per beat (an ability's parts, several dead struck at once, all call it in the same moment)
		bool Fresh(std::string_view a_key)
		{
			static std::unordered_map<std::string, std::chrono::steady_clock::time_point> last;
			const auto now = std::chrono::steady_clock::now();
			auto& t = last[std::string(a_key)];
			if (now - t < std::chrono::milliseconds(110)) return false;
			t = now;
			return true;
		}
		int  g_crowd = 0;
		float g_crowdT = 0;

		// (Noah, 2026-11-26: "the enemies kind of teleport around when I'm hitting them, and it glitches my character out" -
		// the hit's hold slowed the whole world's clock, physics and all, every blow) the hold is now only the striker's and
		// the struck one's animation: theirs alone stands still for its moment (as Precision's hit-stop does); the world, its
		// physics and everyone else go on at full speed
		struct Hold { RE::FormID id; std::chrono::steady_clock::time_point until; };
		std::vector<Hold> g_hold;
		std::mutex g_holdLock;
		float HeldScale(RE::Actor* a_actor)
		{
			if (!a_actor) return 1.0f;
			std::lock_guard lk(g_holdLock);
			if (g_hold.empty()) return 1.0f;
			const auto now = std::chrono::steady_clock::now();
			std::erase_if(g_hold, [&](const Hold& h) { return h.until <= now; });
			for (const auto& h : g_hold) if (h.id == a_actor->GetFormID()) return 0.05f;
			return 1.0f;
		}
		template <int N>
		struct HoldAnim
		{
			static void thunk(RE::Actor* a_this, float a_delta) { func(a_this, a_delta * HeldScale(a_this)); }
			static inline REL::Relocation<decltype(thunk)> func;
		};
		void HoldFor(RE::Actor* a_actor, float a_seconds)
		{
			if (!a_actor || a_seconds <= 0.0f) return;
			std::lock_guard lk(g_holdLock);
			const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(static_cast<long long>(a_seconds * 1.0e6f));
			for (auto& h : g_hold) if (h.id == a_actor->GetFormID()) { h.until = std::max(h.until, until); return; }
			g_hold.push_back({ a_actor->GetFormID(), until });
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> pc{ RE::VTABLE_PlayerCharacter[0] };
		HoldAnim<0>::func = pc.write_vfunc(0x7D, HoldAnim<0>::thunk);
		REL::Relocation<std::uintptr_t> ch{ RE::VTABLE_Character[0] };
		HoldAnim<1>::func = ch.write_vfunc(0x7D, HoldAnim<1>::thunk);
		logger::info("picks: the hit's hold on the striker's and the struck one's animation only (UpdateAnimation hooked)");
	}

	void OnDataLoaded()
	{
		// (Noah: "Every weapon in the game") Skyrim's own draw, sheathe, swing and impact sounds at 20% - about 14 dB down
		const auto down = static_cast<std::uint16_t>(std::lround(-20.0 * std::log10(kWeaponVol) * 100.0));
		int n = 0;
		auto* dh = RE::TESDataHandler::GetSingleton();
		for (const auto& [plugin, id] : kQuietSndr) {
			auto* f = dh ? dh->LookupForm<RE::BGSSoundDescriptorForm>(id, plugin) : nullptr;
			auto* sd = f ? skyrim_cast<RE::BGSStandardSoundDef*>(f->soundDescriptor) : nullptr;
			if (!sd) continue;
			sd->soundCharacteristics.staticAttenuation = static_cast<std::uint16_t>(std::min<int>(10000, sd->soundCharacteristics.staticAttenuation + down));
			++n;
		}
		logger::info("picks: {} of Skyrim's weapon sounds {:.1f} dB down (20%); {} ability sounds, {} trial sounds of Noah's", n, down / 100.0, std::size(kCardSnd), std::size(kTrialSnd));
	}

	bool Card(std::string_view a_card, std::string_view a_slot, const RE::NiPoint3& a_at)
	{
		const auto* c = Find(a_card, a_slot);
		if (!c) return false;
		if (Fresh(std::string(a_card) + ":" + std::string(a_slot))) {
			for (int i = 0; i < c->n; ++i) PlayOne(c->s[i], a_at, 1.0f);
		}
		return true;
	}
	bool Owner(std::string_view a_owner, std::string_view a_slot, const RE::NiPoint3& a_at)
	{
		const char* card = CardOf(a_owner);
		return card && Card(card, a_slot, a_at);
	}
	bool OwnerHasSound(std::string_view a_owner)
	{
		const char* card = CardOf(a_owner);
		return card && Find(card, "impact");
	}

	bool Trial(std::string_view a_event, const RE::NiPoint3* a_at)
	{
		for (const auto& c : kTrialSnd) {
			if (a_event != c.card) continue;
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return true;
			for (int i = 0; i < c.n; ++i) PlayOne(c.s[i], a_at ? *a_at : p->GetPosition(), 1.0f);
			return true;
		}
		return false;
	}
	bool HasTrial(std::string_view a_event)
	{
		for (const auto& c : kTrialSnd) if (a_event == c.card) return true;
		return false;
	}
	void TrialLoop(bool a_on)
	{
		static RE::BSSoundHandle h;
		static bool on = false;
		const CardSnd* c = nullptr;
		for (const auto& t : kTrialSnd) if (std::string_view("inside") == t.card) c = &t;
		if (!c) return;
		if (a_on && !on) {
			auto* f = Form(c->s[0]);
			auto* am = RE::BSAudioManager::GetSingleton();
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (f && am && p && am->GetSoundHandle(h, static_cast<RE::BSISoundDescriptor*>(f))) {
				h.SetPosition(p->GetPosition());
				if (auto* node = p->Get3D()) h.SetObjectToFollow(node);
				h.SetVolume(0.8f);
				h.Play();
				on = true;
			}
		} else if (!a_on && on) {
			h.FadeOutAndRelease(800);
			on = false;
		}
	}

	// (the workshop's Hit feel) a melee blow on one of the dead: the picture holds for a beat by the weapon's weight - longer on a
	// power attack, which also shakes the view; a power attack that kills throws the body
	void OnMeleeHit(RE::Actor* a_target, const RE::TESForm* a_source, bool a_heavy)
	{
		auto* w = a_source ? a_source->As<RE::TESObjectWEAP>() : nullptr;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!w || !p || !a_target || w->IsBow() || w->IsCrossbow() || w->IsStaff()) return;
		int k = 1;
		switch (w->GetWeaponType()) {
		case RE::WEAPON_TYPE::kHandToHandMelee:
		case RE::WEAPON_TYPE::kOneHandDagger: k = 0; break;
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe: k = 2; break;
		default: k = 1; break;
		}
		static auto last = std::chrono::steady_clock::time_point{};
		const auto  now = std::chrono::steady_clock::now();
		if (now - last > std::chrono::milliseconds(140)) {
			last = now;
			if (const float ms = a_heavy ? kStopH[k] : kStopL[k]; ms > 0.5f && !Net::InLobby()) HoldFor(p, ms / 1000.0f), HoldFor(a_target, ms / 1000.0f);
			if (a_heavy && kShakeH[k] > 0.5f && Settings::ScreenShake()) RE::ShakeCamera(kShakeH[k] / 100.0f * 0.6f, p->GetPosition(), 0.25f);
		}
		// (The dead: "Heavies throw them") a power attack's killing blow hurls the body back - after the night's own damage for the
		// blow has landed (next on the main thread), before its death is dealt
		if (kHeavyThrows && a_heavy && Director::IsRoundUndead(a_target)) {
			Tasks::Add([h = a_target->GetHandle()] {
				auto a = h.get();
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (!a || !pl || a->IsDeleted()) return;
				if (a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) > 0.0f && !a->IsDead()) return;
				if (auto* proc = pl->GetActorRuntimeData().currentProcess) {
					RE::NiPoint3 away = a->GetPosition() - pl->GetPosition();
					away.z = 0;
					if (away.Length() < 1.0f) away = { 0, 1, 0 };
					away.Unitize();
					proc->KnockExplosion(a.get(), a->GetPosition() - away * 60.0f + RE::NiPoint3{ 0, 0, 55.0f }, 5.0f * Gun::kRagdoll);   // (from waist height behind, as every kill's fall - Director - but three times as hard; halved with all the rest)
				}
			});
		}
	}

	int Crowd() { return g_crowd; }
	void Tick(float a_delta)
	{
		if ((g_crowdT -= a_delta) > 0) return;
		g_crowdT = 0.5f;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		int n = 0;
		const auto pp = p->GetPosition();
		for (auto h : Director::AliveHandles()) if (auto a = h.get(); a && !a->IsDead() && a->GetPosition().GetDistance(pp) < 3000.0f) ++n;
		g_crowd = n;
		// (Noah, 2026-11-26: "Some enemies float in the sky, alive and unkillable") one of the night's dead held high over the land
		// for four seconds running - not a dragon, not a body flying from a blow - set down on the ground under it, awake
		{
			static std::unordered_map<RE::FormID, float> high;
			std::unordered_map<RE::FormID, float> now;
			for (auto h : Director::AliveHandles()) {
				auto a = h.get();
				if (!a || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() || a->HasKeywordString("ActorTypeDragon") || a->IsInRagdollState()) continue;
				auto* cell = a->GetParentCell();
				if (!cell || cell->IsInteriorCell()) continue;
				auto at = a->GetPosition();
				float land = at.z;
				if (!RE::TES::GetSingleton()->GetLandHeight(at, land) || at.z - land < 600.0f) continue;
				const float t = (high.contains(a->GetFormID()) ? high[a->GetFormID()] : 0.0f) + 0.5f;
				if (t >= 4.0f) {
					at.z = Director::GroundAt({ at.x, at.y, land }, 200.0f).value_or(land) + 10.0f;
					a->SetPosition(at, true);
					a->EnableAI(true);
					logger::info("night: {} was held {:.0f} over the land - set down", a->GetName(), a->GetPosition().z - land);
					continue;
				}
				now[a->GetFormID()] = t;
			}
			high = std::move(now);
		}
		// (Noah, 2026-11-26: "Scale dragons to 50%") each dragon about at half the size it was made at - the bosses, the
		// mini-bosses, the three wyrms and the Echo of Alduin (Diverse Dragons' all) and Skyrim's own. Told by its own scale: one
		// at its full size or more (100+) is halved; one already under it (halved here, kept so in the save) is left be
		if (auto* pl = RE::ProcessLists::GetSingleton()) {
			for (auto& h : pl->highActorHandles) {
				auto a = h.get();
				if (!a || a->IsDead() || !a->HasKeywordString("ActorTypeDragon")) continue;
				const auto rs = a->GetReferenceRuntimeData().refScale;
				if (rs < 100) continue;
				a->SetScale(rs / 100.0f * kDragonScale);
				logger::info("dragons: {} at half its size ({:.2f} -> {:.2f})", a->GetName(), rs / 100.0f, rs / 100.0f * kDragonScale);
			}
		}
	}
}
