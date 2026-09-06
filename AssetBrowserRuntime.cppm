module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Editor.AssetBrowserRuntime;

import Kairo.Assets;

export namespace kairo::editor
{
    inline constexpr std::uint32_t DefaultAssetThumbnailExtent = 96u;
    inline constexpr std::uint32_t MaximumAssetThumbnailExtent = 512u;

    /// Stable ImGui/backend-neutral drag payload. The payload contains only the
    /// persistent UUID bytes; the drop target resolves the current metadata
    /// from AssetRegistry and therefore never trusts a stale path or type tag.
    struct AssetDragPayload final
    {
        std::array<std::uint8_t, 16> Bytes{};

        [[nodiscard]] static AssetDragPayload FromAsset(kairo::assets::AssetID id) noexcept
        {
            return { id.Bytes() };
        }

        [[nodiscard]] kairo::assets::AssetID Asset() const noexcept
        {
            return kairo::assets::AssetID{ Bytes };
        }

        friend bool operator==(const AssetDragPayload&, const AssetDragPayload&) = default;
    };

    /// Renderer-neutral thumbnail request. Revision is part of the identity so
    /// an edited/reimported asset cannot accidentally retain an old preview.
    struct AssetThumbnailRequest final
    {
        kairo::assets::AssetID Asset;
        kairo::assets::AssetType Type = kairo::assets::AssetType::Other;
        std::uint64_t Revision = 0u;
        std::uint32_t Extent = DefaultAssetThumbnailExtent;

        friend bool operator==(const AssetThumbnailRequest&, const AssetThumbnailRequest&) = default;
    };

    [[nodiscard]] constexpr bool SupportsAssetThumbnail(kairo::assets::AssetType type) noexcept
    {
        using kairo::assets::AssetType;
        return type == AssetType::Mesh || type == AssetType::Material ||
            type == AssetType::Texture2D || type == AssetType::Scene;
    }

    /// Deduplicates visible thumbnail work independently from any graphics API.
    /// A host drains Pending() and publishes a backend texture through the UI
    /// bridge. Invalidate() re-enables one asset after reimport/revision change.
    class AssetThumbnailScheduler final
    {
    public:
        void Request(const kairo::assets::AssetMetadata& metadata,
            std::uint32_t extent = DefaultAssetThumbnailExtent)
        {
            if (!SupportsAssetThumbnail(metadata.Type)) return;
            if (extent == 0u || extent > MaximumAssetThumbnailExtent)
                throw std::out_of_range("Asset thumbnail extent is outside the supported range.");
            const AssetThumbnailRequest request{
                metadata.ID, metadata.Type, metadata.Revision, extent };
            if (std::ranges::find(m_Requested, request) != m_Requested.end()) return;
            m_Requested.push_back(request);
            m_Pending.push_back(request);
        }

        [[nodiscard]] std::vector<AssetThumbnailRequest> TakePending() noexcept
        {
            return std::exchange(m_Pending, {});
        }

        void Invalidate(kairo::assets::AssetID asset) noexcept
        {
            std::erase_if(m_Requested, [asset](const AssetThumbnailRequest& request)
            { return request.Asset == asset; });
            std::erase_if(m_Pending, [asset](const AssetThumbnailRequest& request)
            { return request.Asset == asset; });
        }

        void Clear() noexcept
        {
            m_Requested.clear();
            m_Pending.clear();
        }

        [[nodiscard]] std::size_t RequestedCount() const noexcept { return m_Requested.size(); }

    private:
        std::vector<AssetThumbnailRequest> m_Requested;
        std::vector<AssetThumbnailRequest> m_Pending;
    };

    enum class AssetBrowserRequestKind : std::uint8_t
    {
        Reimport,
        RevealInFileManager
    };

    /// UI-to-host request. Reimport is intentionally a request rather than a
    /// direct importer call: ProjectSession does not own importer provenance or
    /// GPU resources, and Editor must stay backend-neutral.
    struct AssetBrowserRequest final
    {
        AssetBrowserRequestKind Kind = AssetBrowserRequestKind::Reimport;
        kairo::assets::AssetID Asset;

        friend bool operator==(const AssetBrowserRequest&, const AssetBrowserRequest&) = default;
    };

    class AssetBrowserRequestQueue final
    {
    public:
        void Push(AssetBrowserRequest request)
        {
            if (!request.Asset.IsValid())
                throw std::invalid_argument("Asset browser request requires a valid asset ID.");
            if (std::ranges::find(m_Requests, request) == m_Requests.end())
                m_Requests.push_back(request);
        }

        [[nodiscard]] std::vector<AssetBrowserRequest> Take() noexcept
        {
            return std::exchange(m_Requests, {});
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_Requests.size(); }

    private:
        std::vector<AssetBrowserRequest> m_Requests;
    };
}