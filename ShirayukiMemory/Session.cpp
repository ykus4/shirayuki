// Session persistence.
//
// Pure C++, as ShirayukiMemory/'s layering contract requires. The previous
// Session.mm wrote JSON with a hand-rolled serialiser and read it back with
// NSJSONSerialization, so the write and read paths used different technologies
// for one format — and were not round-trip compatible, since the writer left
// `\r`, `\b`, `\f` and control characters unescaped. Both directions now go
// through Json.
#include "Session.hpp"

#include "Json.hpp"

#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace Shirayuki {
namespace {

/// Addresses are written as hex strings, not numbers: JSON numbers are doubles,
/// which cannot represent an address above 2^53 exactly.
std::string addressToHex(uintptr_t address) {
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase << static_cast<unsigned long long>(address);
    return os.str();
}

Json::Value bytesToHexValue(const std::vector<uint8_t> &bytes) {
    return Json::Value(Hex::fromBytes(bytes));
}

std::vector<uint8_t> hexValueToBytes(const Json::Value &value) {
    return Hex::toBytes(value.asString());
}

Json::Value encode(const Bookmark &bookmark) {
    Json::Object o;
    o["name"] = Json::Value(bookmark.name);
    o["address"] = Json::Value(addressToHex(bookmark.address));
    o["type"] = Json::Value(ValueFormat::toTag(bookmark.type));
    o["notes"] = Json::Value(bookmark.notes);
    o["group"] = Json::Value(bookmark.group);
    return Json::Value(std::move(o));
}

Bookmark decodeBookmark(const Json::Value &v) {
    Bookmark b;
    b.name = v["name"].asString();
    b.address = static_cast<uintptr_t>(v["address"].asUInt64());
    b.type = ValueFormat::fromTag(v["type"].asString("int32"));
    b.notes = v["notes"].asString();
    b.group = v["group"].asString();
    return b;
}

Json::Value encode(const FreezeEntry &entry) {
    Json::Object o;
    o["address"] = Json::Value(addressToHex(entry.address));
    // Type tags, not the enum's integer value: reordering the enum would
    // otherwise silently reinterpret every saved entry.
    o["type"] = Json::Value(ValueFormat::toTag(entry.type));
    o["value"] = bytesToHexValue(entry.value);
    o["label"] = Json::Value(entry.label);
    o["active"] = Json::Value(entry.active);
    o["autoIncrement"] = Json::Value(entry.autoIncrement);
    o["incrementStep"] = Json::Value(static_cast<long long>(entry.incrementStep));
    return Json::Value(std::move(o));
}

FreezeEntry decodeFreezeEntry(const Json::Value &v) {
    FreezeEntry e{};
    e.id = 0; // assigned by FreezeManager on re-add
    e.address = static_cast<uintptr_t>(v["address"].asUInt64());
    e.type = ValueFormat::fromTag(v["type"].asString("int32"));
    e.value = hexValueToBytes(v["value"]);
    e.label = v["label"].asString();
    e.active = v["active"].asBool(true);
    e.autoIncrement = v["autoIncrement"].asBool(false);
    e.incrementStep = static_cast<int64_t>(v["incrementStep"].asNumber(1));
    return e;
}

Json::Value encode(const Session::PatchRecord &patch) {
    Json::Object o;
    o["address"] = Json::Value(addressToHex(patch.address));
    o["patchHex"] = Json::Value(patch.patchHex);
    o["originalHex"] = Json::Value(patch.originalHex);
    o["label"] = Json::Value(patch.label);
    o["autoApply"] = Json::Value(patch.autoApply);
    return Json::Value(std::move(o));
}

Session::PatchRecord decodePatchRecord(const Json::Value &v) {
    Session::PatchRecord p{};
    p.address = static_cast<uintptr_t>(v["address"].asUInt64());
    p.patchHex = v["patchHex"].asString();
    p.originalHex = v["originalHex"].asString();
    p.label = v["label"].asString();
    p.autoApply = v["autoApply"].asBool(false);
    return p;
}

Json::Value encode(const PointerChain &chain) {
    Json::Object o;
    o["moduleName"] = Json::Value(chain.moduleName);
    o["moduleOffset"] = Json::Value(addressToHex(chain.moduleOffset));
    // Chain offsets are signed and small, so plain JSON numbers are exact here —
    // unlike addresses, which need hex strings to survive a double.
    Json::Array offsets;
    for (int64_t offset : chain.offsets)
        offsets.push_back(Json::Value(static_cast<long long>(offset)));
    o["offsets"] = Json::Value(std::move(offsets));
    return Json::Value(std::move(o));
}

PointerChain decodePointerChain(const Json::Value &v) {
    PointerChain c{};
    c.moduleName = v["moduleName"].asString();
    c.moduleOffset = static_cast<uintptr_t>(v["moduleOffset"].asUInt64());
    for (const auto &offset : v["offsets"].asArray())
        c.offsets.push_back(static_cast<int64_t>(offset.asNumber()));
    return c;
}

/// Create `path` and any missing parents. `save` used to write straight to a
/// path whose directory was only created as a side effect of calling
/// defaultDirectory(), so saving to any other location silently failed.
bool ensureDirectory(const std::string &path) {
    if (path.empty())
        return false;

    std::string partial;
    partial.reserve(path.size());
    for (size_t i = 0; i < path.size(); i++) {
        partial += path[i];
        const bool last = (i + 1 == path.size());
        if (path[i] == '/' || last) {
            if (partial == "/" || partial.empty())
                continue;
            struct stat st{};
            if (stat(partial.c_str(), &st) == 0) {
                if (!S_ISDIR(st.st_mode))
                    return false;
            } else if (mkdir(partial.c_str(), 0755) != 0) {
                return false;
            }
        }
    }
    return true;
}

std::string parentDirectory(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return "";
    return path.substr(0, slash);
}

} // namespace

bool SessionManager::save(const Session &session, const std::string &filePath) {
    if (filePath.empty())
        return false;

    Json::Object root;
    root["name"] = Json::Value(session.name);
    root["targetBundle"] = Json::Value(session.targetBundle);
    // Format version, so a future change can migrate instead of misreading.
    root["version"] = Json::Value(1);

    Json::Array bookmarks;
    for (const auto &b : session.bookmarks)
        bookmarks.push_back(encode(b));
    root["bookmarks"] = Json::Value(std::move(bookmarks));

    Json::Array freezes;
    for (const auto &f : session.freezeEntries)
        freezes.push_back(encode(f));
    root["freezeEntries"] = Json::Value(std::move(freezes));

    Json::Array patches;
    for (const auto &p : session.patches)
        patches.push_back(encode(p));
    root["patches"] = Json::Value(std::move(patches));

    Json::Array chains;
    for (const auto &c : session.pointerChains)
        chains.push_back(encode(c));
    root["pointerChains"] = Json::Value(std::move(chains));

    Json::Array history;
    for (const auto &h : session.searchHistory)
        history.push_back(Json::Value(h));
    root["searchHistory"] = Json::Value(std::move(history));

    const std::string parent = parentDirectory(filePath);
    if (!parent.empty() && !ensureDirectory(parent))
        return false;

    // Write to a temporary file and rename, so an interrupted save cannot leave
    // a truncated session behind in place of a good one.
    const std::string tempPath = filePath + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file << Json::Value(std::move(root)).serialize(true) << "\n";
        if (!file.good())
            return false;
    }
    if (::rename(tempPath.c_str(), filePath.c_str()) != 0) {
        ::unlink(tempPath.c_str());
        return false;
    }
    return true;
}

bool SessionManager::load(const std::string &filePath, Session &outSession) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();

    Json::Value root;
    std::string error;
    if (!Json::parse(buffer.str(), root, &error))
        return false;
    if (root.type() != Json::Type::Object)
        return false;

    Session session;
    session.name = root["name"].asString();
    session.targetBundle = root["targetBundle"].asString();

    for (const auto &v : root["bookmarks"].asArray())
        session.bookmarks.push_back(decodeBookmark(v));
    for (const auto &v : root["freezeEntries"].asArray())
        session.freezeEntries.push_back(decodeFreezeEntry(v));
    for (const auto &v : root["patches"].asArray())
        session.patches.push_back(decodePatchRecord(v));
    for (const auto &v : root["pointerChains"].asArray())
        session.pointerChains.push_back(decodePointerChain(v));
    for (const auto &v : root["searchHistory"].asArray())
        session.searchHistory.push_back(v.asString());

    outSession = std::move(session);
    return true;
}

std::string SessionManager::defaultDirectory() {
    // On iOS, HOME is the app's container, so this resolves to the same
    // Documents directory NSSearchPathForDirectoriesInDomains returned — without
    // pulling Foundation into the pure C++ core.
    const char *home = std::getenv("HOME");
    const std::string base = (home && *home) ? std::string(home) : std::string("/tmp");
    const std::string dir = base + "/Documents/Shirayuki";
    ensureDirectory(dir);
    return dir;
}

std::vector<std::string> SessionManager::listSessions() {
    std::vector<std::string> files;
    const std::string dir = defaultDirectory();

    DIR *handle = opendir(dir.c_str());
    if (!handle)
        return files;

    while (struct dirent *entry = readdir(handle)) {
        const std::string name(entry->d_name);
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
            files.push_back(dir + "/" + name);
    }
    closedir(handle);
    return files;
}

bool SessionManager::deleteSession(const std::string &filePath) {
    return ::unlink(filePath.c_str()) == 0;
}

std::string SessionManager::autoSavePath(const std::string &bundleId) {
    return defaultDirectory() + "/" + bundleId + "_autosave.json";
}

} // namespace Shirayuki
