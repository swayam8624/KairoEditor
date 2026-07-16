module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <type_traits>
#include <variant>

export module Kairo.Editor.PhysicsPreview;

import Kairo.EngineCore;
import Kairo.Foundation.Math;
import Kairo.Foundation.PhysicsEngine;
import Kairo.Foundation.PhysicsMath;
import Kairo.Renderer.PhysicsDebugBridge;
import Kairo.Renderer.DebugDraw;

export namespace kairo::editor
{
    /// Input: an isolated runtime scene copy containing entities marked with
    /// `RigidBodyComponent` and/or `ColliderComponent`.
    /// Output: a deterministic KairoPhysicsEngine simulation and renderer debug
    /// lines. Task: provide a truthful editor Play preview without allowing
    /// simulation steps to mutate or silently save the authored scene.
    class PhysicsPreview final
    {
    public:
        void Start(kairo::engine::Scene& runtimeScene)
        {
            Reset();
            for (const kairo::engine::Entity entity : runtimeScene.Entities())
            {
                if (!runtimeScene.HasRigidBody(entity) && !runtimeScene.HasCollider(entity)) continue;
                if (runtimeScene.HasRigidBody(entity) && !runtimeScene.HasCollider(entity))
                    throw std::invalid_argument("A rigid body requires an authored collider in physics preview.");
                const auto transform = runtimeScene.WorldTransform(entity);
                const auto authoredCollider = runtimeScene.HasCollider(entity)
                    ? runtimeScene.Collider(entity) : kairo::engine::ColliderComponent{};
                const auto shape = MakeShape(authoredCollider, transform.Scale);
                kairo::foundation::physics::RigidBodyDesc body;
                const auto authoredBody = runtimeScene.HasRigidBody(entity)
                    ? runtimeScene.RigidBody(entity) : kairo::engine::RigidBodyComponent{
                        .Motion = kairo::engine::RigidBodyMotion::Static };
                body.Type = ToRuntimeMotion(authoredBody.Motion);
                body.State.Position = transform.Translation;
                body.State.Rotation = transform.Rotation;
                body.GravityScale = authoredBody.GravityScale;
                body.LinearDamping = authoredBody.LinearDamping;
                body.AngularDamping = authoredBody.AngularDamping;
                if (body.Type == kairo::foundation::physics::BodyType::Dynamic)
                    body.Mass = MakeMass(shape, authoredBody.Density);
                const auto bodyID = m_World.CreateRigidBody(body);
                const kairo::foundation::physics::PhysicsMaterial material{
                    authoredCollider.Restitution, authoredCollider.Friction, authoredCollider.Friction };
                const auto colliderID = m_World.AddCollider(bodyID, shape, material);
                m_World.SetCollisionFilter(colliderID,
                    authoredCollider.BelongsTo, authoredCollider.CollidesWith);
                m_World.SetColliderTrigger(colliderID, authoredCollider.IsTrigger);
                m_Entities.emplace(entity.Value, bodyID);
            }
            m_Active = true;
        }

        void Step(kairo::engine::Scene& runtimeScene, float deltaSeconds)
        {
            if (!m_Active) return;
            if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
                throw std::invalid_argument("Physics preview requires finite non-negative delta time.");
            // Bound editor stalls so returning from a breakpoint does not make
            // an authoring preview request an excessively large physics step.
            m_World.Step(std::min(deltaSeconds, 1.0f / 20.0f));
            for (const auto& [entityValue, bodyID] : m_Entities)
            {
                const kairo::engine::Entity entity{ entityValue };
                if (!runtimeScene.Contains(entity) || !m_World.IsValidBody(bodyID)) continue;
                const auto& body = m_World.Bodies().at(bodyID);
                auto world = runtimeScene.WorldTransform(entity);
                world.Translation = body.State.Position;
                world.Rotation = body.State.Rotation;
                runtimeScene.Transform(entity).Local = ToLocal(runtimeScene, entity, world);
            }
        }

        void Reset() noexcept
        {
            m_World = {};
            m_Entities.clear();
            m_Active = false;
        }

        [[nodiscard]] bool Active() const noexcept { return m_Active; }
        [[nodiscard]] const kairo::foundation::physics::PhysicsWorld& World() const noexcept { return m_World; }

        [[nodiscard]] kairo::renderer::DebugDrawList DebugDraw(bool broadphase = false) const
        {
            kairo::renderer::PhysicsDebugDrawOptions options;
            options.DrawBroadphaseAABBs = broadphase;
            return kairo::renderer::BuildPhysicsDebugDraw(m_World, options);
        }

    private:
        kairo::foundation::physics::PhysicsWorld m_World;
        std::unordered_map<std::uint32_t, kairo::foundation::physics::BodyID> m_Entities;
        bool m_Active = false;

        [[nodiscard]] static kairo::foundation::physics::BodyType ToRuntimeMotion(
            kairo::engine::RigidBodyMotion motion)
        {
            switch (motion)
            {
                case kairo::engine::RigidBodyMotion::Static:
                    return kairo::foundation::physics::BodyType::Static;
                case kairo::engine::RigidBodyMotion::Dynamic:
                    return kairo::foundation::physics::BodyType::Dynamic;
                case kairo::engine::RigidBodyMotion::Kinematic:
                    return kairo::foundation::physics::BodyType::Kinematic;
            }
            throw std::invalid_argument("Authored rigid body motion enum is invalid.");
        }

        [[nodiscard]] static kairo::foundation::physics::ColliderShape MakeShape(
            const kairo::engine::ColliderComponent& collider,
            const kairo::foundation::math::Vec3f& scale)
        {
            const kairo::foundation::math::Vec3f absolute{
                std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) };
            switch (collider.Shape)
            {
                case kairo::engine::ColliderShape::Box:
                    return kairo::foundation::physics::BoxCollider{
                        collider.HalfExtents * absolute };
                case kairo::engine::ColliderShape::Sphere:
                    return kairo::foundation::physics::SphereCollider{
                        collider.Radius * std::max({ absolute.x, absolute.y, absolute.z }) };
                case kairo::engine::ColliderShape::Capsule:
                    return kairo::foundation::physics::CapsuleCollider{
                        collider.Radius * std::max(absolute.x, absolute.z),
                        collider.HalfHeight * absolute.y };
            }
            throw std::invalid_argument("Authored collider shape enum is invalid.");
        }

        [[nodiscard]] static kairo::foundation::physics::MassProperties MakeMass(
            const kairo::foundation::physics::ColliderShape& shape, float density)
        {
            return std::visit([density](const auto& value) -> kairo::foundation::physics::MassProperties
            {
                using Shape = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Shape, kairo::foundation::physics::BoxCollider>)
                    return kairo::foundation::physics::BoxMassProperties(value.HalfExtents, density);
                else if constexpr (std::is_same_v<Shape, kairo::foundation::physics::SphereCollider>)
                    return kairo::foundation::physics::SphereMassProperties(value.Radius, density);
                else if constexpr (std::is_same_v<Shape, kairo::foundation::physics::CapsuleCollider>)
                    return kairo::foundation::physics::CapsuleMassProperties(
                        value.Radius, value.HalfHeight * 2.0f, density);
                else
                    throw std::invalid_argument("Dynamic preview bodies require a finite primitive collider.");
            }, shape);
        }

        [[nodiscard]] static kairo::foundation::math::Transformf ToLocal(
            const kairo::engine::Scene& scene, kairo::engine::Entity entity,
            const kairo::foundation::math::Transformf& world)
        {
            const auto parent = scene.Parent(entity);
            if (!parent.has_value()) return world;
            const auto parentWorld = scene.WorldTransform(*parent);
            constexpr float epsilon = std::numeric_limits<float>::epsilon() * 10.0f;
            if (std::abs(parentWorld.Scale.x) <= epsilon ||
                std::abs(parentWorld.Scale.y) <= epsilon ||
                std::abs(parentWorld.Scale.z) <= epsilon)
                throw std::invalid_argument("Cannot update physics below a zero-scale parent.");
            auto local = world;
            local.Translation = kairo::foundation::math::WorldToLocal(parentWorld, world.Translation);
            local.Rotation = (kairo::foundation::math::Inverse(parentWorld.Rotation) *
                world.Rotation).Normalized();
            local.Scale = {
                world.Scale.x / parentWorld.Scale.x,
                world.Scale.y / parentWorld.Scale.y,
                world.Scale.z / parentWorld.Scale.z };
            return local;
        }
    };
}
