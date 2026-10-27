// The host side of the NAND file system (nand_fs.cpp), for the other SDK layers that keep files on the
// NAND (WiiConnect24: nwc24_*.cpp, vf_host_revoex.cpp)
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace cf {
namespace sdk {

// The host path of a NAND path (relative ones are under the title's home directory, as for NAND)
std::filesystem::path NandHostPath(const char* nandPath);

// The user directory (the NAND root's parent: config, saves, console keys)
std::filesystem::path UserDirPath();

// The title's home directory: /title/00010000/<game ID>/data
std::string NandHomeDir();

// fopen of a host path (wide on Windows, so non-ASCII user directories work)
std::FILE* OpenHostFile(const std::filesystem::path& path, const char* mode);

} // namespace sdk
} // namespace cf
