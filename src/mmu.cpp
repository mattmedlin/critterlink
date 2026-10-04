#include "critterlink/mmu.hpp"

namespace critterlink {
namespace {
constexpr std::uint32_t hi_mask=0xffffe0ffU,lo_mask=0x03ffffffU;
bool scratch(const TlbEntry& e) { return (e.lo0&0x80000000U)!=0; }
std::uint32_t match_mask(const TlbEntry& e) { return scratch(e)?0x3fffU:e.mask|0x1fffU; }
bool matches(const TlbEntry& e,std::uint32_t hi) {
    return (hi&~match_mask(e)&0xffffe000U)==(e.hi&0xffffe000U) &&
           ((e.lo0&e.lo1&1U)!=0 || (hi&0xffU)==(e.hi&0xffU));
}
std::optional<unsigned> find(const MmuState& s,std::uint32_t hi) {
    std::optional<unsigned> found;
    for(unsigned i=0;i<s.entries.size();++i) if(matches(s.entries[i],hi)) {
        if(found) throw std::invalid_argument("multiple matching TLB entries have undefined hardware behavior");
        found=i;
    }
    return found;
}
void validate_entry(const TlbEntry& e) {
    if(!valid_page_mask(e.mask) || (e.hi&~hi_mask)!=0 || (e.lo0&~(lo_mask|0x80000000U))!=0 ||
       (e.lo1&~lo_mask)!=0 || (e.lo0&1U)!=(e.lo1&1U) || (e.hi&match_mask(e)&0xffffe000U)!=0)
        throw std::invalid_argument("noncanonical TLB entry");
    if(scratch(e) && (e.mask!=0 || (e.lo0&6U)!=(e.lo1&6U) || (e.lo0&0x38U)!=0x10U || (e.lo1&0x38U)!=0x10U))
        throw std::invalid_argument("invalid scratchpad TLB entry");
}
}
bool kernel_mode(std::uint32_t status) noexcept { return (status&6U)!=0 || (status&0x18U)==0; }
bool valid_page_mask(std::uint32_t mask) noexcept {
    return mask==0 || mask==0x6000 || mask==0x1e000 || mask==0x7e000 ||
           mask==0x1fe000 || mask==0x7fe000 || mask==0x1ffe000;
}
void validate_mmu(const MmuState& s) {
    if((s.index&~0x8000003fU)!=0 || s.wired>47 || s.random<s.wired || s.random>47 ||
       (s.lo0&~(lo_mask|0x80000000U))!=0 || (s.lo1&~lo_mask)!=0 || (s.context&15U)!=0 ||
       (s.mask&~0x01ffe000U)!=0 || (s.hi&~hi_mask)!=0) throw std::invalid_argument("invalid MMU snapshot registers");
    for(const auto& e:s.entries) validate_entry(e);
}
std::optional<std::uint32_t> read_mmu_register(const MmuState& s,unsigned reg) {
    switch(reg) {
    case 0:return s.index;case 1:return s.random;case 2:return s.lo0;case 3:return s.lo1;
    case 4:return s.context;case 5:return s.mask;case 6:return s.wired;case 10:return s.hi;
    default:return {};
    }
}
bool write_mmu_register(MmuState& s,unsigned reg,std::uint32_t value) {
    switch(reg) {
    case 0:s.index=value&0x8000003fU;break;
    case 2:s.lo0=value&(lo_mask|0x80000000U);break;
    case 3:s.lo1=value&lo_mask;break;
    case 4:s.context=(s.context&0x007ffff0U)|(value&0xff800000U);break;
    case 5:s.mask=value&0x01ffe000U;break;
    case 6:
        if((value&63U)>47) throw std::invalid_argument("Wired above 47 has undefined hardware behavior");
        s.wired=value&63U;s.random=47;break;
    case 10:s.hi=value&hi_mask;break;
    default:return false;
    }
    return true;
}
void tlb_read(MmuState& s) {
    const auto index=s.index&63U;
    if(index>=48) throw std::invalid_argument("TLBR index exceeds 47");
    const auto& e=s.entries[index];s.mask=e.mask;s.hi=e.hi;s.lo0=e.lo0;s.lo1=e.lo1;
}
void tlb_write(MmuState& s,unsigned index) {
    if(index>=48) throw std::invalid_argument("TLB write index exceeds 47");
    TlbEntry e{s.mask,s.hi,s.lo0,s.lo1};
    const auto global=e.lo0&e.lo1&1U;e.lo0=(e.lo0&~1U)|global;e.lo1=(e.lo1&~1U)|global;
    if(scratch(e)) {
        if(e.mask!=0 || (e.hi&0x2000U)!=0 || (e.lo0&6U)!=(e.lo1&6U))
            throw std::invalid_argument("scratchpad mapping requires zero mask, 16-KiB alignment and paired V/D bits");
        e.lo0=(e.lo0&~0x38U)|0x10U;e.lo1=(e.lo1&~0x38U)|0x10U;
    }
    e.hi&=~(match_mask(e)&0xffffe000U);
    validate_entry(e);s.entries[index]=e;
}
void tlb_probe(MmuState& s) { const auto index=find(s,s.hi);s.index=index?*index:0x80000000U; }
void advance_random(MmuState& s) noexcept { s.random=s.random==s.wired?47:s.random-1; }
Translation translate(const MmuState& s,std::uint32_t status,std::uint32_t address,Access access) {
    const auto mode=kernel_mode(status)?0U:(status>>3U)&3U;
    if((mode==2 && address>=0x80000000U) || (mode==1 && address>=0x80000000U &&
       (address<0xc0000000U || address>=0xe0000000U))) throw TranslationFault{access==Access::store?5U:4U,address};
    if(mode==0 && address>=0x80000000U && address<0xc0000000U) return {address&0x1fffffffU,false,true,false,address<0xa0000000U?3U:2U};
    if(mode==0 && (status&4U)!=0 && address<0x80000000U) return {address};
    const auto index=find(s,(address&0xffffe000U)|(s.hi&0xffU));
    if(!index) throw TranslationFault{access==Access::store?3U:2U,address,true};
    const auto& e=s.entries[*index];const bool spr=scratch(e);
    const auto page_size=((e.mask|0x1fffU)+1U)/2U;
    const auto lo=spr || (address&page_size)==0?e.lo0:e.lo1;
    if((lo&2U)==0) throw TranslationFault{access==Access::store?3U:2U,address};
    if(access==Access::store && (lo&4U)==0) throw TranslationFault{1,address};
    if(spr) {
        if(access==Access::fetch) throw std::invalid_argument("scratchpad instruction fetch is not implemented");
        return {address&0x3fffU,true,(e.lo0&1U)!=0,true};
    }
    const auto cache=(lo>>3U)&7U;
    if(cache!=2 && cache!=3 && cache!=7) throw std::invalid_argument("reserved TLB cache attribute");
    return {(((lo&0x03ffffc0U)<<6U)&~(page_size-1U)) | (address&(page_size-1U)),false,(e.lo0&1U)!=0,true,cache};
}
} // namespace critterlink
