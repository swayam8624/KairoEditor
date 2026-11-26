#include "../detail/LogicTraceCapture.hpp"
#include <iostream>
#include <random>
using namespace kairo::editor::detail;
void Check(bool ok){if(!ok)throw std::runtime_error("trace regression");}
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected);}
int main()
{
    const std::string session="00000000-0000-4000-8000-000000000501";
    const std::string header="kairo.logic.trace.v1 "+session+"\n";
    const std::string revision(64,'a');
    const auto row=[&](unsigned seq,unsigned pc){
        return "step "+std::to_string(seq)+" 1 "+session+" "+revision+" Tick "+std::to_string(pc)+" AddFloat 4 2 3 0 | r0=0 r4=7\n";
    };
    const auto sample=header+row(0,9)+row(1,3)+"step incomplete";
    const auto result=ParseTrace(sample,session);
    Check(result.Rows.size()==2&&result.Rows[0].PC==9&&result.Rows[1].PC==3);
    Check(result.Rows[0].Registers=="r0=0 r4=7"&&result.Rows[0].Operands=="4 2 3 0");
    Reject([&]{(void)ParseTrace(header+row(1,9),session);});
    Reject([&]{(void)ParseTrace(header+row(0,9),"00000000-0000-4000-8000-000000000502");});
    Reject([&]{(void)ParseTrace(header+"step broken\n",session);});
    Reject([&]{(void)ParseTrace(header+"limit 4096 instruction records reached\n",session);});
    Check(ParseTrace(header+row(0,1)+"error invalid host\n",session).Error=="invalid host");
    std::string full=header;
    for(unsigned i=0;i<TraceMaxRows;++i)full+=row(i,i);
    Check(ParseTrace(full+"limit 4096 instruction records reached\n",session).LimitReached);
    Reject([&]{(void)ParseTrace(full+row(4096,1),session);});
    std::mt19937 rng(2026);
    for(unsigned i=0;i<10000;++i)
    {
        const auto pc=rng();
        const auto data=header+row(0,pc);
        const auto parsed=ParseTrace(data,session);
        Check(parsed.Rows.size()==1&&parsed.Rows[0].PC==pc);
        const auto cut=header.size()+rng()%(data.size()-header.size());
        Check(ParseTrace(std::string_view(data).substr(0,cut),session).Rows.empty());
    }
    std::cout<<"10000 trace records and partial-write/rejection fixtures passed\n";
}
