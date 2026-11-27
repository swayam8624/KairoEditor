#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace kairo::editor::detail
{
    struct Extent {std::uint32_t W=0,H=0;friend bool operator==(Extent,Extent)=default;};
    // Preserve aspect ratio while bounding expensive offscreen attachments.
    inline Extent DesiredExtent(double w,double h,double sx,double sy)
    {
        w*=sx;h*=sy;
        if(!std::isfinite(w)||!std::isfinite(h)||w<1||h<1)return {};
        const double scale=std::min({1.,4096./w,4096./h,std::sqrt(8388608./w/h)});
        return {static_cast<std::uint32_t>(std::clamp(std::floor(w*scale),1.,4096.)),
                static_cast<std::uint32_t>(std::clamp(std::floor(h*scale),1.,4096.))};
    }
    inline std::optional<Extent> PickPixel(double x,double y,double width,double height,Extent displayed)
    {
        if(!displayed.W||!displayed.H||!std::isfinite(x)||!std::isfinite(y)||
            !std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||
            x<0||y<0||x>=width||y>=height)return {};
        return Extent{std::min(displayed.W-1,static_cast<std::uint32_t>(x/width*displayed.W)),
            std::min(displayed.H-1,static_cast<std::uint32_t>(y/height*displayed.H))};
    }
    // Coalesce dock dragging. A stable size commits after three observations;
    // continuous resize still refreshes after at most twelve visible frames.
    // First visible frame commits immediately; hidden panels never resize.
    class ResizePolicy
    {
        Extent m_Current{},m_Pending{};
        unsigned m_Stable=0,m_Wait=0;
        bool m_Started=false;
    public:
        explicit ResizePolicy(Extent current):m_Current(current){}
        Extent Current() const noexcept{return m_Current;}
        std::optional<Extent> Observe(Extent requested)
        {
            if(!requested.W||!requested.H||requested==m_Current)
            {m_Pending={};m_Stable=m_Wait=0;return {};}
            if(requested==m_Pending)++m_Stable;
            else {m_Pending=requested;m_Stable=1;}
            ++m_Wait;
            if(!m_Started||m_Stable>=3||m_Wait>=12)
            {
                m_Started=true;m_Stable=m_Wait=0;
                m_Current=requested;m_Pending={};return requested;
            }
            return {};
        }
    };
    enum class Gesture {None,Orbit,Pan,Dolly,Fly};
    // Once acquired, a specific button/modifier mode owns the drag until
    // release; modifiers cannot transform a selection click into navigation.
    class GestureCapture
    {
        Gesture m_Mode=Gesture::None;
        bool m_Cancelled=false;
    public:
        Gesture Update(Gesture start,bool held,bool anyHeld,bool cancel,bool focused)
        {
            if(!anyHeld){m_Mode=Gesture::None;m_Cancelled=false;return m_Mode;}
            if(cancel||!focused){m_Mode=Gesture::None;m_Cancelled=true;return m_Mode;}
            if(m_Mode!=Gesture::None&&!held){m_Mode=Gesture::None;m_Cancelled=true;}
            if(!m_Cancelled&&m_Mode==Gesture::None&&start!=Gesture::None)m_Mode=start;
            return m_Mode;
        }
        Gesture Mode() const noexcept{return m_Mode;}
        void Cancel() noexcept{m_Mode=Gesture::None;m_Cancelled=true;}
    };
}
