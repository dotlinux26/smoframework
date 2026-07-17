#pragma once

#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "core/errors/error.hpp"

namespace smo {
namespace mesh {

// ---------------------------------------------------------------------------
// SMO_HOME: top-level data directory
// Priority: SMO_HOME env var > ~/.smo
// ---------------------------------------------------------------------------
inline std::string smo_home() {
    const char* env = std::getenv("SMO_HOME");
    if (env && env[0]) return env;
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.smo" : "/tmp/.smo";
}

// ---------------------------------------------------------------------------
// Resolve a mesh directory from a mesh name (default layout)
//   smo_home()/meshes/<name>/
// ---------------------------------------------------------------------------
inline std::string mesh_dir_from_name(const std::string& name) {
    return smo_home() + "/meshes/" + name;
}

// ---------------------------------------------------------------------------
// Read the current mesh name from context.json
// ---------------------------------------------------------------------------
inline Result<std::string> read_current_mesh() {
    std::string path = smo_home() + "/context.json";
    std::ifstream f(path);
    if (!f) {
        return SMO_ERR_STORAGE(404, Info, NoRetry, None,
                               "context.json not found: " + path);
    }
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    auto pos = json.find("\"current_mesh\"");
    if (pos == std::string::npos) return std::string();
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return std::string();
    auto start = json.find('"', colon + 1);
    if (start == std::string::npos) return std::string();
    auto end = json.find('"', start + 1);
    if (end == std::string::npos) return std::string();
    return json.substr(start + 1, end - start - 1);
}

// ---------------------------------------------------------------------------
// Write the current mesh name to context.json
// ---------------------------------------------------------------------------
inline Result<void> write_current_mesh(const std::string& name) {
    std::string dir = smo_home();
    std::filesystem::create_directories(dir);
    std::string path = dir + "/context.json";
    std::ofstream f(path);
    if (!f) {
        return SMO_ERR_STORAGE(500, Error, NoRetry, None,
                               "cannot write context.json: " + path);
    }
    f << "{\n";
    f << "  \"current_mesh\": \"" << name << "\"\n";
    f << "}\n";
    return {};
}

// ---------------------------------------------------------------------------
// Resolve a mesh directory given optional overrides.
//
// Resolution order:
//   1. explicit_mesh_dir (--mesh-dir) — highest priority
//   2. explicit_mesh_name (--mesh <name>) — resolves via mesh_dir_from_name
//   3. current_mesh_name from context.json
//   4. Error if none found
//
// Returns (mesh_name, mesh_dir).
// ---------------------------------------------------------------------------
struct MeshResolution {
    std::string name;
    std::string dir;
};

inline Result<MeshResolution> resolve_mesh(
    const std::string& explicit_mesh_dir,
    const std::string& explicit_mesh_name)
{
    // Priority 1: --mesh-dir
    if (!explicit_mesh_dir.empty()) {
        std::string d = explicit_mesh_dir;
        // Extract name from directory basename if possible
        std::string n = std::filesystem::path(d).filename().string();
        return MeshResolution{n, d};
    }

    // Priority 2: --mesh <name>
    if (!explicit_mesh_name.empty()) {
        std::string d = mesh_dir_from_name(explicit_mesh_name);
        return MeshResolution{explicit_mesh_name, d};
    }

    // Priority 3: current mesh from context
    auto ctx = read_current_mesh();
    if (ctx && !ctx.value().empty()) {
        std::string n = ctx.value();
        std::string d = mesh_dir_from_name(n);
        return MeshResolution{n, d};
    }

    return SMO_ERR_STORAGE(404, Info, NoRetry, None,
        "no mesh specified. Use --mesh <name>, --mesh-dir <path>, "
        "or set a current mesh via 'mesh use <name>'");
}

// ---------------------------------------------------------------------------
// Resolve a mesh directory with fallback to interactive help
// (returns error message guiding user when no mesh is found)
// ---------------------------------------------------------------------------
inline Result<MeshResolution> require_mesh(
    const std::string& explicit_mesh_dir,
    const std::string& explicit_mesh_name)
{
    auto result = resolve_mesh(explicit_mesh_dir, explicit_mesh_name);
    if (result) return result;

    // Build helpful error message listing available meshes
    std::string msg = "No mesh specified.\n";
    msg += "  Use --mesh <name> to select by name\n";
    msg += "  Use --mesh-dir <path> for custom directory\n";
    msg += "  Or set a default mesh: smo mesh use <name>\n";

    std::string meshes_dir = smo_home() + "/meshes";
    if (std::filesystem::is_directory(meshes_dir)) {
        msg += "\nAvailable meshes:\n";
        for (const auto& entry : std::filesystem::directory_iterator(meshes_dir)) {
            if (entry.is_directory()) {
                msg += "    " + entry.path().filename().string() + "\n";
            }
        }
    }

    return SMO_ERR_STORAGE(404, Info, NoRetry, None, msg);
}

} // namespace mesh
} // namespace smo
