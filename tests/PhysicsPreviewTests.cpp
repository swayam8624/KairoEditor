#include <catch2/catch_test_macros.hpp>
#include <variant>

import Kairo.Editor.PhysicsPreview;
import Kairo.EngineCore;
import Kairo.Foundation.PhysicsEngine;

using namespace kairo::editor;

TEST_CASE("Physics preview simulates marked runtime entities without authored-scene ownership", "[KairoEditor][Physics]")
{
    kairo::engine::Scene runtime;
    const auto dynamic = runtime.CreateEntity("Dynamic");
    runtime.Transform(dynamic).Local.Translation = { 0.0f, 3.0f, 0.0f };
    runtime.SetRigidBody(dynamic, {
        kairo::engine::RigidBodyMotion::Dynamic, 2.0f, 0.5f, 0.15f, 0.25f });
    runtime.SetCollider(dynamic, {
        .Shape = kairo::engine::ColliderShape::Sphere,
        .Radius = 0.75f,
        .Friction = 0.9f,
        .Restitution = 0.4f,
        .BelongsTo = 8u,
        .CollidesWith = 2u,
        .IsTrigger = true });

    const auto staticBody = runtime.CreateEntity("Static");
    runtime.Transform(staticBody).Local.Translation = { 0.0f, -2.0f, 0.0f };
    runtime.Transform(staticBody).Local.Scale = { 8.0f, 0.25f, 8.0f };
    runtime.SetCollider(staticBody, {});

    PhysicsPreview preview;
    preview.Start(runtime);
    REQUIRE(preview.Active());
    REQUIRE(preview.World().Bodies().size() == 2u);
    REQUIRE(preview.World().Colliders().size() == 2u);
    const auto& runtimeDynamic = preview.World().Bodies().front();
    CHECK(runtimeDynamic.GravityScale == 0.5f);
    CHECK(runtimeDynamic.LinearDamping == 0.15f);
    CHECK(runtimeDynamic.AngularDamping == 0.25f);
    const auto& runtimeCollider = preview.World().Colliders().front();
    CHECK(runtimeCollider.IsTrigger);
    CHECK(runtimeCollider.Material.StaticFriction == 0.9f);
    CHECK(runtimeCollider.Material.Restitution == 0.4f);
    CHECK(runtimeCollider.BelongsTo == 8u);
    CHECK(runtimeCollider.CollidesWith == 2u);
    REQUIRE(std::holds_alternative<kairo::foundation::physics::SphereCollider>(
        runtimeCollider.Shape));
    CHECK(std::get<kairo::foundation::physics::SphereCollider>(runtimeCollider.Shape).Radius == 0.75f);
    REQUIRE_FALSE(preview.DebugDraw().Empty());
    const float initialY = runtime.Transform(dynamic).Local.Translation.y;
    preview.Step(runtime, 1.0f / 60.0f);
    CHECK(runtime.Transform(dynamic).Local.Translation.y < initialY);
    preview.Reset();
    CHECK_FALSE(preview.Active());
}
