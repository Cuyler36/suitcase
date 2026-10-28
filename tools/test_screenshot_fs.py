"""Exercise the production FA screenshot backend with a temporary host directory.

Run: python tools/test_screenshot_fs.py (requires a C++17 compiler environment).
Only the user-directory and host-fopen helpers are replaced; FA headers are real.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TYPES = """#pragma once
#include <cstdint>
using u8=uint8_t; using s8=int8_t; using u16=uint16_t; using s16=int16_t;
using u32=uint32_t; using s32=int32_t;
"""
TEST = r'''
#include <revolution/FA/fa.h>
#include "nand_fs.h"
#include <algorithm>
#include <cctype>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
namespace fs = std::filesystem;
namespace cf::sdk {
fs::path UserDirPath() { return fs::current_path() / fs::u8path("user-\xc3\xa9"); }
std::FILE* OpenHostFile(const fs::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wide;
    while (*mode) wide += *mode++;
    return _wfopen(path.c_str(), wide.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}
}
int main() {
    FADrvTbl drive{};
    FADta find;
    assert(FAInit(0x20000) == 0);
    assert(FARegistCB(0, nullptr, nullptr) == 0);
    assert(FAAttach(0, nullptr, 0, &drive) == 0);
    assert(drive.drive == 'S' && (drive.stat & 0x10));
    assert(FAMount(drive.drive) == 0);
    assert(FAFsfirst("S:\\DCIM", 0x10, &find) == -1 && FAErrnum() == 2);
    assert(FAMkdir("S:\\DCIM") == 0);
    assert(FAMkdir("S:\\DCIM") == -1 && FAErrnum() == 17);
    assert(FAMkdir("S:\\DCIM\\100NIN01") == 0);
    assert(FAFsfirst("S:\\DCIM\\100?????", 0x10, &find) == 0);
    assert(std::strcmp(find.fileName, "100NIN01") == 0);
    const char jpeg[] = "test JPEG bytes";
    const char* photo = "S:\\DCIM\\100NIN01\\RUU_0001.JPG";
    auto* file = FACreate(photo, 0);
    assert(file);
    assert(FAFwrite(jpeg, sizeof(jpeg), 1, file) == 1);
    assert(FAUnmount('S', 0) == -1 && FAErrnum() == FA_ERR_EBUSY);
    assert(FAFclose(file) == 0);
    assert(!FACreate(photo, 0) && FAErrnum() == FA_ERR_EEXIST);
    const auto folder = cf::sdk::UserDirPath() / "screenshots/DCIM/100NIN01";
    std::ifstream input(folder / "RUU_0001.JPG", std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    assert(bytes == std::string(jpeg, sizeof(jpeg)));
    input.close();
    // Mixed-case existing names participate in numbering and can be removed on all hosts.
    std::ofstream(folder / "ruu_0002.jpg") << "second";
    std::ofstream(folder / "ABCD0009.JPG") << "another camera";
    std::ofstream(folder / "notes.txt") << "keep";
    assert(FAFsfirst("s:\\dcim\\100nin01\\????????.JPG", 0x7f, &find) == 0);
    int count = 0, maxNumber = 0;
    do {
        ++count;
        maxNumber = std::max(maxNumber, std::atoi(find.fileName + 4));
    } while (FAFsnext(&find) == 0);
    assert(count == 3 && maxNumber == 9 && FAErrnum() == 2);
    assert(FAFsfirst("S:\\DCIM\\100NIN01\\RUU_????.JPG", 0x7f, &find) == 0);
    count = 0;
    do {
        std::string path = std::string("S:\\DCIM\\100NIN01\\") + find.fileName;
        assert(FARemove(path.c_str()) == 0);
        ++count;
    } while (FAFsnext(&find) == 0);
    assert(count == 2 && FAErrnum() == 2);
    assert(fs::exists(folder / "ABCD0009.JPG") && fs::exists(folder / "notes.txt"));
    assert(FARmdir("S:\\DCIM\\100NIN01") == -1 && FAErrnum() == FA_ERR_ENOTEMPTY);
    assert(FAFsfirst("S:\\DCIM\\100NIN01\\?*", 0x7f, &find) == 0);
    assert(FARemove("S:\\DCIM\\100NIN01\\ABCD0009.JPG") == 0);
    assert(FARemove("S:\\DCIM\\100NIN01\\notes.txt") == 0);
    assert(FAFsfirst("S:\\DCIM\\100NIN01\\?*", 0x7f, &find) == -1 && FAErrnum() == 2);
    // Long host names appear under an 8.3 alias so deleteProc keeps the folder instead of failing rmdir.
    std::ofstream(folder / "RUU_1234 copy.JPG") << "user copy";
    assert(FAFsfirst("S:\\DCIM\\100NIN01\\?*", 0x7f, &find) == 0);
    assert(std::strcmp(find.fileName, "RUU_12~1.JPG") == 0 && find.attr == 0x20);
    assert(FAFsfirst("S:\\DCIM\\100NIN01\\RUU_????.JPG", 0x7f, &find) == -1 && FAErrnum() == 2);
    assert(FARmdir("S:\\DCIM\\100NIN01") == -1 && FAErrnum() == FA_ERR_ENOTEMPTY);
    fs::remove(folder / "RUU_1234 copy.JPG");
    assert(FARmdir("S:\\DCIM\\100NIN01") == 0);
    assert(!FACreate("S:\\..\\outside.JPG", 0) && FAErrnum() == FA_ERR_EINVAL);
    assert(!FACreate("C:\\outside.JPG", 0));
    assert(!FACreate("S:\\DCIM\\photo:stream", 0));
    assert(FARmdir("S:\\") == -1);
    // Links the user placed in the folder are ordinary entries: followed for writes, and erasing one
    // removes only the link. Skipped where the host does not permit creating links.
    std::error_code ec;
    fs::create_directory(cf::sdk::UserDirPath() / "outside");
    fs::create_directory_symlink(cf::sdk::UserDirPath() / "outside",
                                cf::sdk::UserDirPath() / "screenshots/DCIM/link", ec);
    if (!ec) {
        assert(FAFsfirst("S:\\DCIM\\link", 0x10, &find) == 0 && find.attr == 0x10);
        file = FACreate("S:\\DCIM\\link\\RUU_0001.JPG", 0);
        assert(file && FAFclose(file) == 0);
        assert(fs::exists(cf::sdk::UserDirPath() / "outside/RUU_0001.JPG"));
        assert(FARemove("S:\\DCIM\\link\\RUU_0001.JPG") == 0);
        assert(FARmdir("S:\\DCIM\\link") == 0);
        assert(fs::is_directory(cf::sdk::UserDirPath() / "outside"));
    } else {
        std::cout << "Directory symlink cases skipped: " << ec.message() << "\n";
    }
    // deleteProc erases photo-named links (live or dangling) like photos, leaving their targets.
    assert(FAMkdir("S:\\DCIM\\101NIN01") == 0);
    const auto linkFolder = cf::sdk::UserDirPath() / "screenshots/DCIM/101NIN01";
    std::ofstream(cf::sdk::UserDirPath() / "outside/target.JPG") << "outside";
    ec.clear();
    fs::create_symlink(cf::sdk::UserDirPath() / "outside/target.JPG", linkFolder / "RUU_0001.JPG", ec);
    if (!ec) {
        fs::create_symlink(cf::sdk::UserDirPath() / "outside/missing.JPG", linkFolder / "RUU_0003.JPG");
        std::ofstream(linkFolder / "RUU_0002.JPG") << "photo";
        // Mirror deleteProc: erase every RUU_#### result, failing on the first FARemove error.
        int ret = FAFsfirst("S:\\DCIM\\101NIN01\\RUU_????.JPG", 0x7f, &find);
        int removed = 0;
        while (ret == 0) {
            assert(std::isdigit(find.fileName[4]) && std::isdigit(find.fileName[7]) && find.attr == 0x20);
            std::string path = std::string("S:\\DCIM\\101NIN01\\") + find.fileName;
            assert(FARemove(path.c_str()) == 0);
            ++removed;
            ret = FAFsnext(&find);
        }
        assert(removed == 3 && FAErrnum() == 2);
        assert(fs::is_empty(linkFolder));
        assert(fs::exists(cf::sdk::UserDirPath() / "outside/target.JPG"));
    } else {
        std::cout << "File symlink cases skipped: " << ec.message() << "\n";
    }
    assert(FARmdir("S:\\DCIM\\101NIN01") == 0);
    // Worker errors do not leak into another worker's FAErrnum.
    assert(FARemove("S:\\DCIM\\missing.JPG") == -1 && FAErrnum() == 2);
    std::thread worker([] { assert(!FACreate("invalid", 0)); assert(FAErrnum() == FA_ERR_EINVAL); });
    worker.join();
    assert(FAErrnum() == 2);
    assert(FAUnmount('S', 0) == 0 && FADetach('S') == 0);
    assert(FAAttach(0, nullptr, 0, &drive) == 0 && FAMount('S') == 0);
    assert(FAFsfirst("S:\\DCIM", 0x10, &find) == 0);
    // Deleting the host root while mounted behaves like a blank card rather than a hard error.
    fs::remove_all(cf::sdk::UserDirPath() / "screenshots");
    assert(FAFsfirst("S:\\DCIM", 0x10, &find) == -1 && FAErrnum() == 2);
    assert(FAMkdir("S:\\DCIM") == 0);
    assert(FAUnmount('S', 1) == 0 && FADetach('S') == 0);
    // A root that is a link (e.g. into a Pictures folder) mounts and writes into its target.
    fs::remove_all(cf::sdk::UserDirPath() / "screenshots");
    fs::create_directory(cf::sdk::UserDirPath() / "pictures");
    ec.clear();
    fs::create_directory_symlink(cf::sdk::UserDirPath() / "pictures",
                                cf::sdk::UserDirPath() / "screenshots", ec);
    if (!ec) {
        assert(FAAttach(0, nullptr, 0, &drive) == 0 && FAMount('S') == 0);
        assert(FAMkdir("S:\\DCIM") == 0);
        file = FACreate("S:\\DCIM\\RUU_0001.JPG", 0);
        assert(file && FAFclose(file) == 0);
        assert(fs::exists(cf::sdk::UserDirPath() / "pictures/DCIM/RUU_0001.JPG"));
        assert(FARmdir("S:\\") == -1 && FAErrnum() == FA_ERR_EACCES);
        assert(FAUnmount('S', 1) == 0 && FADetach('S') == 0);
    } else {
        std::cout << "Linked root case skipped: " << ec.message() << "\n";
    }
    std::cout << "Screenshot filesystem checks passed\n";
}
'''


def main():
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cf-screenshot-", dir=build) as directory:
        directory = Path(directory)
        (directory / "types.h").write_text(TYPES)
        cpp = directory / "test.cpp"
        cpp.write_text(TEST)
        exe = directory / ("test.exe" if os.name == "nt" else "test")
        includes = [directory, ROOT / "include", ROOT / "include/lib", ROOT / "src/cf/sdk"]
        source = ROOT / "src/cf/sdk/fa_fs.cpp"
        if os.name == "nt":
            cmd = ["cl", "/nologo", "/EHsc", "/std:c++17", "/D_CRT_SECURE_NO_WARNINGS"]
            cmd += ["/I" + str(p) for p in includes] + [str(cpp), str(source), "/Fe:" + str(exe)]
        else:
            cmd = ["c++", "-std=c++17", "-pthread"]
            cmd += ["-I" + str(p) for p in includes] + [str(cpp), str(source), "-o", str(exe)]
        subprocess.run(cmd, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
