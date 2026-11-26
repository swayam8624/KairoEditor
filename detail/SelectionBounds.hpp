#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace kairo::editor::detail
{
    using BoundsPoint = std::array<float,3>;
    struct MeshBounds { BoundsPoint Min,Max; };
    // Compute once at upload. The CPU geometry is not retained by the editor.
    class BoundsBuilder
    {
        std::optional<MeshBounds> m_Bounds;
    public:
        bool Add(BoundsPoint point) noexcept
        {
            for(float value:point) if(!std::isfinite(value)) return false;
            if(!m_Bounds) m_Bounds=MeshBounds{point,point};
            else for(unsigned axis=0;axis<3;++axis)
            {
                m_Bounds->Min[axis]=std::min(m_Bounds->Min[axis],point[axis]);
                m_Bounds->Max[axis]=std::max(m_Bounds->Max[axis],point[axis]);
            }
            return true;
        }
        [[nodiscard]] const std::optional<MeshBounds>& Result() const noexcept {return m_Bounds;}
    };
    // Transform all eight corners, not only min/max: rotations, mirrors and
    // hierarchical nonuniform transforms can move any corner to an extremum.
    [[nodiscard]] inline std::array<BoundsPoint,8> BoundsCorners(const MeshBounds& b) noexcept
    {
        std::array<BoundsPoint,8> result{};
        for(unsigned i=0;i<8;++i)
            for(unsigned axis=0;axis<3;++axis)
                result[i][axis]=(i&(1u<<axis))?b.Max[axis]:b.Min[axis];
        return result;
    }
    inline constexpr std::array<std::array<unsigned,2>,12> BoundsEdges{{
        {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}}};
}
