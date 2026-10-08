#include "critterlink/vector.hpp"
#include <iostream>
#include <stdexcept>
using namespace critterlink;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("vector check failed"); }
template<class F> void rejects(F fn) { bool caught=false; try { fn(); } catch(const std::invalid_argument&) {caught=true;} require(caught); }
void transfers() {
    VectorUnit vu;
    require(vu.submit_word(0x01000101));
    require(vu.submit_word(0x6c0103ff));
    require(vu.submit_word(7));
    const auto partial=vu.state();
    require(vu.submit_word(9)); require(vu.submit_word(11)); require(vu.submit_word(13));
    const auto complete=vu.state();
    vu.restore(partial);
    require(vu.submit_word(9)); require(vu.submit_word(11)); require(vu.submit_word(13));
    require(vu.state()==complete);
    require(vu.state().data[1023]==std::array<std::uint32_t,4>{7,9,11,13});
    for (auto bad : {0x01000201U,0xec010000U,0x6c028000U,0x6c0203ffU,0x4a0207ffU,0x17000000U,0x14000800U}) {
        rejects([&]{vu.submit_word(bad);}); require(vu.state()==complete);
    }
    auto bad=complete; bad.vi[0]=1; rejects([&]{vu.restore(bad);}); require(vu.state()==complete);
    bad=partial; bad.payload_lane=4; rejects([&]{vu.restore(bad);}); require(vu.state()==complete);
    bad=partial; bad.payload_remaining=4; rejects([&]{vu.restore(bad);}); require(vu.state()==complete);
    bad=complete; bad.end_pending=true; rejects([&]{vu.restore(bad);});
    bad=complete; bad.vf[0][3]=0; rejects([&]{vu.restore(bad);});
}
void execution() {
    VectorUnit vu;
    // Independently written real words: IADDIU VI1,VI0,1; LQ VF1,0(VI0);
    // four neutral pairs; SQ VF1,0(VI1); NOP[E]; one integer neutral delay.
    constexpr std::array<std::uint32_t,9> lower{0x10010001,0x01e10000,0x10000000,0x10000000,0x10000000,0x10000000,0x03e10800,0x10000000,0x10000000};
    require(vu.submit_word(0x4a090000));
    require(vu.submit_word(lower[0]));
    const auto upload=vu.state();
    require(vu.submit_word(0x2ff));
    vu.restore(upload);
    require(vu.submit_word(0x2ff));
    for (unsigned i=1;i<lower.size();++i) { require(vu.submit_word(lower[i])); require(vu.submit_word(i==7?0x400002ffU:0x2ffU)); }
    require(vu.submit_word(0x6c010000));
    for(auto word:{7U,9U,11U,13U}) require(vu.submit_word(word));
    require(vu.submit_word(0x14000000));
    const auto start=vu.state();
    require(!vu.submit_word(0x10000000)); require(!vu.submit_word(0x4a010000)); require(!vu.submit_word(0x14000000)); require(vu.state()==start);
    for(unsigned i=0;i<8;++i) vu.tick();
    require(vu.busy()&&vu.state().end_pending);
    const auto end=vu.state();
    vu.tick(); require(!vu.busy());
    const auto result=vu.state(); vu.restore(end); vu.tick(); require(vu.state()==result);
    require(vu.state().data[1]==std::array<std::uint32_t,4>{7,9,11,13});
    require(vu.state().vi[1]==1); require(vu.state().vi[0]==0);
    require(vu.submit_word(0x10000000));
    auto invalid=start; invalid.micro[0][1]=0x800002ff; vu.restore(invalid);
    rejects([&]{vu.tick();}); require(vu.state()==invalid);
    invalid=start; invalid.micro[0][0]=0x12000000; vu.restore(invalid);
    rejects([&]{vu.tick();}); require(vu.state()==invalid);
    invalid=start; invalid.micro[0][0]=0x01e103ff; invalid.vi[0]=0; // legal max qword
    vu.restore(invalid); vu.tick();
    invalid=start; invalid.micro[0][0]=0x01e107ff; vu.restore(invalid);
    rejects([&]{vu.tick();}); require(vu.state()==invalid);
}
void arithmetic_masks() {
    VectorUnit vu;
    auto initial=vu.state();
    initial.running=true;
    initial.vi[1]=65535;
    // IADDIU VI2,VI1,0x7fff: upper immediate bits occupy dest.
    initial.micro[0]={0x11e20fff,0x2ff};
    initial.micro[1]={0x01030000,0x2ff}; // LQ.x VF3,0(VI0)
    initial.micro[2]={0x02201801,0x2ff}; // SQ.w VF3,1(VI0)
    initial.data[0]={7,9,11,13};
    initial.vf[3]={1,2,3,4};
    vu.restore(initial); vu.tick();
    require(vu.state().vi[2]==32766);
    vu.tick(); require(vu.state().vf[3]==std::array<std::uint32_t,4>{7,2,3,4});
    // All other destination lanes retain zero.
    vu.tick(); require(vu.state().data[1]==std::array<std::uint32_t,4>{0,0,0,4});
    auto negative=vu.state(); negative.vi[1]=1;
    negative.micro[3]={0x01e10fff,0x2ff}; // LQ.xyzw VF1,-1(VI1), valid address zero.
    vu.restore(negative); vu.tick();
    require(vu.state().vf[1]==std::array<std::uint32_t,4>{7,9,11,13});
}
void zero_count() {
    VectorUnit vu; require(vu.submit_word(0x6c000000));
    for (unsigned i=0;i<1024;++i) require(vu.submit_word(i));
    require(vu.state().payload==0 && vu.state().data[255][3]==1023);
    require(vu.submit_word(0x4a000000));
    for (unsigned i=0;i<512;++i) require(vu.submit_word(i));
    require(vu.state().payload==0 && vu.state().micro[255][1]==511);
}
}
int main() { try { transfers(); execution(); arithmetic_masks(); zero_count(); } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
