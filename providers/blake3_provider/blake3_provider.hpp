#pragma once

#include "core/crypto/hash_provider.hpp"
#include "core/errors/error.hpp"

namespace smo {

class Blake3Provider : public HashProvider {
public:
    Blake3Provider();

    Bytes hash(BytesView data) override;

    static Result<void> register_as_default();
};

} // namespace smo
