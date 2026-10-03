#include "critterlink/spu.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
constexpr std::uint32_t base = 0x1f900000, words = Spu::ram_size/2;
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

std::int32_t floor_shift(std::int64_t n, unsigned bits) {
    const auto d = std::int64_t{1} << bits;
    return static_cast<std::int32_t>(n >= 0?n/d: -((-n+d-1)/d));
}

std::int16_t clip(std::int32_t n) {
    return static_cast<std::int16_t>(std::clamp(n, -32768, 32767));
}

std::int32_t gain(std::uint16_t v) {
    return (v&0x4000?static_cast<std::int32_t>(v)-32768: v)*2;
}

void volume(std::uint16_t v) {
    require((v&0x8000) == 0, "SPU volume sweep unsupported");
}

void adsr1(std::uint16_t v) {
    require(!(v&0x8000), "SPU exponential attack unsupported");
    require(((v>>10)&31) <= 26 || ((v>>8)&127) == 127, "SPU attack rate unsupported");
}

void adsr2(std::uint16_t v) {
    require(!(v&0xa020), "SPU exponential sustain/release or reserved ADSR bits unsupported");
    require(((v>>8)&31) <= 26 || ((v>>6)&127) == 127, "SPU sustain rate unsupported");
    require((v&31) <= 26 || (v&31) == 31, "SPU release rate unsupported");
}

void pitch(std::uint16_t v) {
    require(v == 0 || v == 0x1000 || v == 0x2000, "SPU fractional/other pitch unsupported");
}

std::uint32_t offset(std::uint32_t p) {
    require(Spu::address(p) && !(p&1), "Unsupported SPU halfword address");
    return p-base;
}

void envelope(SpuVoiceState& v) {
    if (v.phase == SpuEnvelope::off) return;
    if (v.phase == SpuEnvelope::decay && v.envelope <= std::min(32767, ((v.adsr1&15)+1)*2048)) {
        v.phase = SpuEnvelope::sustain;
        v.envelope_counter = 0;
        return;
    }
    unsigned shift = 0, step_bits = 0;
    bool decrease = false, exponential = false, frozen = false;
    switch (v.phase) {
        case SpuEnvelope::attack:
            shift = (v.adsr1>>10)&31;
        step_bits = (v.adsr1>>8)&3;
        frozen = ((v.adsr1>>8)&127) == 127;
        break;
        case SpuEnvelope::decay:
            shift = (v.adsr1>>4)&15;
        decrease = true;
        exponential = true;
        break;
        case SpuEnvelope::sustain:
            shift = (v.adsr2>>8)&31;
        step_bits = (v.adsr2>>6)&3;
        decrease = (v.adsr2&0x4000) != 0;
        frozen = ((v.adsr2>>6)&127) == 127;
        break;
        case SpuEnvelope::release:
            shift = v.adsr2&31;
        decrease = true;
        frozen = shift == 31;
        break;
        case SpuEnvelope::off:
            return;
    }
    if (frozen) return;
    auto step = 7-static_cast<std::int32_t>(step_bits);
    if (decrease)step = ~step;
    step *= 1<<std::max(0, 11-static_cast<int>(shift));
    const auto increment = 0x8000U>>std::max(0, static_cast<int>(shift)-11);
    v.envelope_counter += increment;
    if (v.envelope_counter<0x8000) return;
    v.envelope_counter -= 0x8000;
    if (exponential)step = floor_shift(static_cast<std::int64_t>(step)*v.envelope, 15);
    v.envelope = std::clamp(v.envelope+step, 0, 32767);
    if (v.phase == SpuEnvelope::attack && v.envelope == 32767) {
        v.phase = SpuEnvelope::decay;
        v.envelope_counter = 0;
    }
    if (v.phase == SpuEnvelope::release && v.envelope == 0) {
        v.phase = SpuEnvelope::off;
        v.envelope_counter = 0;
    }
}

void decode(SpuVoiceState& v, const std::vector<std::uint8_t>& ram) {
    require(v.current <= words-8, "SPU ADPCM block outside sound RAM");
    auto pos = v.current*2;
    const auto header = ram[pos], flags = ram[pos+1];
    const unsigned filter = header>>4, shift = header&15;
    require(filter <= 4 && shift <= 12 && !(flags&0xf8), "Unsupported SPU ADPCM header");
    constexpr std::int32_t c1[] {
        0, 60, 115, 98, 122
    }, c2[] {
        0, 0, -52, -55, -60
    };
    for (unsigned i = 0; i<28; ++i) {
        auto n = (ram[pos+2+i/2]>>((i&1)*4))&15;
        auto signed_n = n >= 8?static_cast<int>(n)-16: static_cast<int>(n);
        auto sample = floor_shift(signed_n*4096, shift)+floor_shift(static_cast<std::int64_t>(v.history1)*c1[filter]+static_cast<std::int64_t>(v.history2)*c2[filter]+32, 6);
        v.decoded[i] = clip(sample);
        v.history2 = v.history1;
        v.history1 = v.decoded[i];
    }
    v.flags = flags;
    if (flags&4)v.loop = v.current;
    v.loaded = true;
    v.cursor = 0;
}
}

bool Spu::address(std::uint32_t p) noexcept {
    return p >= base && p<base+0x800;
}

std::uint16_t Spu::read16(std::uint32_t p) const {
    const auto o = offset(p);
    if (o<0x20) {
        const auto&v = state_.voices[o/16];
        switch (o%16) {
            case 0:
                return v.volume_left;
            case 2:
                return v.volume_right;
            case 4:
                return v.pitch;
            case 6:
                return v.adsr1;
            case 8:
                return v.adsr2;
            case 10:
                return static_cast<std::uint16_t>(v.envelope);
            default:
                break;
        }
    }
    if (o >= 0x1c0 && o<0x1d8) {
        const auto&v = state_.voices[(o-0x1c0)/12];
        const auto r = (o-0x1c0)%12;
        if (r<8) {
            const auto a = r<4?v.start: v.loop;
            return static_cast<std::uint16_t>((r&2)?a: a>>16);
        }
    }
    switch (o) {
        case 0x188:
            return state_.mix_left;
        case 0x190:
            return state_.mix_right;
        case 0x198:
            return state_.mmix;
        case 0x19a:
            return state_.attr;
        case 0x1a8:
            return static_cast<std::uint16_t>(state_.transfer_address>>16);
        case 0x1aa:
            return static_cast<std::uint16_t>(state_.transfer_address);
        case 0x340:
            return state_.endx;
        case 0x342:
            return 0;
        case 0x344:
            return state_.transfer_active?0x400: 0;
        case 0x760:
            return state_.master_left;
        case 0x762:
            return state_.master_right;
        default:
            throw std::invalid_argument("Unsupported SPU register read");
    }
}

void Spu::write16(std::uint32_t p, std::uint16_t value) {
    const auto o = offset(p);
    if (o<0x20) {
        auto&v = state_.voices[o/16];
        switch (o%16) {
            case 0:
                volume(value);
            v.volume_left = value;
            return;
            case 2:
                volume(value);
            v.volume_right = value;
            return;
            case 4:
                pitch(value);
            require(v.phase == SpuEnvelope::off || v.pitch == value, "SPU live pitch change unsupported");
            v.pitch = value;
            return;
            case 6:
                adsr1(value);
            v.adsr1 = value;
            return;
            case 8:
                adsr2(value);
            v.adsr2 = value;
            return;
            default:
                throw std::invalid_argument("Unsupported SPU voice write");
        }
    }
    if (o >= 0x1c0 && o<0x1d8) {
        auto&v = state_.voices[(o-0x1c0)/12];
        const auto r = (o-0x1c0)%12;
        require(r<8, "SPU NAX write unsupported");
        require(v.phase == SpuEnvelope::off, "SPU active voice address write unsupported");
        auto& a = r<4?v.start: v.loop;
        require((r&2)?!(value&7): value<16, "SPU address outside aligned RAM");
        a = (r&2)?((a&0xf0000)|value): ((a&0xffff)|(static_cast<std::uint32_t>(value)<<16));
        return;
    }
    switch (o) {
        case 0x188:
            case 0x190: require(!(value&~3U), "SPU voices beyond 0/1 unsupported");
        (o == 0x188?state_.mix_left: state_.mix_right) = value;
        return;
        case 0x198:
            require(value == 0 || value == 0xff0, "Unsupported SPU MMIX routing");
        state_.mmix = value;
        return;
        case 0x19a:
            require(!(value&~0xc010U), "Unsupported SPU ATTR mode");
        if ((value&0x10) && !state_.transfer_active && !state_.transfer_fifo.empty()) {
            require(state_.transfer_address <= words-state_.transfer_fifo.size(), "SPU transfer outside RAM");
            state_.transfer_active = true;
        }
        require(!state_.transfer_active || (value&0x10), "Cannot cancel active SPU transfer");
        state_.attr = value;
        return;
        case 0x1a0:
            case 0x1a4: require(!(value&~3U), "SPU voices beyond 0/1 unsupported");
        for (unsigned i = 0; i<2; ++i) if (value&(1U<<i)) {
            auto&v = state_.voices[i];
            v.envelope_counter = 0;
            if (o == 0x1a0) {
                v.current = v.start;
                v.envelope = 0;
                v.phase = SpuEnvelope::attack;
                v.history1 = v.history2 = 0;
                v.loaded = false;
                v.cursor = 0;
                v.flags = 0;
                state_.endx &= static_cast<std::uint16_t>(~(1U<<i));
            }
            else if (v.phase != SpuEnvelope::off)v.phase = SpuEnvelope::release;
        }
        return;
        case 0x1a8:
            case 0x1aa: require(state_.transfer_fifo.empty() && !state_.transfer_active, "SPU transfer address changed while pending");
        require(o == 0x1aa || value<16, "SPU transfer address outside RAM");
        state_.transfer_address = o == 0x1aa?(state_.transfer_address&0xf0000)|value: (state_.transfer_address&0xffff)|(static_cast<std::uint32_t>(value)<<16);
        return;
        case 0x1ac:
            require(!state_.transfer_active && state_.transfer_fifo.size()<32, "SPU transfer FIFO busy/full");
        state_.transfer_fifo.push_back(value);
        return;
        case 0x760:
            case 0x762: volume(value);
        (o == 0x760?state_.master_left: state_.master_right) = value;
        return;
        default:
            throw std::invalid_argument("Unsupported SPU register write");
    }
}

void Spu::tick() {
    if (state_.stop) return;
    try {
        require(state_.sample_count != std::numeric_limits<std::uint64_t>::max(), "SPU sample counter exhausted");
        if (state_.transfer_active) {
            auto value = state_.transfer_fifo[state_.transfer_cursor++];
            auto a = state_.transfer_address++*2;
            state_.ram[a] = static_cast<std::uint8_t>(value);
            state_.ram[a+1] = static_cast<std::uint8_t>(value>>8);
            if (state_.transfer_cursor == state_.transfer_fifo.size()) {
                state_.transfer_fifo.clear();
                state_.transfer_cursor = 0;
                state_.transfer_active = false;
            }
        }
        auto voices = state_.voices;
        auto endx = state_.endx;
        std::array<std::int32_t, 2> mix {};
        if (state_.attr&0x8000) for (unsigned i = 0; i<2; ++i) {
            auto&v = voices[i];
            if (v.phase == SpuEnvelope::off) continue;
            envelope(v);
            if (v.phase == SpuEnvelope::off) continue;
            if (!v.loaded) {
                try {
                    decode(v, state_.ram);
                }
                catch (const std::invalid_argument&e) {
                    throw std::invalid_argument("SPU voice "+std::to_string(i)+" block halfword "+std::to_string(v.current)+": "+e.what());
                }
            }
            auto sample = v.decoded[v.cursor];
            if ((state_.attr&0x4000) && state_.mmix == 0xff0) for (unsigned ch = 0; ch<2; ++ch) if ((ch?state_.mix_right: state_.mix_left)&(1U<<i)) {
                auto effective = floor_shift(static_cast<std::int64_t>(v.envelope)*gain(ch?v.volume_right: v.volume_left), 15);
                mix[ch] += floor_shift(static_cast<std::int64_t>(sample)*effective, 15);
            }
            v.cursor = static_cast<std::uint8_t>(v.cursor+v.pitch/0x1000);
            if (v.cursor >= 28) {
                v.loaded = false;
                v.cursor = 0;
                if (v.flags&1) {
                    endx |= static_cast<std::uint16_t>(1U<<i);
                    v.current = v.loop;
                    if (!(v.flags&2)) {
                        v.phase = SpuEnvelope::off;
                        v.envelope = 0;
                        v.envelope_counter = 0;
                    }
                }
                else v.current += 8;
            }
        }
        state_.voices = voices;
        state_.endx = endx;
        for (unsigned ch = 0; ch<2; ++ch) {
            state_.last_sample[ch] = clip(floor_shift(static_cast<std::int64_t>(mix[ch])*gain(ch?state_.master_right: state_.master_left), 15));
            auto u = static_cast<std::uint16_t>(state_.last_sample[ch]);
            for (unsigned byte = 0; byte<2; ++byte) {
                state_.signature ^= (u>>(byte*8))&255;
                state_.signature *= 1099511628211ULL;
            }
        }
        ++state_.sample_count;
    }
    catch (const std::invalid_argument&e) {
        state_.stop = e.what();
    }
}

void Spu::restore(const SpuState& s) {
    require(s.ram.size() == ram_size, "Invalid SPU RAM snapshot");
    require(!(s.attr&~0xc010U) && (s.mmix == 0 || s.mmix == 0xff0) && !(s.mix_left&~3U) && !(s.mix_right&~3U) && !(s.endx&~3U), "Invalid SPU register snapshot");
    volume(s.master_left);
    volume(s.master_right);
    require(s.transfer_fifo.size() <= 32 && s.transfer_address <= words, "Invalid SPU transfer snapshot");
    require(s.transfer_active?((s.attr&0x10) && s.transfer_cursor<s.transfer_fifo.size() && s.transfer_address <= words-(s.transfer_fifo.size()-s.transfer_cursor)): s.transfer_cursor == 0, "Invalid SPU transfer cursor");
    require(!s.stop || !s.stop->empty(), "Invalid SPU stop snapshot");
    for (const auto&v: s.voices) {
        volume(v.volume_left);
        volume(v.volume_right);
        pitch(v.pitch);
        adsr1(v.adsr1);
        adsr2(v.adsr2);
        require(v.phase == SpuEnvelope::off || (v.pitch == 0x1000) || (v.pitch == 0x2000 && !(v.cursor&1)) || (v.pitch == 0 && v.cursor == 0), "Invalid SPU fixed pitch cursor");
        require(v.start<words && v.loop<words && v.current <= words && !(v.start&7) && !(v.loop&7) && !(v.current&7), "Invalid SPU voice address snapshot");
        require(v.envelope >= 0 && v.envelope <= 32767 && v.envelope_counter<0x8000 && v.phase <= SpuEnvelope::release && v.cursor<28 && v.flags <= 7 && (!v.loaded || v.current <= words-8) && (v.loaded || v.cursor == 0), "Invalid SPU decoder/envelope snapshot");
    }
    auto replacement = s;
    state_ = std::move(replacement);
}
}
// namespace critterlink
