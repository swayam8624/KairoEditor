module;

// MSVC's IFC importer instantiates Entity comparison and optional traits here.
// Keep the standard declarations visible in this importing translation unit.
#include <compare>
#include <optional>

export module Kairo.Editor.SceneEntityData;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.RuntimeComponents;
import Kairo.EngineCore.AudioSceneComponents;
import Kairo.EngineCore.Scene;

export namespace kairo::editor
{
    // The same authored-component transfer policy serves undo snapshots,
    // duplicate, clipboard and prefabs. Identity and hierarchy are handled
    // by the caller; no runtime GPU, mixer or physics handles enter this path.
    template<class Data>
    void CaptureAuthoredComponents(const kairo::engine::Scene& scene,
        kairo::engine::Entity entity, Data& data)
    {
        if (scene.HasMeshRenderer(entity)) data.MeshRenderer = scene.MeshRenderer(entity);
        if (scene.HasSceneInstance(entity)) data.SceneInstance = scene.SceneInstance(entity);
        if (scene.HasCamera(entity)) data.Camera = scene.Camera(entity);
        if (scene.HasLight(entity)) data.Light = scene.Light(entity);
        if (scene.HasEnvironment(entity)) data.Environment = scene.Environment(entity);
        if (scene.HasLogic(entity)) data.Logic = scene.Logic(entity);
        if (scene.HasRigidBody(entity)) data.RigidBody = scene.RigidBody(entity);
        if (scene.HasCollider(entity)) data.Collider = scene.Collider(entity);
        if (scene.HasAudioEmitter(entity)) data.AudioEmitter = scene.AudioEmitter(entity);
        if (scene.HasAudioListener(entity)) data.AudioListener = scene.AudioListenerComponentFor(entity);
    }

    template<class Data>
    void ValidateAuthoredComponents(const Data& data)
    {
        if (data.MeshRenderer) data.MeshRenderer->Validate();
        if (data.SceneInstance) data.SceneInstance->Validate();
        if (data.Camera) data.Camera->Validate();
        if (data.Light) data.Light->Validate();
        if (data.Environment) data.Environment->Validate();
        if (data.Logic) data.Logic->Validate();
        if (data.RigidBody) data.RigidBody->Validate();
        if (data.Collider) data.Collider->Validate();
        if (data.AudioEmitter) data.AudioEmitter->Validate();
        if (data.AudioListener) data.AudioListener->Validate();
    }

    // Cloning must not steal an existing scene's unique primary roles.
    // Undo uses the unmodified snapshot to restore the original roles.
    template<class Data>
    void ClearCopiedPrimaryRoles(Data& data, bool cameraAlreadyPrimary = true,
        bool listenerAlreadyPrimary = true) noexcept
    {
        if (cameraAlreadyPrimary && data.Camera) data.Camera->Primary = false;
        if (listenerAlreadyPrimary && data.AudioListener) data.AudioListener->Primary = false;
    }

    template<class Data>
    void ApplyAuthoredComponents(kairo::engine::Scene& scene,
        kairo::engine::Entity entity, const Data& data)
    {
        if (data.MeshRenderer) scene.SetMeshRenderer(entity, *data.MeshRenderer);
        if (data.SceneInstance) scene.SetSceneInstance(entity, *data.SceneInstance);
        if (data.Camera) scene.SetCamera(entity, *data.Camera);
        if (data.Light) scene.SetLight(entity, *data.Light);
        if (data.Environment) scene.SetEnvironment(entity, *data.Environment);
        if (data.Logic) scene.SetLogic(entity, *data.Logic);
        if (data.RigidBody) scene.SetRigidBody(entity, *data.RigidBody);
        if (data.Collider) scene.SetCollider(entity, *data.Collider);
        if (data.AudioEmitter) scene.SetAudioEmitter(entity, *data.AudioEmitter);
        if (data.AudioListener) scene.SetAudioListener(entity, *data.AudioListener);
    }
}
