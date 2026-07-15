#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "core/types.hpp"

namespace smo {

class HashProvider {
public:
    virtual ~HashProvider() = default;

    virtual Bytes hash(BytesView data) = 0;

    Bytes hash(std::string_view data) {
        return hash(BytesView{
            reinterpret_cast<const uint8_t*>(data.data()), data.size()
        });
    }

    std::string hash_hex(BytesView data) {
        auto raw = hash(data);
        return bytes_to_hex(raw);
    }

    std::string hash_hex(std::string_view data) {
        return hash_hex(BytesView{
            reinterpret_cast<const uint8_t*>(data.data()), data.size()
        });
    }

    static std::string bytes_to_hex(BytesView data);
    static Bytes hex_to_bytes(std::string_view hex);

    static HashProvider& default_provider();
    static void set_default_provider(std::unique_ptr<HashProvider> provider);
};

} // namespace smo
