#include "sha256.hpp"

#include <cstring>

namespace smo {
namespace hash {

static constexpr std::array<uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32 - n));
}

void Sha256Provider::init(Context& ctx) noexcept {
    ctx.state[0] = 0x6a09e667;
    ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372;
    ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f;
    ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab;
    ctx.state[7] = 0x5be0cd19;
    ctx.total_bits = 0;
    ctx.buf_len = 0;
}

void Sha256Provider::compress(Context& ctx) noexcept {
    std::array<uint32_t, 64> w;
    for (size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(ctx.buffer[i * 4]) << 24)
             | (static_cast<uint32_t>(ctx.buffer[i * 4 + 1]) << 16)
             | (static_cast<uint32_t>(ctx.buffer[i * 4 + 2]) << 8)
             | (static_cast<uint32_t>(ctx.buffer[i * 4 + 3]));
    }
    for (size_t i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    auto a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
    auto e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];

    for (size_t i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

void Sha256Provider::update(Context& ctx, BytesView data) noexcept {
    for (size_t i = 0; i < data.size(); ++i) {
        ctx.buffer[ctx.buf_len++] = data[i];
        if (ctx.buf_len == 64) {
            compress(ctx);
            ctx.total_bits += 512;
            ctx.buf_len = 0;
        }
    }
}

void Sha256Provider::finalize(Context& ctx, BytesMutView out) noexcept {
    ctx.total_bits += ctx.buf_len * 8;
    ctx.buffer[ctx.buf_len++] = 0x80;

    if (ctx.buf_len > 56) {
        while (ctx.buf_len < 64) ctx.buffer[ctx.buf_len++] = 0;
        compress(ctx);
        ctx.buf_len = 0;
    }

    while (ctx.buf_len < 56) ctx.buffer[ctx.buf_len++] = 0;

    ctx.buffer[56] = static_cast<uint8_t>(ctx.total_bits >> 56);
    ctx.buffer[57] = static_cast<uint8_t>(ctx.total_bits >> 48);
    ctx.buffer[58] = static_cast<uint8_t>(ctx.total_bits >> 40);
    ctx.buffer[59] = static_cast<uint8_t>(ctx.total_bits >> 32);
    ctx.buffer[60] = static_cast<uint8_t>(ctx.total_bits >> 24);
    ctx.buffer[61] = static_cast<uint8_t>(ctx.total_bits >> 16);
    ctx.buffer[62] = static_cast<uint8_t>(ctx.total_bits >> 8);
    ctx.buffer[63] = static_cast<uint8_t>(ctx.total_bits);
    compress(ctx);

    for (size_t i = 0; i < 8; ++i) {
        out[i * 4]     = static_cast<uint8_t>(ctx.state[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(ctx.state[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(ctx.state[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i]);
    }
}

Bytes Sha256Provider::hash(BytesView data) {
    Context ctx;
    init(ctx);
    update(ctx, data);
    Bytes result(kDigestSize);
    finalize(ctx, BytesMutView{result.data(), result.size()});
    return result;
}

void Sha256Provider::register_as_default() {
    HashProvider::set_default_provider(std::make_unique<Sha256Provider>());
}

} // namespace hash
} // namespace smo
