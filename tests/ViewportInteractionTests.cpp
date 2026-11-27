#include "../detail/ViewportInteraction.hpp"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace kairo::editor::detail;
void Check(bool ok){if(!ok)throw std::runtime_error("Viewport regression");}
int main()
{
    Check(DesiredExtent(800,600,2,2)==Extent{1600,1200});
    Check(DesiredExtent(0,600,2,2)==Extent{});
    Check(DesiredExtent(1e9,1e9,1,1)==Extent{2896,2896});
    Check(PickPixel(799,599,800,600,{1600,1200})==Extent{1598,1198});
    Check(!PickPixel(800,599,800,600,{1600,1200}));
    ResizePolicy resize({1600,1000});
    Check(resize.Observe({800,600})==Extent{800,600});
    Check(!resize.Observe({801,600}));
    Check(!resize.Observe({802,600}));
    Check(!resize.Observe({802,600}));
    Check(resize.Observe({802,600})==Extent{802,600});
    for(unsigned i=0;i<11;++i)Check(!resize.Observe({900+i,600}));
    Check(resize.Observe({911,600})==Extent{911,600});
    Check(!resize.Observe({}));Check(resize.Current()==Extent{911,600});
    GestureCapture capture;
    Check(capture.Update(Gesture::Orbit,true,true,false,true)==Gesture::Orbit);
    Check(capture.Update(Gesture::Fly,true,true,false,true)==Gesture::Orbit);
    Check(capture.Update(Gesture::None,false,true,false,true)==Gesture::None);
    Check(capture.Update(Gesture::Fly,true,true,false,true)==Gesture::None);
    capture.Update(Gesture::None,false,false,false,true);
    Check(capture.Update(Gesture::Fly,true,true,false,true)==Gesture::Fly);
    Check(capture.Update(Gesture::None,true,true,false,false)==Gesture::None);
    Check(capture.Update(Gesture::Pan,true,true,false,true)==Gesture::None);
    capture.Update(Gesture::None,false,false,false,true);
    Check(capture.Update(Gesture::Pan,true,true,false,true)==Gesture::Pan);
    Check(capture.Update(Gesture::None,true,true,true,true)==Gesture::None);
    std::mt19937 rng(2026);
    for(unsigned i=0;i<10000;++i)
    {
        const double w=1+rng()%30000,h=1+rng()%30000;
        const auto e=DesiredExtent(w,h,1+rng()%3,1+rng()%3);
        Check(e.W<=4096&&e.H<=4096&&std::uint64_t(e.W)*e.H<=8388608);
        const auto p=PickPixel(w*.999,h*.999,w,h,e);
        Check(p&&p->W<e.W&&p->H<e.H);
    }
    std::cout<<"10000 randomized viewport budgets and pick mapping cases passed\n";
}
