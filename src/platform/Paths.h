#pragma once

#include <string>

namespace cr {

// Returns the directory we should write user-specific files (save data, settings) to.
//   macOS  : ~/Library/Application Support/CellRoyale/
//   Linux  : $XDG_DATA_HOME/cell_royale/  or  ~/.local/share/cell_royale/
//   Windows: %APPDATA%/CellRoyale/
// Falls back to the current working directory if no env vars are usable. Always ends
// with a path separator. Creates the directory on first call if missing.
std::string userDataDir();

// Convenience: userDataDir() + filename.
std::string userDataPath(const std::string& filename);

} // namespace cr
