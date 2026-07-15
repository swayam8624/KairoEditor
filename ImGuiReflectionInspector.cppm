module;

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

export module Kairo.Editor.ImGuiReflectionInspector;

import Kairo.Editor.UI;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Reflection;
import Kairo.EngineCore.Scene;
import Kairo.Reflection;

namespace kairo::editor
{
    [[nodiscard]] bool DrawPropertyEditor(const kairo::reflection::PropertyDescriptor& property,
        const kairo::reflection::PropertyValue& current, kairo::reflection::PropertyValue& edited);
}

export namespace kairo::editor
{
    /// A feature-owned callback that commits one semantic property change.
    /// Inputs are stable reflection keys, never display labels, so document
    /// names and localization can evolve without changing command identity.
    using ReflectedPropertyCommit = std::function<void(std::string_view typeKey,
        std::string_view propertyKey, const kairo::reflection::PropertyValue& value)>;

    /// ImGui implementation of the reflection-driven scalar inspector.
    /// Input: a registry, Scene entity, and command-producing callback.
    /// Output: component sections and editors for every currently present,
    /// registered EngineCore component. Task: keep widget choice in the UI
    /// backend while metadata, values, and mutations remain engine-owned.
    /// Complex properties such as Transform are intentionally not rendered
    /// here until their composite reflection adapters have an equally stable
    /// value contract.
    inline void DrawReflectedInspector(const kairo::reflection::ReflectionRegistry& registry,
        kairo::engine::Scene& scene, kairo::engine::Entity entity,
        const ReflectedPropertyCommit& commit)
    {
        using namespace kairo::reflection;

        for (const kairo::engine::ReflectedSceneComponent component :
            kairo::engine::EnumerateReflectedComponents(scene, entity))
        {
            const TypeDescriptor& type = registry.Require(component.TypeKey);
            SectionHeader(type.DisplayName);
            ImGui::PushID(type.Key.c_str());
            for (const PropertyDescriptor& property : type.Properties)
            {
                const PropertyValue current = registry.Read(type.Key, property.Metadata.Key, component.Object);
                PropertyValue edited = current;
                const bool readOnly = HasFlag(property.Metadata.Flags, PropertyFlags::ReadOnly);
                if (readOnly) ImGui::BeginDisabled();
                const bool changed = DrawPropertyEditor(property, current, edited);
                if (readOnly) ImGui::EndDisabled();

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !property.Metadata.Tooltip.empty())
                    ImGui::SetTooltip("%s", property.Metadata.Tooltip.c_str());
                if (changed && !readOnly && edited != current)
                    commit(type.Key, property.Metadata.Key, edited);
            }
            ImGui::PopID();
        }
    }
}

namespace kairo::editor
{
    [[nodiscard]] inline bool DrawPropertyEditor(const kairo::reflection::PropertyDescriptor& property,
        const kairo::reflection::PropertyValue& current, kairo::reflection::PropertyValue& edited)
    {
        using namespace kairo::reflection;
        const char* label = property.Metadata.DisplayName.c_str();
        switch (property.ValueKind)
        {
        case PropertyValueKind::Boolean:
        {
            bool value = current.Get<bool>();
            if (!ImGui::Checkbox(label, &value)) return false;
            edited = PropertyValue(value);
            return true;
        }
        case PropertyValueKind::SignedInteger:
        {
            std::int64_t value = current.Get<std::int64_t>();
            if (!ImGui::DragScalar(label, ImGuiDataType_S64, &value, 1.0f, nullptr, nullptr, "%lld", ImGuiSliderFlags_AlwaysClamp))
                return false;
            edited = PropertyValue(value);
            return true;
        }
        case PropertyValueKind::UnsignedInteger:
        {
            std::uint64_t value = current.Get<std::uint64_t>();
            if (!ImGui::DragScalar(label, ImGuiDataType_U64, &value, 1.0f, nullptr, nullptr, "%llu", ImGuiSliderFlags_AlwaysClamp))
                return false;
            edited = PropertyValue(value);
            return true;
        }
        case PropertyValueKind::FloatingPoint:
        {
            double value = current.Get<double>();
            const NumericRange* range = property.Metadata.Range ? &*property.Metadata.Range : nullptr;
            const double minimum = range ? range->Minimum : -std::numeric_limits<double>::max();
            const double maximum = range ? range->Maximum : std::numeric_limits<double>::max();
            const double step = range && range->Step > 0.0 ? range->Step : 0.01;
            if (!ImGui::DragScalar(label, ImGuiDataType_Double, &value, static_cast<float>(step),
                range ? &minimum : nullptr, range ? &maximum : nullptr, "%.6g", ImGuiSliderFlags_AlwaysClamp))
                return false;
            edited = PropertyValue(value);
            return true;
        }
        case PropertyValueKind::String:
        {
            const std::size_t byteLimit = property.Metadata.MaximumStringBytes == 0u
                ? std::size_t{ 4096u } : std::min(property.Metadata.MaximumStringBytes, std::size_t{ 4096u });
            std::vector<char> buffer(byteLimit + 1u, '\0');
            const std::string& value = current.Get<std::string>();
            std::copy_n(value.data(), std::min(value.size(), byteLimit), buffer.data());
            const bool changed = HasFlag(property.Metadata.Flags, PropertyFlags::Multiline)
                ? ImGui::InputTextMultiline(label, buffer.data(), buffer.size())
                : ImGui::InputText(label, buffer.data(), buffer.size());
            if (!changed) return false;
            edited = PropertyValue(std::string(buffer.data()));
            return true;
        }
        }
        return false;
    }
}
