#include "../detail/AssetPreviewPixels.hpp"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace kairo::editor::detail;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main()
{
    auto red=TexturePreview(16,8,[](unsigned x,unsigned y){check(x<16&&y<8,"source bounds");return std::array<unsigned,4>{255,0,0,255};});
    check(red&&(*red)[16*32+16]==PreviewRGB(255,0,0)&&(*red)[0]==PreviewBackground,"aspect-fit texture");
    auto alpha=TexturePreview(1,1,[](unsigned,unsigned){return std::array<unsigned,4>{255,0,0,0};});
    check(alpha&&(*alpha)[0]==PreviewRGB(48,48,48)&&(*alpha)[4]==PreviewRGB(75,75,75),"alpha checker");
    std::array<PreviewPoint,3> points{{{-1,0,0},{1,0,0},{0,1,0}}};
    const std::array<std::uint32_t,3> triangle{0,1,2};
    const auto read=[&](std::size_t i){return points.at(i);};
    auto image=MeshPreview(points.size(),triangle,read);
    check(image&&std::ranges::count(*image,PreviewBackground)<900,"real triangle coverage");
    check(image==MeshPreview(points.size(),triangle,read),"deterministic raster");
    const std::array<std::uint32_t,3> bad{0,1,3};
    check(!MeshPreview(points.size(),bad,read),"bad index");
    points[0][0]=std::numeric_limits<double>::infinity();
    check(!MeshPreview(points.size(),triangle,read),"nonfinite vertex");
    std::mt19937 rng(2026);
    std::uniform_real_distribution<double> p(-100,100);
    for(int i=0;i<10000;++i)
    {
        const unsigned w=1+rng()%4096,h=1+rng()%4096;
        auto texture=TexturePreview(w,h,[&](unsigned x,unsigned y){check(x<w&&y<h,"random source bounds");return std::array<unsigned,4>{x%256,y%256,42,255};});
        check(texture.has_value(),"valid texture");
        for(auto& v:points)v={p(rng),p(rng),p(rng)};
        auto mesh=MeshPreview(points.size(),triangle,read);
        check(mesh==MeshPreview(points.size(),triangle,read),"random deterministic mesh");
        if(mesh)for(auto pixel:*mesh)check((pixel>>24)==255,"opaque pixels");
    }
    std::cout<<"10000 texture and mesh preview cases passed\n";
}
