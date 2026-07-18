#include "fs_contracts.hpp"

#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <sys/xattr.h>
#include <cstring>
#include <vector>

namespace smo::contract::native {

// ===========================================================================
// Filesystem Contract Implementations
// ===========================================================================

// Helper functions
namespace {
    std::string permission_bits(mode_t mode) {
        std::string perm;
        perm += (mode & S_IRUSR) ? 'r' : '-';
        perm += (mode & S_IWUSR) ? 'w' : '-';
        perm += (mode & S_IXUSR) ? 'x' : '-';
        perm += (mode & S_IRGRP) ? 'r' : '-';
        perm += (mode & S_IWGRP) ? 'w' : '-';
        perm += (mode & S_IXGRP) ? 'x' : '-';
        perm += (mode & S_IROTH) ? 'r' : '-';
        perm += (mode & S_IWOTH) ? 'w' : '-';
        perm += (mode & S_IXOTH) ? 'x' : '-';
        return perm;
    }

    std::string get_owner(uid_t uid) {
        struct passwd* pwd = getpwuid(uid);
        return pwd ? pwd->pw_name : std::to_string(uid);
    }

    std::string get_group(gid_t gid) {
        struct group* grp = getgrgid(gid);
        return grp ? grp->gr_name : std::to_string(gid);
    }

    std::string read_symlink(const std::string& path) {
        char buf[4096];
        ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
        if (len >= 0) {
            buf[len] = '\0';
            return std::string(buf);
        }
        return "";
    }

    std::string get_permissions(mode_t mode) {
        std::string perm = permission_bits(mode & 0777);
        if (mode & S_ISUID) perm[2] = (mode & S_IXUSR) ? 's' : 'S';
        if (mode & S_ISGID) perm[5] = (mode & S_IXGRP) ? 's' : 'S';
        if (mode & S_ISVTX) perm[8] = (mode & S_IXOTH) ? 't' : 'T';
        return perm;
    }
}

namespace smo::contract::native {

// ===========================================================================
// Filesystem List Contract
// ===========================================================================

Result<FSListResponse> fs_list(const FSListRequest& req) {
    FSListResponse resp;
    resp.path = req.path;

    std::error_code ec;
    std::filesystem::path path(req.path);
    
    if (!std::filesystem::exists(path, ec)) {
        return SMO_ERR(Contract, 200, Error, NoRetry, None, "Path does not exist: " + req.path);
    }

    if (req.recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(req.path, ec)) {
            if (ec) break;
            
            FSListResponse::Entry entry;
            entry.name = entry.path().filename().string();
            entry.path = entry.path().string();
            entry.is_directory = entry.is_directory(ec);
            entry.is_symlink = entry.is_symlink(ec);
            entry.modified_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::filesystem::last_write_time(entry, ec).time_since_epoch()).count();
            
            struct stat st;
            if (lstat(entry.path().c_str(), &st) == 0) {
                entry.size = st.st_size;
                entry.permissions = get_permissions(st.st_mode);
                entry.owner = get_owner(st.st_uid);
                entry.group = get_group(st.st_gid);
                
                if (entry.is_symlink) {
                    entry.symlink_target = read_symlink(entry.path().string());
                }
            }
            
            resp.entries.push_back(std::move(entry));
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(req.path, ec)) {
            if (ec) break;
            
            if (!req.show_hidden && entry.path().filename().string().starts_with('.')) {
                continue;
            }
            
            FSListResponse::Entry entry;
            entry.name = entry.path().filename().string();
            entry.path = entry.path().string();
            entry.is_directory = entry.is_directory(ec);
            entry.is_symlink = entry.is_symlink(ec);
            entry.modified_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::filesystem::last_write_time(entry, ec).time_since_epoch()).count();
            
            struct stat st;
            if (lstat(entry.path().c_str(), &st) == 0) {
                entry.size = st.st_size;
                entry.permissions = get_permissions(st.st_mode);
                entry.owner = get_owner(st.st_uid);
                entry.group = get_group(st.st_gid);
                
                if (entry.is_symlink) {
                    entry.symlink_target = read_symlink(entry.path().string());
                }
            }
            
            resp.entries.push_back(std::move(entry));
        }
    }
    
    return resp;
}

Result<FSMkdirResponse> fs_mkdir(const FSMkdirRequest& req) {
    FSMkdirResponse resp;
    resp.path = req.path;
    
    std::error_code ec;
    if (req.parents) {
        std::filesystem::create_directories(req.path, ec);
    } else {
        std::filesystem::create_directory(req.path, ec);
    }
    
    if (ec) {
        return SMO_ERR(Contract, 201, Error, NoRetry, None, "Failed to create directory: " + ec.message());
    }
    
    // Set permissions
    chmod(req.path.c_str(), req.mode);
    resp.created = true;
    return resp;
}

Result<FSRemoveResponse> fs_remove(const FSRemoveRequest& req) {
    FSRemoveResponse resp;
    resp.path = req.path;
    
    std::error_code ec;
    if (req.recursive) {
        std::filesystem::remove_all(req.path, ec);
    } else {
        std::filesystem::remove(req.path, ec);
    }
    
    if (ec && !req.force) {
        return SMO_ERR(Contract, 202, Error, NoRetry, None, "Failed to remove: " + ec.message());
    }
    
    resp.removed = true;
    return resp;
}

Result<FSCopyResponse> fs_copy(const FSCopyRequest& req) {
    FSCopyResponse resp;
    resp.src = req.src;
    resp.dst = req.dst;
    
    std::error_code ec;
    if (req.recursive) {
        std::filesystem::copy(req.src, req.dst, 
            req.overwrite ? std::filesystem::copy_options::overwrite_existing 
                          : std::filesystem::copy_options::none
                          | (req.preserve ? std::filesystem::copy_options::copy_symlinks 
                                          : std::filesystem::copy_options::none), ec);
    } else {
        std::filesystem::copy_file(req.src, req.dst,
            req.overwrite ? std::filesystem::copy_options::overwrite_existing
                          : std::filesystem::copy_options::none, ec);
    }
    
    if (ec) {
        return SMO_ERR(Contract, 203, Error, NoRetry, None, "Failed to copy: " + ec.message());
    }
    
    resp.copied = true;
    struct stat st;
    if (stat(req.dst.c_str(), &st) == 0) {
        resp.bytes_copied = st.st_size;
    }
    return resp;
}

Result<FSMoveResponse> fs_move(const FSMoveRequest& req) {
    FSMoveResponse resp;
    resp.src = req.src;
    resp.dst = req.dst;
    
    std::error_code ec;
    std::filesystem::rename(req.src, req.dst, ec);
    
    if (ec) {
        if (ec == std::errc::file_exists && !req.overwrite) {
            return SMO_ERR(Contract, 204, Error, NoRetry, None, "Destination exists, use --overwrite");
        }
        return SMO_ERR(Contract, 205, Error, NoRetry, None, "Failed to move: " + ec.message());
    }
    
    resp.moved = true;
    return resp;
}

Result<FSStatResponse> fs_stat(const FSStatRequest& req) {
    FSStatResponse resp;
    resp.path = req.path;
    
    struct stat st;
    int rc = req.follow_symlinks ? stat(req.path.c_str(), &st) : lstat(req.path.c_str(), &st);
    
    if (rc != 0) {
        resp.exists = false;
        return resp;
    }
    
    resp.exists = true;
    resp.is_directory = S_ISDIR(st.st_mode);
    resp.is_file = S_ISREG(st.st_mode);
    resp.is_symlink = S_ISLNK(st.st_mode);
    resp.size = st.st_size;
    resp.modified_ns = static_cast<int64_t>(st.st_mtime) * 1'000'000'000LL;
    resp.accessed_ns = static_cast<int64_t>(st.st_atime) * 1'000'000'000LL;
    resp.created_ns = static_cast<int64_t>(st.st_ctime) * 1'000'000'000LL;
    resp.permissions = get_permissions(st.st_mode);
    resp.owner = get_owner(st.st_uid);
    resp.group = get_group(st.st_gid);
    
    if (S_ISLNK(st.st_mode)) {
        resp.symlink_target = read_symlink(req.path);
    }
    
    return resp;
}

Result<FSReadResponse> fs_read(const FSReadRequest& req) {
    FSReadResponse resp;
    resp.path = req.path;
    
    int fd = open(req.path.c_str(), O_RDONLY);
    if (fd < 0) {
        return SMO_ERR(Contract, 206, Error, NoRetry, None, "Failed to open file: " + std::string(strerror(errno)));
    }
    
    off_t offset = req.offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        close(fd);
        return SMO_ERR(Contract, 207, Error, NoRetry, None, "Failed to seek");
    }
    
    size_t to_read = req.length > 0 ? req.length : 1024 * 1024;  // Default 1MB
    std::vector<uint8_t> buffer(to_read);
    
    ssize_t n = read(fd, buffer.data(), to_read);
    close(fd);
    
    if (n < 0) {
        return SMO_ERR(Contract, 208, Error, NoRetry, None, "Failed to read file");
    }
    
    buffer.resize(n);
    resp.data = std::move(buffer);
    resp.offset = req.offset;
    resp.length = n;
    resp.eof = (n < static_cast<ssize_t>(to_read));
    
    return resp;
}

Result<FSWriteResponse> fs_write(const FSWriteRequest& req) {
    FSWriteResponse resp;
    resp.path = req.path;
    
    int flags = O_WRONLY | O_CREAT | (req.append ? O_APPEND : O_TRUNC);
    int fd = open(req.path.c_str(), flags, req.mode);
    if (fd < 0) {
        return SMO_ERR(Contract, 209, Error, NoRetry, None, "Failed to open file for writing");
    }
    
    if (req.offset > 0 && lseek(fd, req.offset, SEEK_SET) == -1) {
        close(fd);
        return SMO_ERR(Contract, 210, Error, NoRetry, None, "Failed to seek");
    }
    
    ssize_t written = write(fd, req.data.data(), req.data.size());
    close(fd);
    
    if (written < 0) {
        return SMO_ERR(Contract, 211, Error, NoRetry, None, "Failed to write file");
    }
    
    resp.bytes_written = written;
    resp.offset = req.offset;
    return resp;
}

Result<FSChmodResponse> fs_chmod(const FSChmodRequest& req) {
    FSChmodResponse resp;
    resp.path = req.path;
    
    struct stat st;
    if (stat(req.path.c_str(), &st) == 0) {
        resp.old_mode = st.st_mode & 0777;
    }
    
    int rc = chmod(req.path.c_str(), req.mode);
    if (rc != 0) {
        return SMO_ERR(Contract, 212, Error, NoRetry, None, "Failed to chmod");
    }
    
    if (req.recursive) {
        // Recursive chmod
        for (const auto& entry : std::filesystem::recursive_directory_iterator(req.path)) {
            chmod(entry.path().c_str(), req.mode);
        }
    }
    
    if (stat(req.path.c_str(), &st) == 0) {
        resp.new_mode = st.st_mode & 0777;
    }
    
    return resp;
}

Result<FSChownResponse> fs_chown(const FSChownRequest& req) {
    FSChownResponse resp;
    resp.path = req.path;
    
    struct stat st;
    if (stat(req.path.c_str(), &st) == 0) {
        resp.old_owner = get_owner(st.st_uid);
        resp.old_group = get_group(st.st_gid);
    }
    
    uid_t uid = req.owner.empty() ? -1 : getpwnam(req.owner.c_str())->pw_uid;
    gid_t gid = req.group.empty() ? -1 : getgrnam(req.group.c_str())->gr_gid;
    
    int rc = chown(req.path.c_str(), uid, gid);
    if (rc != 0) {
        return SMO_ERR(Contract, 213, Error, NoRetry, None, "Failed to chown");
    }
    
    if (req.recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(req.path)) {
            chown(entry.path().c_str(), uid, gid);
        }
    }
    
    if (stat(req.path.c_str(), &st) == 0) {
        resp.new_owner = get_owner(st.st_uid);
        resp.new_group = get_group(st.st_gid);
    }
    
    return resp;
}

Result<FSSymlinkResponse> fs_symlink(const FSSymlinkRequest& req) {
    FSSymlinkResponse resp;
    resp.link_path = req.link_path;
    resp.target = req.target;
    
    if (symlink(req.target.c_str(), req.link_path.c_str()) != 0) {
        return SMO_ERR(Contract, 214, Error, NoRetry, None, "Failed to create symlink");
    }
    
    resp.created = true;
    return resp;
}

Result<FSReadlinkResponse> fs_readlink(const FSReadlinkRequest& req) {
    FSReadlinkResponse resp;
    resp.path = req.path;
    
    resp.target = read_symlink(req.path);
    if (resp.target.empty()) {
        return SMO_ERR(Contract, 215, Error, NoRetry, None, "Not a symlink or read failed");
    }
    return resp;
}

Result<FSRealpathResponse> fs_realpath(const FSRealpathRequest& req) {
    FSRealpathResponse resp;
    resp.path = req.path;
    
    char resolved[PATH_MAX];
    if (realpath(req.path.c_str(), resolved)) {
        resp.resolved_path = resolved;
    } else {
        return SMO_ERR(Contract, 216, Error, NoRetry, None, "Failed to resolve path");
    }
    return resp;
}

} // namespace smo::contract::native