#include <algorithm>
#include <cassert>
#include <compare>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <source_location>
#include <vector>

#include <sys/wait.h>

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

void demo_includes_resolution()
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

namespace make
{
	namespace details
	{
		template<typename T>
		concept printable = requires (std::ostream &os, T const& val)
		{
			{ os << val } -> std::same_as<std::ostream&>;
		};
	}

	namespace details
	{
		template<typename T, typename Desired>
		concept value_or_range = std::constructible_from<Desired, T>
			|| (std::ranges::forward_range<T> && std::constructible_from<Desired, std::ranges::range_value_t<T>>);

		// Ensure that the following types work
		static_assert(value_or_range<char const*, std::string>);
		static_assert(value_or_range<std::string_view, std::string>);
		static_assert(value_or_range<std::string, std::string>);
		static_assert(value_or_range<std::vector<char const*>, std::string>);
		static_assert(value_or_range<std::vector<std::string_view>, std::string>);
		static_assert(value_or_range<std::vector<std::string>, std::string>);

		template<typename T, typename U>
		requires std::constructible_from<T, U>
		void append(std::vector<T> &vec, U &&element)
		{
			vec.emplace_back(std::forward<decltype(element)>(element));
		}

		template<typename T, typename U, std::size_t N>
		requires std::convertible_to<T, U>
		void append(std::vector<T> &vec, U(&array)[N])
		{
			vec.reserve(vec.size() + N);
			for (auto const& data : array) {
				vec.emplace_back(data);
			}
		}
	}

	template<typename T, typename ...Xs>
	void append(std::vector<T> &vector, Xs&& ...either_value_or_range)
	{
		(details::append(vector, either_value_or_range), ...);
	}

	struct Cmd
	{
		std::vector<std::string> argv{};
		std::string input_stream;

		constexpr Cmd() = default;

		template<details::value_or_range<std::string> ...T>
		explicit Cmd(T&& ...args)
		{
			append(argv, std::forward<T>(args)...);
		}

		// TODO: Something like shlex quote
		friend std::ostream& operator<<(std::ostream& os, Cmd cmd)
		{
			os << "Cmd{";

			for (auto it = cmd.argv.begin(); std::next(it) != cmd.argv.end(); ++it) {
				os << std::quoted(*it) << ", ";
			}

			if (cmd.argv.size()) {
				os << std::quoted(cmd.argv.back());
			}

			return os << "}";
		}
	};

	int pid_wait(pid_t pid)
	{
		for (;;) {
			int wstatus = 0;

			auto result = ::waitpid(pid, &wstatus, 0);
			assert(result >= 0);

			if (WIFEXITED(wstatus)) {
				return WEXITSTATUS(wstatus);
			}

			assert(not WIFSIGNALED(wstatus)); // TODO(assert)
		}
	}

	// TODO: proper shell quote (like shlex)
	std::string quote(std::string s)
	{
		return s;
	}

	void cmd_run(std::vector<std::string> &argv)
	{
		assert(argv.size());

		std::cout << "[CMD] ";
		for (auto it = argv.begin(); it != argv.end(); ++it) {
			std::cout << quote(*it);
			if (it != argv.end()) std::cout << ' ';
		}
		std::cout << std::endl;

		auto child_pid = ::fork();
		assert(child_pid >= 0); // TODO(assert)

		if (child_pid == 0) {
			auto c_argv = std::make_unique<char*[]>(argv.size() + 1);
			std::transform(argv.begin(), argv.end(), c_argv.get(), [](std::string &s) { return s.data(); });
			auto result = ::execvp(c_argv[0], c_argv.get());
			assert(result >= 0);
		}

		pid_wait(child_pid);
	}

	void append(Cmd &cmd, auto&& ...args)
		requires requires { {append(cmd.argv, std::forward<decltype(args)>(args)...)}; }
	{
		append(cmd.argv, std::forward<decltype(args)>(args)...);
	}

	namespace compiler
	{
		inline namespace cpp
		{
			[[maybe_unused]] constexpr std::string_view gcc = "g++";
			[[maybe_unused]] constexpr std::string_view clang = "clang++";
			[[maybe_unused]] constexpr std::string_view posix = "c++";

			constexpr std::string_view current()
			{
#if defined(__clang__)
				return clang;
#elif defined(__GNUC__)
				return gcc;
#else
#error Unknown compiler
#endif
			}
		}
	}

	void rebuild_self(int argc, char **argv, std::source_location use_location = std::source_location::current())
	{
		assert(argc > 0);
		// TODO: is this the best way to do this? Maybe some /proc/self would be better
		char const *program_path = argv[0];
		char const *source_path  = use_location.file_name();

		if (std::filesystem::last_write_time(program_path) >= std::filesystem::last_write_time(source_path)) {
			return;
		}

		{
			std::filesystem::path old_program = program_path;
			old_program += ".old";
			std::filesystem::copy_file(program_path,  old_program);
		}

		Cmd cmd1{make::compiler::current(), "-std=c++20", "-o", program_path, source_path};
		make::cmd_run(cmd1.argv);

		Cmd cmd2{program_path};
		make::cmd_run(cmd2.argv);
		::exit(0);
	}
}

int main(int argc, char **argv)
{
	make::rebuild_self(argc, argv);
}
