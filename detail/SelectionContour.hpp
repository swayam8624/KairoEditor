#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace kairo::editor::detail
{
    using ContourPoint=std::array<float,3>;
    using ContourSegment=std::array<ContourPoint,2>;
    inline constexpr std::size_t MaximumContourFaces=20000;
    inline constexpr std::size_t MaximumCachedContourFaces=200000;
    inline constexpr std::size_t MaximumContourLines=8192;
    struct ContourTopology
    {
        std::vector<ContourPoint> Points;
        std::vector<std::array<std::uint32_t,3>> Faces;
        struct Edge
        {
            std::array<std::uint32_t,2> Vertices;
            std::vector<std::uint32_t> Faces;
        };
        std::vector<Edge> Edges;
    };

    // Exact position welding removes duplicated UV/normal seam vertices.
    // Do not weld by epsilon: neighboring independent geometry must not merge.
    // Cap work before allocating; oversized or skinned meshes use bounds.
    template<class Position>
    [[nodiscard]] std::optional<ContourTopology> BuildContourTopology(
        std::size_t vertexCount,std::span<const std::uint32_t> indices,Position position,
        std::size_t faceBudget=MaximumContourFaces)
    {
        if(!vertexCount||vertexCount>MaximumContourFaces*3||
            indices.empty()||indices.size()%3||indices.size()/3>std::min(faceBudget,MaximumContourFaces))
            return std::nullopt;
        ContourTopology result;
        std::map<ContourPoint,std::uint32_t> welded;
        std::vector<std::uint32_t> remap(vertexCount);
        for(std::size_t i=0;i<vertexCount;++i)
        {
            const auto p=position(i);
            for(float x:p)if(!std::isfinite(x))return std::nullopt;
            const auto [it,inserted]=welded.try_emplace(p,static_cast<std::uint32_t>(result.Points.size()));
            if(inserted)result.Points.push_back(p);
            remap[i]=it->second;
        }
        std::map<std::array<std::uint32_t,2>,std::vector<std::uint32_t>> edges;
        for(std::size_t i=0;i<indices.size();i+=3)
        {
            if(indices[i]>=vertexCount||indices[i+1]>=vertexCount||indices[i+2]>=vertexCount)
                return std::nullopt;
            const std::array f{remap[indices[i]],remap[indices[i+1]],remap[indices[i+2]]};
            if(f[0]==f[1]||f[1]==f[2]||f[2]==f[0])continue;
            const auto face=static_cast<std::uint32_t>(result.Faces.size());
            result.Faces.push_back(f);
            for(unsigned j=0;j<3;++j)
            {
                std::array key{f[j],f[(j+1)%3]};
                if(key[0]>key[1])std::swap(key[0],key[1]);
                edges[key].push_back(face);
            }
        }
        if(result.Faces.empty())return std::nullopt;
        result.Edges.reserve(edges.size());
        for(auto& [key,faces]:edges)result.Edges.push_back({key,std::move(faces)});
        return result;
    }

    // Classify in transformed world space, which handles mirrors and
    // nonuniform parent transforms without an inverse normal matrix.
    // Return front/back transitions and open boundaries, never coplanar
    // triangulation diagonals. This is a geometric contour, not an ID-mask
    // compositing pass: other objects and alpha cutouts are not considered.
    template<class Transform>
    [[nodiscard]] std::optional<std::vector<ContourSegment>> TraceContour(
        const ContourTopology& topology,Transform transform,ContourPoint eye,
        std::optional<ContourPoint> orthographicTowardEye=std::nullopt,
        std::size_t lineBudget=MaximumContourLines)
    {
        using D=std::array<double,3>;
        const auto subtract=[](ContourPoint a,ContourPoint b)->D
        {return {double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]};};
        std::vector<ContourPoint> world;
        world.reserve(topology.Points.size());
        for(auto p:topology.Points)
        {
            auto q=transform(p);
            for(float x:q)if(!std::isfinite(x))return std::nullopt;
            world.push_back(q);
        }
        // 0 = collapsed or invalid face, 1 = front, 2 = back/edge-on.
        std::vector<unsigned char> facing;
        facing.reserve(topology.Faces.size());
        for(auto f:topology.Faces)
        {
            const D a=subtract(world[f[1]],world[f[0]]),b=subtract(world[f[2]],world[f[0]]);
            const D n{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
            const D v=orthographicTowardEye?subtract(*orthographicTowardEye,{}):subtract(eye,world[f[0]]);
            const double dot=n[0]*v[0]+n[1]*v[1]+n[2]*v[2];
            const double nn=n[0]*n[0]+n[1]*n[1]+n[2]*n[2];
            const double vv=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
            if(nn==0||vv==0||!std::isfinite(dot)||!std::isfinite(nn)||!std::isfinite(vv))
                facing.push_back(0);
            else
                facing.push_back(dot>1e-10*std::sqrt(nn)*std::sqrt(vv)?1:2);
        }
        std::vector<ContourSegment> lines;
        for(const auto& e:topology.Edges)
        {
            bool front=false,back=false;
            unsigned valid=0;
            for(auto f:e.Faces)
            {
                front|=facing[f]==1;back|=facing[f]==2;
                valid+=facing[f]!=0;
            }
            if(valid==1||(front&&back))
            {
                if(lines.size()>=lineBudget)return std::nullopt;
                lines.push_back({world[e.Vertices[0]],world[e.Vertices[1]]});
            }
        }
        return lines;
    }
}
