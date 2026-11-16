# Suitcase
[![Discord Badge]][discord]

[Discord Badge]: https://img.shields.io/discord/727908905392275526?color=%237289DA&logo=discord&logoColor=%23FFFFFF
[discord]: https://discord.gg/hKx3FJJgrV

Experimental PC Port of City Folk based on the [decomp](https://github.com/ACreTeam/cf-decomp).

### LLM Disclosure
This port was LLM-assisted. Claude Opus 5.5 helped with documentation and checking issues with networking. It was also used on occasion to help debug.

This repository does **not** contain any game assets. An existing copy of the game is required.

Supported versions:

- `RUUE01_00`: Rev 0 (USA)

Future supported versions:
- `RUUE01_01`: Rev 1 (USA)
- `RUUP01_00`: Rev 0 (Europe/Australia)
- `RUUP01_01`: Rev 1 (Europe/Australia)
- `RUUJ01_01`: Rev 1 (Japan)
- `RUUJ01_02`: Rev 2 (Japan)
- `RUUK01_01`: Rev 1 (Korea)

## Dependencies

- [CMake](https://cmake.org/download/) 3.25 or newer
- [Ninja](https://github.com/ninja-build/ninja/releases)
- [Python](https://www.python.org/downloads/) 3 (CMake runs `configure.py` and the
  code generators in `tools/` while configuring)
- A C++20 compiler:
  - **Windows:** Visual Studio 2022 or newer with the *Desktop development with C++*
    workload (MSVC or clang-cl)
  - **Linux:** GCC or Clang
  - **macOS:** Xcode command line tools (Apple Clang)

The remaining dependencies (Dawn, SDL3, nod, Dear ImGui, curl, ...) are fetched
by [aurora](https://github.com/encounter/aurora) and
[borealis](https://github.com/encounter/borealis) during the first configure.

## Building

1. Clone the repository & submodules:

   ```sh
   git clone --recursive https://github.com/Cuyler36/suitcase.git
   cd suitcase
   ```

2. Configure and build with a preset for your platform. Each preset builds into
   `build/pc/<preset>`.

   | Platform | Configure preset                       | Notes |
   | -------- | -------------------------------------- | ----- |
   | Windows  | `windows-msvc-<type>`                  | Run from a *Developer PowerShell / Command Prompt for VS* |
   | Windows  | `windows-clang-<type>`                 | clang-cl + lld-link, also from a developer prompt |
   | Linux    | `linux-default-<type>`                 | System default compiler |
   | Linux    | `linux-clang-<type>`                   | Clang |
   | macOS    | `macos-default-<type>`                 | Apple Clang |

   `<type>` is `debug`, `relwithdebinfo` or `release`. For example:

   ```sh
   cmake --preset windows-msvc-relwithdebinfo
   cmake --build --preset windows-msvc-relwithdebinfo
   ```

3. The executable will be at `build/pc/<preset>/Suitcase-<VERSION>`.

## Running

Pass a disc image of the matching version to suitcase:

```sh
build/pc/windows-msvc-relwithdebinfo/Suitcase-RUUE01_00.exe path/to/City_Folk.iso
```

Any disc image format supported by [nod](https://github.com/encounter/nod) works
(ISO, RVZ, WBFS, CISO, ...). Saves, settings (`config.json`) and the other NAND
files are kept in the per-user `suitcase` data directory.

## PC testing controls

Press F1 to bring up the debug menu. You can also press F2 to toggle speedup on/off.

## Credits

- encounter & contributors for aurora and borealis.
