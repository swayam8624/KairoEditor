module;

#include <cstdint>
#include <string_view>

export module Kairo.Editor.AssetDragDrop;

import Kairo.Assets.AssetID;
import Kairo.Assets.Types;

export namespace kairo::editor
{
    inline constexpr std::string_view AssetDragDropPayloadType = "KAIRO_ASSET_REF_V1";

    /// Backend-neutral payload copied into the immediate-mode UI drag buffer.
    /// The persistent 128-bit ID is stored as bytes rather than as a path so a
    /// reference remains valid when an asset is moved or renamed.
    struct AssetDragPayload final
    {
        kairo::assets::AssetID::Storage ID{};
        kairo::assets::AssetType Type = kairo::assets::AssetType::Other;

        [[nodiscard]] kairo::assets::AssetID Asset() const noexcept
        {
            return kairo::assets::AssetID{ ID };
        }
    };

    [[nodiscard]] inline AssetDragPayload MakeAssetDragPayload(
        kairo::assets::AssetID id, kairo::assets::AssetType type) noexcept
    {
        return { id.Bytes(), type };
    }

    /// Reflection target keys are deliberately stable semantic identifiers,
    /// not C++ type names. This lets the content browser reject incompatible
    /// drops before EngineCore's reflected write validation is invoked.
    [[nodiscard]] constexpr std::string_view ReflectionReferenceTarget(
        kairo::assets::AssetType type) noexcept
    {
        using kairo::assets::AssetType;
        switch (type)
        {
            case AssetType::Mesh: return "Kairo.Assets.Mesh";
            case AssetType::Material: return "Kairo.Assets.Material";
            case AssetType::Texture2D: return "Kairo.Assets.Texture2D";
            case AssetType::Scene: return "Kairo.Assets.Scene";
            case AssetType::Shader: return "Kairo.Assets.Shader";
            case AssetType::Audio: return "Kairo.Assets.Audio";
            case AssetType::Script: return "Kairo.Assets.Script";
            case AssetType::Document: return "Kairo.Assets.Document";
            case AssetType::Other: return "Kairo.Assets.Other";
        }
        return "Kairo.Assets.Other";
    }

    [[nodiscard]] constexpr bool AssetMatchesReferenceTarget(
        kairo::assets::AssetType type, std::string_view target) noexcept
    {
        return ReflectionReferenceTarget(type) == target;
    }
}
