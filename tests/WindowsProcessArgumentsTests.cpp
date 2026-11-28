#include "WindowsProcessArguments.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#endif

using kairo::editor::detail::QuoteWindowsArgument;
using kairo::editor::detail::EncodeWindowsArguments;

static const std::vector<std::string> Cases{
    "", "plain", "two words", "C:\\Program Files\\Kairo\\Project.kproject",
    "C:\\project with spaces\\", "a\"b", "a\\\"b", "a\\\\\"b", "a\tb", "&|<>^%"
};

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--child")
    {
        if (argc != static_cast<int>(Cases.size()) + 2) return 11;
        for (std::size_t i = 0; i < Cases.size(); ++i)
            if (Cases[i] != argv[i + 2]) return 12;
        return 0;
    }
    const std::vector<std::string> expected{
        R"q("")q", R"q("plain")q", R"q("two words")q",
        R"q("C:\Program Files\Kairo\Project.kproject")q",
        R"q("C:\project with spaces\\")q", R"q("a\"b")q",
        R"q("a\\\"b")q", R"q("a\\\\\"b")q", "\"a\tb\"", R"q("&|<>^%")q"
    };
    if (EncodeWindowsArguments(Cases) != expected) return 1;
#if defined(_WIN32)
    std::vector<std::string> arguments{argv[0], "--child"};
    arguments.insert(arguments.end(), Cases.begin(), Cases.end());
    const auto encoded = EncodeWindowsArguments(arguments);
    std::vector<const char*> pointers;
    for (const auto& value : encoded) pointers.push_back(value.c_str());
    pointers.push_back(nullptr);
    const auto status = _spawnv(_P_WAIT, argv[0], pointers.data());
    if (status != 0) { std::cerr << "Child argument round-trip failed: " << status << '\n'; return 2; }
#endif
    std::cout << "Windows argument encoding passed\n";
    return 0;
}
