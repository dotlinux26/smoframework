#pragma once

#include "core/crypto/impl.hpp"
#include "core/crypto/fwd.hpp"

namespace smo {
namespace providers {

const CryptoProvider& get_suite3_purepqc_provider() noexcept;
void register_suite3_purepqc();

} // namespace providers
} // namespace smo
