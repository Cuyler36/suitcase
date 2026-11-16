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

`RUUE01_00` is the primary version; the other versions' configurations are
generated from it with `tools/port_version.py`.

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

1. Clone the repository with its submodules:

   ```sh
   git clone --recursive https://github.com/Cuyler36/suitcase.git
   cd suitcase
   ```

   For an existing clone, run `git submodule sync` and then
   `git submodule update --init --recursive`.

   `extern/aurora` and `extern/borealis` come from forks
   ([Cuyler36/aurora](https://github.com/Cuyler36/aurora),
   [Cuyler36/borealis](https://github.com/Cuyler36/borealis)) whose `suitcase`
   branches carry patches the port needs on top of upstream, such as the Wii bus
   clock option and `borealis::socket`. Building against upstream aurora or
   borealis does not work.

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

   On Windows, the MSVC environment must be loaded before configuring; a plain
   shell where `cl` is on `PATH` but `INCLUDE` is not set fails with missing
   standard headers. If a tree was configured without it, delete the tree and
   configure again.

3. The executable is `build/pc/<preset>/Suitcase-<VERSION>` (`.exe` on Windows).

### Other game versions

The presets build `RUUE01_00`. Each version needs its own build tree; set
`CF_VERSION` and a separate binary directory:

```sh
cmake --preset windows-msvc-relwithdebinfo -DCF_VERSION=RUUP01_00 -B build/pc/windows-msvc-relwithdebinfo-RUUP01_00
cmake --build build/pc/windows-msvc-relwithdebinfo-RUUP01_00
```

Reconfiguring an existing tree for a different version is refused.

## Running

Pass a disc image of the matching version to the executable:

```sh
build/pc/windows-msvc-relwithdebinfo/Suitcase-RUUE01_00.exe path/to/City_Folk.rvz
```

Any disc image format supported by [nod](https://github.com/encounter/nod) works
(ISO, RVZ, WBFS, CISO, ...). Saves, settings (`config.json`) and the other NAND
files are kept in the per-user `suitcase` data directory.

## Decompilation build

The original matching decompilation build (`configure.py` + `ninja`, verified
against the retail DOL and RELs with [objdiff](https://github.com/encounter/objdiff)
for diffing) is still present and independent of the PC build. It needs the
extracted game files in `orig/<VERSION>` (`sys/main.dol` and `files/rels/*.rel`):

```sh
python configure.py --version <VERSION>
ninja
```

See [City Folk configuration setup](docs/city_folk_config.md) and
[Dependencies](docs/dependencies.md) for details.

## PC testing controls

Press **F2** to toggle gameplay speedup, or open the debug bar with **F1** and
click **Speedup (F2)**. Speedup unlocks the game's retrace wait and runs a game
tick on every frame as fast as the host allows. An on-screen indicator remains
visible while it is active; press F2 again to return to normal speed.

This is separate from frame interpolation: your selected frame rate is preserved
and resumes when speedup stops. Speedup starts disabled each session.

## Documentation

- [City Folk configuration setup](docs/city_folk_config.md)
- [Identified source splits](docs/identified_splits.txt)
- [`symbols.txt`](docs/symbols.md)
- [`splits.txt`](docs/splits.md)
- [GitHub Actions and decomp.dev setup](docs/github_actions.md)

## Credits

- encounter & contributors for aurora and borealis.
