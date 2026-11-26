#include "../detail/SelectionBounds.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
using namespace kairo::editor::detail;
void Check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int main()
{
    BoundsBuilder empty;
    Check(!empty.Result(),"empty");
    Check(!empty.Add({0,std::numeric_limits<float>::infinity(),0})&&!empty.Result(),"nonfinite");
    std::mt19937 random(2026);
    std::uniform_real_distribution<float> sample(-100,100);
    for(int trial=0;trial<50000;++trial)
    {
        BoundsBuilder builder;
        const BoundsPoint a{sample(random),sample(random),sample(random)};
        const BoundsPoint b{sample(random),sample(random),sample(random)};
        Check(builder.Add(a)&&builder.Add(b),"finite inputs");
        const auto bounds=*builder.Result();
        const auto corners=BoundsCorners(bounds);
        for(unsigned axis=0;axis<3;++axis)
        {
            Check(bounds.Min[axis]==std::min(a[axis],b[axis]),"minimum");
            Check(bounds.Max[axis]==std::max(a[axis],b[axis]),"maximum");
        }
        unsigned degrees[8]{};
        for(auto edge:BoundsEdges)
        {
            const unsigned diff=edge[0]^edge[1];
            Check(diff==1||diff==2||diff==4,"edge must differ in exactly one axis");
            ++degrees[edge[0]];++degrees[edge[1]];
        }
        for(unsigned i=0;i<8;++i)
        {
            Check(degrees[i]==3,"each corner has three incident edges");
            for(unsigned axis=0;axis<3;++axis)
                Check(corners[i][axis]==((i&(1u<<axis))?bounds.Max[axis]:bounds.Min[axis]),"corner");
        }
    }
    std::cout<<"50000 selection bounds cases passed\n";
}
