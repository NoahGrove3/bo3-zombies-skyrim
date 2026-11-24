#include "CurseOfAlduin.h"

// (Noah, 2026-11-24: "Anything that is floating should be fixed for the whole map so nothing looks broken") trees, plants,
// shrubs and rocks that Skyrim's own plugins (Update.esm above all, round Riverwood) place well above the ground they stand
// over: each one, as its cell loads, set down onto what is under it. "What is under it" is found by casting down from five
// points of its foot (its corners and its middle) through the world's collision - land, rocks, roads, buildings - so a plant
// on a rock or a ledge, a tree on a slope or a stone half over a cliff counts as standing; only something with nothing under
// any of its foot for 30 units or more is moved, and only by the least of those gaps (it comes to rest on its highest
// support). Water plants over water, markers, effects, ivy, clutter and anything made by people (signs, lanterns, banners,
// buildings) are never touched. Each ref is looked at once a game session; a move is kept in the save like any other.
namespace coa::Ground
{
	namespace
	{
		std::unordered_set<RE::FormID> g_seen;
		int                            g_moved = 0;
		std::map<std::string, int>     g_byFile;

		enum class Kind { None, Plant, Rock, Road };
		Kind Wanted(RE::TESObjectREFR* a_r, RE::TESBoundObject*& a_base)
		{
			auto* b = a_r->GetBaseObject();
			if (!b) return Kind::None;
			const auto ft = b->GetFormType();
			if (ft != RE::FormType::Tree && ft != RE::FormType::Flora && ft != RE::FormType::Static && ft != RE::FormType::MovableStatic) return Kind::None;
			auto* mdl = b->As<RE::TESModel>();
			std::string m = mdl && mdl->GetModel() ? mdl->GetModel() : "";
			std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (m.empty() || m.find("marker") != std::string::npos) return Kind::None;
			a_base = b->As<RE::TESBoundObject>();
			if (!a_base) return Kind::None;
			if (m.find("\\roads\\") != std::string::npos) return Kind::Road;   // (never moved: a road lies on the land as built - the first pass sank some)
			// the land's own pieces only: plants, trees, shrubs, logs, rocks
			const bool land = m.starts_with("landscape\\") || m.starts_with("plants\\") || m.find("\\plants\\") != std::string::npos || m.find("\\trees\\") != std::string::npos ||
			                  m.find("\\rocks\\") != std::string::npos || m.find("\\shrub") != std::string::npos;
			if (!land) return Kind::None;
			if (m.find("ivy") != std::string::npos || m.find("hanging") != std::string::npos || m.find("vine") != std::string::npos || m.find("waterfall") != std::string::npos ||
				m.find("\\cliffs\\") != std::string::npos || m.find("\\mountains\\") != std::string::npos || m.find("mountainslab") != std::string::npos ||
				m.find("lily") != std::string::npos || m.find("water") != std::string::npos)
				return Kind::None;   // (meant to hang, or too big to judge by a few points: the cliffs and mountain slabs)
			return m.find("\\rocks\\") != std::string::npos || m.find("log") != std::string::npos || m.find("stump") != std::string::npos || m.find("fallen") != std::string::npos ? Kind::Rock : Kind::Plant;
		}

		// the highest thing under a point, between a little below where it stands and 2000 units down (nullopt: nothing)
		std::optional<float> Under(RE::bhkWorld* a_world, const RE::NiPoint3& a_at)
		{
			const float s = RE::bhkWorld::GetWorldScale();
			const RE::NiPoint3 from{ a_at.x, a_at.y, a_at.z - 2.0f }, to{ a_at.x, a_at.y, a_at.z - 2000.0f };
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(from * s);
			pick.rayInput.to = RE::hkVector4(to * s);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
			{
				RE::BSReadLockGuard lock(a_world->worldLock);
				a_world->PickObject(pick);
			}
			if (!pick.rayOutput.HasHit()) return std::nullopt;
			return from.z + (to.z - from.z) * pick.rayOutput.hitFraction;
		}

		void Check(RE::TESObjectREFR* a_r, RE::TESBoundObject* a_base, Kind a_kind)
		{
			auto* cell = a_r->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			auto* o = a_r->Get3D();
			if (!world || !o) return;
			// its foot: the bound box's corners turned as it stands; the lowest of them, and five points across it
			const auto& bd = a_base->boundData;
			const float sc = a_r->GetScale();
			const RE::NiMatrix3& R = o->world.rotate;
			const auto pos = a_r->GetPosition();
			float foot = FLT_MAX;
			float minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX;
			// (2026-11-24, Noah's photos: ferns, trees and logs still in the air) a plant or a tree stands on its own origin - its
			// bound box reaches down into the ground by its roots and hid the gap; a rock, a log, a stump (or anything tipped over)
			// by the lowest corner of its box
			for (int c = 0; c < 8; ++c) {
				const RE::NiPoint3 l{ static_cast<float>((c & 1) ? bd.boundMax.x : bd.boundMin.x) * sc, static_cast<float>((c & 2) ? bd.boundMax.y : bd.boundMin.y) * sc,
					static_cast<float>((c & 4) ? bd.boundMax.z : bd.boundMin.z) * sc };
				const RE::NiPoint3 w = pos + R * l;
				foot = std::min(foot, w.z);
				minX = std::min(minX, w.x), maxX = std::max(maxX, w.x), minY = std::min(minY, w.y), maxY = std::max(maxY, w.y);
			}
			const auto  ang = a_r->GetAngle();
			if (a_kind == Kind::Plant && std::abs(ang.x) < 0.3f && std::abs(ang.y) < 0.3f) foot = pos.z;
			const float spanX = maxX - minX, spanY = maxY - minY;
			if (spanX > 3000.0f || spanY > 3000.0f || foot == FLT_MAX) return;   // (a whole hillside: not ours to judge)
			// water: a plant standing at the water's top over a deeper bottom is where it belongs
			if (const float wh = cell->GetExteriorWaterHeight(); wh > -1e8f && std::abs(foot - wh) < 40.0f) return;
			// five points of its foot: its middle and four round it (a plant's: within a short reach of its stem; a rock's: its corners pulled in)
			const float rx = a_kind == Kind::Plant ? std::min(spanX * 0.2f, 40.0f) : spanX * 0.2f, ry = a_kind == Kind::Plant ? std::min(spanY * 0.2f, 40.0f) : spanY * 0.2f;
			const float cx = a_kind == Kind::Plant ? pos.x : (minX + maxX) * 0.5f, cy = a_kind == Kind::Plant ? pos.y : (minY + maxY) * 0.5f;
			const RE::NiPoint3 pts[5] = { { cx, cy, foot }, { cx - rx, cy - ry, foot }, { cx + rx, cy - ry, foot }, { cx - rx, cy + ry, foot }, { cx + rx, cy + ry, foot } };
			float gap = FLT_MAX;
			int   hits = 0;
			for (const auto& p : pts) {
				if (const auto u = Under(world, p)) {
					gap = std::min(gap, p.z - *u);
					++hits;
				}
			}
			if (!hits || gap < 25.0f || gap > 1800.0f) return;
			const float sink = std::min(6.0f, gap * 0.05f);   // (set a hair into the ground, never resting on its very edge)
			RE::NiPoint3 to = pos;
			to.z -= gap + sink;
			a_r->SetPosition(to);
			a_r->Update3DPosition(true);   // (its model and its collision moved with it now, not at the next load)
			auto* made = a_r->GetFile(0);
			const std::string mf = made ? std::string(made->GetFilename()) : std::string("(made in game)");
			++g_byFile[mf];
			if (g_moved++ < 30) {
				auto* mdl = a_base->As<RE::TESModel>();
				logger::info("ground: {:08X} '{}' set down {:.0f} onto the ground at ({:.0f},{:.0f}) - placed by {}", a_r->GetFormID(), mdl ? mdl->GetModel() : "?", gap + sink, pos.x, pos.y, mf);
			}
		}
	}

	void Reset() { g_seen.clear(); }

	void Tick(float a_delta)
	{
		static float acc = 0;
		if ((acc += a_delta) < 1.0f) return;
		acc = 0;
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!p || !tes || !p->Is3DLoaded() || !p->GetParentCell() || p->GetParentCell()->IsInteriorCell()) return;
		int looked = 0;
		const int movedBefore = g_moved;
		tes->ForEachReferenceInRange(p, 12000.0f, [&](RE::TESObjectREFR* r) {
			if (!r || looked >= 400) return RE::BSContainer::ForEachResult::kContinue;
			if (r->IsDisabled() || r->IsDeleted() || !r->Is3DLoaded()) return RE::BSContainer::ForEachResult::kContinue;   // (its 3D in first: looked at once it shows)
			if (!g_seen.insert(r->GetFormID()).second) return RE::BSContainer::ForEachResult::kContinue;
			++looked;
			RE::TESBoundObject* base = nullptr;
			const Kind kind = Wanted(r, base);
			if (kind == Kind::Road) {   // (one the first pass sank: back where it was built)
				if (auto* sp = r->extraList.GetByType<RE::ExtraStartingPosition>()) {
					const auto now = r->GetPosition(), at = sp->startPosition.pos;
					if (std::abs(now.x - at.x) < 1.0f && std::abs(now.y - at.y) < 1.0f && at.z - now.z > 20.0f && at.z - now.z < 500.0f) {
						r->SetPosition(at);
						r->Update3DPosition(true);
						logger::info("ground: road piece {:08X} put back where it was built (+{:.0f})", r->GetFormID(), at.z - now.z);
					}
				}
			} else if (kind != Kind::None) {
				Check(r, base, kind);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (g_moved > movedBefore) {
			std::string files;
			for (auto& [f, n] : g_byFile) files += fmt::format("{} {}, ", f, n);
			logger::info("ground: {} set down so far this session ({})", g_moved, files);
		}
	}
}
