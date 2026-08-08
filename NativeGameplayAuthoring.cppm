module;

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.Editor.NativeGameplayAuthoring;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.NativeGameplay;
import Kairo.EngineCore.NativeGameplayManifest;
import Kairo.EngineCore.Scene;

export namespace kairo::editor
{
    using NativeGameplayRegistration =
        std::function<void(kairo::engine::NativeGameplayRegistry&)>;

    /// Output: the process-wide registry populated by linked game modules.
    /// Task: give the native Editor and Player equivalent explicit registration
    /// boundaries without loading type names from untrusted project text.
    [[nodiscard]] inline kairo::engine::NativeGameplayRegistry&
    EditorNativeGameplayRegistry() noexcept
    {
        static kairo::engine::NativeGameplayRegistry registry;
        return registry;
    }

    inline void RegisterEditorNativeGameplay(NativeGameplayRegistration registration)
    {
        if (!registration)
            throw std::invalid_argument("Editor native gameplay registration callback cannot be empty.");
        registration(EditorNativeGameplayRegistry());
    }

    struct NativeGameplayInspectorProperty final
    {
        std::string Name;
        kairo::engine::NativeGameplayPropertyType Type =
            kairo::engine::NativeGameplayPropertyType::Number;
        kairo::engine::NativeGameplayValue Value = 0.0;
        bool Exposed = true;
        std::optional<double> Minimum;
        std::optional<double> Maximum;
    };

    struct NativeGameplayInspectorSection final
    {
        kairo::engine::Entity Target{};
        std::string TypeName;
        bool Enabled = true;
        std::vector<NativeGameplayInspectorProperty> Properties;
    };

    class NativeGameplayAuthoringWorkspace final
    {
    public:
        NativeGameplayAuthoringWorkspace(
            const kairo::engine::Scene& scene,
            const kairo::engine::NativeGameplayRegistry& registry)
            : m_Scene(scene), m_Registry(registry) {}

        [[nodiscard]] const kairo::engine::NativeGameplayManifest& Manifest() const noexcept
        { return m_Manifest; }

        void NewManifest()
        {
            m_Manifest = {};
            m_Path.clear();
        }

        void Load(const std::filesystem::path& path)
        {
            auto loaded = kairo::engine::LoadNativeGameplayManifest(path);
            kairo::engine::ValidateNativeGameplayManifest(loaded, m_Scene, &m_Registry);
            m_Manifest = std::move(loaded);
            m_Path = path;
        }

        void Save(const std::filesystem::path& path)
        {
            kairo::engine::ValidateNativeGameplayManifest(m_Manifest, m_Scene, &m_Registry);
            kairo::engine::SaveNativeGameplayManifest(path, m_Manifest);
            m_Path = path;
        }

        void Save()
        {
            if (m_Path.empty()) throw std::logic_error("Native gameplay manifest has no save path.");
            Save(m_Path);
        }

        [[nodiscard]] std::vector<kairo::engine::NativeGameplayTypeInfo> AvailableTypes() const
        { return m_Registry.Types(); }

        void Attach(kairo::engine::Entity entity, std::string typeName)
        {
            if (!m_Scene.Contains(entity)) throw std::out_of_range("Native gameplay target entity does not exist.");
            const auto& type = m_Registry.Type(typeName);
            for (const auto& existing : m_Manifest.Attachments)
                if (existing.Target == entity && existing.TypeName == typeName)
                    throw std::invalid_argument("This native gameplay type is already attached to the entity.");
            kairo::engine::NativeGameplayAttachment attachment;
            attachment.Target = entity;
            attachment.TypeName = std::move(typeName);
            for (const auto& property : type.Properties)
                attachment.Properties.emplace(property.Name, property.DefaultValue);
            m_Manifest.Attachments.push_back(std::move(attachment));
            Sort();
        }

        void Remove(kairo::engine::Entity entity, std::string_view typeName)
        {
            const auto before = m_Manifest.Attachments.size();
            std::erase_if(m_Manifest.Attachments, [&](const auto& attachment) {
                return attachment.Target == entity && attachment.TypeName == typeName;
            });
            if (before == m_Manifest.Attachments.size())
                throw std::out_of_range("Native gameplay attachment was not found.");
        }

        void SetEnabled(kairo::engine::Entity entity, std::string_view typeName, bool enabled)
        { Find(entity, typeName).Enabled = enabled; }

        void SetProperty(kairo::engine::Entity entity,
            std::string_view typeName,
            std::string propertyName,
            kairo::engine::NativeGameplayValue value)
        {
            auto& attachment = Find(entity, typeName);
            const auto& type = m_Registry.Type(typeName);
            const auto property = std::find_if(type.Properties.begin(), type.Properties.end(),
                [&](const auto& candidate) { return candidate.Name == propertyName; });
            if (property == type.Properties.end())
                throw std::out_of_range("Native gameplay reflected property was not found.");
            if (!property->Exposed)
                throw std::invalid_argument("Native gameplay property is not exposed to the editor.");
            if (property->DefaultValue.index() != value.index())
                throw std::invalid_argument("Native gameplay property value has the wrong reflected type.");
            if (const auto* number = std::get_if<double>(&value))
            {
                if (property->Minimum.has_value() && *number < *property->Minimum)
                    throw std::out_of_range("Native gameplay property is below its reflected minimum.");
                if (property->Maximum.has_value() && *number > *property->Maximum)
                    throw std::out_of_range("Native gameplay property is above its reflected maximum.");
            }
            attachment.Properties.insert_or_assign(std::move(propertyName), std::move(value));
        }

        [[nodiscard]] std::vector<NativeGameplayInspectorSection> Inspect(
            kairo::engine::Entity entity) const
        {
            if (!m_Scene.Contains(entity)) throw std::out_of_range("Inspector entity does not exist.");
            std::vector<NativeGameplayInspectorSection> result;
            for (const auto& attachment : m_Manifest.Attachments)
            {
                if (attachment.Target != entity) continue;
                NativeGameplayInspectorSection section;
                section.Target = entity;
                section.TypeName = attachment.TypeName;
                section.Enabled = attachment.Enabled;
                const auto& type = m_Registry.Type(attachment.TypeName);
                for (const auto& property : type.Properties)
                {
                    const auto override = attachment.Properties.find(property.Name);
                    section.Properties.push_back({
                        property.Name,
                        property.Type,
                        override == attachment.Properties.end() ? property.DefaultValue : override->second,
                        property.Exposed,
                        property.Minimum,
                        property.Maximum });
                }
                result.push_back(std::move(section));
            }
            std::sort(result.begin(), result.end(),
                [](const auto& a, const auto& b) { return a.TypeName < b.TypeName; });
            return result;
        }

    private:
        const kairo::engine::Scene& m_Scene;
        const kairo::engine::NativeGameplayRegistry& m_Registry;
        kairo::engine::NativeGameplayManifest m_Manifest;
        std::filesystem::path m_Path;

        [[nodiscard]] kairo::engine::NativeGameplayAttachment& Find(
            kairo::engine::Entity entity, std::string_view typeName)
        {
            const auto found = std::find_if(m_Manifest.Attachments.begin(), m_Manifest.Attachments.end(),
                [&](const auto& attachment) {
                    return attachment.Target == entity && attachment.TypeName == typeName;
                });
            if (found == m_Manifest.Attachments.end())
                throw std::out_of_range("Native gameplay attachment was not found.");
            return *found;
        }

        void Sort()
        {
            std::sort(m_Manifest.Attachments.begin(), m_Manifest.Attachments.end(),
                [](const auto& a, const auto& b) {
                    if (a.Target.Value != b.Target.Value) return a.Target.Value < b.Target.Value;
                    return a.TypeName < b.TypeName;
                });
        }
    };
}
