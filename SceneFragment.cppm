module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

export module Kairo.Editor.SceneFragment;

import Kairo.Editor.SceneEntityData;
import Kairo.EngineCore.AudioSceneComponents;
import Kairo.EngineCore.Components;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.RuntimeComponents;
import Kairo.EngineCore.Scene;

export namespace kairo::editor
{
    struct SceneFragmentEntity final
    {
        std::string Name;
        std::int32_t Parent = -1;
        kairo::engine::TransformComponent Transform;
        bool Enabled = true;
        std::uint32_t Layer = 0u;
        std::vector<std::string> Tags;
        std::optional<kairo::engine::MeshRendererComponent> MeshRenderer;
        std::optional<kairo::engine::CameraComponent> Camera;
        std::optional<kairo::engine::LightComponent> Light;
        std::optional<kairo::engine::EnvironmentComponent> Environment;
        std::optional<kairo::engine::LogicComponent> Logic;
        std::optional<kairo::engine::RigidBodyComponent> RigidBody;
        std::optional<kairo::engine::ColliderComponent> Collider;
        std::optional<kairo::engine::SceneInstanceComponent> SceneInstance;
        std::optional<kairo::engine::AudioEmitterComponent> AudioEmitter;
        std::optional<kairo::engine::AudioListenerComponent> AudioListener;
    };

    struct SceneFragment final
    {
        std::vector<SceneFragmentEntity> Entities;
    };

    struct PrefabOverride final
    {
        std::uint32_t EntityIndex = 0u;
        std::optional<std::string> Name;
        std::optional<kairo::engine::TransformComponent> Transform;
        std::optional<bool> Enabled;
    };

    struct PrefabTemplate final
    {
        std::string Name;
        std::uint32_t Version = 1u;
        SceneFragment Fragment;
    };

    namespace scene_fragment_detail
    {
        inline void Gather(const kairo::engine::Scene& scene,
            kairo::engine::Entity entity, bool descendants,
            std::vector<kairo::engine::Entity>& result,
            std::unordered_set<std::uint32_t>& visited)
        {
            std::vector<kairo::engine::Entity> pending{ entity };
            while (!pending.empty())
            {
                const auto current = pending.back();
                pending.pop_back();
                if (!visited.insert(current.Value).second) continue;
                result.push_back(current);
                if (!descendants) continue;
                const auto& children = scene.Children(current);
                pending.insert(pending.end(), children.rbegin(), children.rend());
            }
        }

        inline void Validate(const SceneFragment& fragment)
        {
            if (fragment.Entities.empty())
                throw std::invalid_argument("Scene fragment cannot be empty.");
            if (fragment.Entities.size() >
                static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
                throw std::length_error("Scene fragment contains too many entities.");
            for (std::size_t index = 0u; index < fragment.Entities.size(); ++index)
            {
                const SceneFragmentEntity& entity = fragment.Entities[index];
                if (entity.Name.empty())
                    throw std::invalid_argument("Scene fragment entity name cannot be empty.");
                if (entity.Parent < -1 ||
                    (entity.Parent >= 0 &&
                     static_cast<std::size_t>(entity.Parent) >= fragment.Entities.size()))
                    throw std::out_of_range("Scene fragment parent index is invalid.");
                if (entity.Parent == static_cast<std::int32_t>(index))
                    throw std::invalid_argument("Scene fragment entity cannot parent itself.");
                if (entity.Layer > kairo::engine::MaximumSceneLayer)
                    throw std::invalid_argument("Scene fragment layer is invalid.");
                for (const std::string& tag : entity.Tags)
                    kairo::engine::EntitySettingsComponent::ValidateTag(tag);
                ValidateAuthoredComponents(entity);
            }
            // Each parent chain is visited at most twice. Avoid quadratic
            // ancestor walks and recursive stack overflow on deep prefabs.
            std::vector<std::uint8_t> state(fragment.Entities.size(), 0u);
            for (std::size_t index = 0u; index < fragment.Entities.size(); ++index)
            {
                std::int32_t cursor = static_cast<std::int32_t>(index);
                while (cursor >= 0 && state[static_cast<std::size_t>(cursor)] == 0u)
                {
                    const auto current = static_cast<std::size_t>(cursor);
                    state[current] = 1u;
                    cursor = fragment.Entities[current].Parent;
                }
                if (cursor >= 0 && state[static_cast<std::size_t>(cursor)] == 1u)
                    throw std::invalid_argument("Scene fragment hierarchy contains a cycle.");

                cursor = static_cast<std::int32_t>(index);
                while (cursor >= 0 && state[static_cast<std::size_t>(cursor)] == 1u)
                {
                    const auto current = static_cast<std::size_t>(cursor);
                    state[current] = 2u;
                    cursor = fragment.Entities[current].Parent;
                }
            }
        }
    }

    [[nodiscard]] inline SceneFragment CaptureSceneFragment(
        const kairo::engine::Scene& scene,
        std::vector<kairo::engine::Entity> selection,
        bool includeDescendants = true)
    {
        if (selection.empty())
            throw std::invalid_argument("Scene fragment capture requires a selection.");
        std::vector<kairo::engine::Entity> captured;
        std::unordered_set<std::uint32_t> visited;
        for (kairo::engine::Entity entity : selection)
        {
            if (!scene.Contains(entity))
                throw std::out_of_range("Scene fragment selection contains an unknown entity.");
            scene_fragment_detail::Gather(scene, entity, includeDescendants, captured, visited);
        }
        std::ranges::sort(captured, {}, &kairo::engine::Entity::Value);
        std::map<std::uint32_t, std::size_t> indices;
        for (std::size_t index = 0u; index < captured.size(); ++index)
            indices.emplace(captured[index].Value, index);

        SceneFragment fragment;
        fragment.Entities.reserve(captured.size());
        for (kairo::engine::Entity source : captured)
        {
            SceneFragmentEntity entity;
            entity.Name = scene.Name(source).Value;
            entity.Transform = scene.Transform(source);
            entity.Enabled = scene.IsEnabled(source);
            entity.Layer = scene.Layer(source);
            entity.Tags = scene.Tags(source);
            if (const auto parent = scene.Parent(source); parent.has_value())
            {
                const auto found = indices.find(parent->Value);
                if (found != indices.end())
                    entity.Parent = static_cast<std::int32_t>(found->second);
            }
            CaptureAuthoredComponents(scene, source, entity);
            fragment.Entities.push_back(std::move(entity));
        }
        scene_fragment_detail::Validate(fragment);
        return fragment;
    }

    [[nodiscard]] inline std::vector<kairo::engine::Entity> PasteSceneFragment(
        kairo::engine::Scene& scene, SceneFragment fragment,
        std::optional<kairo::engine::Entity> rootParent = std::nullopt)
    {
        scene_fragment_detail::Validate(fragment);
        if (rootParent.has_value() && !scene.Contains(*rootParent))
            throw std::out_of_range("Scene fragment root parent is unknown.");
        kairo::engine::Scene candidate = scene;
        std::vector<kairo::engine::Entity> created;
        created.reserve(fragment.Entities.size());

        bool cameraPrimary = candidate.PrimaryCamera().has_value();
        bool listenerPrimary = false;
        for (const auto listener : candidate.AudioListenerEntities())
            if (candidate.AudioListenerComponentFor(listener).Primary)
                listenerPrimary = true;

        for (SceneFragmentEntity& source : fragment.Entities)
        {
            const kairo::engine::Entity entity = candidate.CreateEntity(source.Name);
            created.push_back(entity);
            candidate.Transform(entity) = source.Transform;
            candidate.SetEnabled(entity, source.Enabled);
            candidate.SetLayer(entity, source.Layer);
            for (const std::string& tag : source.Tags) candidate.AddTag(entity, tag);
            // Retain a copied primary only if the destination does not have one.
            // Disabled authored primary listeners also reserve their unique role.
            ClearCopiedPrimaryRoles(source, cameraPrimary, listenerPrimary);
            if (source.Camera && source.Camera->Primary) cameraPrimary = true;
            if (source.AudioListener && source.AudioListener->Primary) listenerPrimary = true;
            ApplyAuthoredComponents(candidate, entity, source);
        }
        for (std::size_t index = 0u; index < fragment.Entities.size(); ++index)
        {
            const std::int32_t parent = fragment.Entities[index].Parent;
            if (parent >= 0)
                candidate.SetParent(created[index], created[static_cast<std::size_t>(parent)]);
            else if (rootParent.has_value())
                candidate.SetParent(created[index], rootParent);
        }
        scene = std::move(candidate);
        return created;
    }

    // A common override resolver serves both first placement and refresh.
    [[nodiscard]] inline SceneFragment ResolvePrefab(PrefabTemplate prefab,
        std::vector<PrefabOverride> overrides = {})
    {
        if (prefab.Name.empty() || prefab.Version == 0u)
            throw std::invalid_argument("Prefab requires a name and positive version.");
        scene_fragment_detail::Validate(prefab.Fragment);
        std::ranges::sort(overrides, {}, &PrefabOverride::EntityIndex);
        for (std::size_t index = 1u; index < overrides.size(); ++index)
            if (overrides[index - 1u].EntityIndex == overrides[index].EntityIndex)
                throw std::invalid_argument("Prefab override targets must be unique.");
        for (const PrefabOverride& overrideValue : overrides)
        {
            if (overrideValue.EntityIndex >= prefab.Fragment.Entities.size())
                throw std::out_of_range("Prefab override entity index is invalid.");
            SceneFragmentEntity& target = prefab.Fragment.Entities[overrideValue.EntityIndex];
            if (overrideValue.Name.has_value())
            {
                if (overrideValue.Name->empty())
                    throw std::invalid_argument("Prefab override name cannot be empty.");
                target.Name = *overrideValue.Name;
            }
            if (overrideValue.Transform.has_value()) target.Transform = *overrideValue.Transform;
            if (overrideValue.Enabled.has_value()) target.Enabled = *overrideValue.Enabled;
        }
        return std::move(prefab.Fragment);
    }

    [[nodiscard]] inline std::vector<kairo::engine::Entity> InstantiatePrefab(
        kairo::engine::Scene& scene, PrefabTemplate prefab,
        std::vector<PrefabOverride> overrides = {},
        std::optional<kairo::engine::Entity> rootParent = std::nullopt)
    {
        return PasteSceneFragment(scene,ResolvePrefab(std::move(prefab),std::move(overrides)),rootParent);
    }

    // Explicit editor-session link. The source name and version are checked
    // on refresh; member order corresponds to the source fragment indices.
    // The scene retains its ordinary stable Entity IDs, even on refresh.
    struct LinkedPrefabInstance final
    {
        std::string TemplateName;
        std::uint32_t Version=0;
        std::vector<kairo::engine::Entity> Members;
        std::vector<std::int32_t> Parents;
        std::vector<PrefabOverride> Overrides;
        std::optional<kairo::engine::Entity> RootParent;
    };

    [[nodiscard]] inline LinkedPrefabInstance InstantiateLinkedPrefab(
        kairo::engine::Scene& scene,const PrefabTemplate& prefab,
        std::vector<PrefabOverride> overrides={},
        std::optional<kairo::engine::Entity> rootParent=std::nullopt)
    {
        LinkedPrefabInstance link;
        link.TemplateName=prefab.Name;link.Version=prefab.Version;
        link.Overrides=std::move(overrides);link.RootParent=rootParent;
        for(const auto& part:prefab.Fragment.Entities)link.Parents.push_back(part.Parent);
        link.Members=InstantiatePrefab(scene,prefab,link.Overrides,rootParent);
        return link;
    }

    // Refresh compatible revisions in place, with an all-or-nothing scene
    // copy. Index-based links intentionally reject structural revisions; a
    // future persistent part UUID schema must handle insertions/removals.
    // External children are retained; reparented/missing members fail safely.
    inline void RefreshLinkedPrefab(kairo::engine::Scene& scene,
        LinkedPrefabInstance& link,const PrefabTemplate& prefab,
        std::vector<PrefabOverride> overrides)
    {
        if(prefab.Name!=link.TemplateName||prefab.Version<link.Version)
            throw std::invalid_argument("Prefab source identity or version mismatch.");
        auto resolved=ResolvePrefab(prefab,overrides);
        if(resolved.Entities.size()!=link.Members.size()||link.Parents.size()!=link.Members.size())
            throw std::invalid_argument("Linked prefab topology changed.");
        std::unordered_set<std::uint32_t> seen;
        for(std::size_t i=0;i<link.Members.size();++i)
        {
            const auto member=link.Members[i];
            const auto parent=resolved.Entities[i].Parent;
            if(parent!=link.Parents[i]||!scene.Contains(member)||!seen.insert(member.Value).second)
                throw std::invalid_argument("Linked prefab member is missing, duplicated, or changed.");
            const auto expected=parent<0?link.RootParent:
                std::optional<kairo::engine::Entity>{link.Members[static_cast<std::size_t>(parent)]};
            if(scene.Parent(member)!=expected)
                throw std::invalid_argument("Linked prefab member was reparented.");
        }
        auto candidate=scene;
        // Unique roles must be computed from entities outside this instance
        // before removing and replacing any owned component.
        bool primaryCamera=false,primaryListener=false;
        if(const auto camera=candidate.PrimaryCamera();camera)
            primaryCamera=!seen.contains(camera->Value);
        for(const auto listener:candidate.AudioListenerEntities())
            if(!seen.contains(listener.Value)&&candidate.AudioListenerComponentFor(listener).Primary)
                primaryListener=true;
        for(const auto member:link.Members)
        {
            (void)candidate.RemoveMeshRenderer(member);(void)candidate.RemoveSceneInstance(member);
            (void)candidate.RemoveCamera(member);(void)candidate.RemoveLight(member);
            (void)candidate.RemoveEnvironment(member);(void)candidate.RemoveLogic(member);
            (void)candidate.RemoveRigidBody(member);(void)candidate.RemoveCollider(member);
            (void)candidate.RemoveAudioEmitter(member);(void)candidate.RemoveAudioListener(member);
        }
        for(std::size_t i=0;i<link.Members.size();++i)
        {
            auto& part=resolved.Entities[i];const auto member=link.Members[i];
            candidate.Name(member).Value=part.Name;
            candidate.Transform(member)=part.Transform;
            candidate.SetEnabled(member,part.Enabled);candidate.SetLayer(member,part.Layer);
            const auto oldTags=candidate.Tags(member);
            for(const auto& tag:oldTags)(void)candidate.RemoveTag(member,tag);
            for(const auto& tag:part.Tags)candidate.AddTag(member,tag);
            ClearCopiedPrimaryRoles(part,primaryCamera,primaryListener);
            if(part.Camera&&part.Camera->Primary)primaryCamera=true;
            if(part.AudioListener&&part.AudioListener->Primary)primaryListener=true;
            ApplyAuthoredComponents(candidate,member,part);
        }
        scene=std::move(candidate);
        link.Version=prefab.Version;link.Overrides=std::move(overrides);
    }

    inline void RefreshLinkedPrefab(kairo::engine::Scene& scene,
        LinkedPrefabInstance& link,const PrefabTemplate& prefab)
    {
        RefreshLinkedPrefab(scene,link,prefab,link.Overrides);
    }
}
