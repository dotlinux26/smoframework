#pragma once

#include "core/crypto/impl.hpp"
#include "core/crypto/fwd.hpp"

namespace smo {
namespace providers {

const CryptoProvider& get_suite2_modern_provider() noexcept;
void register_suite2_modern();

} // namespace providers
} // namespace smo
