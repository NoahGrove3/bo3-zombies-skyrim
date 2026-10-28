#pragma once
// (Noah: "a performance pass - it needs to be incredibly optimized") what each of our systems costs a frame: every tick's
// pieces timed, and once a minute the log shows each one's average and worst (ms) - so the next pass works from numbers
#include <array>
#include <atomic>
#include <chrono>
#include <string_view>
#include <psapi.h>

namespace bo3z::Perf
{
	enum Slot { kDirector, kGun, kShops, kClasses, kPerks, kParty, kCompass, kLoot, kMovement, kAudio, kIntro, kSave, kAnim, kHud, kSlots };
	inline constexpr std::array<std::string_view, kSlots> kNames{ "director", "gun", "shops", "classes", "perks", "party", "compass", "loot", "movement", "audio", "intro", "save", "anim", "hud" };
	struct Acc
	{
		std::atomic<std::uint64_t> ns{ 0 }, worst{ 0 }, n{ 0 };
	};
	inline std::array<Acc, kSlots> g_acc;
	inline void Add(Slot a_s, std::uint64_t a_ns)
	{
		auto& a = g_acc[a_s];
		a.ns.fetch_add(a_ns, std::memory_order_relaxed);
		a.n.fetch_add(1, std::memory_order_relaxed);
		std::uint64_t w = a.worst.load(std::memory_order_relaxed);
		while (a_ns > w && !a.worst.compare_exchange_weak(w, a_ns, std::memory_order_relaxed)) {}
	}
	struct Scope
	{
		Slot s;
		std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
		explicit Scope(Slot a_s) : s(a_s) {}
		~Scope() { Add(s, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count())); }
	};
	// once a minute (from the player's tick): each system's frame cost, then the counts start over
	inline void Report(std::uint64_t a_frames)
	{
		static auto last = std::chrono::steady_clock::now();
		static std::uint64_t frames0 = 0;
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::seconds(60)) return;
		const std::uint64_t frames = a_frames - frames0;
		frames0 = a_frames, last = now;
		std::string line;
		double total = 0;
		for (int i = 0; i < kSlots; ++i) {
			auto& a = g_acc[i];
			const double ms = a.ns.exchange(0) / 1e6, worst = a.worst.exchange(0) / 1e6;
			a.n.exchange(0);
			const double per = frames ? ms / static_cast<double>(frames) : 0.0;
			total += per;
			if (per >= 0.01 || worst >= 0.5) line += fmt::format(" {} {:.2f}/{:.1f}", kNames[i], per, worst);
		}
		// (the vanishing-dead hunt: every new body stopped loading, spells too) the game's memory beside its frame cost
		PROCESS_MEMORY_COUNTERS_EX pm{};
		pm.cb = sizeof(pm);
		MEMORYSTATUSEX ms{};
		ms.dwLength = sizeof(ms);
		const bool havePm = K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof(pm));
		GlobalMemoryStatusEx(&ms);
		const int highs = RE::ProcessLists::GetSingleton() ? static_cast<int>(RE::ProcessLists::GetSingleton()->highActorHandles.size()) : -1;
		logger::info("perf: {:.2f} ms a frame over {} frames (average/worst ms):{} | memory {} MB in use ({} MB peak), {} MB free of {} MB, {} actors in high process", total, frames, line,
			havePm ? pm.PrivateUsage >> 20 : 0, havePm ? pm.PeakWorkingSetSize >> 20 : 0, ms.ullAvailPhys >> 20, ms.ullTotalPhys >> 20, highs);
	}
}
