module;

#include <algorithm>
#include <cmath>
#include <stdexcept>

export module Kairo.Editor.ViewportController;

import Kairo.Foundation.Math;

export namespace kairo::editor
{
    enum class ViewportAxis : std::uint8_t { Front, Back, Right, Left, Top, Bottom };

    /// Backend-neutral camera pose consumed by a renderer adapter. Coordinates
    /// are right-handed, Y-up, and expressed in authoring world units.
    struct ViewportCameraPose final
    {
        kairo::foundation::math::Vec3f Position{};
        kairo::foundation::math::Vec3f Target{};
        kairo::foundation::math::Vec3f Up = kairo::foundation::math::Vec3f::Up();
    };

    /// Raw viewport interaction supplied by the UI backend once per frame.
    /// Mouse deltas are pixels; movement values are normalized intent axes.
    struct ViewportInput final
    {
        float MouseDeltaX = 0.0f;
        float MouseDeltaY = 0.0f;
        float WheelDelta = 0.0f;
        float MoveForward = 0.0f;
        float MoveRight = 0.0f;
        float MoveUp = 0.0f;
        float DeltaSeconds = 0.0f;
        bool Orbit = false;
        bool Pan = false;
        bool Fly = false;
    };

    /// Input: raw mouse/keyboard intent while the viewport owns focus.
    /// Output: an orbit/fly camera pose without direct GLFW/ImGui dependency.
    /// Task: make Blender-style MMB orbit, Shift+MMB pan, wheel zoom, and
    /// Unreal-style RMB+WASD navigation testable and reusable across shells.
    class ViewportController final
    {
    public:
        [[nodiscard]] ViewportCameraPose Pose() const noexcept
        {
            using namespace kairo::foundation::math;
            const float cosinePitch = std::cos(m_Pitch);
            const Vec3f direction{
                cosinePitch * std::sin(m_Yaw),
                std::sin(m_Pitch),
                cosinePitch * std::cos(m_Yaw)
            };
            // `direction` points from the orbit target toward the camera. Keeping
            // that convention yields the familiar elevated three-quarter editor view.
            return { m_Target + direction * m_Distance, m_Target, Vec3f::Up() };
        }

        [[nodiscard]] float Distance() const noexcept { return m_Distance; }

        /// Input: one canonical world axis view.
        /// Task: snap orientation while preserving focus target and distance.
        void SnapToAxis(ViewportAxis axis) noexcept
        {
            constexpr float Pi = 3.14159265358979323846f;
            switch (axis)
            {
                case ViewportAxis::Front: m_Yaw = 0.0f; m_Pitch = 0.0f; break;
                case ViewportAxis::Back: m_Yaw = Pi; m_Pitch = 0.0f; break;
                case ViewportAxis::Right: m_Yaw = Pi * 0.5f; m_Pitch = 0.0f; break;
                case ViewportAxis::Left: m_Yaw = -Pi * 0.5f; m_Pitch = 0.0f; break;
                case ViewportAxis::Top: m_Yaw = 0.0f; m_Pitch = Pi * 0.5f; break;
                case ViewportAxis::Bottom: m_Yaw = 0.0f; m_Pitch = -Pi * 0.5f; break;
            }
        }

        void Reset() noexcept
        {
            m_Target = {};
            m_Yaw = 0.60f;
            m_Pitch = 0.31f;
            m_Distance = 5.0f;
        }

        /// Input: finite point to inspect and a positive preferred distance.
        /// Output: camera target changes while retaining a stable orientation.
        /// Task: implement the F / frame-selection workflow without guessing an
        /// entity's mesh bounds, which belongs to a later asset bounds service.
        void Focus(const kairo::foundation::math::Vec3f& point, float distance = 4.0f)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
                !std::isfinite(distance) || distance <= 0.0f)
                throw std::invalid_argument("Viewport focus requires a finite point and positive distance.");
            m_Target = point;
            m_Distance = std::clamp(distance, 0.15f, 5000.0f);
        }

        void Update(const ViewportInput& input)
        {
            if (!std::isfinite(input.MouseDeltaX) || !std::isfinite(input.MouseDeltaY) ||
                !std::isfinite(input.WheelDelta) || !std::isfinite(input.DeltaSeconds) ||
                input.DeltaSeconds < 0.0f)
                throw std::invalid_argument("Viewport input must be finite with non-negative delta time.");
            using namespace kairo::foundation::math;
            if (input.Orbit)
            {
                m_Yaw -= input.MouseDeltaX * 0.008f;
                m_Pitch = std::clamp(m_Pitch - input.MouseDeltaY * 0.008f, -1.52f, 1.52f);
            }
            const ViewportCameraPose pose = Pose();
            const Vec3f forward = SafeNormalize(pose.Target - pose.Position, Vec3f::Forward());
            const Vec3f right = SafeNormalize(Cross(forward, pose.Up), Vec3f::Right());
            const Vec3f up = SafeNormalize(Cross(right, forward), Vec3f::Up());
            if (input.Pan)
                m_Target += (-right * input.MouseDeltaX + up * input.MouseDeltaY) * (m_Distance * 0.0022f);
            if (input.WheelDelta != 0.0f)
                m_Distance = std::clamp(m_Distance * std::exp(-input.WheelDelta * 0.13f), 0.15f, 5000.0f);
            if (input.Fly)
            {
                const float speed = std::max(1.0f, m_Distance) * 1.8f * input.DeltaSeconds;
                const Vec3f translation = (forward * input.MoveForward + right * input.MoveRight + up * input.MoveUp) * speed;
                m_Target += translation;
            }
        }

    private:
        kairo::foundation::math::Vec3f m_Target{};
        float m_Yaw = 0.60f;
        float m_Pitch = 0.31f;
        float m_Distance = 5.0f;
    };
}
