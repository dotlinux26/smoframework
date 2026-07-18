#include "file_contract.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

namespace fs = std::filesystem;
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

static uint32_t parse_mode(const std::string& s) {
    char* end = nullptr;
    return static_cast<uint32_t>(std::strtoul(s.c_str(), &end, 8));
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

static std::string perm_str(mode_t mode) {
    std::string p;
    p += (mode & S_IRUSR) ? 'r' : '-';
    p += (mode & S_IWUSR) ? 'w' : '-';
    p += (mode & S_IXUSR) ? 'x' : '-';
    p += (mode & S_IRGRP) ? 'r' : '-';
    p += (mode & S_IWGRP) ? 'w' : '-';
    p += (mode & S_IXGRP) ? 'x' : '-';
    p += (mode & S_IROTH) ? 'r' : '-';
    p += (mode & S_IWOTH) ? 'w' : '-';
    p += (mode & S_IXOTH) ? 'x' : '-';
    if (mode & S_ISUID) p[2] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID) p[5] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX) p[8] = (mode & S_IXOTH) ? 't' : 'T';
    return p;
}

static std::string owner_name(uid_t uid) {
    struct passwd* pw = getpwuid(uid);
    return pw ? pw->pw_name : std::to_string(uid);
}

static std::string group_name(gid_t gid) {
    struct group* gr = getgrgid(gid);
    return gr ? gr->gr_name : std::to_string(gid);
}

static std::string file_type_char(const fs::file_status& st) {
    if (fs::is_symlink(st)) return "l";
    if (fs::is_directory(st)) return "d";
    if (fs::is_fifo(st)) return "p";
    if (fs::is_socket(st)) return "s";
    if (fs::is_character_file(st)) return "c";
    if (fs::is_block_file(st)) return "b";
    return "-";
}

// ── Metadata ────────────────────────────────────────────────────────
ContractMetadata FileContract::default_metadata() {
    ContractMetadata meta;
    meta.id = "system.file";
    meta.name = "File Contract";
    meta.version = "1.0.0";
    meta.api_version = 1;
    meta.author = "SMO Core";
    meta.max_execution_time_ns = 30'000'000'000;
    meta.tags = {"system", "file"};
    meta.provides = {"list", "mkdir", "remove", "copy", "move", "stat",
                     "read", "write", "chmod", "chown", "symlink",
                     "readlink", "realpath", "info"};
    meta.entry_point = "system.file";
    meta.has_validate = true;
    return meta;
}

ContractCapabilities FileContract::required_capabilities() const {
    ContractCapabilities caps;
    caps.set(static_cast<size_t>(ContractCapability::Filesystem));
    return caps;
}

FileContract::FileContract()
    : NativeContract(default_metadata()) {}

Result<ContractResult> FileContract::execute(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    if (input.method == "list")     return handle_list(input, ctx);
    if (input.method == "mkdir")    return handle_mkdir(input, ctx);
    if (input.method == "remove")   return handle_remove(input, ctx);
    if (input.method == "copy")     return handle_copy(input, ctx);
    if (input.method == "move")     return handle_move(input, ctx);
    if (input.method == "stat")     return handle_stat(input, ctx);
    if (input.method == "read")     return handle_read(input, ctx);
    if (input.method == "write")    return handle_write(input, ctx);
    if (input.method == "chmod")    return handle_chmod(input, ctx);
    if (input.method == "chown")    return handle_chown(input, ctx);
    if (input.method == "symlink")  return handle_symlink(input, ctx);
    if (input.method == "readlink") return handle_readlink(input, ctx);
    if (input.method == "realpath") return handle_realpath(input, ctx);
    if (input.method == "info")     return handle_info(input, ctx);
    return ContractResult::denied("unknown method: " + input.method);
}

// ── handle_list ─────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_list(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    bool recursive = opt_bool(input.arguments, "recursive", false);
    bool show_hidden = opt_bool(input.arguments, "show_hidden", false);

    std::error_code ec;
    std::ostringstream oss;
    oss << "{\"path\":\"" << path.value() << "\",\"entries\":[";

    bool first = true;
    auto out_entry = [&](const fs::directory_entry& e) {
        std::string name = e.path().filename().string();
        if (!show_hidden && name.size() > 0 && name[0] == '.') return;
        if (!first) oss << ",";
        first = false;
        auto st = e.symlink_status(ec);
        oss << "{"
            << "\"name\":\"" << name << "\","
            << "\"path\":\"" << e.path().string() << "\","
            << "\"type\":\"" << file_type_char(st) << "\","
            << "\"size\":" << (fs::is_regular_file(e) ? fs::file_size(e, ec) : 0)
            << "}";
    };

    if (recursive) {
        for (auto& entry : fs::recursive_directory_iterator(path.value(), ec))
            out_entry(entry);
    } else {
        for (auto& entry : fs::directory_iterator(path.value(), ec))
            out_entry(entry);
    }
    oss << "]}";

    return ContractResult::ok(oss.str());
}

// ── handle_mkdir ────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_mkdir(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    bool parents = opt_bool(input.arguments, "parents", false);
    auto mode_opt = opt_str(input.arguments, "mode", "0755");
    mode_t mode = parse_mode(mode_opt) & 0777;

    std::error_code ec;
    if (parents)
        fs::create_directories(path.value(), ec);
    else
        fs::create_directory(path.value(), ec);

    if (ec) return ContractResult::denied(ec.message());

    chmod(path.value().c_str(), mode);

    ContractResult result = ContractResult::ok("directory created");
    result.metrics["path"] = ContextValue(path.value());
    return result;
}

// ── handle_remove ───────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_remove(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    bool recursive = opt_bool(input.arguments, "recursive", false);
    std::error_code ec;

    bool removed = false;
    if (recursive && fs::is_directory(path.value(), ec))
        removed = fs::remove_all(path.value(), ec) > 0;
    else
        removed = fs::remove(path.value(), ec);

    if (ec) return ContractResult::denied(ec.message());

    ContractResult result = ContractResult::ok(removed ? "removed" : "not found");
    result.metrics["path"] = ContextValue(path.value());
    result.metrics["removed"] = ContextValue(static_cast<int64_t>(removed));
    return result;
}

// ── handle_copy ─────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_copy(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto src = map_str(input.arguments, "src");
    if (!src) return ContractResult::denied(src.error().message);
    auto dst = map_str(input.arguments, "dst");
    if (!dst) return ContractResult::denied(dst.error().message);

    bool overwrite = opt_bool(input.arguments, "overwrite", false);
    bool recursive = opt_bool(input.arguments, "recursive", false);

    std::error_code ec;
    auto opts = overwrite ? fs::copy_options::overwrite_existing
                          : fs::copy_options::none;
    if (recursive)
        opts |= fs::copy_options::recursive;
    opts |= fs::copy_options::copy_symlinks;

    fs::copy(src.value(), dst.value(), opts, ec);
    if (ec) return ContractResult::denied(ec.message());

    ContractResult result = ContractResult::ok("copied");
    result.metrics["src"] = ContextValue(src.value());
    result.metrics["dst"] = ContextValue(dst.value());
    return result;
}

// ── handle_move ─────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_move(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto src = map_str(input.arguments, "src");
    if (!src) return ContractResult::denied(src.error().message);
    auto dst = map_str(input.arguments, "dst");
    if (!dst) return ContractResult::denied(dst.error().message);

    std::error_code ec;
    fs::rename(src.value(), dst.value(), ec);
    if (ec) return ContractResult::denied(ec.message());

    ContractResult result = ContractResult::ok("moved");
    result.metrics["src"] = ContextValue(src.value());
    result.metrics["dst"] = ContextValue(dst.value());
    return result;
}

// ── handle_stat ─────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_stat(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    std::error_code ec;
    struct stat st;
    if (::stat(path.value().c_str(), &st) != 0) {
        return ContractResult::denied("stat failed: " + std::string(std::strerror(errno)));
    }

    std::ostringstream oss;
    oss << "{"
        << "\"path\":\"" << path.value() << "\","
        << "\"exists\":true,"
        << "\"is_directory\":" << (S_ISDIR(st.st_mode) ? "true" : "false") << ","
        << "\"is_file\":" << (S_ISREG(st.st_mode) ? "true" : "false") << ","
        << "\"is_symlink\":" << (S_ISLNK(st.st_mode) ? "true" : "false") << ","
        << "\"size\":" << st.st_size << ","
        << "\"permissions\":\"" << perm_str(st.st_mode) << "\","
        << "\"owner\":\"" << owner_name(st.st_uid) << "\","
        << "\"group\":\"" << group_name(st.st_gid) << "\","
        << "\"modified_ns\":" << static_cast<int64_t>(st.st_mtim.tv_sec) * 1'000'000'000LL
                               + st.st_mtim.tv_nsec
        << "}";

    ContractResult result = ContractResult::ok(oss.str());
    result.metrics["size"] = ContextValue(static_cast<int64_t>(st.st_size));
    return result;
}

// ── handle_read ─────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_read(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    int64_t offset = opt_int(input.arguments, "offset", 0);
    int64_t length = opt_int(input.arguments, "length", -1);

    std::ifstream file(path.value(), std::ios::binary);
    if (!file) return ContractResult::denied("cannot open: " + path.value());

    file.seekg(0, std::ios::end);
    int64_t file_size = file.tellg();
    if (offset > file_size) offset = file_size;

    file.seekg(offset, std::ios::beg);
    int64_t to_read = (length < 0) ? (file_size - offset)
                                   : std::min(length, file_size - offset);

    Bytes data(static_cast<size_t>(to_read));
    file.read(reinterpret_cast<char*>(data.data()), to_read);
    int64_t actual = file.gcount();

    ContractResult result = ContractResult::ok();
    result.binary = std::move(data);
    result.metrics["path"] = ContextValue(path.value());
    result.metrics["offset"] = ContextValue(offset);
    result.metrics["length"] = ContextValue(actual);
    result.metrics["eof"] = ContextValue(static_cast<int64_t>((offset + actual) >= file_size));
    return result;
}

// ── handle_write ────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_write(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    std::ios::openmode mode = std::ios::binary;
    if (opt_bool(input.arguments, "append", false))
        mode |= std::ios::app;
    else
        mode |= std::ios::trunc;

    std::ofstream file(path.value(), mode);
    if (!file) return ContractResult::denied("cannot open for write: " + path.value());

    auto data_hex = opt_str(input.arguments, "data", "");
    std::string data_str = opt_str(input.arguments, "text", "");
    int64_t written = 0;

    if (!data_hex.empty()) {
        Bytes decoded;
        for (size_t i = 0; i + 1 < data_hex.size(); i += 2) {
            unsigned int b = 0;
            std::istringstream iss(data_hex.substr(i, 2));
            iss >> std::hex >> b;
            decoded.push_back(static_cast<uint8_t>(b));
        }
        file.write(reinterpret_cast<const char*>(decoded.data()), decoded.size());
        written = static_cast<int64_t>(decoded.size());
    } else if (!data_str.empty()) {
        file.write(data_str.data(), data_str.size());
        written = static_cast<int64_t>(data_str.size());
    }

    ContractResult result = ContractResult::ok("written");
    result.metrics["path"] = ContextValue(path.value());
    result.metrics["bytes_written"] = ContextValue(written);
    return result;
}

// ── handle_chmod ────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_chmod(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);
    auto mode_str = map_str(input.arguments, "mode");
    if (!mode_str) return ContractResult::denied(mode_str.error().message);

    mode_t mode = parse_mode(mode_str.value()) & 0777;
    if (::chmod(path.value().c_str(), mode) != 0) {
        return ContractResult::denied("chmod failed: " + std::string(std::strerror(errno)));
    }

    ContractResult result = ContractResult::ok("permissions changed");
    result.metrics["path"] = ContextValue(path.value());
    result.metrics["new_mode"] = ContextValue(static_cast<int64_t>(mode));
    return result;
}

// ── handle_chown ────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_chown(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    auto owner = opt_str(input.arguments, "owner", "");
    auto group = opt_str(input.arguments, "group", "");

    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);

    if (!owner.empty()) {
        struct passwd* pw = getpwnam(owner.c_str());
        if (!pw) {
            return ContractResult::denied("unknown owner: " + owner);
        }
        uid = pw->pw_uid;
    }
    if (!group.empty()) {
        struct group* gr = getgrnam(group.c_str());
        if (!gr) {
            return ContractResult::denied("unknown group: " + group);
        }
        gid = gr->gr_gid;
    }

    if (::chown(path.value().c_str(), uid, gid) != 0) {
        return ContractResult::denied("chown failed: " + std::string(std::strerror(errno)));
    }

    ContractResult result = ContractResult::ok("ownership changed");
    result.metrics["path"] = ContextValue(path.value());
    result.metrics["new_owner"] = ContextValue(owner);
    result.metrics["new_group"] = ContextValue(group);
    return result;
}

// ── handle_symlink ──────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_symlink(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto target = map_str(input.arguments, "target");
    if (!target) return ContractResult::denied(target.error().message);
    auto link_path = map_str(input.arguments, "link_path");
    if (!link_path) return ContractResult::denied(link_path.error().message);

    std::error_code ec;
    fs::create_symlink(target.value(), link_path.value(), ec);
    if (ec) return ContractResult::denied(ec.message());

    ContractResult result = ContractResult::ok("symlink created");
    result.metrics["link_path"] = ContextValue(link_path.value());
    result.metrics["target"] = ContextValue(target.value());
    return result;
}

// ── handle_readlink ─────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_readlink(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    std::error_code ec;
    auto target = fs::read_symlink(path.value(), ec);
    if (ec) return ContractResult::denied(ec.message());

    ContractResult result = ContractResult::ok(target.string());
    result.metrics["path"] = ContextValue(path.value());
    return result;
}

// ── handle_realpath ─────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_realpath(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;
    auto path = map_str(input.arguments, "path");
    if (!path) return ContractResult::denied(path.error().message);

    std::error_code ec;
    auto resolved = fs::canonical(path.value(), ec);
    if (ec) return ContractResult::denied(ec.message());

    ContractResult result = ContractResult::ok(resolved.string());
    result.metrics["path"] = ContextValue(path.value());
    return result;
}

// ── handle_info ─────────────────────────────────────────────────────
Result<ContractResult> FileContract::handle_info(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)input;
    (void)ctx;
    ContractResult result = ContractResult::ok();
    result.data = R"({
        "contract": "system.file",
        "version": "1.0.0",
        "methods": ["list","mkdir","remove","copy","move","stat",
                     "read","write","chmod","chown","symlink",
                     "readlink","realpath","info"],
        "capabilities": ["filesystem"]
    })";
    return result;
}

} // namespace smo::runtime
