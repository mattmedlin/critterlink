#include "critterlink/iop.hpp"

#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <utility>

namespace critterlink {
namespace {
struct IopException {unsigned code;std::optional<std::uint32_t> address;unsigned coprocessor{};};
std::int64_t signed_word(std::uint32_t value) {return (value&0x80000000U)!=0?std::int64_t{value}-0x100000000LL:value;}
std::uint32_t arithmetic_right(std::uint32_t value,unsigned shift) {
    return shift==0?value:(value>>shift)|((value&0x80000000U)!=0?(~0U<<(32U-shift)):0U);
}
std::uint32_t physical(std::uint32_t address) {
    if (address >= 0x80000000U && address < 0xc0000000U) return address & 0x1fffffffU;
    return address;
}
std::uint32_t aligned(std::uint32_t address, unsigned width = 4) {
    if ((address & (width - 1U)) != 0) throw std::invalid_argument("misaligned IOP data access");
    return physical(address);
}
std::uint32_t offset(std::uint32_t instruction) {
    const auto immediate = instruction & 0xffffU;
    return (immediate & 0x8000U) ? immediate | 0xffff0000U : immediate;
}
}

std::uint32_t IopBus::fetch32(std::uint32_t) {throw std::invalid_argument("unsupported IOP instruction fetch");}
std::uint8_t IopBus::read8(std::uint32_t) {
    throw std::invalid_argument("unsupported IOP byte MMIO read");
}
void IopBus::write8(std::uint32_t, std::uint8_t) {
    throw std::invalid_argument("unsupported IOP byte MMIO write");
}
std::uint8_t Iop::read8(std::uint32_t address) const {
    const auto p = physical(address);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM read");
    return state_.ram[p];
}
void Iop::write8(std::uint32_t address, std::uint8_t value) {
    const auto p = physical(address);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM write");
    state_.ram[p] = value;
}
std::uint16_t IopBus::read16(std::uint32_t) {
    throw std::invalid_argument("unsupported IOP halfword MMIO read");
}
void IopBus::write16(std::uint32_t, std::uint16_t) {
    throw std::invalid_argument("unsupported IOP halfword MMIO write");
}
std::uint16_t Iop::read16(std::uint32_t address) const {
    const auto p = aligned(address, 2);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM read");
    return static_cast<std::uint16_t>(state_.ram[p] | (std::uint32_t{state_.ram[p + 1]} << 8U));
}
void Iop::write16(std::uint32_t address, std::uint16_t value) {
    const auto p = aligned(address, 2);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM write");
    state_.ram[p] = static_cast<std::uint8_t>(value);
    state_.ram[p + 1] = static_cast<std::uint8_t>(value >> 8U);
}
const IopState& Iop::state() const noexcept { return state_; }
std::span<std::uint8_t> Iop::mutable_ram() noexcept { return state_.ram; }
void Iop::restore(IopState state) {
    if (state.ram.size() != ram_size || state.gpr[0] != 0 ||
        (!state.architectural_exceptions && ((state.pc & 3U) != 0 || (state.next_pc & 3U) != 0)) ||
        (state.cop0.status&~0x1040ff3fU)!=0 ||
        (!state.delay_slot && (state.next_pc != state.pc + 4U || state.branch_pc!=0)) ||
        (state.delay_slot && (state.branch_pc+4U!=state.pc || (state.branch_pc&3U)!=0)) ||
        (state.pending_load && (state.pending_load->reg == 0 || state.pending_load->reg >= 32)) ||
        (state.stop && state.stop->empty()) ||
        (!state.enabled && (state.stop || state.pending_load || state.delay_slot))) {
        throw std::invalid_argument("invalid IOP snapshot");
    }
    state_ = std::move(state);
}
void Iop::start(std::uint32_t entry) {
    if (aligned(entry) >= ram_size) throw std::invalid_argument("IOP entry is outside RAM");
    state_.gpr = {};state_.hi=0;state_.lo=0;state_.branch_pc=0;state_.cop0={};state_.architectural_exceptions=false;
    state_.pc = entry;
    state_.next_pc = entry + 4U;
    state_.delay_slot = false;
    state_.pending_load.reset();
    state_.stop.reset();
    state_.enabled = true;
}
void Iop::reset_boot_vector() {
    start();state_.pc=0xbfc00000U;state_.next_pc=state_.pc+4;
    state_.cop0.status=0x00400000U;state_.architectural_exceptions=true;
}
void Iop::set_interrupt_line(bool pending) noexcept {
    state_.cop0.cause=(state_.cop0.cause&~0x400U)|(pending?0x400U:0U);
}
std::uint32_t Iop::read32(std::uint32_t address) const {
    const auto p = aligned(address);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM read");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{state_.ram[p + i]} << (i * 8U);
    return value;
}
void Iop::write32(std::uint32_t address, std::uint32_t value) {
    const auto p = aligned(address);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM write");
    for (unsigned i = 0; i < 4; ++i) state_.ram[p + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}

bool Iop::step(IopBus* bus) {
    if (!state_.enabled || state_.stop) return false;
    const auto enter_exception=[&](const IopException& fault) {
        // The older load completes even when the following instruction faults.
        if(state_.pending_load) state_.gpr[state_.pending_load->reg]=state_.pending_load->value;
        state_.pending_load.reset();
        auto& c=state_.cop0;c.epc=state_.delay_slot?state_.branch_pc:state_.pc;
        c.cause=(c.cause&~0xb000007cU)|(fault.code<<2U)|(state_.delay_slot?0x80000000U:0U)|(fault.coprocessor<<28U);
        if(fault.address)c.bad_vaddr=*fault.address;
        c.status=(c.status&~0x3fU)|((c.status<<2U)&0x3fU);
        state_.pc=(c.status&0x400000U)!=0?0xbfc00180U:0x80000080U;
        state_.next_pc=state_.pc+4;state_.delay_slot=false;state_.branch_pc=0;
    };
    if(state_.architectural_exceptions && (state_.cop0.status&1U)!=0 && (state_.cop0.status&state_.cop0.cause&0xff00U)!=0) {
        enter_exception({0,{}});return false;
    }
    std::optional<std::uint32_t> opcode;
    const auto diagnostic=[&](const std::string& reason) {
        std::ostringstream text;text<<"pc=0x"<<std::hex<<std::setfill('0')<<std::setw(8)<<state_.pc;
        if(opcode)text<<" opcode=0x"<<std::setw(8)<<*opcode;else text<<" fetch";
        if(state_.delay_slot)text<<" delay-slot";
        text<<": "<<reason;state_.stop=text.str();
    };
    try {
        const auto address=[&](std::uint32_t va,unsigned width,bool store=false) {
            if((va&(width-1U))!=0 || (state_.architectural_exceptions && (state_.cop0.status&2U)!=0 && (va&0x80000000U)!=0))
                throw IopException{store?5U:4U,va};
            return physical(va);
        };
        const auto fetch=address(state_.pc,4);
        if(fetch>=ram_size && !(bus && fetch>=0x1fc00000U && fetch<0x20000000U))
            throw std::invalid_argument("unsupported IOP instruction fetch outside RAM/ROM");
        const auto instruction=fetch<ram_size?read32(fetch):bus->fetch32(fetch);opcode=instruction;
        const auto op=instruction>>26U,rs=(instruction>>21U)&31U,rt=(instruction>>16U)&31U;
        const auto rd=(instruction>>11U)&31U,shift=(instruction>>6U)&31U,fn=instruction&63U;
        const auto left=state_.gpr[rs],right=state_.gpr[rt];
        std::optional<IopLoad> write,load;bool branch=false;
        auto following=state_.next_pc+4U,hi=state_.hi,lo=state_.lo;auto cop0=state_.cop0;
        const auto set_branch=[&](std::uint32_t destination) {
            if(state_.delay_slot)throw std::invalid_argument("IOP branch in delay slot unsupported");
            branch=true;following=destination;
        };
        const auto conditional=[&](bool taken) {set_branch(taken?state_.pc+4U+(offset(instruction)<<2U):state_.pc+8U);};
        const auto checked_add=[&](std::uint32_t a,std::uint32_t b,bool subtract) {
            const auto result=signed_word(a)+(subtract?-signed_word(b):signed_word(b));
            if(result<-2147483648LL || result>2147483647LL)throw IopException{12,{}};
            return static_cast<std::uint32_t>(result);
        };
        const auto delayed=[&](std::uint32_t value,bool merge=false) {
            if(!merge && state_.pending_load && state_.pending_load->reg==rt)
                throw std::invalid_argument("overlapping IOP loads to the same register unsupported");
            if(rt!=0)load=IopLoad{rt,value};
        };
        switch(op) {
        case 0:
            switch(fn) {
            case 0:case 2:case 3:
                if(rs!=0)throw std::invalid_argument("reserved IOP shift field");
                write=IopLoad{rd,fn==0?right<<shift:fn==2?right>>shift:arithmetic_right(right,shift)};break;
            case 4:case 6:case 7:
                if(shift!=0)throw std::invalid_argument("reserved IOP variable-shift field");
                write=IopLoad{rd,fn==4?right<<(left&31U):fn==6?right>>(left&31U):arithmetic_right(right,left&31U)};break;
            case 8:case 9:
                if(rt!=0 || shift!=0 || (fn==8 && rd!=0) || (fn==9 && rd!=0 && rd==rs))
                    throw std::invalid_argument("unsupported IOP jump encoding or link overlap");
                if(!state_.architectural_exceptions && (left&3U)!=0)throw std::invalid_argument("misaligned IOP jump target");
                set_branch(left);if(fn==9)write=IopLoad{rd,state_.pc+8U};break;
            case 12:case 13:throw IopException{fn==12?8U:9U,{}};
            case 16:case 18:
                if(rs!=0 || rt!=0 || shift!=0)throw std::invalid_argument("reserved IOP HI/LO read field");
                write=IopLoad{rd,fn==16?hi:lo};break;
            case 17:case 19:
                if(rt!=0 || rd!=0 || shift!=0)throw std::invalid_argument("reserved IOP HI/LO write field");
                if(fn==17)hi=left;else lo=left;break;
            case 24:case 25:case 26:case 27: {
                if(rd!=0 || shift!=0)throw std::invalid_argument("reserved IOP multiply/divide field");
                if(fn<26) {
                    const auto product=fn==24?static_cast<std::uint64_t>(signed_word(left)*signed_word(right)):std::uint64_t{left}*right;
                    lo=static_cast<std::uint32_t>(product);hi=static_cast<std::uint32_t>(product>>32U);
                } else {
                    if(right==0)throw std::invalid_argument("IOP divide-by-zero hardware result unverified");
                    if(fn==26) {lo=static_cast<std::uint32_t>(signed_word(left)/signed_word(right));hi=static_cast<std::uint32_t>(signed_word(left)%signed_word(right));}
                    else {lo=left/right;hi=left%right;}
                }
                break;
            }
            case 32:case 33:case 34:case 35:case 36:case 37:case 38:case 39:case 42:case 43: {
                if(shift!=0)throw std::invalid_argument("reserved IOP ALU field");
                std::uint32_t value=0;
                if(fn==32 || fn==34)value=checked_add(left,right,fn==34);
                else if(fn==33)value=left+right;else if(fn==35)value=left-right;
                else if(fn==36)value=left&right;else if(fn==37)value=left|right;
                else if(fn==38)value=left^right;else if(fn==39)value=~(left|right);
                else if(fn==42)value=signed_word(left)<signed_word(right);else value=left<right;
                write=IopLoad{rd,value};break;
            }
            default:throw std::invalid_argument("unsupported IOP SPECIAL instruction");
            }
            break;
        case 1:
            if(rt!=0 && rt!=1 && rt!=16 && rt!=17)throw std::invalid_argument("unsupported IOP REGIMM instruction");
            if(rt>=16 && rs==31)throw std::invalid_argument("IOP conditional link/source overlap is undefined");
            conditional((rt&1U)!=0?signed_word(left)>=0:signed_word(left)<0);
            if(rt>=16)write=IopLoad{31,state_.pc+8U};break;
        case 2:case 3:
            set_branch(((state_.pc+4U)&0xf0000000U)|((instruction&0x03ffffffU)<<2U));
            if(op==3)write=IopLoad{31,state_.pc+8U};break;
        case 4:case 5:conditional(op==4?left==right:left!=right);break;
        case 6:case 7:
            if(rt!=0)throw std::invalid_argument("reserved IOP zero-compare branch field");
            conditional(op==6?signed_word(left)<=0:signed_word(left)>0);break;
        case 8:write=IopLoad{rt,checked_add(left,offset(instruction),false)};break;
        case 9:write=IopLoad{rt,left+offset(instruction)};break;
        case 10:write=IopLoad{rt,static_cast<std::uint32_t>(signed_word(left)<signed_word(offset(instruction)))};break;
        case 11:write=IopLoad{rt,static_cast<std::uint32_t>(left<offset(instruction))};break;
        case 12:write=IopLoad{rt,left&(instruction&0xffffU)};break;
        case 13:write=IopLoad{rt,left|(instruction&0xffffU)};break;
        case 14:write=IopLoad{rt,left^(instruction&0xffffU)};break;
        case 15:
            if(rs!=0)throw std::invalid_argument("unsupported IOP LUI encoding");
            write=IopLoad{rt,instruction<<16U};break;
        case 16:
            if((cop0.status&2U)!=0 && (cop0.status&0x10000000U)==0)throw IopException{11,{},0};
            if(instruction==0x42000010U) {cop0.status=(cop0.status&~0xfU)|((cop0.status>>2U)&0xfU);break;}
            if((rs!=0 && rs!=4) || (instruction&0x7ffU)!=0)throw std::invalid_argument("unsupported IOP COP0 encoding");
            if(rs==0) {
                if(rd==8)delayed(cop0.bad_vaddr);else if(rd==12)delayed(cop0.status);
                else if(rd==13)delayed(cop0.cause);else if(rd==14)delayed(cop0.epc);
                else throw std::invalid_argument("unsupported IOP COP0 register");
            } else {
                if(rd==12) {
                    if((right&~0x1040ff3fU)!=0)throw std::invalid_argument("unsupported IOP Status mode");
                    cop0.status=right;
                } else if(rd==13)cop0.cause=(cop0.cause&~0x300U)|(right&0x300U);
                else if(rd==14)cop0.epc=right;
                else throw std::invalid_argument("unsupported IOP COP0 register write");
            }
            break;
        case 17:case 18:case 19:
            if((cop0.status&(1U<<(28U+op-16U)))==0)throw IopException{11,{},op-16U};
            throw std::invalid_argument("unsupported IOP coprocessor");
        case 32:case 33:case 35:case 36:case 37: {
            // Check overlap before any peripheral side effect.
            if(state_.pending_load && state_.pending_load->reg==rt)throw std::invalid_argument("overlapping IOP loads to the same register unsupported");
            const bool byte=op==32 || op==36;const auto p=address(left+offset(instruction),op==35?4:byte?1:2);
            if(p>=ram_size && !bus)throw std::invalid_argument("unsupported IOP MMIO read");
            auto value=byte?std::uint32_t{p<ram_size?read8(p):bus->read8(p)}:op==35?(p<ram_size?read32(p):bus->read32(p)):
                std::uint32_t{p<ram_size?read16(p):bus->read16(p)};
            if(op==32 && (value&0x80U)!=0)value|=0xffffff00U;
            if(op==33 && (value&0x8000U)!=0)value|=0xffff0000U;
            delayed(value);break;
        }
        case 34:case 38:case 42:case 46: {
            const bool store=op>=40,left_merge=op==34 || op==42;
            const auto va=left+offset(instruction),p=address(va,1,store),base=p&~3U,byte=p&3U;
            if(base>=ram_size && !(bus && !store && base>=0x1fc00000U && base<0x20000000U))
                throw std::invalid_argument("IOP merge access outside RAM/ROM is unsupported");
            if(store) {
                const auto first=left_merge?0U:byte,count=left_merge?byte+1:4-byte;
                const auto value=left_merge?right>>((3-byte)*8):right;
                for(unsigned n=0;n<count;++n)write8(base+first+n,static_cast<std::uint8_t>(value>>(8*n)));
            } else {
                const auto data=base<ram_size?read32(base):bus->read32(base);
                const auto old=state_.pending_load && state_.pending_load->reg==rt?state_.pending_load->value:right;
                const auto amount=(left_merge?3-byte:byte)*8;
                const auto mask=left_merge?(~0U<<amount):(~0U>>amount);
                delayed((old&~mask)|((left_merge?data<<amount:data>>amount)&mask),true);
            }
            break;
        }
        case 40:case 41:case 43: {
            const auto p=address(left+offset(instruction),op==40?1:op==41?2:4,true);
            if(p>=ram_size && !bus)throw std::invalid_argument("unsupported IOP MMIO write");
            if(op==40) {if(p<ram_size)write8(p,static_cast<std::uint8_t>(right));else bus->write8(p,static_cast<std::uint8_t>(right));}
            else if(op==41) {if(p<ram_size)write16(p,static_cast<std::uint16_t>(right));else bus->write16(p,static_cast<std::uint16_t>(right));}
            else {if(p<ram_size)write32(p,right);else bus->write32(p,right);}
            break;
        }
        default:throw std::invalid_argument("unsupported IOP instruction");
        }
        if(state_.pending_load)state_.gpr[state_.pending_load->reg]=state_.pending_load->value;
        if(write && write->reg!=0)state_.gpr[write->reg]=write->value;
        state_.pending_load=load;state_.hi=hi;state_.lo=lo;state_.cop0=cop0;
        state_.branch_pc=branch?state_.pc:0;state_.pc=state_.next_pc;state_.next_pc=following;
        state_.delay_slot=branch;state_.gpr[0]=0;return true;
    } catch(const IopException& fault) {
        if(state_.architectural_exceptions)enter_exception(fault);
        else diagnostic("IOP diagnostic profile does not dispatch exception "+std::to_string(fault.code));
        return false;
    } catch(const std::exception& error) {diagnostic(error.what());return false;}
}
} // namespace critterlink
