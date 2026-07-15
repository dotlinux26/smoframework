#pragma once

#include "core/crypto/hash_provider.hpp"

#include <array>
#include <cstdint>

namespace smo {
namespace hash {

class Sha256Provider final : public HashProvider {
public:
    static void register_as_default();

    Bytes hash(BytesView data) override;

    static constexpr size_t kDigestSize = 32;

    struct Context {
        std::array<uint32_t, 8> state;
        std::array<uint8_t, 64> buffer;
        uint64_t total_bits = 0;
        size_t buf_len = 0;
    };

    static void init(Context& ctx) noexcept;
    static void update(Context& ctx, BytesView data) noexcept;
    static void finalize(Context& ctx, BytesMutView out) noexcept;

private:
    static void compress(Context& ctx) noexcept;
};

} // namespace hash
} // namespace smo
