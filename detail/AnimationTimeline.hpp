#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace kairo::editor::detail
{
    struct TimelineClock
    {
        float Time=0;
        bool Playing=false;
    };

    // Looping belongs to the clock, not the sampler. A paused scrub at
    // duration must sample the final key, even when looping is enabled.
    inline TimelineClock AdvanceTimeline(TimelineClock clock,float duration,
        float delta,float speed,bool loop) noexcept
    {
        if(!std::isfinite(duration)||duration<=0)return {};
        if(!std::isfinite(clock.Time))clock.Time=0;
        clock.Time=std::clamp(clock.Time,0.f,duration);
        if(!clock.Playing)return clock;
        if(!std::isfinite(delta)||delta<0||!std::isfinite(speed)||speed<0)
            return {clock.Time,false};
        const double next=double(clock.Time)+double(delta)*speed;
        if(delta==0||speed==0)return clock;
        if(loop)clock.Time=static_cast<float>(std::fmod(next,double(duration)));
        else if(next>=duration)return {duration,false};
        else clock.Time=static_cast<float>(next);
        return clock;
    }

    // Input keys are sorted by the asset validator. Binary search avoids
    // scanning dense tracks during drawing or previous/next navigation.
    template<class Key>
    [[nodiscard]] std::optional<float> AdjacentKey(std::span<const Key> keys,
        float time,bool forward) noexcept
    {
        if(!std::isfinite(time)||keys.empty())return std::nullopt;
        if(forward)
        {
            const auto it=std::upper_bound(keys.begin(),keys.end(),time,
                [](float t,const Key& k){return t<k.TimeSeconds;});
            if(it!=keys.end())return it->TimeSeconds;
        }
        else
        {
            const auto it=std::lower_bound(keys.begin(),keys.end(),time,
                [](const Key& k,float t){return k.TimeSeconds<t;});
            if(it!=keys.begin())return (it-1)->TimeSeconds;
        }
        return std::nullopt;
    }

    template<class Key>
    [[nodiscard]] bool HasKeyInBin(std::span<const Key> keys,float begin,
        float end,bool includeEnd) noexcept
    {
        if(!std::isfinite(begin)||!std::isfinite(end)||begin>end)return false;
        const auto it=std::lower_bound(keys.begin(),keys.end(),begin,
            [](const Key& k,float t){return k.TimeSeconds<t;});
        return it!=keys.end()&&(it->TimeSeconds<end||(includeEnd&&it->TimeSeconds==end));
    }

    [[nodiscard]] inline float TimelineTimeAt(float fraction,float duration) noexcept
    {
        if(!std::isfinite(fraction)||!std::isfinite(duration)||duration<=0)return 0;
        return std::clamp(fraction,0.f,1.f)*duration;
    }
}
