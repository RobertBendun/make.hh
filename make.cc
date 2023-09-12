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
#include <string.h>
#include <errno.h>

#include <sys/wait.h>

namespace make
{
	[[noreturn]]
	inline void panic(std::string why, std::source_location where = std::source_location::current())
	{
		std::cerr << "[ERROR] at " << where.file_name() << ':' << where.line() << ':' << where.column() << ": " << why << std::endl;
		std::abort();
	}

	inline void panic_if(bool should, std::string why, std::source_location where = std::source_location::current())
	{
		if (!should) return;
		panic(std::move(why), where);
	}
}

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

		template<typename T, std::ranges::forward_range R>
		requires (!std::is_array_v<R> and std::constructible_from<T, std::ranges::range_value_t<R>>)
		void append(std::vector<T> &vec, R&& range)
		{
			if constexpr (std::ranges::sized_range<R>) {
				vec.reserve(vec.size() + std::size(vec));
			}
			for (auto&& element : range) {
				// TODO: Use forward_like
				vec.emplace_back(element);
			}
		}
	}

	template<typename T, details::value_or_range<T> ...Xs>
	void append(std::vector<T> &vector, Xs&& ...either_value_or_range)
	{
		(details::append(vector, either_value_or_range), ...);
	}

	template<typename T, details::value_or_range<T> ...Xs>
	std::vector<T> or_default(std::vector<T> &&vector, Xs&& ...either_value_or_range)
	{
		if (vector.empty()) {
			append(vector, std::forward<Xs>(either_value_or_range)...);
		}
		return vector;
	}

	std::vector<std::string> cmd_parse(std::string_view command)
	{
		std::vector<std::string> args;

		char previous = '\0';
		auto start = 0u;
		for (auto i = 0u; i < command.size(); ++i) {
			switch (command[i]) {
			break; case '\'': case '"':
				assert(false && "unimplemented");

			break; case ' ':
				if (previous != ' ' and i != 0u) {
					args.emplace_back(command.substr(start, i - start));
				}
				start = i + 1;
			}
			previous = command[i];
		}

		if (start < command.size() && command[start] != ' ') {
			args.emplace_back(command.substr(start));
		}

		return args;
	}

	std::string cmd_render(std::vector<std::string> const& argv)
	{
		std::string cmd;
		for (auto it = argv.begin(); it != argv.end(); ++it) {
			if (it != argv.begin()) cmd += ' ';

			if (it->find_first_of(" \"") != std::string::npos) {
				cmd += '\'';
				cmd += *it;
				cmd += '\'';
			} else if (it->find('\'') != std::string::npos) {
				assert(0 && "unimplemented");
			} else {
				cmd += *it;
			}
		}
		return cmd;
	}

	struct Status
	{
		enum { EXIT, SIGNAL } kind = EXIT;
		union { int exit_code, signal; };

		constexpr operator bool() const { return kind == EXIT && exit_code == 0; }

		constexpr int normalize_to_exit_code() const {
			switch (kind) {
			case EXIT: return exit_code;
			case SIGNAL: return 128 + exit_code;
			}
			assert(0 && "unreachable");
		}
	};

	[[nodiscard]]
	Status pid_wait(pid_t pid)
	{
		for (;;) {
			int wstatus = 0;

			if (::waitpid(pid, &wstatus, 0) < 0) {
				panic(std::string("Failed to wait for process: ") + strerror(errno));
			}

			if (WIFEXITED(wstatus)) {
				return Status { .exit_code = WEXITSTATUS(wstatus) };
			}

			if (WIFSIGNALED(wstatus)) {
				return Status { .kind = Status::SIGNAL, .exit_code = WTERMSIG(wstatus) };
			}
		}
	}

	[[nodiscard]]
	Status cmd_run(std::vector<std::string> &argv)
	{
		panic_if(argv.empty(), "couldn't execute empty command");
		std::cout << "[CMD] " << cmd_render(argv) << std::endl;

		auto child_pid = ::fork();
		if (child_pid < 0) {
			panic(std::string("Failed to execute command: ") + strerror(errno));
		}

		if (child_pid == 0) {
			auto c_argv = std::make_unique<char*[]>(argv.size() + 1);
			std::transform(argv.begin(), argv.end(), c_argv.get(), [](std::string &s) { return s.data(); });
			::execvp(c_argv[0], c_argv.get());
			panic(std::string("Failed to execute command: ") + strerror(errno));
		}

		return pid_wait(child_pid);
	}

	struct Cmd
	{
		std::vector<std::string> argv{};

		constexpr Cmd() = default;

		template<details::value_or_range<std::string> ...T>
		explicit Cmd(T&& ...args)
		{
			append(argv, std::forward<T>(args)...);
		}

		// run command and ensure that we returned success
		void run_and_check(std::source_location where = std::source_location::current())
		{
			auto result = cmd_run(argv);
			if (not result) {
				switch (result.kind) {
				break; case Status::EXIT:
					panic("Command " + cmd_render(argv) + " returned non-zero exit code (exit_code = " + std::to_string(result.exit_code) + ")", where);
				break; case Status::SIGNAL:
					panic("Command " + cmd_render(argv) + " stopped with a signal: " + strsignal(result.signal), where);
				}
			}
		}
	};

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
			std::filesystem::copy_file(program_path, old_program, std::filesystem::copy_options::overwrite_existing);
		}

		Cmd compile{make::compiler::current(), "-std=c++20", "-o", program_path, source_path};
		compile.run_and_check();

		Cmd run{program_path};
		auto status = make::cmd_run(run.argv);
		::exit(status.normalize_to_exit_code());
	}

	std::vector<std::string> flags_from_env(std::string environment_variable) {
		if ([[maybe_unused]] auto env = ::getenv(environment_variable.c_str())) {
			return cmd_parse(env);
		} else {
			return {};
		}
	}
}

int main(int argc, char **argv)
{
	using namespace std::string_literals;

	make::rebuild_self(argc, argv);

	auto cxx = make::or_default(make::flags_from_env("CXX"), make::compiler::gcc);
	auto cxxflags = std::vector { "-Wall"s, "-Wextra"s, };
	make::append(cxxflags, make::flags_from_env("CXXFLAGS"));

	make::Cmd{"echo", cxx, cxxflags, "-o", "main", "main.cc"}.run_and_check();
}
