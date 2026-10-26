#!/usr/bin/env python3
# Builds the PC port (CMakePresets.json): configures the preset's build directory (build/pc/<preset>)
# when it has none yet, then builds it. On Windows it loads the MSVC environment (vcvars64.bat, found
# with vswhere) unless it runs in an x64 developer prompt, so it runs from any prompt.
#
# --for-release builds the Release preset (optimized, no debug info) and packages it for testers:
# dist/suitcase-<version>-<platform>-<commit>.zip with the executable, the libraries next to it
# (aurora's runtime DLLs, Dawn's shader compiler), on Windows the Visual C++ runtime (app-local, so
# testers need no redistributable), and a README.txt.
#
# Usage: python build.py [--preset NAME] [--configure] [--target TARGET ...] [-j N] [--for-release]
import argparse
import glob
import os
import platform
import re
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))

DEFAULT_PRESETS = {
    "Windows": "windows-msvc-relwithdebinfo",
    "Linux": "linux-default-relwithdebinfo",
    "Darwin": "macos-default-relwithdebinfo",
}

RELEASE_PRESETS = {
    "Windows": "windows-msvc-release",
    "Linux": "linux-default-release",
    "Darwin": "macos-default-release",
}

PLATFORM_NAMES = {"Windows": "windows-x64", "Linux": "linux-x64", "Darwin": "macos"}

# Where aurora keeps the user data (saves, config.json, the shader cache)
DATA_DIRS = {
    "Windows": "%APPDATA%\\suitcase",
    "Linux": "~/.local/share/suitcase",
    "Darwin": "~/Library/Application Support/suitcase",
}

# The game ID's region letter (RUUE01: E)
REGIONS = {"E": "US", "P": "European", "J": "Japanese", "K": "Korean"}

README = """suitcase {version} ({commit})
A test build of the Animal Crossing: City Folk PC port.

You need your own disc image of {game_id} (the game's {region} release, revision {revision}).

Run:   {exe} "path/to/{game_id}.iso"
       (or drag the disc image onto {exe})

Saves, settings and the shader cache are in {data_dir}.
F1 opens the debug menu.
"""

VSWHERE = os.path.join(
    os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    "Microsoft Visual Studio",
    "Installer",
    "vswhere.exe",
)


def msvc_environment():
    """The environment of vcvars64.bat from the newest Visual Studio with the C++ tools."""
    if not os.path.exists(VSWHERE):
        sys.exit(f"build.py: vswhere not found ({VSWHERE}); install Visual Studio with the C++ tools")
    install = subprocess.run(
        [
            VSWHERE,
            "-latest",
            "-prerelease",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    vcvars = os.path.join(install, "VC", "Auxiliary", "Build", "vcvars64.bat")
    if not install or not os.path.exists(vcvars):
        sys.exit("build.py: no Visual Studio with the C++ tools (x64) found")
    print(f"build.py: MSVC environment from {install}")
    output = subprocess.run(
        f'cmd /s /c ""{vcvars}" >nul && set"', capture_output=True, text=True, check=True, shell=True
    ).stdout
    env = {}
    for line in output.splitlines():
        name, sep, value = line.partition("=")
        if sep:
            env[name] = value
    return env


def run(command, env):
    print("build.py: " + " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, env=env)
    if result.returncode != 0:
        # Not the code itself: Windows reports ninja's as 4294967295, which sys.exit cannot pass on
        sys.exit(1)


def cache_value(build_dir, name):
    with open(os.path.join(build_dir, "CMakeCache.txt"), encoding="utf-8") as cache:
        for line in cache:
            match = re.match(rf"{re.escape(name)}:[A-Z]+=(.*)", line)
            if match:
                return match.group(1).strip()
    sys.exit(f"build.py: {name} is not in the CMake cache")


def git_commit():
    try:
        return subprocess.run(
            ["git", "describe", "--always", "--dirty"], cwd=ROOT, capture_output=True, text=True, check=True
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def runtime_files(build_dir, env):
    """The libraries to ship next to the executable, as (source path, name in the package)."""
    files = []
    if platform.system() == "Windows":
        files += [(path, os.path.basename(path)) for path in glob.glob(os.path.join(build_dir, "*.dll"))]
        # The Visual C++ runtime, app-local (allowed by Microsoft's redistribution terms): the build's
        # toolset needs a newer runtime than testers may have installed
        redist = env.get("VCToolsRedistDir")
        crt = sorted(glob.glob(os.path.join(redist, "x64", "Microsoft.VC*.CRT"))) if redist else []
        if not crt:
            sys.exit("build.py: the Visual C++ runtime was not found (VCToolsRedistDir); "
                     "install Visual Studio's C++ redistributable files")
        files += [(path, os.path.basename(path)) for path in glob.glob(os.path.join(crt[-1], "*.dll"))]
    else:
        for pattern in ("*.so", "*.so.*", "*.dylib"):
            files += [(path, os.path.basename(path)) for path in glob.glob(os.path.join(build_dir, pattern))]
    return files


def package(build_dir, env):
    """Zips the build for testers into dist/."""
    system = platform.system()
    version = cache_value(build_dir, "CF_VERSION")  # e.g. RUUE01_00
    exe = f"suitcase-{version}" + (".exe" if system == "Windows" else "")
    exe_path = os.path.join(build_dir, exe)
    if not os.path.exists(exe_path):
        sys.exit(f"build.py: {exe_path} was not built")

    commit = git_commit()
    game_id, revision = version.split("_")
    readme = README.format(
        version=version,
        commit=commit,
        game_id=game_id,
        region=REGIONS.get(game_id[3], game_id[3]),
        revision=int(revision),
        exe=exe,
        data_dir=DATA_DIRS.get(system, "the user data directory"),
    )
    if system == "Windows":
        readme = readme.replace("\n", "\r\n")

    name = f"suitcase-{version}-{PLATFORM_NAMES.get(system, system.lower())}-{commit}"
    dist = os.path.join(ROOT, "dist")
    os.makedirs(dist, exist_ok=True)
    zip_path = os.path.join(dist, name + ".zip")
    files = [(exe_path, exe)] + runtime_files(build_dir, env)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, arcname in files:
            archive.write(path, f"{name}/{arcname}")
        archive.writestr(f"{name}/README.txt", readme)
    for _, arcname in files:
        print(f"build.py:   {arcname}")
    print(f"build.py: packaged {zip_path} ({os.path.getsize(zip_path) / 1e6:.1f} MB)")


def main():
    system = platform.system()
    parser = argparse.ArgumentParser(description="Builds the PC port.")
    parser.add_argument("--preset", help="the configure preset (default: this platform's RelWithDebInfo, "
                        "or its Release with --for-release)")
    parser.add_argument("--configure", action="store_true", help="configure again first")
    parser.add_argument("--target", nargs="+", help="build only these targets")
    parser.add_argument("-j", "--jobs", type=int, help="parallel jobs")
    parser.add_argument("--for-release", action="store_true",
                        help="build the Release preset and package it into dist/ as a zip")
    args = parser.parse_args()
    if args.for_release and args.target:
        sys.exit("build.py: --for-release builds everything; drop --target")
    if args.preset is None:
        args.preset = (RELEASE_PRESETS if args.for_release else DEFAULT_PRESETS).get(system)
    if args.preset is None:
        sys.exit(f"build.py: no default preset for {system}; pass --preset")

    env = os.environ.copy()
    # Not just "cl on PATH": other shells (Git Bash) can have another toolset's cl without its
    # INCLUDE and LIB. An x64 developer environment sets this.
    if system == "Windows" and env.get("VSCMD_ARG_TGT_ARCH") != "x64":
        env = msvc_environment()

    build_dir = os.path.join(ROOT, "build", "pc", args.preset)
    if args.configure or not os.path.exists(os.path.join(build_dir, "CMakeCache.txt")):
        run(["cmake", "--preset", args.preset], env)

    command = ["cmake", "--build", build_dir]
    if args.target:
        command += ["--target", *args.target]
    if args.jobs:
        command += ["-j", str(args.jobs)]
    run(command, env)

    if args.for_release:
        package(build_dir, env)


if __name__ == "__main__":
    main()
