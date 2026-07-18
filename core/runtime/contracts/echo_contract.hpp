#pragma once

#include "core/runtime/contract_interface.hpp"
#include "core/runtime/runtime_types.hpp"
#include "core/runtime/runtime_context.hpp"

namespace smo::runtime {

// EchoContract: returns the input as output (Sprint 37 E2E test)
//
// Input:  { "message": "hello" }
// Output: { "message": "hello" }
// No retry, no scheduling, no event — pure echo.
class EchoContract : public NativeContract {
public:
    EchoContract()
        : NativeContract(ContractMetadata{
              .id = "system.echo",
              .name = "Echo",
              .version = "0.1.0",
              .description = "Echo test contract for E2E pipeline",
          }) {}

    Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx) override
    {
        (void)ctx;

        // Echo: return input arguments as output data
        ContractResult result;
        result.status = ContractResult::Status::Success;
        result.data = input.arguments.to_string();

        // For network E2E: produce DispatchMessage action
        // The ActionExecutor will send this as a response Packet
        result.next_actions.push_back(ActionDispatchMessage{
            .opcode = "echo_response",
            .data = std::vector<uint8_t>(result.data.begin(), result.data.end()),
            .target_node = "",
            .timeout_ns = 5'000'000'000,
        });

        return result;
    }
};

} // namespace smo::runtime
