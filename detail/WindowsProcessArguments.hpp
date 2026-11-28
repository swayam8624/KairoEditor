#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kairo::editor::detail
{
    // The Microsoft CRT spawn family concatenates the supplied argument
    // strings into a command line. Encode each argument for the child CRT:
    // backslashes before quotes and the closing quote must be doubled.
    [[nodiscard]] inline std::string QuoteWindowsArgument(std::string_view argument)
    {
        std::string result{"\""};
        std::size_t slashes = 0;
        for (const char c : argument)
        {
            if (c == '\\') { ++slashes; continue; }
            if (c == '"') result.append(slashes * 2 + 1, '\\');
            else result.append(slashes, '\\');
            slashes = 0;
            result.push_back(c);
        }
        result.append(slashes * 2, '\\');
        result.push_back('"');
        return result;
    }

    // Own encoded strings until the call returns; pointers are formed only
    // after the vector is complete, avoiding invalidation on reallocation.
    [[nodiscard]] inline std::vector<std::string> EncodeWindowsArguments(
        const std::vector<std::string>& arguments)
    {
        std::vector<std::string> encoded;
        encoded.reserve(arguments.size());
        for (const auto& argument : arguments)
            encoded.push_back(QuoteWindowsArgument(argument));
        return encoded;
    }
}
