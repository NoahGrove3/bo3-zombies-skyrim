// (Noah: "hang them at the hip") the four class swords, sheathed at the hip, made on the player's own PC from Light Greatswords'
// files the first time the game starts with this build (the class robes once built here from Black Mage Armor SE are gone:
// each class wears its own set now - cut 2026-11-24)
#include "CurseOfAlduin.h"

#include <fstream>

namespace coa::ArmorForge
{
	namespace
	{
		std::vector<std::uint8_t> ReadGameFile(const std::string& a_path)
		{
			RE::BSResourceNiBinaryStream s(a_path);
			if (!s.good() || !s.stream) return {};
			std::vector<std::uint8_t> buf(s.stream->totalSize);
			if (buf.empty() || !s.read(reinterpret_cast<char*>(buf.data()), static_cast<std::uint32_t>(buf.size()))) return {};
			return buf;
		}
	}

	// (Noah: "when putting away my sword ... the sword was on my back but the animation was putting it on my side" - "hang
	// them at the hip") the four class swords are Light Greatswords' models, each naming the back as its sheath (its Prn
	// string): copied here from that mod with the sheath named the hip (WeaponSword) - the header's string table rewritten,
	// every block byte for byte - so Skyrim draws and sheathes them as the one-handed swords they are
	namespace
	{
		std::vector<std::uint8_t> HipPrn(const std::vector<std::uint8_t>& d)
		{
			auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, d.data() + o, 4); return v; };
			auto u16 = [&](std::size_t o) { std::uint16_t v; std::memcpy(&v, d.data() + o, 2); return v; };
			std::size_t o = 0;
			while (o < d.size() && d[o] != '\n') ++o;
			if (++o + 13 > d.size()) return {};
			o += 4 + 1;                                  // version, endian
			const auto user = u32(o), blocks = u32(o + 4);
			o += 8;
			if (user < 12) return {};
			const auto bs = u32(o);
			o += 4;
			if (bs != 100) return {};                    // (Skyrim SE's header only)
			for (int i = 0; i < 3 && o < d.size(); ++i) o += 1 + d[o];   // author, process and export scripts
			if (o + 2 > d.size()) return {};
			const auto types = u16(o);
			o += 2;
			for (std::uint32_t i = 0; i < types && o + 4 <= d.size(); ++i) o += 4 + u32(o);
			o += 2ull * blocks + 4ull * blocks;
			if (o + 8 > d.size()) return {};
			const auto count = u32(o);
			const std::size_t maxAt = o + 4;
			o += 8;
			std::vector<std::string> strs;
			std::size_t q = o;
			for (std::uint32_t i = 0; i < count; ++i) {
				if (q + 4 > d.size()) return {};
				const auto l = u32(q);
				if (q + 4 + l > d.size()) return {};
				strs.emplace_back(reinterpret_cast<const char*>(d.data() + q + 4), l);
				q += 4 + l;
			}
			bool found = false;
			std::uint32_t longest = 0;
			for (auto& x : strs) {
				if (x == "WeaponBack") x = "WeaponSword", found = true;
				longest = std::max<std::uint32_t>(longest, static_cast<std::uint32_t>(x.size()));
			}
			if (!found) return {};
			std::vector<std::uint8_t> out(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(o));
			std::memcpy(out.data() + maxAt, &longest, 4);
			for (auto& x : strs) {
				const auto l = static_cast<std::uint32_t>(x.size());
				out.insert(out.end(), reinterpret_cast<const std::uint8_t*>(&l), reinterpret_cast<const std::uint8_t*>(&l) + 4);
				out.insert(out.end(), x.begin(), x.end());
			}
			out.insert(out.end(), d.begin() + static_cast<std::ptrdiff_t>(q), d.end());
			return out;
		}
	}
	void BuildHipBlades()
	{
		const std::filesystem::path data = std::filesystem::current_path() / "Data";
		int made = 0, kept = 0;
		for (const char* m : { "silver", "stahlrim", "daedric", "nordic" }) {
			const auto out = data / "Meshes" / "CurseOfAlduin" / "hipblades" / (std::string(m) + ".nif");
			const auto src = ReadGameFile(std::string("meshes\\Light Greatswords\\") + m + "\\LightGreatsword.nif");
			if (src.empty()) { logger::warn("hip blades: Light Greatswords' {} sword not found - its class sword can't be made", m); continue; }
			const auto nif = HipPrn(src);
			if (nif.empty()) { logger::warn("hip blades: {}'s model couldn't be read", m); continue; }
			std::error_code ec;
			if (std::filesystem::exists(out, ec) && std::filesystem::file_size(out, ec) == nif.size()) { ++kept; continue; }
			std::filesystem::create_directories(out.parent_path(), ec);
			std::ofstream f(out, std::ios::binary);
			f.write(reinterpret_cast<const char*>(nif.data()), static_cast<std::streamsize>(nif.size()));
			++made;
		}
		logger::info("hip blades: {} made, {} already there (Light Greatswords' class swords, sheathed at the hip)", made, kept);
	}
}
