#include "critterlink/dma_chain.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using critterlink::SourceChainPacket;
using critterlink::SourceChainStack;
using critterlink::decode_source_chain;
using Tag = std::array<std::uint32_t, 4>;
void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejects(F fn) {
    try { fn(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected malformed source-chain rejection");
}
void all_ids() {
    // Literal manufacturer ID encodings. Deliberately nonzero upper64 proves
    // those bytes are tag payload, not decoded DMA control.
    constexpr std::array<Tag, 8> tags{{
        {0x00000003, 0x4000, 0xfedcba98, 0x76543210},
        {0x10000003, 0x4000, 0xfedcba98, 0x76543210},
        {0x20000003, 0x4000, 0xfedcba98, 0x76543210},
        {0x30000003, 0x4000, 0xfedcba98, 0x76543210},
        {0x40000003, 0x4000, 0xfedcba98, 0x76543210},
        {0x50000003, 0x4000, 0xfedcba98, 0x76543210},
        {0x60000003, 0x4000, 0xfedcba98, 0x76543210},
        {0x70000003, 0x4000, 0xfedcba98, 0x76543210}
    }};
    constexpr std::array<std::uint32_t, 8> addresses{
        0x4000, 0x1010, 0x1010, 0x4000, 0x4000, 0x1010, 0x1010, 0x1010};
    constexpr std::array<std::uint32_t, 8> following{
        0x1000, 0x1040, 0x4000, 0x1010, 0x1010, 0x4000, 0x6000, 0x1000};
    const SourceChainStack input{{0x6000, 0x7000}, 1};
    const auto saved = input;
    for (unsigned id = 0; id < tags.size(); ++id) {
        const auto raw = tags[id];
        const auto packet = decode_source_chain(0x1000, raw, input);
        check(packet.id == id && packet.tag == id * 0x1000 && packet.qwords == 3,
              "literal ID, TAG or QWC decoded incorrectly");
        check(packet.address == addresses[id] && packet.next_tag == following[id],
              "packet source or next tag differs from source-chain table");
        check(packet.end == (id == 0 || id == 7) && !packet.stack_overflow,
              "wrong termination for valid source tag");
        auto expected_stack = input;
        if (id == 5) { expected_stack.addresses[1] = 0x1040; expected_stack.depth = 2; }
        if (id == 6) { expected_stack.depth = 0; }
        check(packet.stack == expected_stack, "unexpected stack side effect");
        check(raw == tags[id] && input == saved, "decoder mutated caller inputs");
        auto irq_raw = raw;
        irq_raw[0] |= 0x80000000;
        const auto irq_packet = decode_source_chain(0x1000, irq_raw, input);
        auto expected_irq = packet;
        expected_irq.tag = static_cast<std::uint16_t>(packet.tag | 0x8000);
        check(irq_packet == expected_irq, "IRQ retention changes packet planning or stack");
    }
}
void stack_and_endings() {
    const SourceChainStack empty{{0x1110, 0x2220}, 0};
    const auto first = decode_source_chain(0x1000, Tag{0x50000002, 0x4000, 0, 0}, empty);
    check(first.stack == SourceChainStack{{0x1030, 0x2220}, 1}, "first CALL return address");
    const auto second = decode_source_chain(0x4000, Tag{0x50000001, 0x8000, 0, 0}, first.stack);
    check(second.stack == SourceChainStack{{0x1030, 0x4020}, 2}, "second CALL return address");
    const auto overflow = decode_source_chain(0x8000, Tag{0xd0000007, 0xc000, 0, 0}, second.stack);
    check(overflow.end && overflow.stack_overflow && overflow.qwords == 0 && overflow.tag == 0xd000 &&
          overflow.address == 0x8000 && overflow.next_tag == 0x8000 && overflow.stack == second.stack,
          "third CALL must preserve offending tag and stack without payload");
    const auto ret2 = decode_source_chain(0x9000, Tag{0x60000004, 0, 0, 0}, second.stack);
    check(!ret2.end && ret2.address == 0x9010 && ret2.qwords == 4 && ret2.next_tag == 0x4020 &&
          ret2.stack == SourceChainStack{{0x1030, 0x4020}, 1}, "RET pops second address");
    const auto ret1 = decode_source_chain(0xa000, Tag{0x60000000, 0, 0, 0}, ret2.stack);
    check(!ret1.end && ret1.next_tag == 0x1030 &&
          ret1.stack == SourceChainStack{{0x1030, 0x4020}, 0}, "RET pops first address without clearing saved storage");
    const auto underflow = decode_source_chain(0xb000, Tag{0xe0000003, 0, 0, 0}, ret1.stack);
    check(underflow.end && !underflow.stack_overflow && underflow.qwords == 3 && underflow.address == 0xb010 &&
          underflow.next_tag == 0xb000 && underflow.tag == 0xe000 && underflow.stack == ret1.stack,
          "empty RET must retain and transfer its payload before termination");
    // Terminal next_tag=current tag is the diagnostic planner policy, not a
    // claim about terminal TADR visibility measured on a console.
    const auto end = decode_source_chain(0xc000, Tag{0x70000000, 0, 0, 0}, empty);
    check(end.end && end.qwords == 0 && end.next_tag == 0xc000, "zero-sized END");
    const auto cnt = decode_source_chain(0xc000, Tag{0x10000000, 0, 0, 0}, empty);
    check(!cnt.end && cnt.qwords == 0 && cnt.address == 0xc010 && cnt.next_tag == 0xc010, "zero-sized CNT advances one tag");
    const auto next = decode_source_chain(0xc000, Tag{0x20000000, 0xc000, 0, 0}, empty);
    check(!next.end && next.next_tag == 0xc000, "zero-sized NEXT self-loop remains a single bounded plan");
    const auto maximum = decode_source_chain(0x1000, Tag{0x1000ffff, 0, 0, 0}, empty);
    check(maximum.qwords == 65535 && maximum.next_tag == 0x101000, "full-width QWC boundary");
}
void address_limits() {
    const SourceChainStack empty{};
    const auto last_ref = decode_source_chain(0x7ffffff0, Tag{0x00000001, 0x7ffffff0, 0, 0}, empty);
    check(last_ref.address == 0x7ffffff0 && last_ref.qwords == 1 && last_ref.end,
          "terminal referenced packet may use last representable qword");
    const auto last_end = decode_source_chain(0x7fffffe0, Tag{0x70000001, 0, 0, 0}, empty);
    check(last_end.address == 0x7ffffff0 && last_end.qwords == 1 && last_end.end,
          "terminal inline packet need not represent unused one-past address");
    const SourceChainStack full{{0x1000, 0x2000}, 2};
    const auto overflow = decode_source_chain(0x7ffffff0, Tag{0x5000ffff, 0, 0, 0}, full);
    check(overflow.stack_overflow && overflow.qwords == 0 && overflow.stack == full,
          "overflow CALL must not derive or validate untransferred payload extent");
    rejects([&] { (void)decode_source_chain(0x1000, Tag{0x00000002, 0x7ffffff0, 0, 0}, empty); });
    rejects([&] { (void)decode_source_chain(0x1000, Tag{0x30000002, 0x7ffffff0, 0, 0}, empty); });
    rejects([&] { (void)decode_source_chain(0x1000, Tag{0x40000002, 0x7ffffff0, 0, 0}, empty); });
    rejects([&] { (void)decode_source_chain(0x7fffffe0, Tag{0x70000002, 0, 0, 0}, empty); });
    rejects([&] { (void)decode_source_chain(0x7fffffe0, Tag{0x20000002, 0, 0, 0}, empty); });
    rejects([&] { (void)decode_source_chain(0x7fffffe0, Tag{0x60000002, 0, 0, 0}, empty); });
}
void invalid_inputs() {
    const SourceChainStack original{{0x6000, 0x7000}, 1};
    constexpr Tag valid{0x50000003, 0x4000, 0, 0};
    for (unsigned bit = 16; bit < 28; ++bit) {
        auto raw = valid;
        raw[0] |= std::uint32_t{1} << bit;
        const auto saved = raw;
        rejects([&] { (void)decode_source_chain(0x1000, raw, original); });
        check(raw == saved && original == SourceChainStack{{0x6000, 0x7000}, 1},
              "rejected control mutated caller inputs");
    }
    auto pce3 = valid; pce3[0] |= 0x0c000000;
    rejects([&] { (void)decode_source_chain(0x1000, pce3, original); });
    for (std::uint32_t bad_address : {0x1001u, 0x1008u, 0x80001000u}) {
        rejects([&] { (void)decode_source_chain(bad_address, valid, original); });
        auto raw = valid; raw[1] = bad_address;
        rejects([&] { (void)decode_source_chain(0x1000, raw, original); });
        auto stack = original; stack.addresses[0] = bad_address;
        rejects([&] { (void)decode_source_chain(0x1000, valid, stack); });
        stack = original; stack.addresses[1] = bad_address;
        rejects([&] { (void)decode_source_chain(0x1000, valid, stack); });
    }
    auto invalid_depth = original; invalid_depth.depth = 3;
    rejects([&] { (void)decode_source_chain(0x1000, valid, invalid_depth); });
    rejects([&] { (void)decode_source_chain(0x7ffffff0, Tag{0x10000000, 0, 0, 0}, original); });
    rejects([&] { (void)decode_source_chain(0x7fffffd0, Tag{0x10000003, 0, 0, 0}, original); });
    rejects([&] { (void)decode_source_chain(0x7fffffd0, Tag{0x50000003, 0x4000, 0, 0}, original); });
    check(original == SourceChainStack{{0x6000, 0x7000}, 1}, "range rejection changed stack");
}
} // namespace
int main() {
    try { all_ids(); stack_and_endings(); address_limits(); invalid_inputs(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Source-chain packet planner tests passed\n";
}
