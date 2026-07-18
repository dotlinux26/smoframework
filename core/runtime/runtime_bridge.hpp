#pragma once

#include "runtime_types.hpp"
#include "runtime_kernel.hpp"
#include "protocol/packet/packet.h"

#include <string>
#include <unordered_map>
#include <cstdint>

namespace smo::runtime {

// OpcodeRoute: maps an opcode_id to a contract + method (RFC 0041 §2.2)
struct OpcodeRoute {
    std::string contract_id;
    std::string method;
};

// RuntimeBridge: network → runtime adapter (RFC 0041)
//
//   Packet → OpcodeRoute → RuntimeRequest → RuntimeKernel → RuntimeResult
//
// Bridge does NOT know Transport. It only converts between wire objects
// (Packet) and runtime objects (RuntimeRequest / RuntimeResult).
class RuntimeBridge {
public:
    explicit RuntimeBridge(RuntimeKernel& kernel)
        : kernel_(kernel) {}

    // Register an opcode → contract_id + method mapping
    void register_route(uint32_t opcode_id,
                        std::string contract_id,
                        std::string method);

    // Resolve opcode to route
    const OpcodeRoute* resolve(uint32_t opcode_id) const;

    // Bridge: Packet → RuntimeRequest → Kernel → RuntimeResult
    // Returns the runtime result and any NextActions produced.
    Result<RuntimeResult> bridge(Packet&& pkt);

private:
    RuntimeKernel& kernel_;
    std::unordered_map<uint32_t, OpcodeRoute> routes_;
};

} // namespace smo::runtime
