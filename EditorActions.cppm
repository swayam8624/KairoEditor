module;

#include <array>
#include <cstdint>
#include <string_view>

export module Kairo.Editor.Actions;

export namespace kairo::editor
{
    /// Commands intentionally describe user intent rather than ImGui, GLFW, or
    /// OS key codes. The native shell maps the same actions to platform-native
    /// shortcuts today; future Qt/Tauri/native shells can preserve the editor
    /// contract without duplicating command semantics.
    enum class EditorAction : std::uint8_t
    {
        Save,
        Undo,
        Redo,
        Duplicate,
        DeleteSelection,
        AddPrimitive,
        FocusSelection,
        SelectTool,
        TranslateTool,
        RotateTool,
        ScaleTool,
        TogglePlay,
        Count
    };

    struct EditorActionBinding final
    {
        EditorAction Action{};
        std::string_view DisplayName;
        std::string_view Shortcut;
    };

    /// Output: immutable default bindings displayed by menus, tooltips, and a
    /// future input preferences panel. `Shortcut` uses Cmd notation because
    /// ImGui's shortcut modifier resolves to Cmd on macOS and Ctrl elsewhere.
    [[nodiscard]] constexpr std::array<EditorActionBinding,
        static_cast<std::size_t>(EditorAction::Count)> DefaultEditorActionBindings() noexcept
    {
        return {{
            { EditorAction::Save, "Save", "Cmd+S" },
            { EditorAction::Undo, "Undo", "Cmd+Z" },
            { EditorAction::Redo, "Redo", "Cmd+Shift+Z" },
            { EditorAction::Duplicate, "Duplicate", "Cmd+D" },
            { EditorAction::DeleteSelection, "Delete", "Delete / X" },
            { EditorAction::AddPrimitive, "Add Primitive", "Shift+A" },
            { EditorAction::FocusSelection, "Focus Selection", "F" },
            { EditorAction::SelectTool, "Select Tool", "Q" },
            { EditorAction::TranslateTool, "Move Tool", "W / G" },
            { EditorAction::RotateTool, "Rotate Tool", "E / R" },
            { EditorAction::ScaleTool, "Scale Tool", "R / S" },
            { EditorAction::TogglePlay, "Play / Stop", "F5" }
        }};
    }

    [[nodiscard]] constexpr const EditorActionBinding& BindingFor(EditorAction action)
    {
        static constexpr auto bindings = DefaultEditorActionBindings();
        return bindings.at(static_cast<std::size_t>(action));
    }
}
