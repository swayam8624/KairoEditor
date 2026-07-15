module;

#include <array>
#include <cstddef>
#include <stdexcept>

export module Kairo.Editor.PanelState;

import Kairo.Editor.Types;

export namespace kairo::editor
{
    /// Persistent panel visibility used by the eventual docking frontend.
    /// Task: let UI code restore a deliberate workspace without putting any
    /// ImGui types or persistence format into the editor domain model.
    class PanelState final
    {
    public:
        PanelState()
        {
            m_Visible.fill(true);
        }

        [[nodiscard]] bool IsVisible(Panel panel) const { return m_Visible.at(Index(panel)); }
        void SetVisible(Panel panel, bool visible) { m_Visible.at(Index(panel)) = visible; }
        void Toggle(Panel panel) { m_Visible.at(Index(panel)) = !m_Visible.at(Index(panel)); }

    private:
        std::array<bool, 6u> m_Visible{};
        [[nodiscard]] static constexpr std::size_t Index(Panel panel)
        {
            const auto index = static_cast<std::size_t>(panel);
            if (index >= 6u) throw std::out_of_range("Unknown KairoEditor panel.");
            return index;
        }
    };
}
