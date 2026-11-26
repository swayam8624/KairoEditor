#include "../detail/AnimationTimeline.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>
using namespace kairo::editor::detail;
struct Key {float TimeSeconds;};
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main()
{
    Check(AdvanceTimeline({2,false},2,1,1,true).Time==2,"paused final key");
    Check(AdvanceTimeline({2,true},2,.25f,1,true).Time==.25f,"loop from final key");
    Check(!AdvanceTimeline({1,true},2,2,1,false).Playing,"clamp stops");
    Check(AdvanceTimeline({1,true},2,2,1,false).Time==2,"clamp endpoint");
    Check(AdvanceTimeline({2,true},2,1,0,true).Time==2,"zero speed preserves endpoint");
    Check(!AdvanceTimeline({1,true},2,-1,1,true).Playing,"invalid delta stops");
    Check(AdvanceTimeline({1,true},0,1,1,true).Time==0,"zero duration");
    Check(TimelineTimeAt(-1,2)==0&&TimelineTimeAt(2,2)==2,"clamped scrubbing");
    Check(TimelineTimeAt(std::numeric_limits<float>::quiet_NaN(),2)==0,"invalid scrub");
    const std::vector<Key> keys{{0},{.5f},{1},{2}};
    const std::span<const Key> track(keys);
    Check(AdjacentKey(track,.5f,true)==1,"strict next");
    Check(AdjacentKey(track,.5f,false)==0,"strict previous");
    Check(!AdjacentKey(track,0,false)&&!AdjacentKey(track,2,true),"endpoints");
    Check(!HasKeyInBin(track,1.1f,2,false)&&HasKeyInBin(track,1.1f,2,true),"last bin includes endpoint");
    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> sample(0,2);
    for(unsigned i=0;i<10000;++i)
    {
        const float t=sample(rng),d=sample(rng);
        const auto a=AdvanceTimeline({t,true},2,d,1,true);
        Check(a.Time>=0&&a.Time<=2,"bounded loop");
        const auto b=AdvanceTimeline({t,true},2,d,1,false);
        Check(b.Time>=t&&b.Time<=2,"monotonic clamp");
        std::optional<float> previous,next;
        for(auto k:keys){if(k.TimeSeconds<t)previous=k.TimeSeconds;if(k.TimeSeconds>t&&!next)next=k.TimeSeconds;}
        Check(AdjacentKey(track,t,false)==previous&&AdjacentKey(track,t,true)==next,"binary search parity");
        const float end=std::min(t+d,2.f);
        bool expected=false;
        for(auto k:keys)expected|=k.TimeSeconds>=t&&k.TimeSeconds<end;
        Check(HasKeyInBin(track,t,end,false)==expected,"bin parity");
    }
    std::cout<<"animation timeline fixtures and 10000 seeded cases passed\n";
}
