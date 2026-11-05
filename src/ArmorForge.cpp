// (Noah: Black Mage Armor SE, Light Greatswords and Vibrant Weapons - "make them required") the four class robes are built on
// the player's own PC from Black Mage Armor SE's files the first time the game starts with this build: none of its author's
// meshes or textures ship with The Curse of Alduin.
//
//   - its robe models (read from its archive through the game's own file system) copied for each class, only their colour
//     maps' folder changed, in place, to one of the same length: textures\BlackMage\ -> textures\UNarmor_A..D\ (the normal
//     maps stay its own) - byte for byte otherwise (rewritten models once drew no body)
//   - its colour maps dyed with the class's colour: the stitching, lacing, trims and buckles - what stands out lighter than the
//     cloth round it - lit up in the colour, the cloth itself dyed with it
//
// Written into Data\Meshes\CurseOfAlduin\classarmor\<class>\ and Data\Textures\UNarmor_<A-D>\; done again only when the
// recipe's version changes or a file is missing.
#include "BO3Z.h"

#include <DirectXTex.h>
#include <fstream>

namespace bo3z::ArmorForge
{
	namespace
	{
		constexpr const char* kVersion = "classarmor 1";
		constexpr const char* kModels[] = {
			"blackmage\\1stperson_blackmage_female_jmrobes_0.nif", "blackmage\\1stperson_blackmage_female_jmrobes_1.nif",
			"blackmage\\1stperson_blackmage_male_jmrobes_0.nif", "blackmage\\1stperson_blackmage_male_jmrobes_1.nif",
			"blackmage\\blackmage_female_archboots_0.nif", "blackmage\\blackmage_female_archboots_1.nif",
			"blackmage\\blackmage_female_armwraps_0.nif", "blackmage\\blackmage_female_armwraps_1.nif",
			"blackmage\\blackmage_female_hoodscarf_0.nif", "blackmage\\blackmage_female_hoodscarf_1.nif",
			"blackmage\\blackmage_female_hoodscarf_arg_0.nif", "blackmage\\blackmage_female_hoodscarf_arg_1.nif",
			"blackmage\\blackmage_female_jmrobes_0.nif", "blackmage\\blackmage_female_jmrobes_1.nif",
			"blackmage\\blackmage_male_archboots_0.nif", "blackmage\\blackmage_male_archboots_1.nif",
			"blackmage\\blackmage_male_armwraps_0.nif", "blackmage\\blackmage_male_armwraps_1.nif",
			"blackmage\\blackmage_male_armwraps_arg_0.nif", "blackmage\\blackmage_male_armwraps_arg_1.nif",
			"blackmage\\blackmage_male_armwraps_kha_0.nif", "blackmage\\blackmage_male_armwraps_kha_1.nif",
			"blackmage\\blackmage_male_hoodscarf_0.nif", "blackmage\\blackmage_male_hoodscarf_1.nif",
			"blackmage\\blackmage_male_hoodscarf_arg_0.nif", "blackmage\\blackmage_male_hoodscarf_arg_1.nif",
			"blackmage\\blackmage_male_jmrobes_0.nif", "blackmage\\blackmage_male_jmrobes_1.nif",
		};
		struct Cls { const char* name; const char* dir; float r, g, b; };
		constexpr Cls kCls[4] = { { "summoner", "UNarmor_A", 0.62f, 0.36f, 1.0f }, { "destruction", "UNarmor_B", 1.0f, 0.43f, 0.12f },
			{ "alchemist", "UNarmor_C", 0.36f, 0.92f, 0.24f }, { "spellsword", "UNarmor_D", 0.31f, 0.6f, 1.0f } };

		std::vector<std::uint8_t> ReadGameFile(const std::string& a_path)
		{
			RE::BSResourceNiBinaryStream s(a_path);
			if (!s.good() || !s.stream) return {};
			std::vector<std::uint8_t> buf(s.stream->totalSize);
			if (buf.empty() || !s.read(reinterpret_cast<char*>(buf.data()), static_cast<std::uint32_t>(buf.size()))) return {};
			return buf;
		}
		std::string Lower(std::string a) { for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return a; }
		// every colour map a model names in Black Mage's folder (not its normal maps)
		std::vector<std::string> ColourMaps(const std::vector<std::uint8_t>& a_nif)
		{
			std::vector<std::string> out;
			const std::string low = Lower(std::string(a_nif.begin(), a_nif.end()));
			const std::string key = "textures\\blackmage\\";
			for (std::size_t at = low.find(key); at != std::string::npos; at = low.find(key, at + 1)) {
				const auto end = low.find(".dds", at);
				if (end == std::string::npos || end - at > 120) continue;
				std::string t = low.substr(at, end + 4 - at);
				if (t.ends_with("_n.dds")) continue;
				if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
			}
			return out;
		}
		// textures\BlackMage\ -> textures\UNarmor_X\ (same length) wherever a colour map is named
		std::vector<std::uint8_t> Retex(std::vector<std::uint8_t> a_nif, const char* a_dir)
		{
			const std::string low = Lower(std::string(a_nif.begin(), a_nif.end()));
			const std::string key = "textures\\blackmage\\";
			const std::string rep = std::string("textures\\") + a_dir + "\\";
			for (std::size_t at = low.find(key); at != std::string::npos; at = low.find(key, at + 1)) {
				const auto end = low.find(".dds", at);
				if (end != std::string::npos && end - at < 120 && low.compare(end - 2, 2, "_n") == 0) continue;
				std::memcpy(a_nif.data() + at, rep.data(), rep.size());
			}
			return a_nif;
		}
		// three box blurs: a Gaussian of that sigma
		void Blur(std::vector<float>& a, int w, int h, float sigma)
		{
			const int n = 3;
			const float wIdeal = std::sqrt(12.0f * sigma * sigma / n + 1.0f);
			int wl = static_cast<int>(wIdeal); if (wl % 2 == 0) --wl;
			const int wu = wl + 2;
			const float mIdeal = (12.0f * sigma * sigma - n * wl * wl - 4.0f * n * wl - 3.0f * n) / (-4.0f * wl - 4.0f);
			const int m = static_cast<int>(std::round(mIdeal));
			std::vector<float> t(a.size());
			for (int pass = 0; pass < n; ++pass) {
				const int r = ((pass < m ? wl : wu) - 1) / 2;
				if (r <= 0) continue;
				const float inv = 1.0f / (2 * r + 1);
				for (int y = 0; y < h; ++y) {   // across
					float acc = 0;
					for (int k = -r; k <= r; ++k) acc += a[y * w + std::clamp(k, 0, w - 1)];
					for (int x = 0; x < w; ++x) {
						t[y * w + x] = acc * inv;
						acc += a[y * w + std::min(x + r + 1, w - 1)] - a[y * w + std::max(x - r, 0)];
					}
				}
				for (int x = 0; x < w; ++x) {   // down
					float acc = 0;
					for (int k = -r; k <= r; ++k) acc += t[std::clamp(k, 0, h - 1) * w + x];
					for (int y = 0; y < h; ++y) {
						a[y * w + x] = acc * inv;
						acc += t[std::min(y + r + 1, h - 1) * w + x] - t[std::max(y - r, 0) * w + x];
					}
				}
			}
		}
		bool Dye(const DirectX::Image& a_src, const Cls& a_c, const std::filesystem::path& a_out)
		{
			const int w = static_cast<int>(a_src.width), h = static_cast<int>(a_src.height);
			std::vector<float> l(static_cast<std::size_t>(w) * h);
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x) {
					const auto* p = a_src.pixels + y * a_src.rowPitch + x * 4;
					l[y * w + x] = (p[0] + p[1] + p[2]) / (3.0f * 255.0f);
				}
			std::vector<float> bl = l;
			Blur(bl, w, h, std::max(4.0f, w / 128.0f));
			std::vector<float> m(l.size());
			for (std::size_t i = 0; i < l.size(); ++i) {
				const float detail = std::clamp((l[i] - bl[i] - 0.022f) / 0.08f, 0.0f, 1.0f);   // lighter than its surround: stitches, laces, edges
				const float bright = std::clamp((l[i] - 0.18f) / 0.22f, 0.0f, 1.0f);           // the light parts outright: buckles, trims
				m[i] = std::clamp(detail * 1.2f + bright, 0.0f, 1.0f);
			}
			Blur(m, w, h, std::max(1.0f, w / 1024.0f));
			DirectX::ScratchImage dyed;
			if (FAILED(dyed.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, 1))) return false;
			const auto* d = dyed.GetImage(0, 0, 0);
			const float c[3] = { a_c.r, a_c.g, a_c.b };
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x) {
					const auto* s = a_src.pixels + y * a_src.rowPitch + x * 4;
					auto* o = d->pixels + y * d->rowPitch + x * 4;
					const std::size_t i = static_cast<std::size_t>(y) * w + x;
					for (int k = 0; k < 3; ++k) {
						const float coloured = c[k] * (std::max(l[i], 0.05f) * 3.4f + 0.10f);
						const float cloth = s[k] / 255.0f * 0.55f + l[i] * c[k] * 1.25f;
						o[k] = static_cast<std::uint8_t>(std::clamp(cloth * (1 - m[i]) + coloured * m[i], 0.0f, 1.0f) * 255.0f + 0.5f);
					}
					o[3] = s[3];
				}
			DirectX::ScratchImage mips, bc;
			if (FAILED(DirectX::GenerateMipMaps(*d, DirectX::TEX_FILTER_DEFAULT, 0, mips))) return false;
			if (FAILED(DirectX::Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), DXGI_FORMAT_BC3_UNORM, DirectX::TEX_COMPRESS_PARALLEL, DirectX::TEX_THRESHOLD_DEFAULT, bc))) return false;
			std::error_code ec;
			std::filesystem::create_directories(a_out.parent_path(), ec);
			return SUCCEEDED(DirectX::SaveToDDSFile(bc.GetImages(), bc.GetImageCount(), bc.GetMetadata(), DirectX::DDS_FLAGS_NONE, a_out.wstring().c_str()));
		}
	}

	void Build()
	{
		const auto t0 = std::chrono::steady_clock::now();
		const std::filesystem::path data = std::filesystem::current_path() / "Data";
		const auto stamp = data / "SKSE" / "Plugins" / "CurseOfAlduin" / "classarmor.ver";
		// already built by this recipe, every file in place: nothing to do
		{
			std::ifstream in(stamp);
			std::string v;
			std::getline(in, v);
			bool all = v == kVersion;
			for (int c = 0; all && c < 4; ++c)
				for (auto* m : kModels) {
					const std::string base = std::string(m).substr(std::string(m).rfind('\\') + 1);
					if (!std::filesystem::exists(data / "Meshes" / "CurseOfAlduin" / "classarmor" / kCls[c].name / base)) { all = false; break; }
				}
			if (all) { logger::info("class robes: already built from Black Mage Armor SE"); return; }
		}
		if (ReadGameFile(std::string("meshes\\") + kModels[0]).empty()) {
			logger::warn("class robes: Black Mage Armor SE is not installed - the class robes can't be built (they need its files)");
			return;
		}
		std::vector<std::string> maps;
		int models = 0;
		for (auto* m : kModels) {
			auto nif = ReadGameFile(std::string("meshes\\") + m);
			if (nif.empty()) { logger::warn("class robes: {} missing from Black Mage Armor SE", m); continue; }
			for (auto& t : ColourMaps(nif)) if (std::find(maps.begin(), maps.end(), t) == maps.end()) maps.push_back(t);
			const std::string base = std::string(m).substr(std::string(m).rfind('\\') + 1);
			for (auto& c : kCls) {
				const auto out = data / "Meshes" / "CurseOfAlduin" / "classarmor" / c.name / base;
				std::error_code ec;
				std::filesystem::create_directories(out.parent_path(), ec);
				const auto bytes = Retex(nif, c.dir);
				std::ofstream f(out, std::ios::binary);
				f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			}
			++models;
		}
		int dyed = 0;
		for (auto& t : maps) {
			const auto dds = ReadGameFile(t);
			DirectX::TexMetadata meta{};
			DirectX::ScratchImage img, rgba;
			if (dds.empty() || FAILED(DirectX::LoadFromDDSMemory(dds.data(), dds.size(), DirectX::DDS_FLAGS_NONE, &meta, img))) {
				logger::warn("class robes: couldn't read {}", t);
				continue;
			}
			const DirectX::Image* top = img.GetImage(0, 0, 0);
			if (DirectX::IsCompressed(meta.format)) {
				if (FAILED(DirectX::Decompress(*top, DXGI_FORMAT_R8G8B8A8_UNORM, rgba))) { logger::warn("class robes: couldn't unpack {}", t); continue; }
			} else if (FAILED(DirectX::Convert(*top, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba))) {
				logger::warn("class robes: couldn't convert {}", t);
				continue;
			}
			const std::string name = t.substr(t.rfind('\\') + 1);
			for (auto& c : kCls) dyed += Dye(*rgba.GetImage(0, 0, 0), c, data / "Textures" / c.dir / name) ? 1 : 0;
		}
		std::error_code ec;
		std::filesystem::create_directories(stamp.parent_path(), ec);
		std::ofstream(stamp) << kVersion << "\n";
		logger::info("class robes: built from Black Mage Armor SE - {} models x4, {} colour maps dyed, in {:.1f} s", models, dyed,
			std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count());
	}
}
