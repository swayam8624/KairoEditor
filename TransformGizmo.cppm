module;

#include <imgui.h>
#include <ImGuizmo.h>

#include <array>
#include <cmath>
#include <cstdint>

export module Kairo.Editor.TransformGizmo;

import Kairo.Foundation.Math;

export namespace kairo::editor
{
    enum class TransformGizmoOperation : std::uint8_t { Translate, Rotate, Scale };
    enum class TransformGizmoSpace : std::uint8_t { Local, World };

    struct TransformGizmoFrame final
    {
        kairo::foundation::math::Mat4f View;
        kairo::foundation::math::Mat4f Projection;
        kairo::foundation::math::Transformf Transform;
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 1.0f;
        float Height = 1.0f;
        float Snap = 0.0f;
        TransformGizmoOperation Operation = TransformGizmoOperation::Translate;
        TransformGizmoSpace Space = TransformGizmoSpace::World;
    };

    struct TransformGizmoResult final
    {
        kairo::foundation::math::Transformf Transform;
        bool Changed = false;
        bool Active = false;
        bool Hovered = false;
    };

    /// Native-only adapter around pinned ImGuizmo. No ImGuizmo type crosses
    /// this module boundary; scene, command, renderer, and math APIs remain
    /// independently usable and testable.
    class TransformGizmo final
    {
    public:
        [[nodiscard]] TransformGizmoResult Draw(const TransformGizmoFrame& frame) const
        {
            using namespace kairo::foundation::math;
            auto view = ToColumnMajor(frame.View);
            auto projection = ToColumnMajor(frame.Projection);
            auto model = ToColumnMajor(ToMatrix4(frame.Transform));
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(frame.X, frame.Y, frame.Width, frame.Height);

            const ImGuizmo::OPERATION operation = frame.Operation == TransformGizmoOperation::Translate
                ? ImGuizmo::TRANSLATE : frame.Operation == TransformGizmoOperation::Rotate
                ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
            const ImGuizmo::MODE mode = frame.Space == TransformGizmoSpace::Local
                ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            const std::array snap{ frame.Snap, frame.Snap, frame.Snap };
            const bool changed = ImGuizmo::Manipulate(view.data(), projection.data(), operation, mode,
                model.data(), nullptr, frame.Snap > 0.0f ? snap.data() : nullptr);

            Transformf transform = frame.Transform;
            if (changed)
            {
                const Mat4f matrix = FromColumnMajor(model);
                transform.Translation = ExtractTranslation(matrix);
                // ImGuizmo returns a TRS matrix. Quaternion conversion requires a
                // rotation-only basis; feeding non-uniform scale to FromMatrix4
                // changes the orientation when the user rotates a scaled object.
                const Vec3f magnitude = ExtractScale(matrix);
                transform.Scale = {
                    std::copysign(magnitude.x, frame.Transform.Scale.x),
                    std::copysign(magnitude.y, frame.Transform.Scale.y),
                    std::copysign(magnitude.z, frame.Transform.Scale.z)
                };
                Mat4f rotationOnly = matrix;
                const std::array axisScale{
                    transform.Scale.x, transform.Scale.y, transform.Scale.z
                };
                for (std::size_t column = 0u; column < 3u; ++column)
                {
                    if (std::abs(axisScale[column]) < 1.0e-6f) continue;
                    for (std::size_t row = 0u; row < 3u; ++row)
                        rotationOnly(row, column) /= axisScale[column];
                }
                transform.Rotation = FromMatrix4(rotationOnly).Normalized();
            }
            return { transform, changed, ImGuizmo::IsUsing(), ImGuizmo::IsOver() };
        }

    private:
        [[nodiscard]] static std::array<float, 16u> ToColumnMajor(
            const kairo::foundation::math::Mat4f& matrix) noexcept
        {
            std::array<float, 16u> result{};
            for (std::size_t row = 0u; row < 4u; ++row)
                for (std::size_t column = 0u; column < 4u; ++column)
                    result[column * 4u + row] = matrix(row, column);
            return result;
        }

        [[nodiscard]] static kairo::foundation::math::Mat4f FromColumnMajor(
            const std::array<float, 16u>& values) noexcept
        {
            kairo::foundation::math::Mat4f result{};
            for (std::size_t row = 0u; row < 4u; ++row)
                for (std::size_t column = 0u; column < 4u; ++column)
                    result(row, column) = values[column * 4u + row];
            return result;
        }
    };
}
