#include "intent_parser.hpp"

#include <algorithm>
#include <sstream>
#include <regex>
#include <cstring>

namespace smo {

class IntentParser::Impl {
public:
    struct CommandDef {
        std::string name;
        std::string description;
        IntentType type;
        std::vector<std::string> aliases;
        std::vector<std::string> required_args;
        std::vector<std::pair<std::string, std::string>> optional_flags;
    };

    Impl() {
        // Define all commands using insert to avoid brace-init issues
        auto add = [this](const std::string& name, std::string desc, IntentType t,
                          std::vector<std::string> required,
                          std::vector<std::pair<std::string, std::string>> flags) {
            CommandDef def;
            def.description = std::move(desc);
            def.type = t;
            def.required_args = std::move(required);
            def.optional_flags = std::move(flags);
            commands_[name] = std::move(def);
        };
        auto add2 = [this](const std::string& name, std::string desc, IntentType t,
                           std::vector<std::string> aliases,
                           std::vector<std::string> required,
                           std::vector<std::pair<std::string, std::string>> flags) {
            CommandDef def;
            def.description = std::move(desc);
            def.type = t;
            def.aliases = std::move(aliases);
            def.required_args = std::move(required);
            def.optional_flags = std::move(flags);
            commands_[name] = std::move(def);
        };
        add("ls", "List directory contents", IntentType::Filesystem, {"path"}, {{"recursive", "Recursive listing"}, {"long", "Long format"}});
        add("pwd", "Print working directory", IntentType::Filesystem, {}, {});
        add("mkdir", "Create directory", IntentType::Filesystem, {"path"}, {{"parents", "Create parent directories"}, {"mode", "Permission mode"}});
        add("rm", "Remove file or directory", IntentType::Filesystem, {"path"}, {{"recursive", "Recursive removal"}, {"force", "Force removal"}});
        add("cp", "Copy file or directory", IntentType::Filesystem, {"src", "dst"}, {{"recursive", "Recursive copy"}, {"preserve", "Preserve attributes"}});
        add("mv", "Move/rename file or directory", IntentType::Filesystem, {"src", "dst"}, {{"force", "Force overwrite"}});
        add("cat", "Display file contents", IntentType::Filesystem, {"path"}, {{"lines", "Number of lines"}, {"follow", "Follow output"}});
        add("touch", "Create empty file", IntentType::Filesystem, {"path"}, {});
        add("echo", "Echo text", IntentType::Filesystem, {"text"}, {{"newline", "Add newline"}});
        add("get", "Get file from node", IntentType::Transfer, {"remote", "local"}, {{"overwrite", "Overwrite local"}, {"resume", "Resume transfer"}});
        add("put", "Put file to node", IntentType::Transfer, {"local", "remote"}, {{"overwrite", "Overwrite remote"}, {"resume", "Resume transfer"}});
        add("sync", "Synchronize directories", IntentType::Transfer, {"local", "remote"}, {{"delete", "Delete extra files"}, {"dry-run", "Dry run"}});
        add2("exec", "Execute command on node", IntentType::Execute, {}, {"command"}, {{"args", "Command arguments"}, {"timeout", "Timeout ms"}, {"shell", "Use shell"}});
        add("ping", "Ping node", IntentType::Execute, {}, {{"count", "Ping count"}, {"interval", "Interval ms"}});
        add("ps", "List processes", IntentType::Process, {}, {{"all", "All processes"}, {"user", "Filter by user"}});
        add("kill", "Kill process", IntentType::Process, {"pid"}, {{"signal", "Signal number"}, {"force", "Force kill"}});
        add("top", "Show process stats", IntentType::Process, {}, {{"delay", "Update delay"}, {"sort", "Sort field"}});
        add("deploy", "Deploy contract", IntentType::Deploy, {"contract_path"}, {{"policy", "Policy preset"}, {"mesh", "Target mesh"}, {"force", "Force deploy"}});
        add("undeploy", "Undeploy contract", IntentType::Undeploy, {"contract_id"}, {{"force", "Force undeploy"}});
        add("status", "Show status", IntentType::Status, {}, {{"contract", "Contract ID"}, {"details", "Show details"}});
        add("history", "Show history", IntentType::History, {}, {{"contract", "Contract ID"}, {"execution", "Execution ID"}, {"trace", "Trace ID"}, {"node", "Node ID"}, {"failed", "Failed only"}, {"from", "From timestamp"}, {"to", "To timestamp"}, {"limit", "Limit results"}});
        add("trace", "Show trace", IntentType::History, {"trace_id"}, {{"detail", "Show details"}});
        add("select", "Select nodes", IntentType::Select, {}, {{"name", "Selection name"}, {"role", "Role filter"}, {"tag", "Tag filter"}, {"where", "Where expression"}, {"mesh", "Mesh filter"}, {"os", "OS filter"}, {"arch", "Arch filter"}, {"version", "Version filter"}, {"trust", "Trust range"}, {"clear", "Clear selection"}});
        add2("policy", "Policy management", IntentType::Policy, {}, {}, {{"preset", "Policy preset"}, {"custom", "Custom policy file"}, {"list", "List policies"}, {"show", "Show policy details"}});
        add2("control", "Control level", IntentType::Control, {}, {}, {{"level", "Control level: safe|normal|force|emergency"}, {"scope", "Scope: single|mesh|quorum|witness"}, {"timeout", "Timeout ms"}, {"retry", "Retry count"}});
        add2("mesh", "Mesh management", IntentType::Mesh, {}, {}, {{"list", "List meshes"}, {"use", "Use mesh"}, {"create", "Create mesh"}, {"health", "Show mesh health"}, {"join", "Join mesh"}, {"leave", "Leave mesh"}, {"remove", "Remove mesh"}, {"invite", "Generate Join Token"}, {"expire", "Token expiry duration (e.g. 30m, 1h)"}, {"role", "Identity role: Authority, Member, Contributor, Observer"}, {"profile", "Setup profile: server, desktop, embedded, gateway"}});
        add2("genesis", "Genesis management", IntentType::Genesis, {}, {}, {{"create", "Create genesis mesh"}, {"status", "Show genesis status"}, {"manifest", "Show genesis manifest"}});
        add2("governance", "Governance management", IntentType::Governance, {}, {}, {{"propose", "Submit governance proposal"}, {"sign", "Sign a proposal"}, {"commit", "Commit a proposal"}, {"list", "List proposals"}, {"status", "Show governance status"}});
        add2("recovery", "Mesh recovery operations", IntentType::Recovery, {}, {}, {{"restore", "Start soft recovery"}, {"force", "Force hard recovery"}, {"status", "Show recovery status"}, {"cancel", "Cancel recovery"}});
        add("connect", "Connect to node", IntentType::Connect, {}, {{"address", "Node address"}, {"name", "Connection name"}});
        add("disconnect", "Disconnect", IntentType::Disconnect, {}, {});
        add2("context", "Context management", IntentType::Context, {}, {}, {{"list", "List contexts"}, {"save", "Save context"}, {"load", "Load context"}, {"clear", "Clear context"}});
        add2("help", "Show help", IntentType::Help, {}, {}, {{"command", "Command name"}});
        add("exit", "Exit shell", IntentType::Exit, {}, {});
    }

    std::unordered_map<std::string, CommandDef> commands_;
};

IntentParser::IntentParser() : impl_(std::make_unique<Impl>()) {}

IntentParser::~IntentParser() = default;

Result<ParsedCommand> IntentParser::parse(const std::vector<std::string>& args) const {
    if (args.empty()) {
        return SMO_ERR(Protocol, 100, Error, NoRetry, None, "No command provided");
    }

    std::string cmd = args[0];
    
    // Check for help
    if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        if (args.size() > 1) {
            return ParsedCommand{Intent{IntentType::Help, "", {}, {{"command", args[1]}}}, cmd, {}};
        }
        return ParsedCommand{Intent{IntentType::Help, "", {}, {}}, "help", {}};
    }

    // Find command
    auto it = impl_->commands_.find(cmd);
    if (it == impl_->commands_.end()) {
        // Check aliases
        for (const auto& [name, def] : impl_->commands_) {
            if (std::find(def.aliases.begin(), def.aliases.end(), cmd) != def.aliases.end()) {
                cmd = name;
                break;
            }
        }
        if (impl_->commands_.find(cmd) == impl_->commands_.end()) {
            return SMO_ERR(Protocol, 101, Error, NoRetry, None, "Unknown command: " + cmd);
        }
    }

    const auto& def = impl_->commands_[cmd];
    Intent intent;
    intent.type = def.type;
    intent.opcode = cmd;

    // Parse arguments
    std::vector<std::string> positional;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        
        if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
            // Long flag
            size_t eq_pos = arg.find('=');
            if (eq_pos != std::string::npos) {
                std::string flag = arg.substr(2, eq_pos - 2);
                std::string value = arg.substr(eq_pos + 1);
                intent.flags[flag] = value;
            } else if (arg == "--") {
                // End of flags
                for (size_t j = i + 1; j < args.size(); ++j) {
                    positional.push_back(args[j]);
                }
                break;
            } else {
                std::string flag = arg.substr(2);
                // Check if next arg is value
                if (i + 1 < args.size() && args[i + 1][0] != '-') {
                    intent.flags[flag] = args[++i];
                } else {
                    intent.flags[flag] = "true";
                }
            }
        } else if (arg.size() >= 1 && arg[0] == '-') {
            // Short flags
            for (size_t j = 1; j < arg.size(); ++j) {
                char c = arg[j];
                if (c == 'h') {
                    intent.flags["help"] = "true";
                } else if (c == 'v') {
                    intent.flags["verbose"] = "true";
                } else if (c == 'f') {
                    intent.flags["force"] = "true";
                } else if (c == 'r') {
                    intent.flags["recursive"] = "true";
                } else if (c == 'd') {
                    intent.flags["dry-run"] = "true";
                } else {
                    intent.flags[std::string(1, c)] = "true";
                }
            }
        } else {
            positional.push_back(arg);
        }
    }

    // Assign all positional args to intent.args
    for (size_t i = 0; i < positional.size(); ++i) {
        intent.args.push_back(positional[i]);
    }

    // Validate required args count
    if (positional.size() < def.required_args.size()) {
        return SMO_ERR(Protocol, 102, Error, NoRetry, None, 
                      "Missing required argument: " + def.required_args[positional.size()]);
    }

    // Parse special flags
    if (intent.flags.count("nearest")) intent.nearest = true;
    if (intent.flags.count("random")) {
        try { intent.random_n = std::stoi(intent.flags["random"]); } catch (...) {}
    }
    if (intent.flags.count("top")) {
        try { intent.top_n = std::stoi(intent.flags["top"]); } catch (...) {}
    }
    if (intent.flags.count("scope")) intent.scope = intent.flags["scope"];
    if (intent.flags.count("control")) intent.control = intent.flags["control"];
    if (intent.flags.count("timeout")) {
        try { intent.timeout_ms = std::stoi(intent.flags["timeout"]); } catch (...) {}
    }
    if (intent.flags.count("retry")) {
        try { intent.retry_count = std::stoi(intent.flags["retry"]); } catch (...) {}
    }
    if (intent.flags.count("dry-run")) intent.dry_run = true;

    // Save selection name if provided
    if (intent.flags.count("save")) {
        intent.selection_name = intent.flags["save"];
        intent.save_selection = true;
    }

    // Handle selection flags
    if (intent.flags.count("role")) intent.roles.push_back(intent.flags["role"]);
    if (intent.flags.count("tag")) intent.tags.push_back(intent.flags["tag"]);
    if (intent.flags.count("where")) intent.where_expression = intent.flags["where"];
    if (intent.flags.count("mesh")) intent.mesh_filter = intent.flags["mesh"];
    if (intent.flags.count("os")) intent.os_filter = intent.flags["os"];
    if (intent.flags.count("arch")) intent.arch_filter = intent.flags["arch"];
    if (intent.flags.count("version")) intent.version_filter = intent.flags["version"];
    if (intent.flags.count("trust-min")) intent.trust_min = intent.flags["trust-min"];
    if (intent.flags.count("trust-max")) intent.trust_max = intent.flags["trust-max"];

    if (intent.flags.count("name")) intent.node_names.push_back(intent.flags["name"]);

    if (intent.flags.count("control")) intent.control = intent.flags["control"];
    if (intent.flags.count("scope")) intent.scope = intent.flags["scope"];
    if (intent.flags.count("timeout")) {
        try { intent.timeout_ms = std::stoi(intent.flags["timeout"]); } catch (...) {}
    }
    if (intent.flags.count("retry")) {
        try { intent.retry_count = std::stoi(intent.flags["retry"]); } catch (...) {}
    }

    if (intent.flags.count("dry-run")) intent.dry_run = true;
    if (intent.flags.count("force")) intent.flags["force"] = "true";

    return ParsedCommand{
        std::move(intent),
        impl_->commands_.begin()->first, // placeholder
        {}
    };
}

Result<ParsedCommand> IntentParser::parse(const std::string& command_line) const {
    // Simple tokenization respecting quotes
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    bool in_single_quotes = false;
    char escape = '\\';

    for (size_t i = 0; i < command_line.size(); ++i) {
        char c = command_line[i];
        
        if (c == escape && i + 1 < command_line.size()) {
            current.push_back(command_line[++i]);
        } else if (c == '"' && !in_single_quotes) {
            in_quotes = !in_quotes;
        } else if (c == '\'' && !in_quotes) {
            in_single_quotes = !in_single_quotes;
        } else if ((c == ' ' || c == '\t') && !in_quotes && !in_single_quotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        args.push_back(current);
    }

    return parse(args);
}

Result<void> IntentParser::validate(const Intent& intent) const {
    if (intent.type == IntentType::Unknown) {
        return SMO_ERR(Protocol, 103, Error, NoRetry, None, "Unknown intent type");
    }
    return {};
}

std::string IntentParser::generate_help() const {
    std::ostringstream oss;
    oss << "SMO CLI - Secure Mesh Operation\n\n";
    oss << "Usage: smo <command> [args...] [flags...]\n\n";
    oss << "Commands:\n";
    
    for (const auto& [name, def] : impl_->commands_) {
        oss << "  " << std::setw(15) << std::left << name << def.description << "\n";
    }
    
    oss << "\nGlobal flags:\n";
    oss << "  --help, -h          Show help\n";
    oss << "  --verbose, -v       Verbose output\n";
    oss << "  --dry-run, -d       Dry run (no execution)\n";
    oss << "  --force, -f         Force operation\n\n";
    
    oss << "Selection flags (for select command):\n";
    oss << "  --role ROLE         Filter by role\n";
    oss << "  --tag TAG           Filter by tag\n";
    oss << "  --where EXPR        Where expression\n";
    oss << "  --mesh MESH         Filter by mesh\n";
    oss << "  --os OS             Filter by OS\n";
    oss << "  --arch ARCH         Filter by architecture\n";
    oss << "  --trust-min VAL     Minimum trust score\n";
    oss << "  --trust-max VAL     Maximum trust score\n\n";
    
    oss << "Execution flags:\n";
    oss << "  --scope SCOPE       single|mesh|quorum|witness\n";
    oss << "  --control LEVEL     safe|normal|force|emergency\n";
    oss << "  --timeout MS        Timeout in milliseconds\n";
    oss << "  --retry N           Retry count\n";
    oss << "  --dry-run           Dry run only\n";
    oss << "  --force, -f         Force operation\n\n";
    
    oss << "Context flags:\n";
    oss << "  --save NAME         Save current selection as NAME\n";
    oss << "  --load NAME         Load saved selection\n";
    oss << "  --clear             Clear current selection\n\n";
    
    oss << "Examples:\n";
    oss << "  smo ls /\n";
    oss << "  smo exec hostname --scope mesh\n";
    oss << "  smo select --role Storage --tag backup\n";
    oss << "  smo exec ls /var/log --scope mesh --control safe\n";
    oss << "  smo policy use enterprise-standard\n";
    oss << "  smo context use production\n";
    
    return oss.str();
}

std::string IntentParser::generate_usage(const std::string& command) const {
    auto it = impl_->commands_.find(command);
    if (it == impl_->commands_.end()) {
        return "Unknown command: " + command;
    }

    const auto& def = it->second;
    std::ostringstream oss;
    oss << "Usage: smo " << command;
    
    if (!def.required_args.empty()) {
        for (const auto& arg : def.required_args) {
            oss << " <" << arg << ">";
        }
    }
    oss << " [flags...]\n\n";
    oss << def.description << "\n\n";
    
    if (!def.required_args.empty()) {
        oss << "Arguments:\n";
        for (const auto& arg : def.required_args) {
            oss << "  " << arg << "\n";
        }
    }
    
    return oss.str();
}

} // namespace smo