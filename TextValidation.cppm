module;

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

export module Kairo.Editor.TextValidation;

export namespace kairo::editor
{
    struct TextRules final
    {
        std::size_t MinimumBytes = 0u;
        std::size_t MaximumBytes = 64u * 1024u;
        bool AllowNewlines = true;
        bool AllowTabs = true;
    };

    /// Input: arbitrary UTF-8 byte sequence.
    /// Output: true only for shortest-form Unicode scalar encodings.
    /// Task: reject truncated sequences, overlong forms, surrogate code points,
    /// and values above U+10FFFF before text reaches persistence or native UI.
    [[nodiscard]] inline bool IsValidUtf8(std::string_view text) noexcept
    {
        std::size_t index = 0u;
        while (index < text.size())
        {
            const auto first = static_cast<unsigned char>(text[index]);
            if (first <= 0x7fu) { ++index; continue; }

            std::size_t continuationCount = 0u;
            std::uint32_t value = 0u;
            if ((first & 0xe0u) == 0xc0u) { continuationCount = 1u; value = first & 0x1fu; }
            else if ((first & 0xf0u) == 0xe0u) { continuationCount = 2u; value = first & 0x0fu; }
            else if ((first & 0xf8u) == 0xf0u) { continuationCount = 3u; value = first & 0x07u; }
            else return false;

            if (index + continuationCount >= text.size()) return false;
            for (std::size_t offset = 1u; offset <= continuationCount; ++offset)
            {
                const auto continuation = static_cast<unsigned char>(text[index + offset]);
                if ((continuation & 0xc0u) != 0x80u) return false;
                value = (value << 6u) | (continuation & 0x3fu);
            }

            const bool overlong = (continuationCount == 1u && value < 0x80u) ||
                (continuationCount == 2u && value < 0x800u) ||
                (continuationCount == 3u && value < 0x10000u);
            if (overlong || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) return false;
            index += continuationCount + 1u;
        }
        return true;
    }

    /// Task: enforce one reusable text boundary for project metadata, document
    /// schemas, authored values, and future UI labels.
    inline void ValidateUtf8Text(std::string_view text, const TextRules& rules, std::string_view role)
    {
        if (rules.MaximumBytes < rules.MinimumBytes)
            throw std::invalid_argument("Text validation maximum cannot be smaller than its minimum.");
        if (text.size() < rules.MinimumBytes || text.size() > rules.MaximumBytes)
            throw std::invalid_argument(std::string(role) + " must contain " +
                std::to_string(rules.MinimumBytes) + " to " + std::to_string(rules.MaximumBytes) + " UTF-8 bytes.");
        if (!IsValidUtf8(text)) throw std::invalid_argument(std::string(role) + " is not valid UTF-8.");
        for (const unsigned char byte : text)
        {
            if (byte == '\n' && rules.AllowNewlines) continue;
            if (byte == '\t' && rules.AllowTabs) continue;
            if (byte < 0x20u || byte == 0x7fu)
                throw std::invalid_argument(std::string(role) + " contains a disallowed control character.");
        }
    }
}
