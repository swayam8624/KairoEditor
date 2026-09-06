module;

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

export module Kairo.Editor.AssetScenePlacement;

import Kairo.Assets;
import Kairo.Editor.CommandHistory;
import Kairo.Editor.PrimitiveTypes;
import Kairo.Editor.ProjectSession;
import Kairo.EngineCore;

export namespace kairo::editor
{
    [[nodiscard]] constexpr bool CanPlaceAssetInScene(kairo::assets::AssetType type) noexcept
    {
        return type == kairo::assets::AssetType::Mesh ||
            type == kairo::assets::AssetType::Scene;
    }

    /// Creates one scene entity from a persistent asset reference as a single
    /// undoable command. Mesh drops use the same validated default material as
    /// primitive creation; scene drops create a SceneInstanceComponent.
    class PlaceAssetInSceneCommand final : public EditorCommand
    {
    public:
        PlaceAssetInSceneCommand(ProjectSession& project, kairo::assets::AssetID asset)
            : m_Project(&project), m_Asset(asset)
        {
            if (!m_Asset.IsValid())
                throw std::invalid_argument("Scene placement requires a valid asset ID.");
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "Place Asset In Scene";
        }

        [[nodiscard]] kairo::engine::Entity CreatedEntity() const
        {
            if (!m_Entity.has_value())
                throw std::logic_error("Place Asset In Scene has not executed yet.");
            return *m_Entity;
        }

        void Execute() override
        {
            const auto metadata = m_Project->Assets().At(m_Asset);
            if (!CanPlaceAssetInScene(metadata.Type))
                throw std::invalid_argument("This asset type cannot be placed directly in a scene.");

            // Resolve every referenced asset before mutating the scene, keeping
            // placement atomic when a project catalog is incomplete.
            std::optional<kairo::assets::MaterialAssetHandle> material;
            if (metadata.Type == kairo::assets::AssetType::Mesh)
            {
                (void)m_Project->Assets().Resolve(kairo::assets::MeshAssetHandle{ m_Asset });
                material = DefaultPrimitiveMaterial();
                (void)m_Project->Assets().Resolve(*material);
            }
            else
            {
                (void)m_Project->Assets().Resolve(kairo::assets::SceneAssetHandle{ m_Asset });
            }

            auto& scene = m_Project->EditScene();
            std::string name = metadata.Path.stem().string();
            if (name.empty()) name = metadata.Type == kairo::assets::AssetType::Mesh
                ? "Mesh" : "Scene";
            const auto entity = m_Entity.has_value()
                ? scene.CreateEntityWithID(*m_Entity, name)
                : scene.CreateEntity(name);
            if (!m_Entity.has_value()) m_Entity = entity;

            if (metadata.Type == kairo::assets::AssetType::Mesh)
            {
                scene.SetMeshRenderer(entity, {
                    kairo::assets::MeshAssetHandle{ m_Asset }, *material, true });
            }
            else
            {
                kairo::engine::SceneInstanceComponent instance;
                instance.SceneAsset = kairo::assets::SceneAssetHandle{ m_Asset };
                scene.SetSceneInstance(entity, instance);
            }
        }

        void Undo() override
        {
            m_Project->EditScene().DestroyEntity(CreatedEntity());
        }

    private:
        ProjectSession* m_Project;
        kairo::assets::AssetID m_Asset;
        std::optional<kairo::engine::Entity> m_Entity;
    };
}