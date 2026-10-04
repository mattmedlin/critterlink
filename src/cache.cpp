#include "critterlink/cache.hpp"

namespace critterlink {
namespace {
constexpr std::uint32_t valid=0x20,dirty=0x40,lrf=0x10,locked=8;
void ensure(CacheState& s) {
    if(s.instruction.empty()) s.instruction.resize(256);
    if(s.data.empty()) s.data.resize(128);
}
unsigned set_index(std::uint32_t va,bool instruction) {return (va>>6U)&(instruction?127U:63U);}
int hit(const std::vector<CacheLine>& lines,unsigned index,std::uint32_t pa) {
    int result=-1;
    for(unsigned way=0;way<2;++way) {
        const auto tag=lines[index*2+way].tag;
        if((tag&valid)!=0 && (tag&0xfffff000U)==(pa&0xfffff000U)) {
            if(result!=-1) throw std::invalid_argument("multiple matching cache ways are unsupported");
            result=static_cast<int>(way);
        }
    }
    return result;
}
void writeback(CacheLine& line,Memory& memory,unsigned index) {
    if((line.tag&(valid|dirty))!=(valid|dirty)) return;
    const auto address=(line.tag&0xfffff000U)|(index<<6U);
    if(address>Memory::ram_size-64) throw std::invalid_argument("cache writeback outside main RAM is unsupported");
    for(unsigned n=0;n<64;++n) memory.write(address+n,1,line.bytes[n]);
    line.tag&=~dirty;
}
CacheLine& fill(CacheState& s,Memory& memory,std::uint32_t va,std::uint32_t pa,bool instruction,bool force=false) {
    ensure(s);auto& lines=instruction?s.instruction:s.data;const auto index=set_index(va,instruction);
    const auto found=hit(lines,index,pa);
    if(found>=0 && !force) return lines[index*2+static_cast<unsigned>(found)];
    auto& a=lines[index*2];auto& b=lines[index*2+1];
    unsigned way=((a.tag^b.tag)&lrf)!=0?1U:0U;
    if(instruction) {
        if((a.tag&valid)==0) way=0;
        else if((b.tag&valid)==0) way=1;
    } else {
        if((a.tag&b.tag&locked)!=0) throw std::invalid_argument("both data-cache ways are locked");
        if((a.tag&locked)!=0) way=1;
        if((b.tag&locked)!=0) way=0;
    }
    auto& destination=lines[index*2+way];CacheLine replacement;
    const auto base=pa&~63U;
    if(base>=Memory::ram_size && !(base>=Memory::boot_rom_base && base<0x20000000U))
        throw std::invalid_argument("cache refill outside RAM/ROM is unsupported");
    // Stage a complete line before mutating tags or evicting dirty data.
    for(unsigned n=0;n<64;++n) replacement.bytes[n]=static_cast<std::uint8_t>(memory.read(base+n,1));
    replacement.tag=(pa&0xfffff000U)|valid|((destination.tag^lrf)&lrf);
    if(!instruction) writeback(destination,memory,index);
    destination=replacement;return destination;
}
void check_range(std::uint32_t va,unsigned count) {
    if(count==0 || count>8 || (va&63U)+count>64) throw std::invalid_argument("cache byte range crosses a line");
}
}
void validate_cache(const CacheState& s) {
    if((s.accelerated.base&127U)!=0 || s.accelerated.base>=0x20000000U)
        throw std::invalid_argument("invalid accelerated-buffer physical tag");
    if((s.config&~0x00073007U)!=0x440U ||
       ((s.config&7U)!=0 && (s.config&7U)!=2 && (s.config&7U)!=3 && (s.config&7U)!=7))
        throw std::invalid_argument("invalid cache Config snapshot");
    if((!s.instruction.empty() && s.instruction.size()!=256) || (!s.data.empty() && s.data.size()!=128))
        throw std::invalid_argument("invalid cache snapshot size");
    for(const auto& line:s.instruction) if((line.tag&~0xfffff030U)!=0) throw std::invalid_argument("invalid instruction-cache tag");
    for(const auto& line:s.data) if((line.tag&~0xfffff078U)!=0 || ((line.tag&dirty)!=0 && (line.tag&valid)==0))
        throw std::invalid_argument("invalid data-cache tag");
    for(unsigned n=0;n<s.data.size();n+=2) if((s.data[n].tag&s.data[n+1].tag&locked)!=0)
        throw std::invalid_argument("both data-cache ways locked in snapshot");
}
void write_config(CacheState& s,std::uint32_t value) {
    const auto mode=value&7U;
    if(mode!=0 && mode!=2 && mode!=3 && mode!=7) throw std::invalid_argument("reserved Config.K0 cache mode");
    s.config=0x440U|(value&0x73007U);
}
bool accelerated_enabled(const CacheState& s,unsigned mode) noexcept {
    // Config.DCE overrides the translated data cache mode, including K0.
    return mode==7 && (s.config&0x10000U)!=0;
}
std::uint64_t accelerated_read(CacheState& s,Memory& memory,std::uint32_t pa,unsigned count) {
    if(count==0 || count>8 || (pa&127U)+count>128)
        throw std::invalid_argument("accelerated byte range crosses a line");
    auto& buffer=s.accelerated;const auto base=pa&~127U;
    if(!buffer.valid || buffer.base!=base) {
        if(base>=Memory::ram_size && !(base>=Memory::boot_rom_base && base<0x20000000U))
            throw std::invalid_argument("accelerated refill outside RAM/ROM is unsupported");
        AcceleratedBuffer replacement;replacement.base=base;
        // Stage the whole refill: unsupported tails must not install partial data.
        for(unsigned n=0;n<128;++n) replacement.bytes[n]=static_cast<std::uint8_t>(memory.read(base+n,1));
        replacement.valid=true;buffer=replacement;
    }
    std::uint64_t result=0;
    for(unsigned n=0;n<count;++n) result|=std::uint64_t{buffer.bytes[(pa&127U)+n]}<<(n*8);
    return result;
}
bool cache_enabled(const CacheState& s,unsigned mode,bool instruction) noexcept {
    return (mode==0 || mode==3) && (s.config&(instruction?0x20000U:0x10000U))!=0;
}
std::uint64_t cache_read(CacheState& s,Memory& memory,std::uint32_t va,std::uint32_t pa,unsigned count,bool instruction) {
    check_range(va,count);auto& line=fill(s,memory,va,pa,instruction);std::uint64_t result=0;
    for(unsigned n=0;n<count;++n) result|=std::uint64_t{line.bytes[(va&63U)+n]}<<(n*8);
    return result;
}
void cache_write(CacheState& s,Memory& memory,std::uint32_t va,std::uint32_t pa,unsigned count,std::uint64_t value,unsigned mode) {
    check_range(va,count);
    if(pa>=Memory::ram_size || count>Memory::ram_size-pa) throw std::invalid_argument("cached stores outside main RAM are unsupported");
    ensure(s);const auto index=set_index(va,false);const auto found=hit(s.data,index,pa);
    if(mode==0 && found<0) {memory.write_partial(pa,count,value);return;}
    auto& line=fill(s,memory,va,pa,false);
    if(mode==0 && (line.tag&locked)==0) memory.write_partial(pa,count,value);
    for(unsigned n=0;n<count;++n) line.bytes[(va&63U)+n]=static_cast<std::uint8_t>(value>>(n*8));
    if(mode==3 && (line.tag&locked)==0) line.tag|=dirty;
}
bool cache_index_operation(unsigned op) noexcept {
    return op==0 || op==4 || op==7 || op==0x10 || op==0x11 || op==0x12 || op==0x13 || op==0x14 || op==0x16;
}
std::optional<bool> cache_operation(CacheState& s,Memory& memory,unsigned op,std::uint32_t va,std::uint32_t pa) {
    // Instruction data/steering and BTAC operations remain explicit unsupported paths.
    if(!cache_index_operation(op) && op!=0xb && op!=0xe && op!=0x18 && op!=0x1a && op!=0x1c)
        throw std::invalid_argument("unsupported CACHE operation");
    ensure(s);const bool instruction=op<0x10;auto& lines=instruction?s.instruction:s.data;
    const auto index=set_index(va,instruction);auto& selected=lines[index*2+(va&1U)];
    if(op==0 || op==0x10) {s.tag_lo=selected.tag;return {};}
    if(op==4 || op==0x12) {
        auto tag=s.tag_lo&(instruction?0xfffff030U:0xfffff078U);
        if(!instruction) {
            if((tag&valid)==0) tag&=~dirty;
            if((tag&locked)!=0 && ((selected.tag&locked)!=0 || (lines[index*2+((va&1U)^1U)].tag&locked)!=0))
                throw std::invalid_argument("cache re-lock or both-way locking is undefined");
        }
        selected.tag=tag;return {};
    }
    if(op==7) {selected.tag&=~valid;return {};}
    if(op==0x14) writeback(selected,memory,index);
    if(op==0x14 || op==0x16) {selected.tag&=~(valid|dirty|locked);return {};}
    if(op==0x11 || op==0x13) {
        const auto offset=va&0x3cU;
        if(op==0x11) {
            s.tag_lo=0;for(unsigned n=0;n<4;++n) s.tag_lo|=std::uint32_t{selected.bytes[offset+n]}<<(8*n);
        } else for(unsigned n=0;n<4;++n) selected.bytes[offset+n]=static_cast<std::uint8_t>(s.tag_lo>>(8*n));
        return {};
    }
    if(op==0xe) {fill(s,memory,va,pa,true,true);return {};}
    const bool report=op==0x18 || op==0x1a;
    const auto found=hit(lines,index,pa);if(found<0) return report?std::optional<bool>{false}:std::nullopt;
    auto& line=lines[index*2+static_cast<unsigned>(found)];
    if(op==0x18 || op==0x1c) writeback(line,memory,index);
    if(op==0xb) line.tag&=~valid;
    else if(op==0x18 || op==0x1a) line.tag&=~(valid|dirty|locked);
    return report?std::optional<bool>{true}:std::nullopt;
}
} // namespace critterlink
