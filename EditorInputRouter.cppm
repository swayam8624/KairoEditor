module;

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.InputRouter;

import Kairo.Editor.Actions;

export namespace kairo::editor
{
    enum class InputContext : std::uint8_t
    {
        Global, Scene, Graph, Modeling, Code, Text, Play, Modal
    };

    enum class KeymapProfile : std::uint8_t { Kairo, Blender, Unreal, Unity };

    enum class EditorKey : std::uint8_t
    {
        A, C, D, E, F, G, N, Q, R, S, V, W, X, Z,
        Space, Home, Backspace, Delete, F5
    };

    [[nodiscard]] constexpr std::string_view Name(InputContext context) noexcept
    {
        switch (context)
        {
            case InputContext::Global: return "global";
            case InputContext::Scene: return "scene";
            case InputContext::Graph: return "graph";
            case InputContext::Modeling: return "modeling";
            case InputContext::Code: return "code";
            case InputContext::Text: return "text";
            case InputContext::Play: return "play";
            case InputContext::Modal: return "modal";
        }
        return "invalid";
    }

    [[nodiscard]] constexpr std::string_view Name(EditorKey key) noexcept
    {
        static constexpr std::array names{
            "a", "c", "d", "e", "f", "g", "n", "q", "r", "s", "v", "w", "x", "z",
            "space", "home", "backspace", "delete", "f5"
        };
        const auto index = static_cast<std::size_t>(key);
        return index < names.size() ? names[index] : std::string_view{ "invalid" };
    }

    [[nodiscard]] constexpr std::optional<InputContext> ParseInputContext(std::string_view name) noexcept
    {
        for (std::uint8_t value = 0u; value <= static_cast<std::uint8_t>(InputContext::Modal); ++value)
        {
            const auto context = static_cast<InputContext>(value);
            if (Name(context) == name) return context;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<EditorKey> ParseEditorKey(std::string_view name) noexcept
    {
        for (std::uint8_t value = 0u; value <= static_cast<std::uint8_t>(EditorKey::F5); ++value)
        {
            const auto key = static_cast<EditorKey>(value);
            if (Name(key) == name) return key;
        }
        return std::nullopt;
    }

    enum class KeyModifiers : std::uint8_t
    {
        None = 0u, Shift = 1u, Shortcut = 2u, Alt = 4u
    };

    [[nodiscard]] constexpr KeyModifiers operator|(KeyModifiers left, KeyModifiers right) noexcept
    {
        return static_cast<KeyModifiers>(static_cast<std::uint8_t>(left) |
            static_cast<std::uint8_t>(right));
    }

    struct InputChord final
    {
        EditorKey Key{};
        KeyModifiers Modifiers = KeyModifiers::None;
        friend bool operator==(const InputChord&, const InputChord&) = default;
    };

    struct RoutedInput final
    {
        InputChord Chord;
        bool Pressed = true;
        bool Repeat = false;
    };

    struct ContextBinding final
    {
        EditorAction Action{};
        InputContext Context = InputContext::Global;
        InputChord Chord{};
    };

    /// Replaces every default chord for one semantic action in one context.
    /// An empty chord list intentionally disables that action/context pair.
    struct KeymapOverride final
    {
        EditorAction Action{};
        InputContext Context = InputContext::Global;
        std::vector<InputChord> Chords;
        friend bool operator==(const KeymapOverride&, const KeymapOverride&) = default;
    };

    /// Input: one supported compatibility profile.
    /// Output: deterministic semantic bindings with no native key-code types.
    /// Task: preserve familiar Blender/Unreal/Unity muscle memory while all
    /// commands continue through Kairo-owned action and undo boundaries.
    [[nodiscard]] inline std::vector<ContextBinding> DefaultInputBindings(KeymapProfile profile)
    {
        using enum EditorAction;
        using enum EditorKey;
        using enum InputContext;
        using enum KeyModifiers;
        std::vector<ContextBinding> result{
            { Save, Global, { S, Shortcut } },
            { SaveAll, Global, { S, Shortcut | Alt } },
            { NewDocument, Global, { N, Shortcut } },
            { CloseDocument, Global, { W, Shortcut } },
            { Undo, Global, { Z, Shortcut } },
            { Redo, Global, { Z, Shortcut | Shift } },
            { TogglePlay, Global, { F5 } },
            { GraphAddNode, Graph, { A, Shift } },
            { GraphAddNode, Graph, { Space } },
            { GraphDelete, Graph, { Delete } },
            { GraphDelete, Graph, { Backspace } },
            { GraphDuplicate, Graph, { D, Shortcut } },
            { GraphCopy, Graph, { C, Shortcut } },
            { GraphPaste, Graph, { V, Shortcut } },
            { GraphFrameSelection, Graph, { F } },
            { GraphFrameAll, Graph, { Home } },
            { AddPrimitive, Scene, { A, Shift } },
            { DeleteSelection, Scene, { Delete } },
            { DeleteSelection, Scene, { Backspace } },
            { DeleteSelection, Scene, { X } },
            { Duplicate, Scene, { D, Shortcut } },
            { FocusSelection, Scene, { F } }
        };

        if (profile == KeymapProfile::Blender)
        {
            result.insert(result.end(), {
                { TranslateTool, Scene, { G } },
                { RotateTool, Scene, { R } },
                { ScaleTool, Scene, { S } },
                { SelectTool, Scene, { Q } }
            });
        }
        else
        {
            result.insert(result.end(), {
                { SelectTool, Scene, { Q } },
                { TranslateTool, Scene, { W } },
                { RotateTool, Scene, { E } },
                { ScaleTool, Scene, { R } }
            });
        }
        return result;
    }

    [[nodiscard]] constexpr bool ContextsOverlap(InputContext left, InputContext right) noexcept
    {
        return left == right || left == InputContext::Global || right == InputContext::Global;
    }

    /// Applies replacement overrides and rejects ambiguous effective chords.
    /// This validation is shared by settings parsing and the live router so a
    /// malformed caller cannot create order-dependent shortcut behavior.
    [[nodiscard]] inline std::vector<ContextBinding> BuildInputBindings(
        KeymapProfile profile, std::span<const KeymapOverride> overrides)
    {
        if (overrides.size() > 256u) throw std::length_error("A keymap cannot contain more than 256 overrides.");
        std::vector<ContextBinding> result = DefaultInputBindings(profile);
        std::vector<std::pair<EditorAction, InputContext>> replaced;
        for (const KeymapOverride& overrideBinding : overrides)
        {
            if (overrideBinding.Action >= EditorAction::Count)
                throw std::invalid_argument("A keymap override contains an invalid action.");
            if (overrideBinding.Context == InputContext::Text || overrideBinding.Context == InputContext::Modal)
                throw std::invalid_argument("Text and modal contexts cannot own editor shortcuts.");
            const auto identity = std::pair{ overrideBinding.Action, overrideBinding.Context };
            if (std::ranges::find(replaced, identity) != replaced.end())
                throw std::invalid_argument("A keymap contains duplicate overrides for one action/context pair.");
            replaced.push_back(identity);
            std::erase_if(result, [&](const ContextBinding& existing)
            {
                return existing.Action == overrideBinding.Action && existing.Context == overrideBinding.Context;
            });
            std::vector<InputChord> unique;
            for (const InputChord chord : overrideBinding.Chords)
            {
                if (static_cast<std::uint8_t>(chord.Key) > static_cast<std::uint8_t>(EditorKey::F5) ||
                    (static_cast<std::uint8_t>(chord.Modifiers) & ~std::uint8_t{ 7u }) != 0u)
                    throw std::invalid_argument("A keymap override contains an invalid chord.");
                if (std::ranges::find(unique, chord) != unique.end())
                    throw std::invalid_argument("A keymap override repeats the same chord.");
                unique.push_back(chord);
                result.push_back({ overrideBinding.Action, overrideBinding.Context, chord });
            }
        }
        for (std::size_t left = 0u; left < result.size(); ++left)
            for (std::size_t right = left + 1u; right < result.size(); ++right)
                if (result[left].Chord == result[right].Chord &&
                    ContextsOverlap(result[left].Context, result[right].Context) &&
                    result[left].Action != result[right].Action)
                    throw std::invalid_argument("A keymap chord resolves to multiple actions in an overlapping context.");
        return result;
    }

    /// Resolves raw host input into one-frame semantic editor actions.
    /// Text and modal contexts consume all editor chords by design; widgets
    /// and dialogs therefore cannot accidentally delete scene or graph data.
    class EditorInputRouter final
    {
    public:
        explicit EditorInputRouter(KeymapProfile profile = KeymapProfile::Kairo,
            std::vector<KeymapOverride> overrides = {})
            : m_Profile(profile), m_Overrides(std::move(overrides)),
              m_Bindings(BuildInputBindings(profile, m_Overrides)) {}

        void BeginFrame() noexcept { m_Triggered.reset(); }
        void SetContext(InputContext context) noexcept { m_Context = context; }
        [[nodiscard]] InputContext Context() const noexcept { return m_Context; }
        [[nodiscard]] KeymapProfile Profile() const noexcept { return m_Profile; }
        [[nodiscard]] const std::vector<KeymapOverride>& Overrides() const noexcept { return m_Overrides; }
        [[nodiscard]] const std::vector<ContextBinding>& Bindings() const noexcept { return m_Bindings; }

        void SetProfile(KeymapProfile profile)
        {
            std::vector<ContextBinding> candidate = BuildInputBindings(profile, m_Overrides);
            m_Profile = profile;
            m_Bindings = std::move(candidate);
            m_Triggered.reset();
        }

        void SetOverrides(std::vector<KeymapOverride> overrides)
        {
            std::vector<ContextBinding> candidate = BuildInputBindings(m_Profile, overrides);
            m_Overrides = std::move(overrides);
            m_Bindings = std::move(candidate);
            m_Triggered.reset();
        }

        /// Returns true when a semantic action accepted this event.
        [[nodiscard]] bool Route(const RoutedInput& input)
        {
            if (!input.Pressed || input.Repeat || m_Context == InputContext::Text ||
                m_Context == InputContext::Modal) return false;
            for (const ContextBinding& binding : m_Bindings)
            {
                if (binding.Chord != input.Chord) continue;
                if (binding.Context != InputContext::Global && binding.Context != m_Context) continue;
                m_Triggered.set(static_cast<std::size_t>(binding.Action));
                return true;
            }
            return false;
        }

        [[nodiscard]] bool Consume(EditorAction action) noexcept
        {
            const std::size_t index = static_cast<std::size_t>(action);
            const bool value = m_Triggered.test(index);
            m_Triggered.reset(index);
            return value;
        }

        [[nodiscard]] bool Triggered(EditorAction action) const noexcept
        {
            return m_Triggered.test(static_cast<std::size_t>(action));
        }

    private:
        KeymapProfile m_Profile;
        InputContext m_Context = InputContext::Global;
        std::vector<KeymapOverride> m_Overrides;
        std::vector<ContextBinding> m_Bindings;
        std::bitset<static_cast<std::size_t>(EditorAction::Count)> m_Triggered;
    };
}
