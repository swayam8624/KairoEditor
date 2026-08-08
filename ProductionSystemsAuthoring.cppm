module;

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Editor.ProductionSystemsAuthoring;

import Kairo.EngineCore.ProductionRuntime;
import Kairo.EngineCore.ProductionSystemsManifest;

export namespace kairo::editor
{
    class ProductionSystemsAuthoringWorkspace final
    {
    public:
        explicit ProductionSystemsAuthoringWorkspace(
            kairo::engine::ProductionPerformanceBudget budget = {})
            : m_Budget(budget) {}

        [[nodiscard]] kairo::engine::ProductionSystemsManifest& Manifest() noexcept { return m_Manifest; }
        [[nodiscard]] const kairo::engine::ProductionSystemsManifest& Manifest() const noexcept { return m_Manifest; }
        [[nodiscard]] const kairo::engine::ProductionPerformanceBudget& Budget() const noexcept { return m_Budget; }

        void NewManifest() { m_Manifest = {}; m_Path.clear(); }

        void Load(const std::filesystem::path& path)
        {
            auto loaded = kairo::engine::LoadProductionSystemsManifest(path);
            kairo::engine::ValidateProductionSystemsManifest(loaded, m_Budget);
            m_Manifest = std::move(loaded);
            m_Path = path;
        }

        void Save(const std::filesystem::path& path)
        {
            kairo::engine::ValidateProductionSystemsManifest(m_Manifest, m_Budget);
            kairo::engine::SaveProductionSystemsManifest(path, m_Manifest);
            m_Path = path;
        }

        void Save()
        {
            if (m_Path.empty()) throw std::logic_error("Production systems manifest has no save path.");
            Save(m_Path);
        }

        void SetBudget(kairo::engine::ProductionPerformanceBudget budget)
        {
            kairo::engine::ValidateProductionSystemsManifest(m_Manifest, budget);
            m_Budget = budget;
        }

        [[nodiscard]] kairo::engine::ProductionWorkloadEstimate Workload() const
        { return kairo::engine::EstimateProductionWorkload(m_Manifest); }

        void AddAnimation(kairo::engine::ProductionAnimationDescriptor animation)
        {
            if (animation.Name.empty()) throw std::invalid_argument("Animation name cannot be empty.");
            for (const auto& existing : m_Manifest.Animations)
                if (existing.Name == animation.Name)
                    throw std::invalid_argument("Production animation name is duplicated.");
            auto candidate = m_Manifest;
            candidate.Animations.push_back(std::move(animation));
            kairo::engine::ValidateProductionSystemsManifest(candidate, m_Budget);
            m_Manifest = std::move(candidate);
        }

        void SetTerrain(std::optional<kairo::engine::ProductionTerrainDescriptor> descriptor)
        { Mutate([&](auto& value) { value.Terrain = descriptor; }); }
        void SetFoliage(std::optional<kairo::engine::ProductionFoliageDescriptor> descriptor)
        { Mutate([&](auto& value) { value.Foliage = descriptor; }); }
        void SetParticles(std::optional<kairo::engine::ProductionParticleDescriptor> descriptor)
        { Mutate([&](auto& value) { value.Particles = descriptor; }); }
        void SetCloth(std::optional<kairo::engine::ProductionClothDescriptor> descriptor)
        { Mutate([&](auto& value) { value.Cloth = descriptor; }); }
        void SetFluid(std::optional<kairo::engine::ProductionFluidDescriptor> descriptor)
        { Mutate([&](auto& value) { value.Fluid = descriptor; }); }
        void SetStreaming(std::optional<kairo::engine::ProductionStreamingDescriptor> descriptor)
        { Mutate([&](auto& value) { value.Streaming = descriptor; }); }

        [[nodiscard]] kairo::engine::ProductionRuntime CreatePreviewRuntime() const
        {
            return kairo::engine::ProductionRuntime(m_Manifest, m_Budget);
        }

    private:
        kairo::engine::ProductionSystemsManifest m_Manifest;
        kairo::engine::ProductionPerformanceBudget m_Budget;
        std::filesystem::path m_Path;

        template<class Function>
        void Mutate(Function&& function)
        {
            auto candidate = m_Manifest;
            std::forward<Function>(function)(candidate);
            kairo::engine::ValidateProductionSystemsManifest(candidate, m_Budget);
            m_Manifest = std::move(candidate);
        }
    };
}
