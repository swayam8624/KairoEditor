#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

import Kairo.Assets;
import Kairo.Editor.AnimationTimeline;

namespace assets = kairo::assets;
namespace editor = kairo::editor;

namespace
{
    [[nodiscard]] editor::AnimationTimelineKey TranslationKey(
        float time, float x, float y, float z)
    {
        return {
            time,
            { x, y, z, 99.0f },
            { 1.0f, 2.0f, 3.0f, 98.0f },
            { 4.0f, 5.0f, 6.0f, 97.0f }
        };
    }
}

TEST_CASE("Animation timeline keeps tracks and keys deterministic")
{
    editor::AnimationTimeline timeline("Walk");
    timeline.AddTrack(7u, assets::GltfAnimationPath::Scale,
        assets::GltfAnimationInterpolation::Step);
    timeline.AddTrack(2u, assets::GltfAnimationPath::Rotation);
    timeline.AddTrack(2u, assets::GltfAnimationPath::Translation);

    REQUIRE(timeline.TrackCount() == 3u);
    CHECK(timeline.Tracks()[0].Identity.TargetNode == 2u);
    CHECK(timeline.Tracks()[0].Identity.Path ==
        assets::GltfAnimationPath::Translation);
    CHECK(timeline.Tracks()[1].Identity.TargetNode == 2u);
    CHECK(timeline.Tracks()[1].Identity.Path ==
        assets::GltfAnimationPath::Rotation);
    CHECK(timeline.Tracks()[2].Identity.TargetNode == 7u);

    timeline.UpsertKey(2u, assets::GltfAnimationPath::Translation,
        TranslationKey(2.0f, 2.0f, 0.0f, 0.0f));
    timeline.UpsertKey(2u, assets::GltfAnimationPath::Translation,
        TranslationKey(0.0f, 0.0f, 0.0f, 0.0f));
    timeline.UpsertKey(2u, assets::GltfAnimationPath::Translation,
        TranslationKey(1.0f, 1.0f, 0.0f, 0.0f));

    const auto& keys = timeline.Track(2u,
        assets::GltfAnimationPath::Translation).Keys;
    REQUIRE(keys.size() == 3u);
    CHECK(keys[0].TimeSeconds == 0.0f);
    CHECK(keys[1].TimeSeconds == 1.0f);
    CHECK(keys[2].TimeSeconds == 2.0f);
    CHECK(keys[1].Value[3] == 0.0f);
    CHECK(keys[1].InTangent[3] == 0.0f);
    CHECK(keys[1].OutTangent[3] == 0.0f);
    CHECK(timeline.DurationSeconds() == 2.0f);
}

TEST_CASE("Animation key upsert and move have explicit collision semantics")
{
    editor::AnimationTimeline timeline("Move");
    timeline.AddTrack(1u, assets::GltfAnimationPath::Translation);
    timeline.UpsertKey(1u, assets::GltfAnimationPath::Translation,
        TranslationKey(0.0f, 1.0f, 0.0f, 0.0f));
    timeline.UpsertKey(1u, assets::GltfAnimationPath::Translation,
        TranslationKey(1.0f, 2.0f, 0.0f, 0.0f));

    CHECK_THROWS_AS(timeline.UpsertKey(1u,
        assets::GltfAnimationPath::Translation,
        TranslationKey(1.0f, 3.0f, 0.0f, 0.0f),
        editor::AnimationKeyCollisionPolicy::Reject), std::invalid_argument);

    timeline.UpsertKey(1u, assets::GltfAnimationPath::Translation,
        TranslationKey(1.0f, 3.0f, 0.0f, 0.0f),
        editor::AnimationKeyCollisionPolicy::Replace);
    CHECK(timeline.Track(1u, assets::GltfAnimationPath::Translation)
        .Keys[1].Value[0] == 3.0f);

    CHECK_THROWS_AS(timeline.MoveKey(1u,
        assets::GltfAnimationPath::Translation, 0.0f, 1.0f),
        std::invalid_argument);
    const auto& unchanged = timeline.Track(1u,
        assets::GltfAnimationPath::Translation).Keys;
    REQUIRE(unchanged.size() == 2u);
    CHECK(unchanged[0].TimeSeconds == 0.0f);
    CHECK(unchanged[0].Value[0] == 1.0f);

    timeline.MoveKey(1u, assets::GltfAnimationPath::Translation,
        0.0f, 0.5f);
    const auto& moved = timeline.Track(1u,
        assets::GltfAnimationPath::Translation).Keys;
    CHECK(moved[0].TimeSeconds == 0.5f);
    CHECK(timeline.RemoveKey(1u, assets::GltfAnimationPath::Translation, 0.5f));
    CHECK_FALSE(timeline.RemoveKey(1u,
        assets::GltfAnimationPath::Translation, 0.5f));
}

TEST_CASE("Rotation keys canonicalize quaternion values without touching cubic tangents")
{
    editor::AnimationTimeline timeline("Turn");
    timeline.AddTrack(4u, assets::GltfAnimationPath::Rotation,
        assets::GltfAnimationInterpolation::CubicSpline);

    editor::AnimationTimelineKey key;
    key.TimeSeconds = 0.25f;
    key.Value = { 0.0f, 0.0f, 0.0f, 2.0f };
    key.InTangent = { 1.0f, 2.0f, 3.0f, 4.0f };
    key.OutTangent = { -1.0f, -2.0f, -3.0f, -4.0f };
    timeline.UpsertKey(4u, assets::GltfAnimationPath::Rotation, key);

    const auto& stored = timeline.Track(4u,
        assets::GltfAnimationPath::Rotation).Keys.front();
    CHECK(stored.Value == std::array<float, 4u>{ 0.0f, 0.0f, 0.0f, 1.0f });
    CHECK(stored.InTangent == key.InTangent);
    CHECK(stored.OutTangent == key.OutTangent);

    key.TimeSeconds = 0.5f;
    key.Value = {};
    CHECK_THROWS_AS(timeline.UpsertKey(4u,
        assets::GltfAnimationPath::Rotation, key), std::invalid_argument);
}

TEST_CASE("Animation timeline compiles to the existing KairoAssets runtime clip contract")
{
    editor::AnimationTimeline timeline("Run");
    timeline.AddTrack(3u, assets::GltfAnimationPath::Translation,
        assets::GltfAnimationInterpolation::Linear);
    timeline.AddTrack(3u, assets::GltfAnimationPath::Rotation,
        assets::GltfAnimationInterpolation::Step);
    timeline.AddTrack(9u, assets::GltfAnimationPath::Scale,
        assets::GltfAnimationInterpolation::CubicSpline);

    timeline.UpsertKey(3u, assets::GltfAnimationPath::Translation,
        TranslationKey(0.0f, 0.0f, 0.0f, 0.0f));
    timeline.UpsertKey(3u, assets::GltfAnimationPath::Translation,
        TranslationKey(1.25f, 5.0f, 0.0f, 0.0f));

    editor::AnimationTimelineKey rotation;
    rotation.Value = { 0.0f, 0.0f, 0.0f, 1.0f };
    timeline.UpsertKey(3u, assets::GltfAnimationPath::Rotation, rotation);

    // Empty tracks are editor state, not runtime channels.
    const auto first = timeline.Compile();
    const auto second = timeline.Compile();
    CHECK(first == second);
    CHECK(first.Name == "Run");
    REQUIRE(first.Channels.size() == 2u);
    CHECK(first.Channels[0].TargetNode == 3u);
    CHECK(first.Channels[0].Path == assets::GltfAnimationPath::Translation);
    CHECK(first.Channels[1].Path == assets::GltfAnimationPath::Rotation);
    CHECK(first.DurationSeconds() == 1.25f);

    const auto reopened = editor::AnimationTimeline::FromClip(first);
    CHECK(reopened.Compile() == first);
    CHECK(reopened.DurationSeconds() == timeline.DurationSeconds());
}

TEST_CASE("Animation timeline rejects malformed authoring input")
{
    CHECK_THROWS_AS(editor::AnimationTimeline(""), std::invalid_argument);

    editor::AnimationTimeline timeline("Validation");
    CHECK_THROWS_AS(timeline.AddTrack(assets::GltfMissingIndex,
        assets::GltfAnimationPath::Translation), std::invalid_argument);

    timeline.AddTrack(1u, assets::GltfAnimationPath::Translation);
    auto key = TranslationKey(-1.0f, 0.0f, 0.0f, 0.0f);
    CHECK_THROWS_AS(timeline.UpsertKey(1u,
        assets::GltfAnimationPath::Translation, key), std::invalid_argument);

    key.TimeSeconds = 0.0f;
    key.Value[0] = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS(timeline.UpsertKey(1u,
        assets::GltfAnimationPath::Translation, key), std::invalid_argument);
}
