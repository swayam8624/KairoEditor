#include "../detail/SelectionContour.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
using namespace kairo::editor::detail;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main()
{
    // Duplicated diagonal endpoints simulate a UV seam on a quad.
    const std::vector<ContourPoint> quad{{0,0,0},{1,0,0},{1,1,0},{0,0,0},{1,1,0},{0,1,0}};
    const std::vector<std::uint32_t> qi{0,1,2,3,4,5};
    auto build=[&](const auto& p,const auto& i,std::size_t budget=MaximumContourFaces){
        return BuildContourTopology(p.size(),i,[&](std::size_t k){return p[k];},budget);
    };
    auto identity=[](ContourPoint p){return p;};
    const auto square=build(quad,qi);
    Check(square&&square->Points.size()==4&&square->Edges.size()==5,"seam welding");
    const auto border=TraceContour(*square,identity,{0,0,4});
    Check(border&&border->size()==4,"no triangulation diagonal");
    Check(!TraceContour(*square,identity,{0,0,4},std::nullopt,3),"atomic line budget");
    Check(!build(quad,qi,1),"face budget");
    auto invalid=qi;invalid[0]=99;
    Check(!build(quad,invalid),"invalid index");
    auto nonfinite=quad;nonfinite[0][0]=std::numeric_limits<float>::infinity();
    Check(!build(nonfinite,qi),"nonfinite");
    Check(!TraceContour(*square,[](ContourPoint p){p[0]=std::numeric_limits<float>::infinity();return p;},{0,0,4}),"invalid transform");

    // Closed tetrahedron: from above its base all three base edges
    // separate front and back faces; interior edges are not drawn.
    const std::vector<ContourPoint> tetra{{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    const std::vector<std::uint32_t> ti{0,2,1,0,1,3,0,3,2,1,2,3};
    const auto solid=build(tetra,ti);
    Check(solid&&solid->Edges.size()==6,"closed adjacency");
    const auto outline=TraceContour(*solid,identity,{.5f,.5f,-10});
    Check(outline&&outline->size()==3,"tetra perspective");
    const auto ortho=TraceContour(*solid,identity,{100,100,100},ContourPoint{0,0,-1});
    Check(ortho&&ortho->size()==3,"tetra orthographic");
    const auto mirrored=TraceContour(*solid,[](ContourPoint p){p[0]*=-2;p[1]*=3;return p;},
        {0,0,-10},ContourPoint{0,0,-1},MaximumContourLines,true);
    Check(mirrored&&mirrored->size()==3,"mirror and nonuniform scale");
    for(const auto& e:*mirrored)Check(e[0][2]==0&&e[1][2]==0,"mirror retains base silhouette");
    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> shift(-100,100),scale(.1f,10);
    for(unsigned k=0;k<10000;++k)
    {
        const ContourPoint t{shift(rng),shift(rng),shift(rng)};
        const float x=scale(rng),y=scale(rng),z=scale(rng);
        auto transform=[&](ContourPoint p){return ContourPoint{p[0]*x+t[0],p[1]*y+t[1],p[2]*z+t[2]};};
        const auto lines=TraceContour(*square,transform,{t[0],t[1],t[2]+100},ContourPoint{0,0,1});
        Check(lines&&lines->size()==4,"translated and scaled seam");
        for(const auto& e:*lines)Check(e[0]!=e[1],"nonzero segment");
    }
    std::cout<<"selection contour fixtures and 10000 transformed cases passed\n";
}
