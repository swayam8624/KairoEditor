module;

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Editor.OfflineRenderAuthoring;

import Kairo.Editor.ProductionAuthoring;

export namespace kairo::editor
{
    struct OfflineRenderRequest final
    {
        std::uint64_t JobID = 0u;
        std::uint32_t Width = 1280u;
        std::uint32_t Height = 720u;
        std::uint32_t Passes = 64u;
        std::filesystem::path ProjectRoot;
        std::filesystem::path RelativeOutput = "Renders/Offline.ppm";

        void Validate() const
        {
            if (JobID == 0u) throw std::invalid_argument("Offline render job ID must be non-zero.");
            if (Width == 0u || Height == 0u || Width > 32768u || Height > 32768u)
                throw std::invalid_argument("Offline render dimensions are invalid.");
            if (Passes == 0u || Passes > 1'000'000u)
                throw std::invalid_argument("Offline render pass count is invalid.");
            if (ProjectRoot.empty()) throw std::invalid_argument("Offline render project root cannot be empty.");
            if (RelativeOutput.empty() || RelativeOutput.is_absolute())
                throw std::invalid_argument("Offline render output must be project-relative.");
            for (const auto& component : RelativeOutput.lexically_normal())
                if (component == "..") throw std::invalid_argument("Offline render output cannot escape the project root.");
        }
    };

    struct OfflineRenderServiceProgress final
    {
        std::uint64_t JobID = 0u;
        OfflineRenderWorkspaceStatus Status = OfflineRenderWorkspaceStatus::Idle;
        std::uint32_t CompletedPasses = 0u;
        std::uint32_t TotalPasses = 0u;
        std::vector<std::string> Diagnostics;
        std::optional<std::filesystem::path> Output;
    };

    class OfflineRenderService
    {
    public:
        virtual ~OfflineRenderService() = default;
        virtual void Submit(OfflineRenderRequest request) = 0;
        virtual void Cancel(std::uint64_t jobID) = 0;
        [[nodiscard]] virtual OfflineRenderServiceProgress Poll(std::uint64_t jobID) = 0;
    };

    class OfflineRenderAuthoringController final
    {
    public:
        explicit OfflineRenderAuthoringController(std::shared_ptr<OfflineRenderService> service)
            : m_Service(std::move(service))
        {
            if (!m_Service) throw std::invalid_argument("Offline render controller requires a service.");
        }

        void Submit(OfflineRenderRequest request)
        {
            request.Validate();
            if (m_ActiveJob.has_value() &&
                m_State.Status() != OfflineRenderWorkspaceStatus::Completed &&
                m_State.Status() != OfflineRenderWorkspaceStatus::Cancelled &&
                m_State.Status() != OfflineRenderWorkspaceStatus::Failed)
                throw std::logic_error("An offline render is already active in this editor workspace.");
            const auto jobID = request.JobID;
            const auto passes = request.Passes;
            m_Service->Submit(std::move(request));
            m_ActiveJob = jobID;
            m_State.Queue(jobID, passes);
        }

        void Cancel()
        {
            if (!m_ActiveJob.has_value()) return;
            m_Service->Cancel(*m_ActiveJob);
            m_State.Cancel();
        }

        void Refresh()
        {
            if (!m_ActiveJob.has_value()) return;
            const auto progress = m_Service->Poll(*m_ActiveJob);
            if (progress.JobID != *m_ActiveJob)
                throw std::runtime_error("Offline render service returned progress for a different job.");
            for (const auto& diagnostic : progress.Diagnostics) m_State.AddDiagnostic(diagnostic);
            switch (progress.Status)
            {
                case OfflineRenderWorkspaceStatus::Idle:
                case OfflineRenderWorkspaceStatus::Queued:
                    break;
                case OfflineRenderWorkspaceStatus::Running:
                    m_State.SetRunning(progress.CompletedPasses);
                    break;
                case OfflineRenderWorkspaceStatus::Completed:
                    if (!progress.Output.has_value())
                        throw std::runtime_error("Completed offline render has no output path.");
                    m_State.Complete(*progress.Output);
                    break;
                case OfflineRenderWorkspaceStatus::Cancelled:
                    m_State.Cancel();
                    break;
                case OfflineRenderWorkspaceStatus::Failed:
                    if (progress.Diagnostics.empty()) m_State.Fail("Offline render service reported failure.");
                    else m_State.Fail(progress.Diagnostics.back());
                    break;
            }
        }

        [[nodiscard]] const OfflineRenderWorkspaceState& State() const noexcept { return m_State; }
        [[nodiscard]] std::optional<std::uint64_t> ActiveJob() const noexcept { return m_ActiveJob; }

    private:
        std::shared_ptr<OfflineRenderService> m_Service;
        OfflineRenderWorkspaceState m_State;
        std::optional<std::uint64_t> m_ActiveJob;
    };
}
