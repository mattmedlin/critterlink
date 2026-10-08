#include "critterlink/dma_chain.hpp"

#include <stdexcept>

namespace critterlink {
namespace {
void validate_address(std::uint32_t address) {
    if ((address & 0x8000000fu) != 0) {
        throw std::invalid_argument("source-chain address requires aligned RAM without SPR");
    }
}
std::uint32_t add_address(std::uint32_t address, std::uint64_t bytes) {
    const auto result = std::uint64_t{address} + bytes;
    if (result >= 0x80000000ULL) {
        throw std::invalid_argument("source-chain address arithmetic exceeds RAM address field");
    }
    return static_cast<std::uint32_t>(result);
}
}

SourceChainPacket decode_source_chain(std::uint32_t tadr,
                                     std::array<std::uint32_t, 4> raw,
                                     SourceChainStack stack) {
    // Sony EE User's Manual v6.0, source-chain tables (pp46/60), tag layout
    // (p59), and channel termination/stack behavior (pp55–56). Upper64 tag bits
    // do not encode DMA control; TTE forwarding is outside this pure planner.
    validate_address(tadr);
    validate_address(raw[1]);
    for (const auto saved : stack.addresses) { validate_address(saved); }
    if (stack.depth > stack.addresses.size()) {
        throw std::invalid_argument("source-chain stack depth exceeds two addresses");
    }
    if ((raw[0] & 0x0fff0000u) != 0) {
        throw std::invalid_argument("source-chain priority or reserved tag fields unsupported");
    }

    SourceChainPacket packet;
    packet.qwords = raw[0] & 0xffffu;
    packet.tag = static_cast<std::uint16_t>(raw[0] >> 16u);
    packet.stack = stack;
    packet.id = static_cast<std::uint8_t>((raw[0] >> 28u) & 7u);
    packet.next_tag = tadr;
    if (packet.id == 5 && stack.depth == 2) {
        // A third CALL stops before its packet. Keep both saved addresses and
        // the offending tag address; the caller supplies CIS, not BUSERR.
        packet.address = tadr;
        packet.qwords = 0;
        packet.end = true;
        packet.stack_overflow = true;
        return packet;
    }

    const bool referenced = packet.id == 0 || packet.id == 3 || packet.id == 4;
    packet.address = referenced ? raw[1] : add_address(tadr, 16);
    if (packet.qwords != 0) {
        // Check every payload address without requiring a terminal packet's
        // unused one-past address to fit. No emulated physical RAM size assumed.
        static_cast<void>(add_address(packet.address, std::uint64_t{packet.qwords - 1u} * 16u));
    }
    switch (packet.id) {
    case 0: // REFE
    case 7: // END
        packet.end = true;
        break;
    case 1: // CNT
        packet.next_tag = add_address(packet.address, std::uint64_t{packet.qwords} * 16u);
        break;
    case 2: // NEXT
        packet.next_tag = raw[1];
        break;
    case 3: // REF
    case 4: // REFS; caller determines whether DMAC stall control is enabled.
        packet.next_tag = add_address(tadr, 16);
        break;
    case 5: // CALL
        packet.stack.addresses[packet.stack.depth] =
            add_address(packet.address, std::uint64_t{packet.qwords} * 16u);
        ++packet.stack.depth;
        packet.next_tag = raw[1];
        break;
    case 6: // RET
        if (packet.stack.depth == 0) { packet.end = true; }
        else { packet.next_tag = packet.stack.addresses[--packet.stack.depth]; }
        break;
    default:
        // ID is three bits; all eight values are defined for GIF source chains.
        throw std::invalid_argument("invalid source-chain tag ID");
    }
    return packet;
}

} // namespace critterlink
