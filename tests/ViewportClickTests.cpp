#include "../detail/ViewportClick.hpp"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace kairo::editor::detail;
static void Check(bool ok){if(!ok)throw std::runtime_error("Viewport click regression");}
int main()
{
    const PickSurface surface{100,200,800,600,{1600,1200}};
    ViewportClick click;
    Check(!click.Observe(true,true,false,true,200,300,surface));
    Check(click.Observe(false,false,true,true,201,301,surface)==Extent{202,202});
    Check(!click.Observe(false,false,true,true,201,301,surface));
    std::mt19937 rng(2026);
    for(unsigned i=0;i<10000;++i)
    {
        const double x=110+rng()%750,y=210+rng()%550;
        Check(!click.Observe(true,true,false,true,x,y,surface));
        switch(i%5)
        {
            case 0:Check(click.Observe(false,false,true,true,x+1,y+1,surface)==PickPixel(x+1-surface.X,y+1-surface.Y,surface.W,surface.H,surface.Displayed));break;
            case 1:Check(!click.Observe(false,true,false,true,x+8,y,surface));break;
            case 2:Check(!click.Observe(false,true,false,false,x,y,surface));break;
            case 3:{auto resized=surface;resized.Displayed.W++;Check(!click.Observe(false,true,false,true,x,y,resized));break;}
            case 4:click.Cancel();break;
        }
        Check(!click.Observe(false,false,true,true,x,y,surface));
    }
    Check(!click.Observe(true,true,false,true,900,300,surface));
    Check(!click.Observe(false,false,true,true,899,300,surface));
    std::cout<<"10000 viewport click/drag/cancellation cases passed\n";
}
