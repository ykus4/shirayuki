#ifndef SHIRAYUKI_SESSION_HPP
#define SHIRAYUKI_SESSION_HPP

#include "Freeze.hpp"
#include "PointerScan.hpp"
#include "ShirayukiMemory.hpp"
#include <string>

namespace Shirayuki {

// Bookmark: a named address with associated metadata
struct Bookmark {
    std::string name;
    uintptr_t address;
    ValueType type;
    std::string notes;
    std::string group;
};

// Session: everything persisted between launches
struct Session {
    std::string name;
    std::string targetBundle; // bundle ID of target app
    std::vector<Bookmark> bookmarks;
    std::vector<FreezeEntry> freezeEntries;
    std::vector<PointerChain> pointerChains;

    // Patch records (address + patch hex + original hex)
    struct PatchRecord {
        uintptr_t address;
        std::string patchHex;
        std::string originalHex;
        std::string label;
        bool autoApply; // apply on load
    };
    std::vector<PatchRecord> patches;

    // Search history
    std::vector<std::string> searchHistory;
};

namespace SessionManager {
// Save session to JSON file
bool save(const Session &session, const std::string &filePath);

// Load session from JSON file
bool load(const std::string &filePath, Session &outSession);

// Get default save directory (app Documents)
std::string defaultDirectory();

// List available session files
std::vector<std::string> listSessions();

// Delete a session file
bool deleteSession(const std::string &filePath);

// Auto-save path for current target
std::string autoSavePath(const std::string &bundleId);
} // namespace SessionManager

} // namespace Shirayuki

#endif // SHIRAYUKI_SESSION_HPP
