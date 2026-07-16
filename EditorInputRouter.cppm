module;

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
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

    /// Resolves raw host input into one-frame semantic editor actions.
    /// Text and modal contexts consume all editor chords by design; widgets
    /// and dialogs therefore cannot accidentally delete scene or graph data.
    class EditorInputRouter final
    {
    public:
        explicit EditorInputRouter(KeymapProfile profile = KeymapProfile::Kairo)
            : m_Profile(profile), m_Bindings(DefaultInputBindings(profile)) {}

        void BeginFrame() noexcept { m_Triggered.reset(); }
        void SetContext(InputContext context) noexcept { m_Context = context; }
        [[nodiscard]] InputContext Context() const noexcept { return m_Context; }
        [[nodiscard]] KeymapProfile Profile() const noexcept { return m_Profile; }

        void SetProfile(KeymapProfile profile)
        {
            m_Profile = profile;
            m_Bindings = DefaultInputBindings(profile);
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
        std::vector<ContextBinding> m_Bindings;
        std::bitset<static_cast<std::size_t>(EditorAction::Count)> m_Triggered;
    };
}
