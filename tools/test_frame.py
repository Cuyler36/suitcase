"""Exercise the production frame scheduler without game assets (requires cl or c++)."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

TEST = r'''
#include <cf/frame.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    using namespace cf::frame;
    using Clock = std::chrono::steady_clock;
    assert(!IsSpeedup());
    for (int rate : {60, 120, 0}) {
        SetFrameRate(rate);
        NextStep(true);
        SetSpeedup(true);
        const auto start = Clock::now();
        for (int i = 0; i < 120; ++i) {
            assert(NextStep(i % 2 == 0) == Step::Tick);
        }
        // A capped scheduler would spend about two seconds here.
        assert(Clock::now() - start < std::chrono::milliseconds(500));
        assert(GetFrameRate() == rate);
        assert(GetTickFraction() == 1.f);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        SetSpeedup(false);
        assert(NextStep(true) == Step::Tick);
        if (rate == 60) {
            assert(!IsDecoupled());
            assert(NextStep(true) == Step::Tick);
        } else {
            assert(IsDecoupled());
            bool presented = false;
            for (int i = 0; i < 20; ++i) {
                if (NextStep(true) == Step::Present) {
                    presented = true;
                    assert(GetTickFraction() >= 0.f && GetTickFraction() <= 1.f);
                    break;
                }
            }
            assert(presented);
        }
    }
    std::cout << "Frame speedup and restoration checks passed\n";
}
'''


def main():
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cf-frame-', dir=build) as directory:
        directory = Path(directory)
        cpp = directory / 'frame_test.cpp'
        exe = directory / ('frame_test.exe' if os.name == 'nt' else 'frame_test')
        cpp.write_text(TEST, encoding='utf-8')
        source = str(ROOT / 'src/cf/game/frame.cpp')
        include = str(ROOT / 'src/cf/include')
        if os.name == 'nt':
            command = ['cl', '/nologo', '/EHsc', '/std:c++17', '/I' + include,
                       str(cpp), source, '/Fe:' + str(exe)]
        else:
            command = ['c++', '-std=c++17', '-pthread', '-I' + include,
                       str(cpp), source, '-o', str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == '__main__':
    main()
