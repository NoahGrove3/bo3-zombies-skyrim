#include "BO3Z.h"

namespace bo3z
{
	namespace
	{
		std::array<RE::TESBoundObject*, gen::kEnemies.size()>            g_enemies{};
		std::array<RE::BGSSoundDescriptorForm*, gen::kSounds.size()>     g_sounds{};
		RE::TESQuest*                                                     g_intro = nullptr;

		RE::TESForm* Lookup(const gen::FormRef& a_ref)
		{
			auto* form = RE::TESDataHandler::GetSingleton()->LookupForm(a_ref.localId, a_ref.plugin);
			if (!form) {
				logger::error("missing form {} {:06X} ({})", a_ref.plugin, a_ref.localId, a_ref.editorId);
			}
			return form;
		}
	}

	namespace Forms
	{
		bool Resolve()
		{
			bool ok = true;
			for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
				auto* f = Lookup(gen::kEnemies[i].form);
				g_enemies[i] = f ? f->As<RE::TESBoundObject>() : nullptr;
				if (g_enemies[i]) {
					logger::info("enemy {} = {:08X} {}", gen::kEnemies[i].id, f->GetFormID(), gen::kEnemies[i].form.editorId);
				} else {
					ok = false;
				}
			}
			for (std::size_t i = 0; i < gen::kSounds.size(); ++i) {
				auto* f = Lookup(gen::kSounds[i]);
				g_sounds[i] = f ? f->As<RE::BGSSoundDescriptorForm>() : nullptr;
			}
			auto* q = Lookup(gen::kIntroQuest);
			g_intro = q ? q->As<RE::TESQuest>() : nullptr;
			ok = ok && g_intro;
			return ok;
		}

		RE::TESBoundObject* Enemy(std::size_t a_index) { return a_index < g_enemies.size() ? g_enemies[a_index] : nullptr; }
		RE::TESQuest*       IntroQuest() { return g_intro; }

		void Play(gen::Sound a_sound)
		{
			const auto i = static_cast<std::size_t>(a_sound);
			if (i < g_sounds.size() && g_sounds[i]) {
				RE::BSAudioManager::GetSingleton()->Play(static_cast<RE::BSISoundDescriptor*>(g_sounds[i]));
			}
		}
	}

	namespace Save
	{
		int Get(int a_av)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p ? static_cast<int>(std::lround(p->AsActorValueOwner()->GetActorValue(static_cast<RE::ActorValue>(a_av)))) : 0;
		}

		void Set(int a_av, int a_value)
		{
			if (auto* p = RE::PlayerCharacter::GetSingleton()) {
				p->AsActorValueOwner()->SetActorValue(static_cast<RE::ActorValue>(a_av), static_cast<float>(a_value));
			}
		}

		void OnWorldReady()
		{
			if (Get(gen::kAV_initialized) == 0) {
				Set(gen::kAV_initialized, 1);
				Set(gen::kAV_round, 0);
				Set(gen::kAV_points, gen::kPoints_start);
				Set(gen::kAV_clip, gen::kGun_m1911.clip);
				Set(gen::kAV_reserve, gen::kGun_m1911.reserve);
				logger::info("start kit: {} points, {} {}/{}", gen::kPoints_start, gen::kGun_m1911.label, gen::kGun_m1911.clip, gen::kGun_m1911.reserve);
				Hud::Banner("Press G for your M1911", 6.0f);
			}
			logger::info("world ready: round {} cleared, {} points", Get(gen::kAV_round), Get(gen::kAV_points));
		}
	}

	namespace Points
	{
		int Get() { return Save::Get(gen::kAV_points); }

		void Add(int a_points)
		{
			if (a_points <= 0) {
				return;
			}
			Save::Set(gen::kAV_points, Get() + a_points);
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.lastGain = a_points;
			h.gainTime = 1.0f;
		}
	}
}
