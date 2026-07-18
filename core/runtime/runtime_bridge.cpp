#include "runtime_bridge.hpp"

#include <chrono>

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
    auto* route = resolve(pkt.opcode_id);
    if (!route) {
        return Result<RuntimeResult>(
            static_cast<Error>(RuntimeError::not_found(
                "unknown opcode: " + std::to_string(pkt.opcode_id))));
    }

    RuntimeRequest req;
    req.contract_id = route->contract_id;
    req.input.method = route->method;
    req.input.arguments = ContextValue(
        Bytes(pkt.payload.begin(), pkt.payload.end()));

    return kernel_.execute(req);
}

} // namespace smo::runtime