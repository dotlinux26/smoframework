#include "cli_context.hpp"
#include "intent_parser.hpp"
#include "core/enroll/join_token.hpp"
#include "core/enroll/auto_enroll.hpp"
#include "core/mesh/mesh_resolver.hpp"

#include <providers/suite1_classical/suite1_classical_provider.hpp>
#include <providers/suite3_purepqc/suite3_purepqc_provider.hpp>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <filesystem>
#include <cstdlib>

namespace smo {

class CLIApplication {
public:
    CLIApplication();
    ~CLIApplication();

    Result<int> run(int argc, char* argv[]);
    Result<void> initialize(const std::string& data_dir = "~/.smo");

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class CLIApplication::Impl {
public:
    CLIContextManager context_;
    std::unique_ptr<IntentParser> parser_;
    bool running_ = true;
    bool interactive_ = false;
    struct termios orig_termios_;

    static constexpr const char* kCommands_[] = {
        "ls", "cd", "pwd", "info", "exec", "ping", "ps", "kill", "top",
        "deploy", "undeploy", "status", "history", "trace",
        "select", "use", "policy", "control",
        "mesh", "connect", "disconnect", "context",
        "help", "exit", "quit", "clear",
        "get", "put", "sync", "cat", "echo", "touch",
        "mkdir", "rm", "cp", "mv",
        nullptr
    };

    Impl() {
        parser_ = std::make_unique<IntentParser>();
    }

    Result<void> init(const std::string& data_dir) {
        return context_.initialize(data_dir);
    }

    // ---- Auto-complete ----
    static char* command_generator(const char* text, int state) {
        static int idx;
        if (state == 0) idx = 0;
        while (kCommands_[idx]) {
            const char* cmd = kCommands_[idx++];
            size_t len = strlen(text);
            if (strncmp(cmd, text, len) == 0) {
                return strdup(cmd);
            }
        }
        return nullptr;
    }

    static char** completer(const char* text, int, int) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }

    // ---- REPL ----
    Result<int> run_repl() {
        rl_attempted_completion_function = completer;
        rl_bind_key('\t', rl_complete);

        std::string hist_file = std::string(getenv("HOME")) + "/.smo_history";
        read_history(hist_file.c_str());

        signal(SIGINT, handle_sigint);
        signal(SIGTERM, handle_sigterm);
        signal(SIGTSTP, SIG_IGN);

        std::cout << "\n"
                  << "  ╔══════════════════════════════════════╗\n"
                  << "  ║    SMO Interactive Shell v0.1        ║\n"
                  << "  ║    Type 'help' for commands          ║\n"
                  << "  ║    Type 'exit'  or Ctrl-D to quit    ║\n"
                  << "  ╚══════════════════════════════════════╝\n"
                  << std::endl;

        interactive_ = true;
        tcgetattr(STDIN_FILENO, &orig_termios_);

        while (running_) {
            std::string prompt = context_.get_prompt();
            char* line = readline(prompt.c_str());
            if (!line) break;

            std::string input(line);
            free(line);

            if (input.empty()) continue;

            add_history(input.c_str());
            context_.add_history(input);

            if (input == "exit" || input == "quit") break;
            if (input == "clear") { (void)std::system("clear"); continue; }

            auto parse_result = parser_->parse(input);
            if (!parse_result) {
                std::cerr << "Error: " << parse_result.error().message << "\n";
                continue;
            }

            auto& intent = parse_result.value().intent;
            auto result = execute_intent(intent);
            if (!result) {
                std::cerr << "Error: " << result.error().message << "\n";
            }
        }

        write_history(hist_file.c_str());
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios_);

        std::cout << "Bye." << std::endl;
        return 0;
    }

    Result<int> run_command(const std::vector<std::string>& args) {
        if (args.empty()) return 1;

        // Reconstruct command string for history
        std::string cmd_line;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) cmd_line += ' ';
            cmd_line += args[i];
        }
        context_.add_history(cmd_line);

        auto result = parser_->parse(args);
        if (!result) {
            std::cerr << "Parse error: " << result.error().message << "\n";
            return 1;
        }

        return execute_intent(result.value().intent);
    }

private:
    static void handle_sigint(int) {
        rl_free_line_state();
        rl_cleanup_after_signal();
        rl_line_buffer[0] = '\0';
        rl_on_new_line();
        rl_replace_line("", 0);
        rl_redisplay();
    }

    static void handle_sigterm(int) {
        // will be caught by the main loop
    }

    Result<int> execute_intent(const Intent& intent) {
        switch (intent.type) {
            case IntentType::Help:      return handle_help(intent);
            case IntentType::Exit:      return 0;
            case IntentType::Select:    return handle_select(intent);
            case IntentType::Execute:   return handle_exec(intent);
            case IntentType::Transfer:  return handle_transfer(intent);
            case IntentType::Filesystem:return handle_filesystem(intent);
            case IntentType::Process:   return handle_process(intent);
            case IntentType::Deploy:    return handle_deploy(intent);
            case IntentType::Undeploy:  return handle_undeploy(intent);
            case IntentType::Status:    return handle_status(intent);
            case IntentType::Policy:    return handle_policy(intent);
            case IntentType::Control:   return handle_control(intent);
            case IntentType::Context:   return handle_context(intent);
            case IntentType::Mesh:      return handle_mesh(intent);
            case IntentType::Connect:   return handle_connect(intent);
            case IntentType::Disconnect:return handle_disconnect(intent);
            case IntentType::Discover:  return handle_discover(intent);
            case IntentType::Export:    return handle_export(intent);
            case IntentType::Session:   return handle_session(intent);
            case IntentType::History:   return handle_history(intent);
            case IntentType::Trace:     return handle_trace(intent);
            default:
                std::cerr << "Unknown intent type: " << (int)intent.type << "\n";
                return 1;
        }
    }

    // ---- Command implementations ----

    Result<int> handle_help(const Intent& intent) {
        if (!intent.flags.empty() && intent.flags.begin()->first != "command") {
            std::cout << parser_->generate_usage(intent.flags.begin()->first);
        } else {
            std::cout << parser_->generate_help();
        }
        return 0;
    }

    Result<int> handle_select(const Intent& intent) {
        // selection context from intent
        SelectionContext sel;
        sel.node_names = intent.node_names;
        sel.node_ids = intent.node_ids;
        sel.roles = intent.roles;
        sel.tags = intent.tags;
        sel.where_expression = intent.where_expression;
        sel.mesh_name = intent.mesh_filter;
        sel.os_filter = intent.os_filter;
        sel.arch_filter = intent.arch_filter;
        sel.version_filter = intent.version_filter;

        if (intent.trust_min) sel.trust_min = std::stof(intent.trust_min.value());
        if (intent.trust_max) sel.trust_max = std::stof(intent.trust_max.value());

        if (intent.flags.count("clear")) {
            context_.clear_selection();
            std::cout << "Selection cleared.\n";
            return 0;
        }

        sel.is_active = true;
        auto res = context_.set_selection(sel);
        if (!res) return 1;

        if (intent.save_selection && !intent.selection_name.empty()) {
            context_.save_selection(intent.selection_name);
            std::cout << "Selection saved as '" << intent.selection_name << "'.\n";
        }

        std::string sel_desc;
        if (!sel.node_names.empty()) sel_desc = sel.node_names.front();
        else if (!sel.roles.empty()) sel_desc = "role:" + sel.roles.front();
        else if (!sel.tags.empty()) sel_desc = "tag:" + sel.tags.front();
        else sel_desc = "*";
        std::cout << "Selection active: " << sel_desc
                  << " (" << (sel.roles.empty() ? "any" : sel.roles.front()) << ")"
                  << "\n";
        return 0;
    }

    Result<int> handle_exec(const Intent& intent) {
        if (intent.args.empty()) {
            std::cerr << "Usage: exec <command> [args...]\n";
            return 1;
        }

        std::string cmd = intent.args[0];
        // Check if in a session (direct connection) or need dispatch
        if (context_.is_connected()) {
            std::cout << "[session " << context_.get_connected_node()
                      << "] $ " << cmd << "\n";
            // Would pipe through transport to connected node
            return 0;
        }

        // Check if there is an active selection
        auto sel_result = context_.get_selection();
        if (!sel_result) {
            std::cerr << "No active selection. Use 'select' or 'connect' first.\n";
            return 1;
        }

        const auto& sel = sel_result.value();
        std::string scope_str = intent.scope.empty() ? "single" : intent.scope;
        std::cout << "[" << scope_str << " over " << sel.node_names.size() << " node(s)]"
                  << " $ " << cmd << "\n";
        std::cout << "(Dispatch not yet implemented)\n";
        return 0;
    }

    Result<int> handle_transfer(const Intent& intent) {
        std::cout << "[transfer] " << intent.opcode;
        for (const auto& a : intent.args) std::cout << " " << a;
        std::cout << "\n";
        std::cout << "(Transfer not yet implemented)\n";
        return 0;
    }

    Result<int> handle_filesystem(const Intent& intent) {
        std::cout << "[filesystem] " << intent.opcode;
        for (const auto& a : intent.args) std::cout << " " << a;
        std::cout << "\n";
        std::cout << "(Filesystem operations not yet implemented)\n";
        return 0;
    }

    Result<int> handle_process(const Intent& intent) {
        std::cout << "[process] " << intent.opcode;
        for (const auto& a : intent.args) std::cout << " " << a;
        std::cout << "\n";
        std::cout << "(Process operations not yet implemented)\n";
        return 0;
    }

    Result<int> handle_deploy(const Intent& intent) {
        if (intent.args.empty()) {
            std::cerr << "Usage: deploy <contract_path> [--policy NAME] [--mesh MESH]\n";
            return 1;
        }
        std::cout << "Deploying contract: " << intent.args[0] << "\n";
        std::cout << "(Deploy not yet implemented)\n";
        return 0;
    }

    Result<int> handle_undeploy(const Intent& intent) {
        if (intent.args.empty()) {
            std::cerr << "Usage: undeploy <contract_id>\n";
            return 1;
        }
        std::cout << "Undeploying contract: " << intent.args[0] << "\n";
        std::cout << "(Undeploy not yet implemented)\n";
        return 0;
    }

    Result<int> handle_status(const Intent& intent) {
        if (intent.args.empty()) {
            std::cout << "Context status:\n";
            std::cout << "  Mesh:     " << (context_.get_current_mesh() ? context_.get_current_mesh().value() : "(none)") << "\n";
            auto sel = context_.get_selection();
            if (sel) {
                std::cout << "  Selection: active (" << sel.value().node_names.size() << " nodes)\n";
            } else {
                std::cout << "  Selection: (none)\n";
            }
            std::cout << "  Control:  " << (int)context_.get_control_level() << "\n";
            std::cout << "  Scope:    " << (int)context_.get_scope() << "\n";
            std::cout << "  Timeout:  " << context_.get_timeout() << " ms\n";
            std::cout << "  Retry:    " << context_.get_retry() << "\n";
            if (context_.is_connected()) {
                std::cout << "  Session:  " << context_.get_connected_node() << "\n";
            }
        } else {
            std::cout << "Contract status for: " << intent.args[0] << "\n";
            std::cout << "(Contract status not yet implemented)\n";
        }
        return 0;
    }

    Result<int> handle_policy(const Intent& intent) {
        if (intent.flags.count("list")) {
            std::cout << "Available policies:\n";
            std::cout << "  - default (safe, single, 30s timeout)\n";
            std::cout << "  - enterprise (elevated, quorum, 60s)\n";
            std::cout << "  - emergency (emergency, mesh, 10s)\n";
            return 0;
        }
        if (intent.flags.count("show")) {
            std::cout << "Policy: " << intent.flags.at("show") << "\n";
            std::cout << "(Policy details not yet implemented)\n";
            return 0;
        }
        if (intent.flags.count("preset")) {
            std::string preset = intent.flags.at("preset");
            ExecutionContext ctx = ExecutionContext{};
            auto exec_ctx = context_.get_execution_context();
            if (exec_ctx) ctx = exec_ctx.value();
            if (preset == "default") {
                ctx.control = ControlLevel::Safe;
                ctx.scope = ExecutionScope::Single;
                ctx.timeout_ms = 30000;
            } else if (preset == "enterprise") {
                ctx.control = ControlLevel::Normal;
                ctx.scope = ExecutionScope::Quorum;
                ctx.timeout_ms = 60000;
            } else if (preset == "emergency") {
                ctx.control = ControlLevel::Emergency;
                ctx.scope = ExecutionScope::Mesh;
                ctx.timeout_ms = 10000;
            } else {
                std::cerr << "Unknown policy preset: " << preset << "\n";
                return 1;
            }
            context_.set_execution_context(ctx);
            std::cout << "Applied policy: " << preset << "\n";
            return 0;
        }
        std::cout << "Policy management.\n";
        return 0;
    }

    Result<int> handle_control(const Intent& intent) {
        if (intent.flags.count("level")) {
            std::string l = intent.flags.at("level");
            if (l == "safe")      context_.set_control_level(ControlLevel::Safe);
            else if (l == "normal") context_.set_control_level(ControlLevel::Normal);
            else if (l == "force")  context_.set_control_level(ControlLevel::Force);
            else if (l == "emergency") context_.set_control_level(ControlLevel::Emergency);
            else { std::cerr << "Invalid level\n"; return 1; }
        }
        if (intent.flags.count("scope")) {
            std::string s = intent.flags.at("scope");
            if (s == "single")   context_.set_scope(ExecutionScope::Single);
            else if (s == "mesh")   context_.set_scope(ExecutionScope::Mesh);
            else if (s == "quorum") context_.set_scope(ExecutionScope::Quorum);
            else if (s == "witness") context_.set_scope(ExecutionScope::Witness);
            else { std::cerr << "Invalid scope\n"; return 1; }
        }
        if (intent.flags.count("timeout")) {
            context_.set_timeout(std::stoi(intent.flags.at("timeout")));
        }
        if (intent.flags.count("retry")) {
            context_.set_retry(std::stoi(intent.flags.at("retry")));
        }
        auto level_to_str = [](ControlLevel l) -> const char* {
            switch (l) {
                case ControlLevel::Safe:      return "safe";
                case ControlLevel::Normal:    return "normal";
                case ControlLevel::Elevated:  return "elevated";
                case ControlLevel::Force:     return "force";
                case ControlLevel::Emergency: return "emergency";
                case ControlLevel::Privileged:return "privileged";
                default:                      return "unknown";
            }
        };
        auto scope_to_str = [](ExecutionScope s) -> const char* {
            switch (s) {
                case ExecutionScope::Single:  return "single";
                case ExecutionScope::Mesh:    return "mesh";
                case ExecutionScope::Cluster: return "cluster";
                case ExecutionScope::Global:  return "global";
                case ExecutionScope::Quorum:  return "quorum";
                case ExecutionScope::Witness: return "witness";
                default:                      return "unknown";
            }
        };
        std::cout << "Control: level=" << level_to_str(context_.get_control_level())
                  << " scope=" << scope_to_str(context_.get_scope())
                  << " timeout=" << context_.get_timeout() << "ms"
                  << " retry=" << context_.get_retry() << "\n";
        return 0;
    }

    Result<int> handle_context(const Intent& intent) {
        if (intent.flags.count("list")) {
            std::cout << "Current context:\n";
            return handle_status(Intent{IntentType::Status, "", {}, {}});
        }
        if (intent.flags.count("save")) {
            context_.save_execution_context(intent.flags.at("save"));
            std::cout << "Context saved.\n";
            return 0;
        }
        if (intent.flags.count("load")) {
            context_.load_execution_context(intent.flags.at("load"));
            std::cout << "Context loaded.\n";
            return 0;
        }
        if (intent.flags.count("clear")) {
            context_.clear_selection();
            context_.disconnect();
            std::cout << "Context cleared.\n";
            return 0;
        }
        return handle_status(Intent{IntentType::Status});
    }

    Result<int> handle_mesh(const Intent& intent) {
        std::string home = smo::mesh::smo_home();

        // ── require_current: helper to get current mesh or error out ──
        auto require_current = [&]() -> Result<std::string> {
            auto cur = context_.get_current_mesh();
            if (cur && !cur.value().empty()) return cur.value();
            // List available meshes interactively
            std::string meshes_dir = home + "/meshes";
            std::vector<std::string> available;
            if (std::filesystem::is_directory(meshes_dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(meshes_dir)) {
                    if (entry.is_directory()) available.push_back(entry.path().filename().string());
                }
            }
            if (available.empty()) {
                return SMO_ERR_STORAGE(404, Info, NoRetry, None,
                    "No meshes found. Create one:\n  smo mesh create <name>\n"
                    "  smo-admin --mesh <name> create-mesh");
            }
            std::string msg = "No active mesh.\n\nAvailable meshes:\n";
            for (size_t i = 0; i < available.size(); ++i) {
                msg += "  " + std::to_string(i + 1) + ". " + available[i] + "\n";
            }
            msg += "\nUse: smo mesh use <name>";
            return SMO_ERR_STORAGE(404, Info, NoRetry, None, msg);
        };

        if (intent.flags.count("list") || intent.flags.empty()) {
            auto current_mesh = context_.get_current_mesh();
            std::string current_name = current_mesh ? current_mesh.value() : "";

            std::string meshes_dir = home + "/meshes";
            std::cout << "Meshes:\n";
            if (std::filesystem::is_directory(meshes_dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(meshes_dir)) {
                    if (!entry.is_directory()) continue;
                    std::string name = entry.path().filename().string();
                    bool is_current = (name == current_name);
                    std::cout << (is_current ? "  * " : "    ") << name << "\n";
                }
            } else {
                std::cout << "  (none)\n";
            }
            if (!current_name.empty()) {
                std::cout << "\nCurrent: " << current_name << "\n";
            }
            return 0;
        }
        if (intent.flags.count("use")) {
            std::string name = intent.flags.at("use");
            std::string mesh_dir = home + "/meshes/" + name;
            if (!std::filesystem::is_directory(mesh_dir)) {
                std::cerr << "Error: mesh '" << name << "' not found\n";
                std::cerr << "  Create it: smo-admin --mesh " << name << " create-mesh\n";
                return 1;
            }
            context_.set_mesh(name);
            std::cout << "Switched to mesh: " << name << "\n";
            return 0;
        }
        if (intent.flags.count("create")) {
            std::string name = intent.flags.at("create");
            std::string mesh_dir = home + "/meshes/" + name;
            if (std::filesystem::create_directories(mesh_dir)) {
                context_.set_mesh(name);
                std::cout << "Created and switched to mesh: " << name << "\n";
                std::cout << "  Initialize with: smo-admin --mesh " << name << " create-mesh\n";
            } else {
                context_.set_mesh(name);
                std::cout << "Using existing mesh: " << name << "\n";
            }
            return 0;
        }
        if (intent.flags.count("publish")) {
            auto current = require_current();
            if (!current) { std::cerr << current.error().message << "\n"; return 1; }
            std::string name = current.value();
            std::cout << "Publishing mesh '" << name << "'...\n";
            std::string cmd = "smo-admin --mesh " + name + " mesh publish";
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                std::cerr << "Publish failed (run manually: " << cmd << ")\n";
                return 1;
            }
            return 0;
        }
        if (intent.flags.count("serve")) {
            auto current = require_current();
            if (!current) { std::cerr << current.error().message << "\n"; return 1; }
            std::string name = current.value();
            uint16_t port = static_cast<uint16_t>(context_.get_port().value_or(5454));
            std::cout << "Starting enroll server for mesh '" << name << "' on port " << port << "...\n";
            std::string cmd = "smo-admin --mesh " + name + " serve --port " + std::to_string(port);
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                std::cerr << "Serve failed (run manually: " << cmd << ")\n";
                return 1;
            }
            return 0;
        }
        if (intent.flags.count("invite")) {
            std::string role  = intent.flags.count("role") ? intent.flags.at("role") : "Worker";
            std::string expire = intent.flags.count("expire") ? intent.flags.at("expire") : "1h";
            auto current = require_current();
            if (!current) { std::cerr << current.error().message << "\n"; return 1; }
            std::string name = current.value();
            std::cout << "Generating invite for mesh '" << name << "'...\n";
            std::string cmd = "smo-admin --mesh " + name + " generate-invite " + role + " --expire " + expire;
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                std::cerr << "Invite generation failed (run manually: " << cmd << ")\n";
                return 1;
            }
            return 0;
        }
        if (intent.flags.count("join")) {
            std::string token = intent.flags.count("join") ? intent.flags.at("join") : "";
            if (token.empty()) {
                std::cout << "Usage: mesh join --token SMO-JOIN-...\n";
                std::cout << "\nThe Join Token must be generated first via:\n";
                std::cout << "  smo mesh invite\n";
            } else {
                std::cout << "Joining mesh with token...\n";

                std::string data_dir = context_.get_data_dir().empty()
                    ? home + "/node"
                    : context_.get_data_dir();
                std::string node_name = context_.get_node_name().empty()
                    ? "node-" + std::to_string(getpid())
                    : context_.get_node_name();
                uint16_t port = static_cast<uint16_t>(context_.get_port().value_or(5454));

                auto result = smo::enroll::run_join_command(token, data_dir, node_name, port, "");
                if (!result) {
                    std::cerr << "Error: " << result.error().message << "\n";
                    return 1;
                }
            }
            return 0;
        }
        if (intent.flags.count("leave")) {
            context_.set_mesh("");
            std::cout << "Left current mesh.\n";
            return 0;
        }
        if (intent.flags.count("current")) {
            auto mesh = context_.get_current_mesh();
            std::cout << (mesh ? mesh.value() : "(none)") << "\n";
            return 0;
        }
        return 0;
    }

    Result<int> handle_connect(const Intent& intent) {
        if (intent.args.empty()) {
            if (context_.is_connected()) {
                std::cout << "Connected to: " << context_.get_connected_node() << "\n";
            } else {
                std::cout << "Not connected.\n";
            }
            return 0;
        }
        auto res = context_.connect(intent.args[0]);
        if (res) {
            std::cout << "Connected to " << intent.args[0] << "\n";
        }
        return res ? 0 : 1;
    }

    Result<int> handle_disconnect(const Intent&) {
        context_.disconnect();
        std::cout << "Disconnected.\n";
        return 0;
    }

    Result<int> handle_discover(const Intent& intent) {
        std::cout << "[discover]";
        for (const auto& a : intent.args) std::cout << " " << a;
        std::cout << "\n";
        std::cout << "(Discovery not yet implemented)\n";
        return 0;
    }

    Result<int> handle_export(const Intent& intent) {
        std::cout << "[export]";
        for (const auto& a : intent.args) std::cout << " " << a;
        std::cout << "\n";
        std::cout << "(Export not yet implemented)\n";
        return 0;
    }

    Result<int> handle_session(const Intent& intent) {
        if (intent.flags.count("status")) {
            if (context_.is_connected()) {
                std::cout << "Active session: " << context_.get_connected_node() << "\n";
            } else {
                std::cout << "No active session.\n";
            }
        }
        return 0;
    }

    Result<int> handle_history(const Intent& intent) {
        const auto& hist = context_.get_history();
        size_t limit = 20;
        if (intent.flags.count("limit")) {
            try { limit = std::stoul(intent.flags.at("limit")); } catch (...) {}
        }
        size_t start = hist.size() > limit ? hist.size() - limit : 0;
        for (size_t i = start; i < hist.size(); ++i) {
            std::cout << std::setw(4) << (i + 1) << ": " << hist[i] << "\n";
        }
        return 0;
    }

    Result<int> handle_trace(const Intent& intent) {
        if (intent.args.empty()) {
            std::cout << "Usage: trace <trace_id>\n";
            return 1;
        }
        std::cout << "Trace: " << intent.args[0] << "\n";
        std::cout << "(Trace not yet implemented)\n";
        return 0;
    }
};

CLIApplication::CLIApplication() : impl_(std::make_unique<Impl>()) {}

CLIApplication::~CLIApplication() = default;

Result<int> CLIApplication::run(int argc, char* argv[]) {
    if (argc < 2) {
        return impl_->run_repl();
    }
    std::vector<std::string> args(argv + 1, argv + argc);
    return impl_->run_command(args);
}

Result<void> CLIApplication::initialize(const std::string& data_dir) {
    return impl_->init(data_dir);
}

} // namespace smo

int main(int argc, char* argv[]) {
    smo::providers::register_suite1_classical();
    smo::providers::register_suite3_purepqc();
    smo::CLIApplication app;
    auto init_result = app.initialize("~/.smo");
    if (!init_result) {
        std::cerr << "Failed to initialize: " << init_result.error().message << "\n";
        return 1;
    }
    auto result = app.run(argc, argv);
    if (!result) return 1;
    return result.value();
}
