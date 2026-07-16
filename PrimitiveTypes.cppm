module;

#include <stdexcept>
#include <string_view>

export module Kairo.Editor.PrimitiveTypes;

import Kairo.Assets;

export namespace kairo::editor
{
    /// First-class editable scene primitives. Their persistent mesh assets are
    /// registered by a project manifest; procedural GPU geometry is supplied
    /// once by the renderer adapter rather than duplicated in editor code.
    enum class PrimitiveKind : unsigned char { Cube, Plane, UVSphere, Cylinder };

    [[nodiscard]] constexpr std::string_view Name(PrimitiveKind kind) noexcept
    {
        switch (kind)
        {
        case PrimitiveKind::Cube: return "Cube";
        case PrimitiveKind::Plane: return "Plane";
        case PrimitiveKind::UVSphere: return "UV Sphere";
        case PrimitiveKind::Cylinder: return "Cylinder";
        }
        return "Unknown";
    }

    [[nodiscard]] inline kairo::assets::MeshAssetHandle PrimitiveMeshAsset(PrimitiveKind kind)
    {
        switch (kind)
        {
        case PrimitiveKind::Cube: return { kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000201") };
        case PrimitiveKind::Plane: return { kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000211") };
        case PrimitiveKind::UVSphere: return { kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000212") };
        case PrimitiveKind::Cylinder: return { kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000213") };
        }
        throw std::invalid_argument("Unknown Kairo editor primitive.");
    }

    [[nodiscard]] inline kairo::assets::MaterialAssetHandle DefaultPrimitiveMaterial()
    {
        return { kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000202") };
    }
}
