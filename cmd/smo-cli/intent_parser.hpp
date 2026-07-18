#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include "core/types.hpp"
#include "core/errors/error.hpp"
#include "core/contract/contract.hpp"

namespace smo {

// ===========================================================================
// Intent Parsing
// ===========================================================================

enum class IntentType : uint8_t {
    Unknown = 0,
    Execute,
    Transfer,
    Filesystem,
    Process,
    Deploy,
    Undeploy,
    Status,
    History,
    Select,
    Policy,
    Control,
    Mesh,
    Connect,
    Disconnect,
    Context,
    Help,
    Exit,
    Discover,
    Export,
    Session,
    Trace,
    Genesis,
    Governance,
    Recovery,
};

struct Intent {
    IntentType type = IntentType::Unknown;
    std::string opcode;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> flags;
    
    // Selection criteria
    std::vector<std::string> node_names;
    std::vector<std::string> node_ids;
    std::vector<std::string> roles;
    std::vector<std::string> tags;
    std::string where_expression;
    std::string os_filter;
    std::string arch_filter;
    std::string version_filter;
    std::string mesh_filter;
    std::optional<std::string> trust_min;
    std::optional<std::string> trust_max;
    
    // Execution options
    bool nearest = false;
    int random_n = 0;
    int top_n = 0;
    std::string scope;  // single, mesh, quorum, witness
    std::string control; // safe, normal, force, emergency
    int timeout_ms = 30000;
    int retry_count = 0;
    bool dry_run = false;
    
    // Selection context (persists across commands)
    std::string selection_name;
    bool save_selection = false;
};

struct ParsedCommand {
    Intent intent;
    std::string raw_command;
    std::vector<std::string> positional_args;
};

class IntentParser {
public:
    explicit IntentParser();
    ~IntentParser();

    IntentParser(const IntentParser&) = delete;
    IntentParser& operator=(const IntentParser&) = delete;

    Result<ParsedCommand> parse(const std::vector<std::string>& args) const;
    Result<ParsedCommand> parse(const std::string& command_line) const;

    // Intent validation
    Result<void> validate(const Intent& intent) const;

    // Help text generation
    std::string generate_help() const;
    std::string generate_usage(const std::string& command) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smo