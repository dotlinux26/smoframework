#pragma once

#include "core/crypto/impl.hpp"
#include "core/crypto/fwd.hpp"

namespace smo {
namespace providers {

const CryptoProvider& get_suite1_classical_provider() noexcept;
void register_suite1_classical();

} // namespace providers
} // namespace smo
