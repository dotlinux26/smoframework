#include "runtime_bridge.hpp"
#include "runtime_kernel.hpp"

#include <chrono>
#include <random>

namespace smo::runtime {

void RuntimeBridge::register_route(uint32_t opcode_id,
                                    std::string contract_id,
                                    std::string method) {
    routes_[opcode_id] = OpcodeRoute{
        std::move(contract_id),
        std::move(method)
    };
}

const OpcodeRoute* RuntimeBridge::resolve(uint32_t opcode_id) const {
    auto it = routes_.find(opcode_id);
    return it != routes_.end() ? &it->second : nullptr;
}

Result<RuntimeResult> RuntimeBridge::bridge(Packet&& pkt) {
    // 1. Resolve opcode → route
    auto* route = resolve(pkt.opcode_id);
    if (!route) {
        return Result<RuntimeResult>(
            static_cast<Error>(RuntimeError::not_found(
                "unknown opcode: " + std::to_string(pkt.opcode_id))));
    }

    // 2. Build RuntimeRequest from packet
    //    Payload is CBOR (or JSON for now) — deserialize via ContextValue
    RuntimeRequest req;
    req.contract_id = route->contract_id;
    req.requester = bytes_to_hex(pkt.session_id);
    req.input.method = route->method;
    req.input.arguments = ContextValue(std::string(
        reinterpret_cast<const char*>(pkt.payload.data()),
        pkt.payload.size()));

    // 3. Execute via RuntimeKernel (direct path, no plan/middleware)
    return kernel_.execute_direct(req);
}

} // namespace smo::runtime
