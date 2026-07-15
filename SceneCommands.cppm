module;

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

export module Kairo.Editor.SceneCommands;

import Kairo.Editor.CommandHistory;
import Kairo.Editor.ProjectSession;
import Kairo.EngineCore;
import Kairo.Foundation.Math;

export namespace kairo::editor
{
    /// Creates one scene entity and preserves its allocated identity across
    /// undo/redo. CreatedEntity is valid after the first successful Execute.
    class CreateEntityCommand final : public EditorCommand
    {
    public:
        CreateEntityCommand(ProjectSession& project, std::string name = "Entity")
            : m_Project(&project), m_Name(std::move(name)) {}

        [[nodiscard]] std::string_view Name() const noexcept override { return "Create Entity"; }
        [[nodiscard]] kairo::engine::Entity CreatedEntity() const
        {
            if (!m_Entity.has_value()) throw std::logic_error("Create Entity has not executed yet.");
            return *m_Entity;
        }

        void Execute() override
        {
            auto& scene = m_Project->EditScene();
            if (m_Entity.has_value())
                (void)scene.CreateEntityWithID(*m_Entity, m_Name);
            else
                m_Entity = scene.CreateEntity(m_Name);
        }

        void Undo() override { m_Project->EditScene().DestroyEntity(CreatedEntity()); }

    private:
        ProjectSession* m_Project;
        std::string m_Name;
        std::optional<kairo::engine::Entity> m_Entity;
    };

    /// A complete entity snapshot for the component types EngineCore currently
    /// exposes. Opaque physics bindings are retained as authored references;
    /// this command never reaches into or snapshots an external runtime world.
    struct EntitySnapshot final
    {
        kairo::engine::Entity ID;
        std::string Name;
        kairo::foundation::math::Transformf Transform;
        std::optional<kairo::engine::MeshRendererComponent> MeshRenderer;
        std::optional<kairo::engine::CameraComponent> Camera;
        std::optional<kairo::engine::RigidBodyComponent> RigidBody;
        std::optional<kairo::engine::ColliderComponent> Collider;
    };

    [[nodiscard]] inline EntitySnapshot CaptureEntity(
        const kairo::engine::Scene& scene, kairo::engine::Entity entity)
    {
        EntitySnapshot result{ entity, scene.Name(entity).Value, scene.Transform(entity).Local,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt };
        if (scene.HasMeshRenderer(entity)) result.MeshRenderer = scene.MeshRenderer(entity);
        if (scene.HasCamera(entity)) result.Camera = scene.Camera(entity);
        if (scene.HasRigidBody(entity)) result.RigidBody = scene.RigidBody(entity);
        if (scene.HasCollider(entity)) result.Collider = scene.Collider(entity);
        return result;
    }

    inline void RestoreEntity(kairo::engine::Scene& scene, const EntitySnapshot& snapshot)
    {
        const auto entity = scene.CreateEntityWithID(snapshot.ID, snapshot.Name);
        scene.Transform(entity).Local = snapshot.Transform;
        if (snapshot.MeshRenderer.has_value()) scene.SetMeshRenderer(entity, *snapshot.MeshRenderer);
        if (snapshot.Camera.has_value()) scene.SetCamera(entity, *snapshot.Camera);
        if (snapshot.RigidBody.has_value()) scene.SetRigidBody(entity, *snapshot.RigidBody);
        if (snapshot.Collider.has_value()) scene.SetCollider(entity, *snapshot.Collider);
    }

    /// Removes one entity while retaining all persistent components needed to
    /// restore the exact authored state and stable scene-local ID.
    class DeleteEntityCommand final : public EditorCommand
    {
    public:
        DeleteEntityCommand(ProjectSession& project, kairo::engine::Entity entity)
            : m_Project(&project), m_Snapshot(CaptureEntity(project.Scene(), entity)) {}

        [[nodiscard]] std::string_view Name() const noexcept override { return "Delete Entity"; }
        void Execute() override { m_Project->EditScene().DestroyEntity(m_Snapshot.ID); }
        void Undo() override { RestoreEntity(m_Project->EditScene(), m_Snapshot); }

    private:
        ProjectSession* m_Project;
        EntitySnapshot m_Snapshot;
    };

    /// Replaces an entity name. Adjacent rename keystrokes merge into one user
    /// operation while retaining the value from before editing began.
    class SetEntityNameCommand final : public EditorCommand
    {
    public:
        SetEntityNameCommand(ProjectSession& project, kairo::engine::Entity entity, std::string value)
            : m_Project(&project), m_Entity(entity), m_Before(project.Scene().Name(entity).Value),
              m_After(std::move(value)) {}

        [[nodiscard]] std::string_view Name() const noexcept override { return "Rename Entity"; }
        void Execute() override { m_Project->EditScene().Name(m_Entity).Value = m_After; }
        void Undo() override { m_Project->EditScene().Name(m_Entity).Value = m_Before; }

        [[nodiscard]] bool TryMerge(EditorCommand& newer) noexcept override
        {
            auto* rename = dynamic_cast<SetEntityNameCommand*>(&newer);
            if (rename == nullptr || rename->m_Project != m_Project || rename->m_Entity != m_Entity) return false;
            m_After.swap(rename->m_After);
            return true;
        }

    private:
        ProjectSession* m_Project;
        kairo::engine::Entity m_Entity;
        std::string m_Before;
        std::string m_After;
    };

    /// Replaces the complete local TRS atomically so position/rotation/scale
    /// cannot become partially undoable. Adjacent drag frames coalesce.
    class SetEntityTransformCommand final : public EditorCommand
    {
    public:
        SetEntityTransformCommand(ProjectSession& project, kairo::engine::Entity entity,
            kairo::foundation::math::Transformf value)
            : m_Project(&project), m_Entity(entity), m_Before(project.Scene().Transform(entity).Local),
              m_After(std::move(value)) {}

        [[nodiscard]] std::string_view Name() const noexcept override { return "Edit Transform"; }
        void Execute() override { m_Project->EditScene().Transform(m_Entity).Local = m_After; }
        void Undo() override { m_Project->EditScene().Transform(m_Entity).Local = m_Before; }

        [[nodiscard]] bool TryMerge(EditorCommand& newer) noexcept override
        {
            auto* transform = dynamic_cast<SetEntityTransformCommand*>(&newer);
            if (transform == nullptr || transform->m_Project != m_Project || transform->m_Entity != m_Entity) return false;
            m_After = transform->m_After;
            return true;
        }

    private:
        ProjectSession* m_Project;
        kairo::engine::Entity m_Entity;
        kairo::foundation::math::Transformf m_Before;
        kairo::foundation::math::Transformf m_After;
    };
}
