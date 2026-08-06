module;

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <vector>

export module Kairo.Editor.SceneSelection;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Scene;

export namespace kairo::editor
{
    class SceneSelection final
    {
    public:
        [[nodiscard]] const std::vector<kairo::engine::Entity>& Entities() const noexcept
        { return m_Selected; }
        [[nodiscard]] std::optional<kairo::engine::Entity> Active() const noexcept
        { return m_Active; }
        [[nodiscard]] bool Empty() const noexcept { return m_Selected.empty(); }
        [[nodiscard]] bool Contains(kairo::engine::Entity entity) const noexcept
        { return std::ranges::binary_search(m_Selected, entity, {}, &kairo::engine::Entity::Value); }

        void Clear() noexcept
        {
            m_Selected.clear();
            m_Active.reset();
            m_Anchor.reset();
        }

        void SelectOnly(kairo::engine::Entity entity)
        {
            m_Selected = { entity };
            m_Active = entity;
            m_Anchor = entity;
        }

        void Toggle(kairo::engine::Entity entity)
        {
            const auto found = std::ranges::lower_bound(
                m_Selected, entity.Value, {}, &kairo::engine::Entity::Value);
            if (found != m_Selected.end() && *found == entity)
            {
                m_Selected.erase(found);
                if (m_Active == entity)
                    m_Active = m_Selected.empty()
                        ? std::nullopt : std::optional<kairo::engine::Entity>{ m_Selected.back() };
            }
            else
            {
                m_Selected.insert(found, entity);
                m_Active = entity;
                m_Anchor = entity;
            }
        }

        void SelectRange(const std::vector<kairo::engine::Entity>& visualOrder,
            kairo::engine::Entity target, bool additive = false)
        {
            const auto targetIt = std::ranges::find(visualOrder, target);
            if (targetIt == visualOrder.end()) return;
            if (!m_Anchor.has_value() ||
                std::ranges::find(visualOrder, *m_Anchor) == visualOrder.end())
                m_Anchor = target;
            const auto anchorIt = std::ranges::find(visualOrder, *m_Anchor);
            const auto first = std::min(anchorIt, targetIt);
            const auto last = std::max(anchorIt, targetIt);
            if (!additive) m_Selected.clear();
            for (auto iterator = first; iterator != last + 1; ++iterator)
                Insert(*iterator);
            m_Active = target;
        }

        void ReplaceFromMarquee(std::vector<kairo::engine::Entity> hits, bool additive)
        {
            std::ranges::sort(hits, {}, &kairo::engine::Entity::Value);
            hits.erase(std::ranges::unique(hits).begin(), hits.end());
            if (!additive) m_Selected.clear();
            for (kairo::engine::Entity entity : hits) Insert(entity);
            if (!hits.empty())
            {
                m_Active = hits.back();
                m_Anchor = hits.back();
            }
            else if (!additive)
            {
                m_Active.reset();
                m_Anchor.reset();
            }
        }

        void SelectAll(const kairo::engine::Scene& scene)
        {
            m_Selected = scene.Entities();
            m_Active = m_Selected.empty()
                ? std::nullopt : std::optional<kairo::engine::Entity>{ m_Selected.back() };
            m_Anchor = m_Active;
        }

        void Prune(const kairo::engine::Scene& scene)
        {
            std::erase_if(m_Selected,
                [&scene](kairo::engine::Entity entity) { return !scene.Contains(entity); });
            if (m_Active.has_value() && !scene.Contains(*m_Active))
                m_Active = m_Selected.empty()
                    ? std::nullopt : std::optional<kairo::engine::Entity>{ m_Selected.back() };
            if (m_Anchor.has_value() && !scene.Contains(*m_Anchor))
                m_Anchor = m_Active;
        }

    private:
        std::vector<kairo::engine::Entity> m_Selected;
        std::optional<kairo::engine::Entity> m_Active;
        std::optional<kairo::engine::Entity> m_Anchor;

        void Insert(kairo::engine::Entity entity)
        {
            const auto insertion = std::ranges::lower_bound(
                m_Selected, entity.Value, {}, &kairo::engine::Entity::Value);
            if (insertion == m_Selected.end() || *insertion != entity)
                m_Selected.insert(insertion, entity);
        }
    };
}
