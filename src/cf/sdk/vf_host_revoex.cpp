// The RevoEX VF library (revolution/vf.h) on the host, for the VF calls the game makes: a WiiConnect24
// download box. On the Wii a VF drive is a FAT image (a .vff file on the NAND, PrFILE2 inside); here
// the .vff is a host directory at the same NAND path, so its files can be read with any tool. NAND
// deletes it whole (NANDDelete removes directories, as on the Wii).
//
// Paths: "C:/name", "C:name", "/name" or "name"; without a drive, the last one mounted.
#include <revolution/vf.h>

#include "nand_fs.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

namespace {

struct HostFile {
    std::FILE* file;
};

std::mutex sMutex;
bool sAvailable = false;
std::map<char, fs::path> sDrives; // drive letter (upper case) -> its directory
char sDefaultDrive = 0;
VFErr sLastError = VF_ERR_SUCCESS;

char DriveLetter(const char* drive) {
    if (drive == nullptr || drive[0] == '\0') {
        return 0;
    }
    const char c = drive[0];
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// The host path of a VF path, or empty (and sLastError) when it names no mounted drive or leaves it
fs::path Resolve(const char* path) {
    if (path == nullptr) {
        sLastError = VF_ERR_EINVAL;
        return {};
    }
    std::string p = path;
    char letter = sDefaultDrive;
    if (p.size() >= 2 && p[1] == ':') {
        letter = DriveLetter(p.c_str());
        p = p.substr(2);
    }
    const auto it = sDrives.find(letter);
    if (it == sDrives.end()) {
        sLastError = VF_ERR_ENOENT;
        return {};
    }
    while (!p.empty() && (p[0] == '/' || p[0] == '\\')) {
        p.erase(0, 1);
    }
    fs::path rel = fs::u8path(p).lexically_normal();
    if (rel.empty() || *rel.begin() == "..") {
        sLastError = VF_ERR_EINVAL;
        return {};
    }
    return it->second / rel;
}

} // namespace

extern "C" {

void VFInitEx(void* heap_start_address, u32 size) {
    (void)heap_start_address;
    (void)size;
    std::lock_guard lock(sMutex);
    sAvailable = true;
}

void VFFinalize() {
    std::lock_guard lock(sMutex);
    sAvailable = false;
    sDrives.clear();
    sDefaultDrive = 0;
}

VFErr VFIsAvailable() {
    std::lock_guard lock(sMutex);
    return sAvailable ? 1 : 0;
}

// The drive is the directory at the NAND path of the system file (the .vff); it must exist (the
// download box is created by NWC24CreateDlVf)
VFErr VFMountDriveNANDFlash(const char* drive, const char* sys_file_name) {
    const fs::path dir = cf::sdk::NandHostPath(sys_file_name);
    std::lock_guard lock(sMutex);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        sLastError = fs::exists(dir, ec) ? VF_ERR_EIO : VF_ERR_ENOENT;
        return sLastError;
    }
    const char letter = DriveLetter(drive);
    if (letter == 0) {
        sLastError = VF_ERR_EINVAL;
        return sLastError;
    }
    sDrives[letter] = dir;
    sDefaultDrive = letter;
    return VF_ERR_SUCCESS;
}

VFErr VFUnmountDrive(const char* drive) {
    std::lock_guard lock(sMutex);
    const char letter = DriveLetter(drive);
    if (sDrives.erase(letter) == 0) {
        sLastError = VF_ERR_EINVAL;
        return sLastError;
    }
    if (sDefaultDrive == letter) {
        sDefaultDrive = sDrives.empty() ? 0 : sDrives.begin()->first;
    }
    return VF_ERR_SUCCESS;
}

VFFile* VFOpenFile(const char* path, const char* mode, u32 attr) {
    (void)attr;
    std::lock_guard lock(sMutex);
    const fs::path host = Resolve(path);
    if (host.empty()) {
        return nullptr;
    }
    // Binary, always
    std::string m = mode != nullptr ? mode : "r";
    if (m.find('b') == std::string::npos) {
        m += 'b';
    }
    std::FILE* file = cf::sdk::OpenHostFile(host, m.c_str());
    if (file == nullptr) {
        std::error_code ec;
        sLastError = fs::exists(host, ec) ? VF_ERR_EACCES : VF_ERR_ENOENT;
        return nullptr;
    }
    return new HostFile{file};
}

VFErr VFCloseFile(VFFile* file) {
    if (file == nullptr) {
        return VF_ERR_EBADF;
    }
    HostFile* f = static_cast<HostFile*>(file);
    std::fclose(f->file);
    delete f;
    return VF_ERR_SUCCESS;
}

VFErr VFReadFile(VFFile* file, void* buf, u32 size, u32* read_size) {
    if (file == nullptr) {
        return VF_ERR_EBADF;
    }
    const size_t n = std::fread(buf, 1, size, static_cast<HostFile*>(file)->file);
    if (read_size != nullptr) {
        *read_size = static_cast<u32>(n);
    }
    if (n < size && std::ferror(static_cast<HostFile*>(file)->file)) {
        std::lock_guard lock(sMutex);
        sLastError = VF_ERR_EIO;
        return sLastError;
    }
    return VF_ERR_SUCCESS;
}

s32 VFGetFileSizeByFd(VFFile* file) {
    if (file == nullptr) {
        return -VF_ERR_EBADF;
    }
    std::FILE* f = static_cast<HostFile*>(file)->file;
    const long pos = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, pos, SEEK_SET);
    return static_cast<s32>(size);
}

VFErr VFDeleteFile(const char* path) {
    std::lock_guard lock(sMutex);
    const fs::path host = Resolve(path);
    if (host.empty()) {
        return sLastError;
    }
    std::error_code ec;
    if (!fs::remove(host, ec)) {
        sLastError = ec ? VF_ERR_EIO : VF_ERR_ENOENT;
        return sLastError;
    }
    return VF_ERR_SUCCESS;
}

VFErr VFGetLastError() {
    std::lock_guard lock(sMutex);
    return sLastError;
}

VFErr VFGetLastDeviceError(const char* drive) {
    (void)drive;
    return VF_ERR_SUCCESS;
}

} // extern "C"
