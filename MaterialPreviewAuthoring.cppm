module;

#include <cstdint>
#include <stdexcept>

export module Kairo.Editor.MaterialPreviewAuthoring;

import Kairo.Assets.MaterialArtifact;

export namespace kairo::editor
{
    enum class MaterialPreviewShape : std::uint8_t
    {
        Sphere = 1u,
        Plane = 2u
    };

    struct MaterialPreviewRequest final
    {
        kairo::assets::MaterialArtifactData Material;
        MaterialPreviewShape Shape = MaterialPreviewShape::Sphere;
        float KeyLightIntensity = 4.0f;
        float FillLightIntensity = 1.0f;
        float EnvironmentIntensity = 0.12f;

        void Validate() const
        {
            kairo::assets::ValidateMaterialArtifactData(Material);
            if (Shape != MaterialPreviewShape::Sphere && Shape != MaterialPreviewShape::Plane)
                throw std::invalid_argument("Material preview shape is invalid.");
            if (KeyLightIntensity < 0.0f || FillLightIntensity < 0.0f || EnvironmentIntensity < 0.0f)
                throw std::invalid_argument("Material preview lighting intensities must be non-negative.");
        }
    };
}
