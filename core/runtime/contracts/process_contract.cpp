#include "process_contract.hpp"

#include <sstream>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#include <fstream>

namespace smo::runtime {

// ── Map-extraction helpers ──────────────────────────────────────────
static Result<std::string> map_str(const ContextValue& args,
                                    const std::string& key)
{
    if (!args.is_map()) {
        return Error(ErrorCode{ErrorCategory::Runtime, 1002,
                                Severity::Error, RetryClass::NoRetry,
                                Recovery::None},
                     "arguments must be a map");
    }
    auto map = args.get<std::unordered_map<std::string, std::string>>();
    auto it = map.value().find(key);
    if (it == map.value().end()) {
        return Error(ErrorCode{ErrorCategory::Runtime, 1001,
                                Severity::Error, RetryClass::NoRetry,
                                Recovery::None},
                     "missing argument: " + key);
    }
    return it->second;
}

static Result<int64_t> map_int(const ContextValue& args,
                                const std::string& key)
{
    auto s = map_str(args, key);
    if (!s) return {s.error()};
    char* end = nullptr;
    int64_t val = std::strtoll(s.value().c_str(), &end, 10);
    if (end == s.value().c_str()) {
        return Error(ErrorCode{ErrorCategory::Runtime, 1001,
                                Severity::Error, RetryClass::NoRetry,
                                Recovery::None},
                     "invalid integer: " + key);
    }
    return val;
}

static Result<bool> map_bool(const ContextValue& args,
                              const std::string& key)
{
    auto s = map_str(args, key);
    if (!s) return {s.error()};
    if (s.value() == "true" || s.value() == "1") return true;
    if (s.value() == "false" || s.value() == "0") return false;
    return Error(ErrorCode{ErrorCategory::Runtime, 1001,
                            Severity::Error, RetryClass::NoRetry,
                            Recovery::None},
                 "invalid bool: " + key);
}

static bool opt_bool(const ContextValue& args, const std::string& key,
                      bool def)
{
    auto v = map_bool(args, key);
    return v ? v.value() : def;
}

static int64_t opt_int(const ContextValue& args, const std::string& key,
                        int64_t def)
{
    auto v = map_int(args, key);
    return v ? v.value() : def;
}

static std::string opt_str(const ContextValue& args, const std::string& key,
                            const std::string& def)
{
    auto v = map_str(args, key);
    return v ? v.value() : def;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ── Metadata ────────────────────────────────────────────────────────
ContractMetadata ProcessContract::default_metadata() {
    ContractMetadata meta;
    meta.id = "system.process";
    meta.name = "Process Contract";
    meta.version = "1.0.0";
    meta.api_version = 1;
    meta.author = "SMO Core";
    meta.max_execution_time_ns = 60'000'000'000;
    meta.tags = {"system", "process"};
    meta.provides = {"exec", "kill", "ps", "top", "systemctl", "service", "info"};
    meta.entry_point = "system.process";
    meta.has_validate = true;
    return meta;
}

ContractCapabilities ProcessContract::required_capabilities() const {
    ContractCapabilities caps;
    caps.set(static_cast<size_t>(ContractCapability::Filesystem));
    return caps;
}

ProcessContract::ProcessContract()
    : NativeContract(default_metadata()) {}

Result<ContractResult> ProcessContract::execute(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    if (input.method == "exec")      return handle_exec(input, ctx);
    if (input.method == "kill")      return handle_kill(input, ctx);
    if (input.method == "ps")        return handle_ps(input, ctx);
    if (input.method == "top")       return handle_top(input, ctx);
    if (input.method == "systemctl") return handle_systemctl(input, ctx);
    if (input.method == "service")   return handle_service(input, ctx);
    if (input.method == "info")      return handle_info(input, ctx);
    return ContractResult::denied("unknown method: " + input.method);
}

// ── handle_exec ─────────────────────────────────────────────────────
Result<ContractResult> ProcessContract::handle_exec(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto command = map_str(input.arguments, "command");
    if (!command) return ContractResult::denied(command.error().message);

    bool shell = opt_bool(input.arguments, "shell", false);
    bool capture = opt_bool(input.arguments, "capture_output", true);
    auto wd = opt_str(input.arguments, "working_dir", "");
    auto stdin_data = opt_str(input.arguments, "stdin_data", "");
    int64_t timeout_ns = opt_int(input.arguments, "timeout_ns", 30'000'000'000);

    int stdin_pipe[2] = {};
    int stdout_pipe[2] = {};
    int stderr_pipe[2] = {};

    if (!stdin_data.empty()) { if (pipe(stdin_pipe) != 0) stdin_pipe[0] = -1; }
    if (capture) {
        if (pipe(stdout_pipe) != 0) { stdout_pipe[0] = -1; stdout_pipe[1] = -1; }
        if (pipe(stderr_pipe) != 0) { stderr_pipe[0] = -1; stderr_pipe[1] = -1; }
    }

    pid_t pid = fork();
    if (pid < 0) return ContractResult::denied("fork failed");

    if (pid == 0) {
        if (!wd.empty()) (void)chdir(wd.c_str());

        if (capture) {
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[0]); close(stdout_pipe[1]);
            close(stderr_pipe[0]); close(stderr_pipe[1]);
        }
        if (!stdin_data.empty()) {
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[1]); close(stdin_pipe[0]);
        }

        if (shell)
            execl("/bin/sh", "sh", "-c", command.value().c_str(), nullptr);
        else
            execlp(command.value().c_str(), command.value().c_str(), nullptr);
        _exit(127);
    }

    if (!stdin_data.empty() && stdin_pipe[1] >= 0) {
        close(stdin_pipe[0]);
        (void)write(stdin_pipe[1], stdin_data.data(), stdin_data.size());
        close(stdin_pipe[1]);
    }

    if (capture) {
        close(stdout_pipe[1]); close(stderr_pipe[1]);
    }

    auto start = std::chrono::steady_clock::now();
    int exit_code = -1;
    bool timed_out = false;
    std::string stdout_str, stderr_str;

    auto wait_until = start + std::chrono::nanoseconds(timeout_ns);
    while (std::chrono::steady_clock::now() < wait_until) {
        int status = 0;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                       : WIFSIGNALED(status) ? -WTERMSIG(status) : -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (exit_code < 0) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        timed_out = true;
    }

    if (capture) {
        char buf[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], buf, sizeof(buf) - 1)) > 0)
            stdout_str.append(buf, static_cast<size_t>(n));
        while ((n = read(stderr_pipe[0], buf, sizeof(buf) - 1)) > 0)
            stderr_str.append(buf, static_cast<size_t>(n));
        close(stdout_pipe[0]); close(stderr_pipe[0]);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();

    std::ostringstream oss;
    oss << "{"
        << "\"exit_code\":" << exit_code << ","
        << "\"timed_out\":" << (timed_out ? "true" : "false") << ","
        << "\"duration_ns\":" << elapsed
        << "}";

    ContractResult result = ContractResult::ok(oss.str());
    result.metrics["exit_code"] = ContextValue(static_cast<int64_t>(exit_code));
    if (capture) {
        result.metrics["stdout"] = ContextValue(stdout_str);
        result.metrics["stderr"] = ContextValue(stderr_str);
    }
    return result;
}

// ── handle_kill ─────────────────────────────────────────────────────
Result<ContractResult> ProcessContract::handle_kill(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto pid = map_int(input.arguments, "pid");
    if (!pid) return ContractResult::denied(pid.error().message);

    int sig = static_cast<int>(opt_int(input.arguments, "signal", SIGTERM));
    bool killed = (::kill(static_cast<pid_t>(pid.value()), sig) == 0);

    ContractResult result = ContractResult::ok(killed ? "signal sent" : "kill failed");
    result.metrics["pid"] = ContextValue(pid.value());
    result.metrics["killed"] = ContextValue(static_cast<int64_t>(killed));
    result.metrics["signal"] = ContextValue(static_cast<int64_t>(sig));
    return result;
}

// ── parse /proc/[pid]/stat ──────────────────────────────────────────
static std::vector<std::string> proc_fields(pid_t pid) {
    auto content = read_file("/proc/" + std::to_string(pid) + "/stat");
    if (content.empty()) return {};
    // stat format: pid (comm) state ppid ... — comm may contain parens
    auto close_paren = content.rfind(')');
    if (close_paren == std::string::npos) return {};
    std::string after = content.substr(close_paren + 2);
    std::vector<std::string> fields;
    std::istringstream iss(after);
    std::string f;
    while (iss >> f) fields.push_back(f);
    return fields;
}

// ── handle_ps ───────────────────────────────────────────────────────
Result<ContractResult> ProcessContract::handle_ps(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto user_filter = opt_str(input.arguments, "user", "");
    int limit = static_cast<int>(opt_int(input.arguments, "limit", 100));

    std::ostringstream oss;
    oss << "[";

    DIR* proc = opendir("/proc");
    if (!proc) return ContractResult::denied("cannot open /proc");

    bool first = true;
    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        pid_t pid = static_cast<pid_t>(std::atol(entry->d_name));
        if (pid <= 0) continue;

        auto f = proc_fields(pid);
        if (f.size() < 40) continue;

        // f[0]=state, f[1]=ppid, ...
        if (!first) oss << ",";
        first = false;

        std::string comm = read_file("/proc/" + std::to_string(pid) + "/comm");
        if (!comm.empty() && comm.back() == '\n') comm.pop_back();

        std::string cmdline = read_file("/proc/" + std::to_string(pid) + "/cmdline");
        for (auto& c : cmdline) if (c == '\0') c = ' ';

        // Get RSS from /proc/pid/status
        auto status = read_file("/proc/" + std::to_string(pid) + "/status");
        uint64_t rss = 0;
        auto rss_pos = status.find("VmRSS:");
        if (rss_pos != std::string::npos) {
            rss = std::stoul(status.substr(rss_pos + 7));
        }

        oss << "{"
            << "\"pid\":" << pid << ","
            << "\"ppid\":" << f[1] << ","
            << "\"stat\":\"" << f[0] << "\","
            << "\"rss\":" << rss << ","
            << "\"command\":\"" << (cmdline.empty() ? comm : cmdline) << "\""
            << "}";

        if (--limit <= 0) break;
    }
    closedir(proc);
    oss << "]";

    return ContractResult::ok(oss.str());
}

// ── handle_top ──────────────────────────────────────────────────────
Result<ContractResult> ProcessContract::handle_top(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    int iterations = static_cast<int>(opt_int(input.arguments, "iterations", 1));
    int delay_ms = static_cast<int>(opt_int(input.arguments, "delay_ms", 1000));

    std::ostringstream oss;
    oss << "[";

    for (int i = 0; i < iterations; ++i) {
        if (i > 0) {
            oss << ",";
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        auto meminfo = read_file("/proc/meminfo");
        uint64_t mem_total = 0, mem_available = 0;
        auto parse_mem = [&](const std::string& key) -> uint64_t {
            auto pos = meminfo.find(key);
            if (pos == std::string::npos) return 0;
            auto val_start = pos + key.size();
            return std::stoul(meminfo.substr(val_start));
        };
        mem_total = parse_mem("MemTotal:");
        mem_available = parse_mem("MemAvailable:");
        uint64_t mem_used = mem_total > mem_available ? mem_total - mem_available : 0;

        auto load = read_file("/proc/loadavg");
        // Format: "1.5 2.3 3.1 ..."
        size_t space1 = load.find(' ');
        size_t space2 = load.find(' ', space1 + 1);

        oss << "{"
            << "\"timestamp_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() << ","
            << "\"mem_total\":" << mem_total << ","
            << "\"mem_used\":" << mem_used << ","
            << "\"load_1m\":" << (space1 != std::string::npos ? load.substr(0, space1) : "0") << ","
            << "\"load_5m\":" << (space2 != std::string::npos ? load.substr(space1 + 1, space2 - space1 - 1) : "0")
            << "}";
    }
    oss << "]";

    return ContractResult::ok(oss.str());
}

// ── handle_systemctl ────────────────────────────────────────────────
Result<ContractResult> ProcessContract::handle_systemctl(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto unit = map_str(input.arguments, "unit");
    if (!unit) return ContractResult::denied(unit.error().message);
    auto action = map_str(input.arguments, "action");
    if (!action) return ContractResult::denied(action.error().message);

    std::string cmd = "/bin/systemctl " + action.value() + " " + unit.value();

    auto result = handle_exec(ContractInput::with_map("exec", {
        {"command", cmd},
        {"capture_output", "true"},
        {"shell", "true"},
    }), ctx);

    return result;
}

// ── handle_service ──────────────────────────────────────────────────
Result<ContractResult> ProcessContract::handle_service(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto service = map_str(input.arguments, "service");
    if (!service) return ContractResult::denied(service.error().message);
    auto action = map_str(input.arguments, "action");
    if (!action) return ContractResult::denied(action.error().message);

    std::string cmd = "/usr/sbin/service " + service.value() + " " + action.value();

    auto result = handle_exec(ContractInput::with_map("exec", {
        {"command", cmd},
        {"capture_output", "true"},
        {"shell", "true"},
    }), ctx);

    return result;
}

// ── handle_info ─────────────────────────────────────────────────────
Result<ContractResult> ProcessContract::handle_info(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)input;
    (void)ctx;
    ContractResult result = ContractResult::ok();
    result.data = R"({
        "contract": "system.process",
        "version": "1.0.0",
        "methods": ["exec","kill","ps","top","systemctl","service","info"],
        "capabilities": ["filesystem"]
    })";
    return result;
}

} // namespace smo::runtime
