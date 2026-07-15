#include "hash_provider.hpp"
#include <cctype>

namespace smo {

static std::unique_ptr<HashProvider> g_provider;

std::string HashProvider::bytes_to_hex(BytesView data) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(data.size() * 2, '\0');
    for (size_t i = 0; i < data.size(); ++i) {
        out[i * 2]     = kHex[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

Bytes HashProvider::hex_to_bytes(std::string_view hex) {
    if (hex.size() % 2 != 0) return {};
    Bytes out(hex.size() / 2, 0);
    for (size_t i = 0; i < out.size(); ++i) {
        auto hi = static_cast<uint8_t>(hex[i * 2]);
        auto lo = static_cast<uint8_t>(hex[i * 2 + 1]);
        auto from_hex = [](uint8_t c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out[i] = (from_hex(hi) << 4) | from_hex(lo);
    }
    return out;
}

HashProvider& HashProvider::default_provider() {
    if (!g_provider)
        throw std::runtime_error("no HashProvider registered");
    return *g_provider;
}

void HashProvider::set_default_provider(std::unique_ptr<HashProvider> provider) {
    g_provider = std::move(provider);
}

} // namespace smo
