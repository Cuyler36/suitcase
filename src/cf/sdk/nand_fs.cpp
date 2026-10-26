// NAND on PC: a host directory mirrors the Wii's NAND tree (<aurora user path>/nand, set by
// main.cpp through cf::sdk::SetNandRoot), enough for the game's saves (d_nand.cpp), RVLFaceLib's
// Mii database and WiiConnect24's checks. Absolute NAND paths map below the root; relative ones
// resolve against the current directory, the title's home (/title/00010000/<game ID>/data), which
// exists from the start as on a console with the game installed.
//
// - Open files are a table indexed by NANDFileInfo::fileDescriptor.
// - Safe opens for writing work on a copy that close renames over the original and cancel deletes,
//   so a save is never left half-written (the SDK copies through /tmp for the same reason).
// - Asynchronous calls do the work at once and report through nand_async.h, so callbacks still run
//   after the call returns.
// - No permissions, owners, attributes or quotas: NANDCheck always reports enough space.
//
// A fuller NAND (e.g. in aurora) can replace this later.
#include <revolution/nand.h>

#include <borealis/log_c.h>

#include "cf/version_config.h"
#include "nand_async.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct OpenFile {
    std::FILE* file = nullptr;
    fs::path tmpPath; // safe opens for writing: the copy being written
    fs::path origPath;
};

std::mutex sMutex;
fs::path sRoot = "nand";
std::string sCurrentDir;
std::vector<OpenFile> sFiles;

std::string HomeDir() {
    const char* id = CF_GAME_ID;
    char path[NAND_MAX_PATH];
    std::snprintf(path, sizeof(path), "/title/00010000/%02x%02x%02x%02x/data", id[0], id[1], id[2], id[3]);
    return path;
}

std::string CurrentDir() {
    if (sCurrentDir.empty()) {
        sCurrentDir = HomeDir();
    }
    return sCurrentDir;
}

fs::path HostPath(const char* nandPath) {
    std::string path = nandPath;
    if (path.empty() || path[0] != '/') {
        path = CurrentDir() + "/" + path;
    }
    return sRoot / fs::u8path(path.substr(1));
}

// fopen with a host path (wide on Windows, so non-ASCII user directories work)
std::FILE* OpenHost(const fs::path& path, const char* mode) {
#if _WIN32
    wchar_t wmode[8] = {};
    for (int i = 0; i < 7 && mode[i] != '\0'; i++) {
        wmode[i] = static_cast<wchar_t>(mode[i]);
    }
    return _wfopen(path.c_str(), wmode);
#else
    return std::fopen(path.c_str(), mode);
#endif
}

OpenFile* GetFile(const NANDFileInfo* info) {
    s32 fd = info->fileDescriptor;
    if (fd < 0 || fd >= static_cast<s32>(sFiles.size()) || sFiles[fd].file == nullptr) {
        return nullptr;
    }
    return &sFiles[fd];
}

s32 AddFile(OpenFile&& file) {
    for (size_t i = 0; i < sFiles.size(); i++) {
        if (sFiles[i].file == nullptr) {
            sFiles[i] = std::move(file);
            return static_cast<s32>(i);
        }
    }
    sFiles.push_back(std::move(file));
    return static_cast<s32>(sFiles.size() - 1);
}

void CopyPath(char (&dst)[NAND_MAX_PATH], const char* src) {
    std::strncpy(dst, src, NAND_MAX_PATH - 1);
    dst[NAND_MAX_PATH - 1] = '\0';
}

s32 Open(const char* path, NANDFileInfo* info, u8 accType, bool safe) {
    std::lock_guard lock(sMutex);
    fs::path host = HostPath(path);
    std::error_code ec;
    if (!fs::is_regular_file(host, ec)) {
        return fs::exists(host, ec) ? NAND_RESULT_INVALID : NAND_RESULT_NOEXISTS;
    }
    OpenFile file;
    fs::path openPath = host;
    if (safe && (accType & NAND_ACCESS_WRITE)) {
        file.origPath = host;
        file.tmpPath = host;
        file.tmpPath += ".safetmp";
        if (!fs::copy_file(host, file.tmpPath, fs::copy_options::overwrite_existing, ec)) {
            return NAND_RESULT_FATAL_ERROR;
        }
        openPath = file.tmpPath;
    }
    file.file = OpenHost(openPath, (accType & NAND_ACCESS_WRITE) ? "r+b" : "rb");
    if (file.file == nullptr) {
        return NAND_RESULT_FATAL_ERROR;
    }
    std::memset(info, 0, sizeof(*info));
    info->fileDescriptor = AddFile(std::move(file));
    info->origFd = -1;
    CopyPath(info->origPath, path);
    info->accType = accType;
    return NAND_RESULT_OK;
}

// Closes the file; a safe open's copy replaces the original (commit) or is dropped (cancel)
s32 Close(NANDFileInfo* info, bool commit) {
    std::lock_guard lock(sMutex);
    OpenFile* file = GetFile(info);
    if (file == nullptr) {
        return NAND_RESULT_INVALID;
    }
    std::fclose(file->file);
    s32 result = NAND_RESULT_OK;
    if (!file->tmpPath.empty()) {
        std::error_code ec;
        if (commit) {
            fs::rename(file->tmpPath, file->origPath, ec);
            if (ec) {
                borealis_log_printf(BOREALIS_LOG_ERROR, "cf::nand", "committing %s: %s",
                                    info->origPath, ec.message().c_str());
                result = NAND_RESULT_FATAL_ERROR;
            }
        } else {
            fs::remove(file->tmpPath, ec);
        }
    }
    *file = OpenFile{};
    info->fileDescriptor = -1;
    return result;
}

s32 Read(NANDFileInfo* info, void* buffer, u32 length) {
    std::lock_guard lock(sMutex);
    OpenFile* file = GetFile(info);
    if (file == nullptr) {
        return NAND_RESULT_INVALID;
    }
    if (!(info->accType & NAND_ACCESS_READ)) {
        return NAND_RESULT_ACCESS;
    }
    return static_cast<s32>(std::fread(buffer, 1, length, file->file));
}

s32 Write(NANDFileInfo* info, const void* buffer, u32 length) {
    std::lock_guard lock(sMutex);
    OpenFile* file = GetFile(info);
    if (file == nullptr) {
        return NAND_RESULT_INVALID;
    }
    if (!(info->accType & NAND_ACCESS_WRITE)) {
        return NAND_RESULT_ACCESS;
    }
    s32 written = static_cast<s32>(std::fwrite(buffer, 1, length, file->file));
    std::fflush(file->file);
    return written;
}

// Returns the new position
s32 Seek(NANDFileInfo* info, s32 offset, s32 whence) {
    std::lock_guard lock(sMutex);
    OpenFile* file = GetFile(info);
    if (file == nullptr) {
        return NAND_RESULT_INVALID;
    }
    int origin = whence == NAND_SEEK_CUR ? SEEK_CUR : whence == NAND_SEEK_END ? SEEK_END : SEEK_SET;
    if (std::fseek(file->file, offset, origin) != 0) {
        return NAND_RESULT_INVALID;
    }
    return static_cast<s32>(std::ftell(file->file));
}

s32 GetLength(NANDFileInfo* info, u32* length) {
    std::lock_guard lock(sMutex);
    OpenFile* file = GetFile(info);
    if (file == nullptr) {
        return NAND_RESULT_INVALID;
    }
    long pos = std::ftell(file->file);
    std::fseek(file->file, 0, SEEK_END);
    *length = static_cast<u32>(std::ftell(file->file));
    std::fseek(file->file, pos, SEEK_SET);
    return NAND_RESULT_OK;
}

s32 Create(const char* path, bool dir) {
    std::lock_guard lock(sMutex);
    fs::path host = HostPath(path);
    std::error_code ec;
    if (fs::exists(host, ec)) {
        return NAND_RESULT_EXISTS;
    }
    if (!fs::is_directory(host.parent_path(), ec)) {
        return NAND_RESULT_NOEXISTS;
    }
    if (dir) {
        return fs::create_directory(host, ec) ? NAND_RESULT_OK : NAND_RESULT_FATAL_ERROR;
    }
    std::FILE* file = OpenHost(host, "wb");
    if (file == nullptr) {
        return NAND_RESULT_FATAL_ERROR;
    }
    std::fclose(file);
    return NAND_RESULT_OK;
}

s32 Delete(const char* path) {
    std::lock_guard lock(sMutex);
    fs::path host = HostPath(path);
    std::error_code ec;
    if (!fs::exists(host, ec)) {
        return NAND_RESULT_NOEXISTS;
    }
    fs::remove_all(host, ec);
    return ec ? NAND_RESULT_FATAL_ERROR : NAND_RESULT_OK;
}

void StoreBE16(u16* dst, u16 value) {
    u8* p = reinterpret_cast<u8*>(dst);
    p[0] = static_cast<u8>(value >> 8);
    p[1] = static_cast<u8>(value);
}

void StoreBE32(u32* dst, u32 value) {
    u8* p = reinterpret_cast<u8*>(dst);
    p[0] = static_cast<u8>(value >> 24);
    p[1] = static_cast<u8>(value >> 16);
    p[2] = static_cast<u8>(value >> 8);
    p[3] = static_cast<u8>(value);
}

} // namespace

namespace cf {
namespace sdk {

// The NAND root lives in the user directory; the title's home directory is created if missing
void SetNandRoot(const char* userPath) {
    std::lock_guard lock(sMutex);
    sRoot = fs::u8path(userPath) / "nand";
    std::error_code ec;
    fs::create_directories(HostPath(HomeDir().c_str()), ec);
    if (ec) {
        borealis_log_printf(BOREALIS_LOG_ERROR, "cf::nand", "cannot create %s: %s",
                            HostPath(HomeDir().c_str()).u8string().c_str(), ec.message().c_str());
    }
    borealis_log_printf(BOREALIS_LOG_INFO, "cf::nand", "NAND root: %s", sRoot.u8string().c_str());
}

} // namespace sdk
} // namespace cf

extern "C" {

s32 NANDOpen(const char* path, NANDFileInfo* info, u8 accType) {
    return Open(path, info, accType, false);
}

s32 NANDPrivateOpen(const char* path, NANDFileInfo* info, u8 accType) {
    return Open(path, info, accType, false);
}

s32 NANDClose(NANDFileInfo* info) {
    return Close(info, true);
}

s32 NANDSimpleSafeOpen(const char* path, NANDFileInfo* info, u8 accType, void* buffer, u32 length) {
    (void)buffer;
    (void)length;
    return Open(path, info, accType, true);
}

s32 NANDSimpleSafeClose(NANDFileInfo* info) {
    return Close(info, true);
}

s32 NANDSimpleSafeCancel(NANDFileInfo* info) {
    return Close(info, false);
}

s32 NANDRead(NANDFileInfo* info, void* buffer, u32 length) {
    return Read(info, buffer, length);
}

s32 NANDWrite(NANDFileInfo* info, const void* buffer, u32 length) {
    return Write(info, buffer, length);
}

s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 whence) {
    return Seek(info, offset, whence);
}

s32 NANDCreate(const char* path, u8 perm, u8 attr) {
    (void)perm;
    (void)attr;
    return Create(path, false);
}

s32 NANDDelete(const char* path) {
    return Delete(path);
}

s32 NANDGetType(const char* path, u8* type) {
    std::lock_guard lock(sMutex);
    fs::path host = HostPath(path);
    std::error_code ec;
    if (!fs::exists(host, ec)) {
        return NAND_RESULT_NOEXISTS;
    }
    *type = fs::is_directory(host, ec) ? NAND_TYPE_DIR : NAND_TYPE_FILE;
    return NAND_RESULT_OK;
}

s32 NANDCheck(u32 fsblock, u32 inode, u32* answer) {
    (void)fsblock;
    (void)inode;
    *answer = 0;
    return NAND_RESULT_OK;
}

// The banner is stored big-endian, as the Wii menu reads it
void NANDInitBanner(NANDBanner* bnr, u32 flag, const u16* title, const u16* comment) {
    std::memset(bnr, 0, sizeof(*bnr));
    StoreBE32(&bnr->signature, NAND_BANNER_SIGNATURE);
    StoreBE32(&bnr->flag, flag);
    const u16* texts[2] = {title, comment};
    for (int i = 0; i < 2; i++) {
        if (texts[i] == nullptr) {
            continue;
        }
        for (int c = 0; c < NAND_BANNER_COMMENT_SIZE - 1 && texts[i][c] != 0; c++) {
            StoreBE16(&bnr->comment[i][c], texts[i][c]);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Asynchronous calls: done at once, reported through the callback later
// ---------------------------------------------------------------------------------------------
s32 NANDPrivateCreateAsync(const char* path, u8 perm, u8 attr, NANDCallback callback, NANDCommandBlock* block) {
    (void)perm;
    (void)attr;
    return cf::sdk::NandCompleteAsync(callback, block, Create(path, false));
}

s32 NANDPrivateCreateDirAsync(const char* path, u8 perm, u8 attr, NANDCallback callback, NANDCommandBlock* block) {
    (void)perm;
    (void)attr;
    return cf::sdk::NandCompleteAsync(callback, block, Create(path, true));
}

s32 NANDPrivateDeleteAsync(const char* path, NANDCallback callback, NANDCommandBlock* block) {
    return cf::sdk::NandCompleteAsync(callback, block, Delete(path));
}

s32 NANDPrivateSafeOpenAsync(const char* path, NANDFileInfo* info, u8 accType, void* buffer, u32 length,
                             NANDCallback callback, NANDCommandBlock* block) {
    (void)buffer;
    (void)length;
    block->fileInfo = info;
    return cf::sdk::NandCompleteAsync(callback, block, Open(path, info, accType, true));
}

s32 NANDSafeCloseAsync(NANDFileInfo* info, NANDCallback callback, NANDCommandBlock* block) {
    return cf::sdk::NandCompleteAsync(callback, block, Close(info, true));
}

s32 NANDReadAsync(NANDFileInfo* info, void* buffer, u32 length, NANDCallback callback, NANDCommandBlock* block) {
    return cf::sdk::NandCompleteAsync(callback, block, Read(info, buffer, length));
}

s32 NANDWriteAsync(NANDFileInfo* info, const void* buffer, u32 length, NANDCallback callback,
                   NANDCommandBlock* block) {
    return cf::sdk::NandCompleteAsync(callback, block, Write(info, buffer, length));
}

s32 NANDSeekAsync(NANDFileInfo* info, s32 offset, s32 whence, NANDCallback callback, NANDCommandBlock* block) {
    return cf::sdk::NandCompleteAsync(callback, block, Seek(info, offset, whence));
}

s32 NANDGetLengthAsync(NANDFileInfo* info, u32* length, NANDCallback callback, NANDCommandBlock* block) {
    return cf::sdk::NandCompleteAsync(callback, block, GetLength(info, length));
}

} // extern "C"
