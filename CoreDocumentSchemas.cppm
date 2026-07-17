module;

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

export module Kairo.Editor.CoreDocumentSchemas;

import Kairo.Editor.DocumentSchema;
import Kairo.Editor.DocumentTypes;
import Kairo.EngineCore.Entity;
import Kairo.Foundation.Math;

export namespace kairo::editor
{
    namespace core_schema_detail
    {
        [[nodiscard]] inline PinSchema FlowInput(std::string key, std::string name,
            bool required = true)
        {
            return { std::move(key), std::move(name), PinDirection::Input, ValueType::Flow,
                PinCardinality::Single, required, std::nullopt };
        }

        [[nodiscard]] inline PinSchema RequiredInput(std::string key, std::string name, ValueType type)
        {
            return { std::move(key), std::move(name), PinDirection::Input, type,
                PinCardinality::Single, true, std::nullopt };
        }

        [[nodiscard]] inline PinSchema DefaultedInput(std::string key, std::string name,
            ValueType type, DocumentValue value)
        {
            return { std::move(key), std::move(name), PinDirection::Input, type,
                PinCardinality::Single, false, std::move(value) };
        }

        [[nodiscard]] inline PinSchema Output(std::string key, std::string name, ValueType type)
        {
            return { std::move(key), std::move(name), PinDirection::Output, type,
                PinCardinality::Multiple, false, std::nullopt };
        }

        [[nodiscard]] inline NodeSchema Node(DocumentKind kind, std::string key,
            std::string name, std::string category)
        {
            return { kind, std::move(key), std::move(name), std::move(category), {}, {} };
        }
    }

    /// Registers the stable node contracts shipped with KairoEditor.
    ///
    /// Input: a mutable registry that must not already contain these type keys.
    /// Output: deterministic schemas for every V1 authoring document domain.
    /// Task: give editor tooling and domain compilers one canonical pin/property
    /// contract. This function defines authored data shape only; runtime meaning
    /// remains the responsibility of the matching logic, material, audio,
    /// animation, or simulation compiler and is never emulated by the editor.
    inline void RegisterCoreDocumentSchemas(DocumentSchemaRegistry& registry)
    {
        using namespace core_schema_detail;
        using kairo::foundation::math::Vec3d;
        using kairo::foundation::math::Vec4d;

        NodeSchema beginPlay = Node(DocumentKind::Logic, "kairo.logic.event-begin-play",
            "Begin Play", "Events");
        beginPlay.Pins = { Output("then", "Then", ValueType::Flow) };
        registry.Register(std::move(beginPlay));

        NodeSchema branch = Node(DocumentKind::Logic, "kairo.logic.branch", "Branch", "Flow Control");
        branch.Pins = {
            FlowInput("in", "In"),
            DefaultedInput("condition", "Condition", ValueType::Boolean, DocumentValue(false)),
            Output("true", "True", ValueType::Flow),
            Output("false", "False", ValueType::Flow)
        };
        registry.Register(std::move(branch));

        NodeSchema addFloat = Node(DocumentKind::Logic, "kairo.logic.add-float", "Add Float", "Math");
        addFloat.Pins = {
            DefaultedInput("a", "A", ValueType::Float, DocumentValue(0.0)),
            DefaultedInput("b", "B", ValueType::Float, DocumentValue(0.0)),
            Output("result", "Result", ValueType::Float)
        };
        registry.Register(std::move(addFloat));

        NodeSchema entityReference = Node(DocumentKind::Logic, "kairo.logic.entity-reference",
            "Scene Entity", "Values");
        entityReference.Pins = { Output("entity", "Entity", ValueType::Entity) };
        entityReference.PropertyDefaults.emplace("entity", DocumentValue(kairo::engine::Entity{ 1u }));
        registry.Register(std::move(entityReference));

        NodeSchema vector3 = Node(DocumentKind::Logic, "kairo.logic.vector3", "Vector 3", "Values");
        vector3.Pins = { Output("value", "Value", ValueType::Vector3) };
        vector3.PropertyDefaults.emplace("value", DocumentValue(Vec3d{}));
        registry.Register(std::move(vector3));

        NodeSchema print = Node(DocumentKind::Logic, "kairo.logic.print", "Print", "Debug");
        print.Pins = {
            FlowInput("in", "In"),
            DefaultedInput("message", "Message", ValueType::String, DocumentValue(std::string{})),
            Output("then", "Then", ValueType::Flow)
        };
        registry.Register(std::move(print));

        NodeSchema logicTick = Node(DocumentKind::Logic, "kairo.logic.event-tick", "Tick", "Events");
        logicTick.Pins = { Output("then", "Then", ValueType::Flow), Output("delta_seconds", "Delta Seconds", ValueType::Float) };
        registry.Register(std::move(logicTick));

        NodeSchema inputAction = Node(DocumentKind::Logic, "kairo.logic.input-action", "Input Action", "Input");
        inputAction.Pins = { Output("pressed", "Pressed", ValueType::Flow), Output("released", "Released", ValueType::Flow),
            Output("value", "Value", ValueType::Float) };
        inputAction.PropertyDefaults.emplace("action", DocumentValue(std::string("Jump")));
        registry.Register(std::move(inputAction));

        NodeSchema setPosition = Node(DocumentKind::Logic, "kairo.logic.set-position", "Set Position", "Transform");
        setPosition.Pins = { FlowInput("in", "In"), RequiredInput("entity", "Entity", ValueType::Entity),
            RequiredInput("position", "Position", ValueType::Vector3), Output("then", "Then", ValueType::Flow) };
        registry.Register(std::move(setPosition));

        NodeSchema applyImpulse = Node(DocumentKind::Logic, "kairo.logic.apply-impulse",
            "Apply Impulse", "Physics");
        applyImpulse.Pins = { FlowInput("in", "In"),
            RequiredInput("entity", "Entity", ValueType::Entity),
            RequiredInput("impulse", "Impulse", ValueType::Vector3),
            Output("then", "Then", ValueType::Flow) };
        registry.Register(std::move(applyImpulse));

        NodeSchema spawnEntity = Node(DocumentKind::Logic, "kairo.logic.spawn-entity", "Spawn Entity", "World");
        spawnEntity.Pins = { FlowInput("in", "In"), RequiredInput("prefab", "Prefab", ValueType::Asset),
            DefaultedInput("position", "Position", ValueType::Vector3, DocumentValue(Vec3d{})),
            Output("entity", "Entity", ValueType::Entity), Output("then", "Then", ValueType::Flow) };
        registry.Register(std::move(spawnEntity));

        NodeSchema color = Node(DocumentKind::Material, "kairo.material.constant-color",
            "Constant Color", "Inputs");
        color.Pins = { Output("color", "Color", ValueType::Vector4) };
        color.PropertyDefaults.emplace("value", DocumentValue(Vec4d{ 1.0, 1.0, 1.0, 1.0 }));
        registry.Register(std::move(color));

        NodeSchema multiply = Node(DocumentKind::Material, "kairo.material.multiply-color",
            "Multiply Color", "Math");
        multiply.Pins = {
            DefaultedInput("a", "A", ValueType::Vector4, DocumentValue(Vec4d{ 1.0, 1.0, 1.0, 1.0 })),
            DefaultedInput("b", "B", ValueType::Vector4, DocumentValue(Vec4d{ 1.0, 1.0, 1.0, 1.0 })),
            Output("result", "Result", ValueType::Vector4)
        };
        registry.Register(std::move(multiply));

        NodeSchema surface = Node(DocumentKind::Material, "kairo.material.surface-output",
            "Surface Output", "Output");
        surface.Pins = {
            DefaultedInput("base_color", "Base Color", ValueType::Vector4,
                DocumentValue(Vec4d{ 0.8, 0.8, 0.8, 1.0 })),
            DefaultedInput("roughness", "Roughness", ValueType::Float, DocumentValue(0.5)),
            DefaultedInput("metallic", "Metallic", ValueType::Float, DocumentValue(0.0)),
            DefaultedInput("emission", "Emission", ValueType::Vector3, DocumentValue(Vec3d{}))
        };
        registry.Register(std::move(surface));

        NodeSchema oscillator = Node(DocumentKind::Audio, "kairo.audio.oscillator",
            "Oscillator", "Sources");
        oscillator.Pins = { Output("signal", "Signal", ValueType::Float) };
        oscillator.PropertyDefaults.emplace("frequency_hz", DocumentValue(440.0));
        registry.Register(std::move(oscillator));

        NodeSchema gain = Node(DocumentKind::Audio, "kairo.audio.gain", "Gain", "Processing");
        gain.Pins = {
            RequiredInput("signal", "Signal", ValueType::Float),
            DefaultedInput("gain", "Gain", ValueType::Float, DocumentValue(1.0)),
            Output("result", "Result", ValueType::Float)
        };
        registry.Register(std::move(gain));

        NodeSchema audioOutput = Node(DocumentKind::Audio, "kairo.audio.output", "Audio Output", "Output");
        audioOutput.Pins = { RequiredInput("signal", "Signal", ValueType::Float) };
        registry.Register(std::move(audioOutput));

        NodeSchema animationEntry = Node(DocumentKind::AnimationState, "kairo.animation.entry",
            "Entry", "State Machine");
        animationEntry.Pins = { Output("state", "State", ValueType::Flow) };
        registry.Register(std::move(animationEntry));

        NodeSchema animationState = Node(DocumentKind::AnimationState, "kairo.animation.state",
            "Animation State", "State Machine");
        animationState.Pins = {
            FlowInput("enter", "Enter", false),
            RequiredInput("clip", "Clip", ValueType::Asset),
            DefaultedInput("speed", "Speed", ValueType::Float, DocumentValue(1.0)),
            Output("exit", "Exit", ValueType::Flow)
        };
        animationState.PropertyDefaults.emplace("loop", DocumentValue(true));
        registry.Register(std::move(animationState));

        NodeSchema transition = Node(DocumentKind::AnimationState, "kairo.animation.transition",
            "Transition", "State Machine");
        transition.Pins = {
            FlowInput("from", "From"),
            DefaultedInput("condition", "Condition", ValueType::Boolean, DocumentValue(false)),
            Output("to", "To", ValueType::Flow)
        };
        transition.PropertyDefaults.emplace("duration_seconds", DocumentValue(0.2));
        registry.Register(std::move(transition));

        NodeSchema tick = Node(DocumentKind::Simulation, "kairo.simulation.tick", "Simulation Tick", "Events");
        tick.Pins = {
            Output("then", "Then", ValueType::Flow),
            Output("delta_seconds", "Delta Seconds", ValueType::Float)
        };
        registry.Register(std::move(tick));

        NodeSchema force = Node(DocumentKind::Simulation, "kairo.simulation.constant-force",
            "Constant Force", "Forces");
        force.Pins = { Output("force", "Force", ValueType::Vector3) };
        force.PropertyDefaults.emplace("newtons", DocumentValue(Vec3d{}));
        registry.Register(std::move(force));

        NodeSchema applyForce = Node(DocumentKind::Simulation, "kairo.simulation.apply-force",
            "Apply Force", "Bodies");
        applyForce.Pins = {
            FlowInput("in", "In"),
            RequiredInput("body", "Body", ValueType::Entity),
            RequiredInput("force", "Force", ValueType::Vector3),
            Output("then", "Then", ValueType::Flow)
        };
        registry.Register(std::move(applyForce));

        NodeSchema impulse = Node(DocumentKind::Simulation, "kairo.simulation.apply-impulse", "Apply Impulse", "Bodies");
        impulse.Pins = { FlowInput("in", "In"), RequiredInput("body", "Body", ValueType::Entity),
            RequiredInput("impulse", "Impulse", ValueType::Vector3), Output("then", "Then", ValueType::Flow) };
        registry.Register(std::move(impulse));

        NodeSchema setTrigger = Node(DocumentKind::Simulation, "kairo.simulation.set-trigger", "Set Trigger", "Collision");
        setTrigger.Pins = { FlowInput("in", "In"), RequiredInput("collider", "Collider", ValueType::Entity),
            DefaultedInput("enabled", "Enabled", ValueType::Boolean, DocumentValue(true)), Output("then", "Then", ValueType::Flow) };
        registry.Register(std::move(setTrigger));

        NodeSchema collisionEvent = Node(DocumentKind::Simulation, "kairo.simulation.on-collision", "On Collision", "Collision");
        collisionEvent.Pins = { Output("then", "Then", ValueType::Flow), Output("other", "Other", ValueType::Entity),
            Output("normal", "Normal", ValueType::Vector3) };
        registry.Register(std::move(collisionEvent));

        NodeSchema raycast = Node(DocumentKind::Simulation, "kairo.simulation.raycast", "Raycast", "Queries");
        raycast.Pins = { FlowInput("in", "In"), RequiredInput("origin", "Origin", ValueType::Vector3),
            RequiredInput("direction", "Direction", ValueType::Vector3),
            DefaultedInput("distance", "Distance", ValueType::Float, DocumentValue(100.0)),
            Output("hit", "Hit", ValueType::Boolean), Output("point", "Point", ValueType::Vector3),
            Output("then", "Then", ValueType::Flow) };
        registry.Register(std::move(raycast));
    }

    /// Creates an isolated registry so callers can extend it with project or
    /// plugin schemas without mutating process-global state.
    [[nodiscard]] inline DocumentSchemaRegistry CreateCoreDocumentSchemaRegistry()
    {
        DocumentSchemaRegistry registry;
        RegisterCoreDocumentSchemas(registry);
        return registry;
    }
}
