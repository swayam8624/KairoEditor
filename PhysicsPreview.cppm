module;

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

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
                const auto& transform = runtimeScene.Transform(entity).Local;
                const bool dynamic = runtimeScene.HasRigidBody(entity);
                const auto halfExtents = PositiveHalfExtents(transform.Scale);
                kairo::foundation::physics::RigidBodyDesc body;
                body.Type = dynamic ? kairo::foundation::physics::BodyType::Dynamic :
                    kairo::foundation::physics::BodyType::Static;
                body.State.Position = transform.Translation;
                body.State.Rotation = transform.Rotation;
                if (dynamic)
                    body.Mass = kairo::foundation::physics::BoxMassProperties(halfExtents, 1.0f);
                const auto bodyID = m_World.CreateRigidBody(body);
                const auto colliderID = m_World.AddCollider(bodyID,
                    kairo::foundation::physics::BoxCollider{ halfExtents });
                runtimeScene.SetRigidBody(entity, { bodyID });
                runtimeScene.SetCollider(entity, { colliderID });
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
                auto& transform = runtimeScene.Transform(entity).Local;
                transform.Translation = body.State.Position;
                transform.Rotation = body.State.Rotation;
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

        [[nodiscard]] static kairo::foundation::math::Vec3f PositiveHalfExtents(
            const kairo::foundation::math::Vec3f& scale) noexcept
        {
            return { std::max(0.05f, std::abs(scale.x)), std::max(0.05f, std::abs(scale.y)),
                std::max(0.05f, std::abs(scale.z)) };
        }
    };
}
