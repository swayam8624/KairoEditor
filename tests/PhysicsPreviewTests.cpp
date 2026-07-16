#include <catch2/catch_test_macros.hpp>

import Kairo.Editor.PhysicsPreview;
import Kairo.EngineCore;

using namespace kairo::editor;

TEST_CASE("Physics preview simulates marked runtime entities without authored-scene ownership", "[KairoEditor][Physics]")
{
    kairo::engine::Scene runtime;
    const auto dynamic = runtime.CreateEntity("Dynamic");
    runtime.Transform(dynamic).Local.Translation = { 0.0f, 3.0f, 0.0f };
    runtime.SetRigidBody(dynamic, {});
    runtime.SetCollider(dynamic, {});

    const auto staticBody = runtime.CreateEntity("Static");
    runtime.Transform(staticBody).Local.Translation = { 0.0f, -2.0f, 0.0f };
    runtime.Transform(staticBody).Local.Scale = { 8.0f, 0.25f, 8.0f };
    runtime.SetCollider(staticBody, {});

    PhysicsPreview preview;
    preview.Start(runtime);
    REQUIRE(preview.Active());
    REQUIRE(preview.World().Bodies().size() == 2u);
    REQUIRE_FALSE(preview.DebugDraw().Empty());
    const float initialY = runtime.Transform(dynamic).Local.Translation.y;
    preview.Step(runtime, 1.0f / 60.0f);
    CHECK(runtime.Transform(dynamic).Local.Translation.y < initialY);
    preview.Reset();
    CHECK_FALSE(preview.Active());
}
