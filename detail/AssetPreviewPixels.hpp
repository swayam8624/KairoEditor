#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace kairo::editor::detail
{
    // Small display-space previews: no GPU resources, no readback, no shader
    // variants. The host builds these from already imported authoritative data.
    inline constexpr unsigned PreviewExtent=32;
    using PreviewPixels=std::array<std::uint32_t,PreviewExtent*PreviewExtent>;
    using PreviewPoint=std::array<double,3>;
    constexpr std::uint32_t PreviewRGB(unsigned r,unsigned g,unsigned b)
    {return r|(g<<8u)|(b<<16u)|0xff000000u;}
    inline constexpr auto PreviewBackground=PreviewRGB(31,34,39);

    // Aspect-fit source texels into the tile. A checker makes transparency
    // visible. This shows encoded channel values, not a material color pipeline.
    template<class ReadPixel>
    std::optional<PreviewPixels> TexturePreview(unsigned width,unsigned height,ReadPixel read)
    {
        if(!width||!height)return {};
        PreviewPixels out;out.fill(PreviewBackground);
        const double scale=double(PreviewExtent)/std::max(width,height);
        const unsigned w=std::clamp(unsigned(std::round(width*scale)),1u,PreviewExtent);
        const unsigned h=std::clamp(unsigned(std::round(height*scale)),1u,PreviewExtent);
        const unsigned ox=(PreviewExtent-w)/2,oy=(PreviewExtent-h)/2;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
        {
            const auto p=read(unsigned((std::uint64_t(x)*width)/w),unsigned((std::uint64_t(y)*height)/h));
            const unsigned a=p[3],bg=((x/4+y/4)%2)?75u:48u;
            const auto mix=[&](unsigned v){return (v*a+bg*(255-a)+127)/255;};
            out[(oy+y)*PreviewExtent+ox+x]=PreviewRGB(mix(p[0]),mix(p[1]),mix(p[2]));
        }
        return out;
    }

    // Orthographic three-quarter view of the real indexed triangles, with a
    // depth buffer and neutral face shading. Large meshes use a type icon
    // rather than a misleading partial mesh or unbounded CPU raster job.
    template<class ReadPoint>
    std::optional<PreviewPixels> MeshPreview(std::size_t vertexCount,
        std::span<const std::uint32_t> indices,ReadPoint read)
    {
        if(!vertexCount||vertexCount>8192||indices.empty()||indices.size()%3||indices.size()>12288)return {};
        std::array<PreviewPoint,8192> projected{};
        double minX=std::numeric_limits<double>::infinity(),minY=minX,maxX=-minX,maxY=-minX;
        for(std::size_t i=0;i<vertexCount;++i)
        {
            const auto p=read(i);
            for(double v:p)if(!std::isfinite(v)||std::abs(v)>1e30)return {};
            const double x=.707106781*p[0]-.707106781*p[2];
            const double y=.408248290*p[0]-.816496581*p[1]+.408248290*p[2];
            const double z=.577350269*(p[0]+p[1]+p[2]);
            projected[i]={x,y,z};minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);
        }
        const double extent=std::max(maxX-minX,maxY-minY);
        if(extent<=0||!std::isfinite(extent))return {};
        for(std::size_t i=0;i<vertexCount;++i)
        {
            auto& p=projected[i];p[0]=16+(p[0]-(minX+maxX)*.5)*27/extent;
            p[1]=16+(p[1]-(minY+maxY)*.5)*27/extent;
            p[2]/=extent;
        }
        PreviewPixels out;out.fill(PreviewBackground);
        std::array<double,PreviewExtent*PreviewExtent> depth;depth.fill(-std::numeric_limits<double>::infinity());
        const auto edge=[](const PreviewPoint& a,const PreviewPoint& b,double x,double y)
        {return (b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]);};
        bool drew=false;
        for(std::size_t i=0;i<indices.size();i+=3)
        {
            if(indices[i]>=vertexCount||indices[i+1]>=vertexCount||indices[i+2]>=vertexCount)return {};
            const auto& a=projected[indices[i]];const auto& b=projected[indices[i+1]];const auto& c=projected[indices[i+2]];
            const double area=edge(a,b,c[0],c[1]);if(std::abs(area)<1e-10)continue;
            const double nx=(b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]);
            const double ny=(b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]);
            const double nz=area;
            const double normal=std::hypot(nx,ny,nz);
            const unsigned shade=unsigned(100+110*std::abs(nz)/normal);
            const int x0=std::max(0,int(std::floor(std::min({a[0],b[0],c[0]}))));
            const int y0=std::max(0,int(std::floor(std::min({a[1],b[1],c[1]}))));
            const int x1=std::min(31,int(std::ceil(std::max({a[0],b[0],c[0]}))));
            const int y1=std::min(31,int(std::ceil(std::max({a[1],b[1],c[1]}))));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)
            {
                const double u=edge(b,c,x+.5,y+.5)/area,v=edge(c,a,x+.5,y+.5)/area,w=1-u-v;
                if(u<0||v<0||w<0)continue;
                const double z=u*a[2]+v*b[2]+w*c[2];const auto index=std::size_t(y)*PreviewExtent+std::size_t(x);
                if(z<=depth[index])continue;
                depth[index]=z;out[index]=PreviewRGB(shade,shade+5,shade+12);drew=true;
            }
        }
        if(!drew)return {};
        return out;
    }
}
