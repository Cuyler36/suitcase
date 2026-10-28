// The screenshot FA subset: virtual SD drive S: under <user directory>/screenshots.
// Searches snapshot their results so deleting the current photo does not invalidate enumeration.
#include <revolution/FA/fa.h>
#include "nand_fs.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
std::mutex mutex;
bool attached = false;
bool mounted = false;
thread_local FAError lastError = 0;
std::unordered_set<std::FILE*> files;
struct Entry { std::string name; u8 attr; };
struct Search { std::vector<Entry> entries; size_t next = 0; };
std::unordered_map<FADta*, Search> searches;

FAError Fail(FAError error) { lastError = error; return -1; }
FAError HostError(const std::error_code& ec) {
    if (ec == std::errc::no_such_file_or_directory) return FA_ERR_ENOENT;
    if (ec == std::errc::file_exists) return FA_ERR_EEXIST;
    if (ec == std::errc::no_space_on_device) return FA_ERR_ENOSPC;
    if (ec == std::errc::permission_denied || ec == std::errc::read_only_file_system)
        return FA_ERR_EWRTPROTECT;
    if (ec == std::errc::directory_not_empty) return FA_ERR_ENOTEMPTY;
    return FA_ERR_EIO;
}
FAError FileError() { return HostError(std::error_code(errno, std::generic_category())); }
char Upper(char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }
std::string FileName(const fs::path& path) {
    const auto utf8 = path.filename().u8string();
    return std::string(utf8.begin(), utf8.end());
}
bool SameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](char x, char y) { return Upper(x) == Upper(y); });
}
bool Match(const char* pattern, const char* name) {
    const char* star = nullptr;
    const char* retry = nullptr;
    while (*name) {
        if (*pattern == '?' || (*pattern && Upper(*pattern) == Upper(*name))) { ++pattern; ++name; }
        else if (*pattern == '*') { star = pattern++; retry = name; }
        else if (star) { pattern = star + 1; name = ++retry; }
        else return false;
    }
    while (*pattern == '*') ++pattern;
    return !*pattern;
}
// Names that do not fit FADta are reported under an 8.3 alias like FAT's SCREEN~1.JPG, so a folder
// holding them is never mistaken for empty. The '~' always lands in fileName[4..7], so the game's
// RUU_#### digit check never treats an alias as a photo to count or erase.
std::string ShortAlias(const std::string& name, int number) {
    auto clean = [](const std::string& text, size_t limit) {
        std::string out;
        for (char c : text) {
            if (out.size() == limit) break;
            if (c == ' ' || c == '.') continue;
            unsigned char u = static_cast<unsigned char>(c);
            out += (u < 128 && (std::isalnum(u) || std::strchr("_-!#$%&'()@^`{}~", c))) ? Upper(c) : '_';
        }
        return out;
    };
    const size_t dot = name.find_last_of('.');
    const std::string suffix = "~" + std::to_string(number);
    std::string base = clean(name.substr(0, dot), 8 - suffix.size());
    if (base.empty()) base = "_";
    std::string ext = dot == std::string::npos ? "" : clean(name.substr(dot + 1), 3);
    return base + suffix + (ext.empty() ? "" : "." + ext);
}

// Only the game's ASCII SD paths are accepted; traversal and alternate streams are rejected so a
// path cannot name anything outside the screenshot directory. Links the user placed there (including
// the root itself, e.g. pointing at a Pictures folder) are followed like any directory.
bool Resolve(const char* input, fs::path& result) {
    if (!mounted) { Fail(FA_ERR_EIO); return false; }
    if (!input || std::strlen(input) < 3 || Upper(input[0]) != 'S' || input[1] != ':' ||
        (input[2] != '\\' && input[2] != '/')) { Fail(FA_ERR_EINVAL); return false; }
    result = cf::sdk::UserDirPath() / "screenshots";
    std::error_code rootError;
    // Recreate the root if the user deleted it while mounted, as a freshly inserted card would have it.
    if (!fs::exists(fs::status(result, rootError)) && rootError == std::errc::no_such_file_or_directory)
        fs::create_directories(result, rootError);
    if (rootError) { Fail(HostError(rootError)); return false; }
    std::string path(input + 3);
    std::replace(path.begin(), path.end(), '\\', '/');
    size_t begin = 0;
    while (begin < path.size()) {
        size_t end = path.find('/', begin);
        if (end == std::string::npos) end = path.size();
        std::string part = path.substr(begin, end - begin);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' ||
            part.find_first_of(":*?\"<>|") != std::string::npos ||
            std::any_of(part.begin(), part.end(), [](unsigned char c) { return c < 32 || c >= 127; })) {
            Fail(FA_ERR_EINVAL); return false;
        }
        std::error_code ec;
        auto status = fs::symlink_status(result / part, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) { Fail(HostError(ec)); return false; }
        if (!fs::exists(status)) {
            // Match existing names case-insensitively even on case-sensitive hosts. The exact spelling
            // is tried first so per-photo calls do not rescan the folder.
            fs::directory_iterator it(result, ec), stop;
            if (ec && ec != std::errc::no_such_file_or_directory) { Fail(HostError(ec)); return false; }
            while (!ec && it != stop) {
                if (SameName(FileName(it->path()), part)) { part = FileName(it->path()); break; }
                it.increment(ec);
            }
            if (ec && ec != std::errc::no_such_file_or_directory) { Fail(HostError(ec)); return false; }
            ec.clear();
            status = fs::symlink_status(result / part, ec);
            if (ec && ec != std::errc::no_such_file_or_directory) { Fail(HostError(ec)); return false; }
        }
        result /= part;
        begin = end + 1;
    }
    return true;
}

FAError Next(FADta* dta) {
    auto it = searches.find(dta);
    if (it == searches.end()) return Fail(FA_ERR_ENOENT);
    auto& search = it->second;
    if (search.next == search.entries.size()) {
        searches.erase(it);
        return Fail(FA_ERR_ENOENT);
    }
    const auto& entry = search.entries[search.next++];
    std::memset(dta, 0, sizeof(*dta));
    std::memcpy(dta->fileName, entry.name.c_str(), entry.name.size() + 1);
    dta->attr = entry.attr;
    return 0;
}
} // namespace

extern "C" {
FAError FAInit(int) { lastError = 0; return 0; }
FAError FARegistCB(u8 device, FAInsertCallback, FAEjectCallback) {
    // A host directory is permanently inserted; there are no media change events.
    return device == FA_DEVICE_SD ? 0 : Fail(FA_ERR_EINVAL);
}
FAError FAErrnum() { return lastError; }
FAError FAAttach(u8 device, char*, u32, FADrvTbl* table) {
    std::lock_guard lock(mutex);
    if (device != FA_DEVICE_SD || !table) return Fail(FA_ERR_EINVAL);
    std::error_code ec;
    fs::create_directories(cf::sdk::UserDirPath() / "screenshots", ec);
    if (ec) return Fail(HostError(ec));
    attached = true;
    table->drive = 'S';
    table->stat = FA_DRV_TBL_STAT_INSERTED;
    return 0;
}
FAError FADetach(s8 drive) {
    std::lock_guard lock(mutex);
    if (drive != 'S' || !attached) return Fail(FA_ERR_EINVAL);
    if (!files.empty()) return Fail(FA_ERR_EBUSY);
    searches.clear();
    attached = mounted = false;
    return 0;
}
FAError FAMount(s8 drive) {
    std::lock_guard lock(mutex);
    if (drive != 'S' || !attached) return Fail(FA_ERR_EIO);
    mounted = true;
    return 0;
}
FAError FAUnmount(s8 drive, u32) {
    std::lock_guard lock(mutex);
    if (drive != 'S' || !attached) return Fail(FA_ERR_EINVAL);
    if (!files.empty()) return Fail(FA_ERR_EBUSY);
    searches.clear();
    mounted = false;
    return 0;
}
FAError FAMkdir(const char* name) {
    std::lock_guard lock(mutex);
    fs::path path;
    if (!Resolve(name, path)) return -1;
    std::error_code ec;
    bool made = fs::create_directory(path, ec);
    if (ec) return Fail(HostError(ec));
    return made ? 0 : Fail(FA_ERR_EEXIST);
}
FAFILE* FACreate(const char* name, int mode) {
    std::lock_guard lock(mutex);
    if (mode != 0) { Fail(FA_ERR_EINVAL); return nullptr; }
    fs::path path;
    if (!Resolve(name, path)) return nullptr;
    // Exclusive creation protects an existing photo if a filename is claimed after enumeration.
    std::FILE* file = cf::sdk::OpenHostFile(path, "wbx");
    if (!file) { Fail(FileError()); return nullptr; }
    files.insert(file);
    return file;
}
size_t FAFwrite(const void* data, size_t size, size_t count, FAFILE* stream) {
    std::lock_guard lock(mutex);
    auto* file = static_cast<std::FILE*>(stream);
    if (!files.count(file)) { Fail(FA_ERR_EBADF); return 0; }
    if (!data && size && count) { Fail(FA_ERR_EINVAL); return 0; }
    size_t written = std::fwrite(data, size, count, file);
    if (written != count && size) Fail(FileError());
    return written;
}
FAError FAFclose(FAFILE* stream) {
    std::lock_guard lock(mutex);
    auto* file = static_cast<std::FILE*>(stream);
    if (!files.erase(file)) return Fail(FA_ERR_EBADF);
    return std::fclose(file) == 0 ? 0 : Fail(FileError());
}
FAError FAFsfirst(const char* name, u8 attr, FADta* dta) {
    std::lock_guard lock(mutex);
    if (!name || !dta) return Fail(FA_ERR_EINVAL);
    searches.erase(dta);
    std::string pattern(name);
    auto slash = pattern.find_last_of("/\\");
    if (slash == std::string::npos) return Fail(FA_ERR_EINVAL);
    std::string mask = pattern.substr(slash + 1);
    fs::path parent;
    // Resolve accepts S:/ as well as paths beneath it.
    if (!Resolve(pattern.substr(0, slash + 1).c_str(), parent)) return -1;
    std::error_code ec;
    Search search;
    int aliases = 0;
    fs::directory_iterator it(parent, ec), stop;
    while (!ec && it != stop) {
        auto status = it->status(ec);
        if (ec == std::errc::no_such_file_or_directory) ec.clear(); // A dangling link lists as a file.
        if (ec) break;
        auto filename = FileName(it->path());
        bool directory = fs::is_directory(status);
        if ((directory ? (attr & 0x10) != 0 : attr != 0x10) && Match(mask.c_str(), filename.c_str())) {
            if (filename.size() >= sizeof(dta->fileName)) filename = ShortAlias(filename, ++aliases);
            search.entries.push_back({filename, static_cast<u8>(directory ? 0x10 : 0x20)});
        }
        it.increment(ec);
    }
    if (ec) return Fail(HostError(ec));
    searches[dta] = std::move(search);
    return Next(dta);
}
FAError FAFsnext(FADta* dta) {
    std::lock_guard lock(mutex);
    return Next(dta);
}
} // extern "C"

namespace {
FAError Remove(const char* name, bool directory) {
    std::lock_guard lock(mutex);
    fs::path path;
    if (!Resolve(name, path)) return -1;
    if (path == cf::sdk::UserDirPath() / "screenshots") return Fail(FA_ERR_EACCES);
    std::error_code ec;
    auto status = fs::symlink_status(path, ec);
    if (ec) return Fail(HostError(ec));
    if (!fs::exists(status)) return Fail(FA_ERR_ENOENT);
    // Type checks follow links, as the search does; a dangling link counts as a file.
    std::error_code targetError;
    if (directory != fs::is_directory(fs::status(path, targetError))) return Fail(FA_ERR_EINVAL);
    // Never recursive, and on a link removes only the link: preserve unrelated files.
    bool removed = fs::remove(path, ec);
    if (ec) return Fail(HostError(ec));
    return removed ? 0 : Fail(FA_ERR_ENOENT);
}
}
extern "C" FAError FARemove(const char* name) { return Remove(name, false); }
extern "C" FAError FARmdir(const char* name) { return Remove(name, true); }
