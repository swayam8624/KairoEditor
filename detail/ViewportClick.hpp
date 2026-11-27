#pragma once
#include "ViewportInteraction.hpp"
#include <cmath>
#include <optional>

namespace kairo::editor::detail
{
    struct PickSurface
    {
        double X=0,Y=0,W=0,H=0;
        Extent Displayed{};
        friend bool operator==(const PickSurface&,const PickSurface&)=default;
    };
    // Mouse-down is a candidate, never a selection. A drag, surface change,
    // loss of ownership or navigation cancels until the next fresh press.
    class ViewportClick
    {
        struct Press {double X,Y;PickSurface Surface;};
        std::optional<Press> press_;
    public:
        void Cancel() noexcept{press_.reset();}
        std::optional<Extent> Observe(bool pressed,bool held,bool released,bool eligible,
            double x,double y,PickSurface surface)
        {
            const auto pixel=PickPixel(x-surface.X,y-surface.Y,surface.W,surface.H,surface.Displayed);
            if(!eligible||!pixel){Cancel();return {};}
            if(pressed){press_=Press{x,y,surface};return {};}
            if(!press_)return {};
            if(surface!=press_->Surface||std::hypot(x-press_->X,y-press_->Y)>4.)
            {Cancel();return {};}
            if(released){Cancel();return pixel;}
            if(!held)Cancel();
            return {};
        }
    };
}
