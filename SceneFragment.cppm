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

export module Kairo.Editor.SceneFragment;

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
        std::optional<kairo::engine::LogicComponent> Logic;
        std::optional<kairo::engine::RigidBodyComponent> RigidBody;
        std::optional<kairo::engine::ColliderComponent> Collider;
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
            std::vector<kairo::engine::Entity>& result)
        {
            if (std::ranges::find(result, entity) != result.end()) return;
            result.push_back(entity);
            if (!descendants) return;
            for (kairo::engine::Entity child : scene.Children(entity))
                Gather(scene, child, true, result);
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
                if (entity.MeshRenderer.has_value()) entity.MeshRenderer->Validate();
                if (entity.Camera.has_value()) entity.Camera->Validate();
                if (entity.Logic.has_value()) entity.Logic->Validate();
                if (entity.RigidBody.has_value()) entity.RigidBody->Validate();
                if (entity.Collider.has_value()) entity.Collider->Validate();
            }
            for (std::size_t index = 0u; index < fragment.Entities.size(); ++index)
            {
                std::size_t cursor = index;
                std::size_t steps = 0u;
                while (fragment.Entities[cursor].Parent >= 0)
                {
                    cursor = static_cast<std::size_t>(fragment.Entities[cursor].Parent);
                    if (++steps > fragment.Entities.size())
                        throw std::invalid_argument("Scene fragment hierarchy contains a cycle.");
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
        for (kairo::engine::Entity entity : selection)
        {
            if (!scene.Contains(entity))
                throw std::out_of_range("Scene fragment selection contains an unknown entity.");
            scene_fragment_detail::Gather(scene, entity, includeDescendants, captured);
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
            if (scene.HasMeshRenderer(source)) entity.MeshRenderer = scene.MeshRenderer(source);
            if (scene.HasCamera(source)) entity.Camera = scene.Camera(source);
            if (scene.HasLogic(source)) entity.Logic = scene.Logic(source);
            if (scene.HasRigidBody(source)) entity.RigidBody = scene.RigidBody(source);
            if (scene.HasCollider(source)) entity.Collider = scene.Collider(source);
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

        for (const SceneFragmentEntity& source : fragment.Entities)
        {
            const kairo::engine::Entity entity = candidate.CreateEntity(source.Name);
            created.push_back(entity);
            candidate.Transform(entity) = source.Transform;
            candidate.SetEnabled(entity, source.Enabled);
            candidate.SetLayer(entity, source.Layer);
            for (const std::string& tag : source.Tags) candidate.AddTag(entity, tag);
            if (source.MeshRenderer.has_value()) candidate.SetMeshRenderer(entity, *source.MeshRenderer);
            if (source.Camera.has_value()) candidate.SetCamera(entity, *source.Camera);
            if (source.Logic.has_value()) candidate.SetLogic(entity, *source.Logic);
            if (source.RigidBody.has_value()) candidate.SetRigidBody(entity, *source.RigidBody);
            if (source.Collider.has_value()) candidate.SetCollider(entity, *source.Collider);
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

    [[nodiscard]] inline std::vector<kairo::engine::Entity> InstantiatePrefab(
        kairo::engine::Scene& scene, PrefabTemplate prefab,
        std::vector<PrefabOverride> overrides = {},
        std::optional<kairo::engine::Entity> rootParent = std::nullopt)
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
        return PasteSceneFragment(scene, std::move(prefab.Fragment), rootParent);
    }
}
