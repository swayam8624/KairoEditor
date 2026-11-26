#pragma once
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kairo::editor::detail
{
    inline constexpr std::size_t TraceMaxRows=4096;
    inline constexpr std::size_t TraceMaxBytes=4u*1024u*1024u;
    struct TraceRow
    {
        std::uint32_t Sequence=0,Entity=0,PC=0;
        std::string Document,Revision,Event,Opcode,Operands,Registers;
    };
    struct TraceCapture
    {
        std::vector<TraceRow> Rows;
        bool LimitReached=false;
        std::string Error;
    };

    inline std::string_view TraceToken(std::string_view& line)
    {
        const auto end=line.find(' ');
        const auto token=line.substr(0,end);
        line=end==std::string_view::npos?std::string_view{}:line.substr(end+1);
        if(token.empty())throw std::invalid_argument("Missing trace token.");
        return token;
    }
    inline std::uint32_t TraceNumber(std::string_view token)
    {
        std::uint32_t value=0;
        const auto result=std::from_chars(token.data(),token.data()+token.size(),value);
        if(result.ec!=std::errc{}||result.ptr!=token.data()+token.size())
            throw std::invalid_argument("Invalid trace number.");
        return value;
    }
    inline bool TraceHex(std::string_view value,std::size_t length)
    {
        if(value.size()!=length)return false;
        for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
        return true;
    }
    inline bool TraceUUID(std::string_view value)
    {
        if(value.size()!=36)return false;
        for(std::size_t i=0;i<value.size();++i)
        {
            const char c=value[i];
            if(i==8||i==13||i==18||i==23){if(c!='-')return false;}
            else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
        }
        return true;
    }

    // A live writer can leave a partial last line. Only complete records
    // are parsed; a later refresh will see the completed line.
    inline TraceCapture ParseTrace(std::string_view bytes,std::string_view session)
    {
        if(bytes.size()>TraceMaxBytes||!TraceUUID(session))
            throw std::invalid_argument("Invalid trace size or session.");
        const auto headerEnd=bytes.find('\n');
        if(headerEnd==std::string_view::npos||
            bytes.substr(0,headerEnd)!="kairo.logic.trace.v1 "+std::string(session))
            throw std::invalid_argument("Trace header does not match this session.");
        bytes.remove_prefix(headerEnd+1);
        TraceCapture result;
        while(!bytes.empty())
        {
            const auto end=bytes.find('\n');
            if(end==std::string_view::npos)break;
            auto line=bytes.substr(0,end);
            bytes.remove_prefix(end+1);
            if(line.size()>1024)throw std::invalid_argument("Trace record exceeds size budget.");
            const auto type=TraceToken(line);
            if(type=="limit")
            {
                if(result.LimitReached||result.Rows.size()!=TraceMaxRows||line!="4096 instruction records reached")
                    throw std::invalid_argument("Invalid trace limit marker.");
                result.LimitReached=true;
                continue;
            }
            if(type=="error")
            {
                if(!result.Error.empty())throw std::invalid_argument("Duplicate trace error.");
                result.Error=std::string(line);
                continue;
            }
            if(type!="step"||result.LimitReached||!result.Error.empty()||result.Rows.size()>=TraceMaxRows)
                throw std::invalid_argument("Invalid trace record type or count.");
            TraceRow row;
            row.Sequence=TraceNumber(TraceToken(line));
            row.Entity=TraceNumber(TraceToken(line));
            row.Document=TraceToken(line);
            row.Revision=TraceToken(line);
            row.Event=TraceToken(line);
            row.PC=TraceNumber(TraceToken(line));
            row.Opcode=TraceToken(line);
            if(row.Sequence!=result.Rows.size()||!row.Entity||
                !TraceUUID(row.Document)||!TraceHex(row.Revision,64))
                throw std::invalid_argument("Invalid trace identity or sequence.");
            for(unsigned i=0;i<4;++i)
            {
                const auto token=TraceToken(line);
                (void)TraceNumber(token);
                if(i)row.Operands+=' ';
                row.Operands+=token;
            }
            if(TraceToken(line)!="|")throw std::invalid_argument("Missing trace register delimiter.");
            row.Registers=std::string(line);
            result.Rows.push_back(std::move(row));
        }
        return result;
    }
}
