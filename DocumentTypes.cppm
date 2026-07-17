module;

#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

export module Kairo.Editor.DocumentTypes;

import Kairo.Assets;
import Kairo.EngineCore.Entity;
import Kairo.Editor.TextValidation;
import Kairo.Foundation.Math;

export namespace kairo::editor
{
    struct NodeIDTag;
    struct PinIDTag;

    /// Strong document-local identity. Zero is reserved as invalid. IDs remain
    /// stable through serialization, graph layout changes, and projection into
    /// code; different Tag types prevent accidental node/pin interchange.
    template<class Tag>
    struct StableLocalID final
    {
        std::uint64_t Value = 0u;
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return Value != 0u; }
        friend constexpr auto operator<=>(const StableLocalID&, const StableLocalID&) noexcept = default;
    };

    using NodeID = StableLocalID<NodeIDTag>;
    using PinID = StableLocalID<PinIDTag>;

    template<class Tag>
    struct StableLocalIDHash final
    {
        [[nodiscard]] constexpr std::size_t operator()(StableLocalID<Tag> id) const noexcept
        {
            const std::uint64_t mixed = id.Value ^ (id.Value >> 33u) ^ (id.Value << 11u);
            return static_cast<std::size_t>(mixed);
        }
    };

    enum class DocumentKind : std::uint8_t { Logic, Material, Audio, AnimationState, Simulation };
    enum class PinDirection : std::uint8_t { Input, Output };
    enum class PinCardinality : std::uint8_t { Single, Multiple };
    enum class ValueType : std::uint8_t
    {
        Flow, Boolean, Integer, Float, Vector2, Vector3, Vector4, String, Asset, Entity
    };

    [[nodiscard]] constexpr std::string_view Name(DocumentKind kind) noexcept
    {
        switch (kind)
        {
            case DocumentKind::Logic: return "logic";
            case DocumentKind::Material: return "material";
            case DocumentKind::Audio: return "audio";
            case DocumentKind::AnimationState: return "animation-state";
            case DocumentKind::Simulation: return "simulation";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr std::string_view Name(ValueType type) noexcept
    {
        switch (type)
        {
            case ValueType::Flow: return "flow";
            case ValueType::Boolean: return "bool";
            case ValueType::Integer: return "int";
            case ValueType::Float: return "float";
            case ValueType::Vector2: return "vec2";
            case ValueType::Vector3: return "vec3";
            case ValueType::Vector4: return "vec4";
            case ValueType::String: return "string";
            case ValueType::Asset: return "asset";
            case ValueType::Entity: return "entity";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr std::string_view Name(PinDirection direction) noexcept
    {
        return direction == PinDirection::Input ? "input" : "output";
    }

    [[nodiscard]] constexpr std::string_view Name(PinCardinality cardinality) noexcept
    {
        return cardinality == PinCardinality::Single ? "single" : "multiple";
    }

    [[nodiscard]] constexpr std::optional<DocumentKind> ParseDocumentKind(std::string_view value) noexcept
    {
        if (value == "logic") return DocumentKind::Logic;
        if (value == "material") return DocumentKind::Material;
        if (value == "audio") return DocumentKind::Audio;
        if (value == "animation-state") return DocumentKind::AnimationState;
        if (value == "simulation") return DocumentKind::Simulation;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<ValueType> ParseValueType(std::string_view value) noexcept
    {
        if (value == "flow") return ValueType::Flow;
        if (value == "bool") return ValueType::Boolean;
        if (value == "int") return ValueType::Integer;
        if (value == "float") return ValueType::Float;
        if (value == "vec2") return ValueType::Vector2;
        if (value == "vec3") return ValueType::Vector3;
        if (value == "vec4") return ValueType::Vector4;
        if (value == "string") return ValueType::String;
        if (value == "asset") return ValueType::Asset;
        if (value == "entity") return ValueType::Entity;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<PinDirection> ParsePinDirection(std::string_view value) noexcept
    {
        if (value == "input") return PinDirection::Input;
        if (value == "output") return PinDirection::Output;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<PinCardinality> ParsePinCardinality(std::string_view value) noexcept
    {
        if (value == "single") return PinCardinality::Single;
        if (value == "multiple") return PinCardinality::Multiple;
        return std::nullopt;
    }

    /// Canonical value domain shared by graph pins, structured text, defaults,
    /// diagnostics, and compiler input. Floating values must be finite; strings
    /// are bounded valid UTF-8; asset values require a persistent non-zero ID.
    class DocumentValue final
    {
    public:
        using Storage = std::variant<std::monostate, bool, std::int64_t, double,
            kairo::foundation::math::Vec2d, kairo::foundation::math::Vec3d,
            kairo::foundation::math::Vec4d, std::string, kairo::assets::AssetID,
            kairo::engine::Entity>;

        DocumentValue() noexcept = default;
        explicit DocumentValue(bool value) noexcept : m_Value(value) {}
        explicit DocumentValue(std::int64_t value) noexcept : m_Value(value) {}
        explicit DocumentValue(double value) : m_Value(value) { Validate(); }
        explicit DocumentValue(kairo::foundation::math::Vec2d value) : m_Value(value) { Validate(); }
        explicit DocumentValue(kairo::foundation::math::Vec3d value) : m_Value(value) { Validate(); }
        explicit DocumentValue(kairo::foundation::math::Vec4d value) : m_Value(value) { Validate(); }
        explicit DocumentValue(std::string value) : m_Value(std::move(value)) { Validate(); }
        explicit DocumentValue(kairo::assets::AssetID value) : m_Value(value) { Validate(); }
        explicit DocumentValue(kairo::engine::Entity value) : m_Value(value) { Validate(); }

        [[nodiscard]] ValueType Type() const noexcept
        {
            constexpr ValueType types[] = { ValueType::Flow, ValueType::Boolean, ValueType::Integer,
                ValueType::Float, ValueType::Vector2, ValueType::Vector3, ValueType::Vector4,
                ValueType::String, ValueType::Asset, ValueType::Entity };
            return types[m_Value.index()];
        }

        [[nodiscard]] const Storage& Data() const noexcept { return m_Value; }

        template<class T>
        [[nodiscard]] const T& Get() const
        {
            const T* value = std::get_if<T>(&m_Value);
            if (value == nullptr) throw std::logic_error("Document value type mismatch.");
            return *value;
        }

        void Validate() const
        {
            const auto finite = [](double value) { return std::isfinite(value); };
            if (const auto* value = std::get_if<double>(&m_Value); value != nullptr && !finite(*value))
                throw std::invalid_argument("Document floating value must be finite.");
            if (const auto* value = std::get_if<kairo::foundation::math::Vec2d>(&m_Value);
                value != nullptr && (!finite(value->x) || !finite(value->y)))
                throw std::invalid_argument("Document vec2 value must be finite.");
            if (const auto* value = std::get_if<kairo::foundation::math::Vec3d>(&m_Value);
                value != nullptr && (!finite(value->x) || !finite(value->y) || !finite(value->z)))
                throw std::invalid_argument("Document vec3 value must be finite.");
            if (const auto* value = std::get_if<kairo::foundation::math::Vec4d>(&m_Value);
                value != nullptr && (!finite(value->x) || !finite(value->y) || !finite(value->z) || !finite(value->w)))
                throw std::invalid_argument("Document vec4 value must be finite.");
            if (const auto* value = std::get_if<std::string>(&m_Value); value != nullptr)
                ValidateUtf8Text(*value, { 0u, 64u * 1024u, true, true }, "Document string value");
            if (const auto* value = std::get_if<kairo::assets::AssetID>(&m_Value);
                value != nullptr && !value->IsValid())
                throw std::invalid_argument("Document asset value requires a valid persistent asset ID.");
            if (const auto* value = std::get_if<kairo::engine::Entity>(&m_Value);
                value != nullptr && !*value)
                throw std::invalid_argument("Document entity value requires a non-zero scene entity ID.");
        }

        [[nodiscard]] bool operator==(const DocumentValue& other) const
        {
            if (Type() != other.Type()) return false;
            switch (Type())
            {
                case ValueType::Flow: return true;
                case ValueType::Boolean: return Get<bool>() == other.Get<bool>();
                case ValueType::Integer: return Get<std::int64_t>() == other.Get<std::int64_t>();
                case ValueType::Float: return Get<double>() == other.Get<double>();
                case ValueType::Vector2: return Get<kairo::foundation::math::Vec2d>() ==
                    other.Get<kairo::foundation::math::Vec2d>();
                case ValueType::Vector3: return Get<kairo::foundation::math::Vec3d>() ==
                    other.Get<kairo::foundation::math::Vec3d>();
                case ValueType::Vector4: return Get<kairo::foundation::math::Vec4d>() ==
                    other.Get<kairo::foundation::math::Vec4d>();
                case ValueType::String: return Get<std::string>() == other.Get<std::string>();
                case ValueType::Asset: return Get<kairo::assets::AssetID>() == other.Get<kairo::assets::AssetID>();
                case ValueType::Entity: return Get<kairo::engine::Entity>() == other.Get<kairo::engine::Entity>();
            }
            return false;
        }

    private:
        Storage m_Value;
    };

    struct CanvasPosition final
    {
        double X = 0.0;
        double Y = 0.0;
        friend constexpr bool operator==(const CanvasPosition&, const CanvasPosition&) noexcept = default;
    };

    /// Input: a document-space node position.
    /// Task: enforce the shared finite coordinate envelope before a position
    /// enters a document, command, serializer, or graph interaction.
    inline void ValidateCanvasPosition(CanvasPosition position)
    {
        constexpr double limit = 1.0e9;
        if (!std::isfinite(position.X) || !std::isfinite(position.Y) ||
            std::abs(position.X) > limit || std::abs(position.Y) > limit)
            throw std::invalid_argument("Canvas position must be finite and within +/-1e9 units.");
    }
}
