#pragma once

#include "../contract_interface.hpp"
#include "../runtime_context.hpp"

#include <string>
#include <vector>
#include <cstdint>

namespace smo::runtime {

class FileContract final : public NativeContract {
public:
    FileContract();

    Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx) override;

    ContractCapabilities required_capabilities() const override;

private:
    static ContractMetadata default_metadata();

    Result<ContractResult> handle_list(const ContractInput& input,
                                        const RuntimeContext& ctx);
    Result<ContractResult> handle_mkdir(const ContractInput& input,
                                         const RuntimeContext& ctx);
    Result<ContractResult> handle_remove(const ContractInput& input,
                                          const RuntimeContext& ctx);
    Result<ContractResult> handle_copy(const ContractInput& input,
                                        const RuntimeContext& ctx);
    Result<ContractResult> handle_move(const ContractInput& input,
                                        const RuntimeContext& ctx);
    Result<ContractResult> handle_stat(const ContractInput& input,
                                        const RuntimeContext& ctx);
    Result<ContractResult> handle_read(const ContractInput& input,
                                        const RuntimeContext& ctx);
    Result<ContractResult> handle_write(const ContractInput& input,
                                         const RuntimeContext& ctx);
    Result<ContractResult> handle_chmod(const ContractInput& input,
                                         const RuntimeContext& ctx);
    Result<ContractResult> handle_chown(const ContractInput& input,
                                         const RuntimeContext& ctx);
    Result<ContractResult> handle_symlink(const ContractInput& input,
                                           const RuntimeContext& ctx);
    Result<ContractResult> handle_readlink(const ContractInput& input,
                                            const RuntimeContext& ctx);
    Result<ContractResult> handle_realpath(const ContractInput& input,
                                            const RuntimeContext& ctx);
    Result<ContractResult> handle_info(const ContractInput& input,
                                        const RuntimeContext& ctx);
};

} // namespace smo::runtime
