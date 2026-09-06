#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>

import Kairo.Assets;
import Kairo.Editor.AssetBrowserRuntime;

TEST_CASE("asset drag payload round trips persistent identity")
{
    const auto id = kairo::assets::AssetID::Parse("12345678-1234-4abc-8def-1234567890ab");
    const auto payload = kairo::editor::AssetDragPayload::FromAsset(id);
    REQUIRE(payload.Asset() == id);
}

TEST_CASE("thumbnail scheduler deduplicates revision keyed requests")
{
    const auto id = kairo::assets::AssetID::Parse("12345678-1234-4abc-8def-1234567890ab");
    kairo::assets::AssetMetadata metadata;
    metadata.ID = id;
    metadata.Type = kairo::assets::AssetType::Mesh;
    metadata.Revision = 7u;

    kairo::editor::AssetThumbnailScheduler scheduler;
    scheduler.Request(metadata);
    scheduler.Request(metadata);
    auto requests = scheduler.TakePending();
    REQUIRE(requests.size() == 1u);
    REQUIRE(requests.front().Asset == id);
    REQUIRE(requests.front().Revision == 7u);
    REQUIRE(scheduler.TakePending().empty());

    metadata.Revision = 8u;
    scheduler.Request(metadata);
    requests = scheduler.TakePending();
    REQUIRE(requests.size() == 1u);
    REQUIRE(requests.front().Revision == 8u);
}

TEST_CASE("thumbnail scheduler rejects invalid extents")
{
    const auto id = kairo::assets::AssetID::Parse("12345678-1234-4abc-8def-1234567890ab");
    kairo::assets::AssetMetadata metadata;
    metadata.ID = id;
    metadata.Type = kairo::assets::AssetType::Mesh;
    metadata.Revision = 1u;

    kairo::editor::AssetThumbnailScheduler scheduler;
    REQUIRE_THROWS_AS(scheduler.Request(metadata, 0u), std::out_of_range);
    REQUIRE_THROWS_AS(scheduler.Request(metadata,
        kairo::editor::MaximumAssetThumbnailExtent + 1u), std::out_of_range);
    REQUIRE(scheduler.TakePending().empty());
}

TEST_CASE("thumbnail scheduler ignores nonvisual asset types")
{
    const auto id = kairo::assets::AssetID::Parse("12345678-1234-4abc-8def-1234567890ab");
    kairo::assets::AssetMetadata metadata;
    metadata.ID = id;
    metadata.Type = kairo::assets::AssetType::Document;
    metadata.Revision = 1u;

    kairo::editor::AssetThumbnailScheduler scheduler;
    scheduler.Request(metadata);
    REQUIRE(scheduler.TakePending().empty());
}

TEST_CASE("asset browser request queue deduplicates exact requests")
{
    const auto id = kairo::assets::AssetID::Parse("12345678-1234-4abc-8def-1234567890ab");
    kairo::editor::AssetBrowserRequestQueue queue;
    queue.Push({ kairo::editor::AssetBrowserRequestKind::Reimport, id });
    queue.Push({ kairo::editor::AssetBrowserRequestKind::Reimport, id });
    queue.Push({ kairo::editor::AssetBrowserRequestKind::RevealInFileManager, id });

    const auto requests = queue.Take();
    REQUIRE(requests.size() == 2u);
    REQUIRE(queue.Size() == 0u);
}

TEST_CASE("asset browser request queue rejects invalid identity")
{
    kairo::editor::AssetBrowserRequestQueue queue;
    REQUIRE_THROWS_AS(queue.Push({ kairo::editor::AssetBrowserRequestKind::Reimport, {} }),
        std::invalid_argument);
    REQUIRE(queue.Size() == 0u);
}