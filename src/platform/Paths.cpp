#include "Paths.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace cr {

namespace {

#if defined(_WIN32)
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

std::string ensureTrailingSep(std::string s) {
    if (s.empty()) return s;
    if (s.back() != '/' && s.back() != '\\') {
        s.push_back(kSep);
    }
    return s;
}

std::string computeUserDataDir() {
#if defined(_WIN32)
    // %APPDATA% is "C:\Users\Name\AppData\Roaming"; standard practice on Windows.
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
        return ensureTrailingSep(std::string(appdata)) + "CellRoyale" + kSep;
    }
    return "."; // last-resort fallback: cwd

#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) {
        return ensureTrailingSep(std::string(home))
             + "Library/Application Support/CellRoyale/";
    }
    return "./";

#else // Linux / other unix
    // Prefer XDG_DATA_HOME; fall back to ~/.local/share (the XDG default).
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return ensureTrailingSep(std::string(xdg)) + "cell_royale/";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return ensureTrailingSep(std::string(home)) + ".local/share/cell_royale/";
    }
    return "./";
#endif
}

} // namespace

std::string userDataDir() {
    // Cache the result -- repeated env lookups + mkdir calls are cheap but pointless.
    static const std::string s_dir = [] {
        std::string dir = computeUserDataDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        // If create_directories failed (e.g. read-only home), the caller's fopen will
        // fail downstream; we don't fall back here because the alternative (cwd) hides
        // configuration bugs.
        return dir;
    }();
    return s_dir;
}

std::string userDataPath(const std::string& filename) {
    return userDataDir() + filename;
}

} // namespace cr
