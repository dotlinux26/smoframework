#pragma once

#include "impl.hpp"
#include "suite.hpp"

#include <vector>

namespace smo {

class CryptoRegistry {
public:
    static CryptoRegistry& instance();

    // Register a suite implementation. Returns error on duplicate or
    // null function pointers.
    Result<void> register_suite(const CryptoProvider& provider);

    // Look up a suite by ID. Returns UNSUPPORTED_SUITE if not found.
    Result<const CryptoProvider*> get_suite(CryptoSuiteID id) const;

    // Return IDs of all registered suites.
    std::vector<CryptoSuiteID> available_suites() const;

private:
    CryptoRegistry() = default;
    CryptoRegistry(const CryptoRegistry&) = delete;
    CryptoRegistry& operator=(const CryptoRegistry&) = delete;

    struct Entry {
        CryptoSuiteID id;
        const CryptoProvider* provider;
    };

    std::vector<Entry> suites_;
};

} // namespace smo
