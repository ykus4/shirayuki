#include "Session.hpp"
#import <Foundation/Foundation.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace Shirayuki {

// Minimal JSON serialization (no external deps)
namespace {

std::string escapeJson(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
        }
    }
    return out;
}

std::string toJson(const Bookmark &b) {
    std::ostringstream ss;
    ss << "{\"name\":\"" << escapeJson(b.name) << "\","
       << "\"address\":" << b.address << ","
       << "\"type\":" << (int)b.type << ","
       << "\"notes\":\"" << escapeJson(b.notes) << "\","
       << "\"group\":\"" << escapeJson(b.group) << "\"}";
    return ss.str();
}

std::string toJson(const FreezeEntry &f) {
    std::ostringstream ss;
    ss << "{\"id\":" << f.id << ","
       << "\"address\":" << f.address << ","
       << "\"type\":" << (int)f.type << ","
       << "\"label\":\"" << escapeJson(f.label) << "\","
       << "\"value\":\"" << Hex::fromBytes(f.value) << "\","
       << "\"active\":" << (f.active ? "true" : "false") << "}";
    return ss.str();
}

std::string toJson(const PointerChain &pc) {
    std::ostringstream ss;
    ss << "{\"module\":\"" << escapeJson(pc.moduleName) << "\","
       << "\"offset\":" << pc.moduleOffset << ","
       << "\"offsets\":[";
    for (size_t i = 0; i < pc.offsets.size(); i++) {
        if (i > 0)
            ss << ",";
        ss << pc.offsets[i];
    }
    ss << "]}";
    return ss.str();
}

std::string toJson(const Session::PatchRecord &p) {
    std::ostringstream ss;
    ss << "{\"address\":" << p.address << ","
       << "\"patchHex\":\"" << escapeJson(p.patchHex) << "\","
       << "\"originalHex\":\"" << escapeJson(p.originalHex) << "\","
       << "\"label\":\"" << escapeJson(p.label) << "\","
       << "\"autoApply\":" << (p.autoApply ? "true" : "false") << "}";
    return ss.str();
}

} // namespace

bool SessionManager::save(const Session &session, const std::string &filePath) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"name\":\"" << escapeJson(session.name) << "\",\n";
    ss << "  \"targetBundle\":\"" << escapeJson(session.targetBundle) << "\",\n";

    // Bookmarks
    ss << "  \"bookmarks\":[";
    for (size_t i = 0; i < session.bookmarks.size(); i++) {
        if (i > 0)
            ss << ",";
        ss << "\n    " << toJson(session.bookmarks[i]);
    }
    ss << "\n  ],\n";

    // Freeze entries
    ss << "  \"freezeEntries\":[";
    for (size_t i = 0; i < session.freezeEntries.size(); i++) {
        if (i > 0)
            ss << ",";
        ss << "\n    " << toJson(session.freezeEntries[i]);
    }
    ss << "\n  ],\n";

    // Pointer chains
    ss << "  \"pointerChains\":[";
    for (size_t i = 0; i < session.pointerChains.size(); i++) {
        if (i > 0)
            ss << ",";
        ss << "\n    " << toJson(session.pointerChains[i]);
    }
    ss << "\n  ],\n";

    // Patches
    ss << "  \"patches\":[";
    for (size_t i = 0; i < session.patches.size(); i++) {
        if (i > 0)
            ss << ",";
        ss << "\n    " << toJson(session.patches[i]);
    }
    ss << "\n  ],\n";

    // Search history
    ss << "  \"searchHistory\":[";
    for (size_t i = 0; i < session.searchHistory.size(); i++) {
        if (i > 0)
            ss << ",";
        ss << "\"" << escapeJson(session.searchHistory[i]) << "\"";
    }
    ss << "]\n";

    ss << "}\n";

    std::ofstream file(filePath);
    if (!file.is_open())
        return false;
    file << ss.str();
    return true;
}

bool SessionManager::load(const std::string &filePath, Session &outSession) {
    // Use NSJSONSerialization for parsing (available on iOS)
    @autoreleasepool {
        NSData *data =
            [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:filePath.c_str()]];
        if (!data)
            return false;

        NSError *error = nil;
        NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
        if (!json || error)
            return false;

        outSession.name = [json[@"name"] UTF8String] ?: "";
        outSession.targetBundle = [json[@"targetBundle"] UTF8String] ?: "";

        // Bookmarks
        outSession.bookmarks.clear();
        for (NSDictionary *b in json[@"bookmarks"]) {
            Bookmark bm;
            bm.name = [b[@"name"] UTF8String] ?: "";
            bm.address = [b[@"address"] unsignedLongLongValue];
            bm.type = (ValueType)[b[@"type"] intValue];
            bm.notes = [b[@"notes"] UTF8String] ?: "";
            bm.group = [b[@"group"] UTF8String] ?: "";
            outSession.bookmarks.push_back(bm);
        }

        // Freeze
        outSession.freezeEntries.clear();
        for (NSDictionary *f in json[@"freezeEntries"]) {
            FreezeEntry fe;
            fe.id = [f[@"id"] unsignedLongLongValue];
            fe.address = [f[@"address"] unsignedLongLongValue];
            fe.type = (ValueType)[f[@"type"] intValue];
            fe.label = [f[@"label"] UTF8String] ?: "";
            fe.value = Hex::toBytes([f[@"value"] UTF8String] ?: "");
            fe.active = [f[@"active"] boolValue];
            outSession.freezeEntries.push_back(fe);
        }

        // Pointer chains
        outSession.pointerChains.clear();
        for (NSDictionary *pc in json[@"pointerChains"]) {
            PointerChain chain;
            chain.moduleName = [pc[@"module"] UTF8String] ?: "";
            chain.moduleOffset = [pc[@"offset"] unsignedLongLongValue];
            for (NSNumber *off in pc[@"offsets"]) {
                chain.offsets.push_back([off longLongValue]);
            }
            outSession.pointerChains.push_back(chain);
        }

        // Patches
        outSession.patches.clear();
        for (NSDictionary *p in json[@"patches"]) {
            Session::PatchRecord pr;
            pr.address = [p[@"address"] unsignedLongLongValue];
            pr.patchHex = [p[@"patchHex"] UTF8String] ?: "";
            pr.originalHex = [p[@"originalHex"] UTF8String] ?: "";
            pr.label = [p[@"label"] UTF8String] ?: "";
            pr.autoApply = [p[@"autoApply"] boolValue];
            outSession.patches.push_back(pr);
        }

        // Search history
        outSession.searchHistory.clear();
        for (NSString *s in json[@"searchHistory"]) {
            outSession.searchHistory.push_back([s UTF8String]);
        }
    }

    return true;
}

std::string SessionManager::defaultDirectory() {
    @autoreleasepool {
        NSArray *paths =
            NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *docs = paths.firstObject;
        NSString *dir = [docs stringByAppendingPathComponent:@"Shirayuki"];

        // Create if needed
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];

        return [dir UTF8String];
    }
}

std::vector<std::string> SessionManager::listSessions() {
    std::vector<std::string> files;
    std::string dir = defaultDirectory();

    @autoreleasepool {
        NSString *nsDir = [NSString stringWithUTF8String:dir.c_str()];
        NSArray *contents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:nsDir
                                                                                error:nil];

        for (NSString *file in contents) {
            if ([file hasSuffix:@".json"]) {
                files.push_back([[nsDir stringByAppendingPathComponent:file] UTF8String]);
            }
        }
    }

    return files;
}

bool SessionManager::deleteSession(const std::string &filePath) {
    @autoreleasepool {
        return [[NSFileManager defaultManager]
            removeItemAtPath:[NSString stringWithUTF8String:filePath.c_str()]
                       error:nil];
    }
}

std::string SessionManager::autoSavePath(const std::string &bundleId) {
    return defaultDirectory() + "/" + bundleId + "_autosave.json";
}

} // namespace Shirayuki
