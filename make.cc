#include <cassert>
#include <compare>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <ranges>
#include <set>
#include <vector>

namespace make
{
	struct Include
	{
		std::string include;
		bool maybe_relative; // "" = true, <> = false

		bool operator==(Include const&) const = default;
		std::strong_ordering operator<=>(Include const&) const = default;

		friend std::ostream& operator<<(std::ostream& out, Include const& include)
		{
			char const open  = include.maybe_relative ? '"' : '<';
			char const close = include.maybe_relative ? '"' : '>';
			return out << open << include.include << close;
		}
	};

	/// Extracts C/C++ preprocesor includes from singular file
	std::set<Include> includes(std::filesystem::path path)
	{
		// FIXME This may lead to incorrect results when using #if and other preprocesor magic
		// The best solution will be probably C++ preprocesor evaluation. However, we don't need
		// to have minmal (enabled only) set of includes for safe dependency resolution.
		// We could keep platform depended list of files or script user can modify list themself

		// FIXME Support preprocesor line escaping
		// FIXME Support skipping comments

		std::ifstream source(path);

		std::set<Include> includes;

		for (std::string line; std::getline(source, line); ) {
			enum
			{
				Waits_For_Hash,
				Waits_For_Include,
				Waits_For_Opening,
				Waits_For_Closing_Greater_Then,
				Waits_For_Closing_Quote
			} state = Waits_For_Hash;

			for (std::string_view l = line; !l.empty(); ) {
					switch (state) {
					break; case Waits_For_Hash:
						if (auto i = l.find_first_not_of(" \t"); i != std::string_view::npos) {
							if (l[i] == '#') {
								l.remove_prefix(i+1);
								state = Waits_For_Include;
								break;
							}
						}
						goto next_line;

					break; case Waits_For_Include:
						if (auto i = l.find_first_not_of(" \t"); i != std::string_view::npos) {
							std::string_view include = "include";
							if (l.substr(i).starts_with(include)) {
								l.remove_prefix(i + include.size());
								state = Waits_For_Opening;
								break;
							}
						}
						goto next_line;

					break; case Waits_For_Opening:
						if (auto i = l.find_first_not_of(" \t"); i != std::string_view::npos) {
							l.remove_prefix(i);
							if (l.starts_with('<')) { l.remove_prefix(1); state = Waits_For_Closing_Greater_Then; break; }
							if (l.starts_with('"')) { l.remove_prefix(1); state = Waits_For_Closing_Quote;        break; }
						}
						goto next_line;

					break; case Waits_For_Closing_Quote:
						if (auto i = l.find('"'); i != std::string_view::npos) {
							includes.emplace(std::string(l.substr(0, i)), true);
						}
						goto next_line;

					break; case Waits_For_Closing_Greater_Then:
						if (auto i = l.find('>'); i != std::string_view::npos) {
							includes.emplace(std::string(l.substr(0, i)), false);
						}
						goto next_line;

					default:
						assert(false);
					}
			}
next_line:
			state = Waits_For_Hash;
		}

		return includes;
	}

	/// Extracts C/C++ preprocesor includes from file or directory (traverses recursively)
	std::map<std::filesystem::path, std::set<Include>> includes_in_directory(
		std::filesystem::path search_path,
		std::ranges::forward_range auto const& extensions)
	{
		// TODO Add requires that ensures equality comparison with std::filesystem::path
		std::map<std::filesystem::path, std::set<Include>> includes_per_file;

		for (auto file : std::filesystem::recursive_directory_iterator(search_path)) {
			auto const& path = file.path();
			if (file.is_regular_file() && std::ranges::find(extensions, path.extension())) {
				includes_per_file.emplace(std::filesystem::canonical(path), includes(path));
			}
		}

		return includes_per_file;
	}

	// Try to resolve given include with given include paths and given relative path
	std::optional<std::filesystem::path> resolve(
		Include include,
		std::ranges::forward_range auto const& include_paths,
		std::filesystem::path const& relative_to
	)
	{
		// FIXME std::filesystem::is_regular_file may not be the best predicate for file validation
		// Resolution algorithm based on GCC behaviour: https://gcc.gnu.org/onlinedocs/cpp/Search-Path.html
		std::filesystem::path p(include.include);
		if (std::filesystem::is_regular_file(p)) {
			return std::filesystem::canonical(p);
		}

		if (p.is_absolute()) {
			return std::nullopt;
		}

		if (include.maybe_relative) {
			if (auto relative = relative_to / p; std::filesystem::is_regular_file(relative)) {
				return std::filesystem::canonical(relative);
			}
		}

		for (auto const& relative_to : include_paths) {
			if (auto relative = relative_to / p; std::filesystem::is_regular_file(relative)) {
				return std::filesystem::canonical(relative);
			}
		}

		return std::nullopt;
	}

	namespace extensions
	{
		std::filesystem::path cpp_implementation[] = {
			".cc",
			".cpp",
			".cxx",
		};

		std::filesystem::path cpp_header[] = {
			".h",
			".hh",
			".hpp",
			".hxx",
		};

		std::filesystem::path cpp[] = {
			".cc",
			".cpp",
			".cxx",
			".h",
			".hh",
			".hpp",
			".hxx",
		};

		std::filesystem::path c[] = { ".c", ".h" };
		std::filesystem::path c_header[] = { ".h" };
		std::filesystem::path c_implementation[] = { ".c" };
	}
}

int main()
{
	auto results = make::includes_in_directory("../musique/musique/", make::extensions::cpp);

	std::filesystem::path include_paths[] = {
		"../musique"
	};

	for (auto const& [filename, includes] : results) {
		std::cout << filename << '\n';
		for (auto const& include : includes) {
			std::cout << "  " << include;
			if (auto p = make::resolve(include, include_paths, filename)) {
				std::cout << " -- " << *p;
			}
			std::cout << '\n';
		}
	}
}
