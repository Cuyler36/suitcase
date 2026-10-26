# Generates link stubs for SDK modules the PC build does not implement (src/cf/sdk/stubs_*.cpp).
#
# Usage: python tools/gen_pc_sdk_stubs.py . <unresolved-symbols.txt>
#
# The symbol list comes from a failed link (one name per line). Each stub is generated from the
# function's declaration in the decomp headers and compiled against that header, so the compiler
# checks every signature. Stubs report failure (FALSE, NULL, a negative error code) so the game
# takes its own "unavailable" paths; audio and keyboard modules return zero instead.
import os
import re
import sys

root, symfile = sys.argv[1], sys.argv[2]
out_dir = os.path.join(root, "src/cf/sdk")

# Module prefix -> (stub file, policy). "fail": negative for signed results; "zero": all zero.
MODULES = [
    ("NWC24", "stubs_nwc24.cpp", "fail", "WiiConnect24 (online mail, downloads)"),
    ("DWC_", "stubs_dwc.cpp", "fail", "DWC / Nintendo Wi-Fi Connection"),
    ("NHTTP", "stubs_net.cpp", "fail", "networking"),
    ("NET", "stubs_net.cpp", "fail", "networking"),
    ("SO", "stubs_net.cpp", "fail", "networking"),
    ("VF", "stubs_vf.cpp", "fail", "the RevoEX virtual file system (PrFILE2)"),
    ("FA", "stubs_vf.cpp", "fail", "the RevoEX virtual file system (PrFILE2)"),
    ("MPDL", "stubs_mp.cpp", "fail", "DS download play"),
    ("MP", "stubs_mp.cpp", "fail", "DS download play"),
    ("VCT", "stubs_vct.cpp", "fail", "Wii Speak voice chat"),
    ("PMIC", "stubs_vct.cpp", "fail", "Wii Speak microphone"),
    ("NAND", "stubs_nand.cpp", "fail", "NAND (to be replaced by host saves)"),
    ("AXFX", "stubs_ax.cpp", "zero", "the AX audio calls the software AX (ax_rvl.cpp) does not implement"),
    ("AX", "stubs_ax.cpp", "zero", "the AX audio calls the software AX (ax_rvl.cpp) does not implement"),
    ("MIX", "stubs_ax.cpp", "zero", "the AX audio calls the software AX (ax_rvl.cpp) does not implement"),
    ("AI", "stubs_ax.cpp", "zero", "the AX audio calls the software AX (ax_rvl.cpp) does not implement"),
    ("WENC", "stubs_ax.cpp", "zero", "the AX audio calls the software AX (ax_rvl.cpp) does not implement"),
    ("WPAD", "stubs_wpad.cpp", "fail", "Wii Remote functions aurora does not provide"),
    ("KPAD", "stubs_wpad.cpp", "zero", "Wii Remote functions aurora does not provide"),
    ("KBD", "stubs_kbd.cpp", "fail", "USB keyboard calls kbd.cpp does not implement"),
]

# Symbols declared only locally in game code (signature copied from that declaration)
MANUAL = {
    # src/dol/game/d_net_dwc.cpp
    "VCT_HandleData": ("BOOL", "u8 aid, const void* data, u32 size"),
    # src/dol/game/d_net_wus.cpp (RevoEX declares them in private/nhttp.h, which is not C++-clean)
    "NHTTPDeleteConnection": ("int", "void* handle"),
    "NHTTPStartConnection": ("int", "void* handle"),
    "NHTTPGetConnectionError": ("int", "void* handle"),
    "NHTTPGetBodyBuffer": ("u32", "void* handle, char** buffer, u32* size"),
    "NHTTPGetHeaderField": ("int", "void* handle, const char* name, char** value"),
    "NHTTPSetBuiltinRootCA": ("int", "void* handle, int id"),
    "NHTTPSetBuiltinClientCert": ("int", "void* handle, int id"),
    "__NHTTPCreateConnection": ("void*", "const char* url, int method, void* buffer, u32 size, void* callback, void* arg"),
}

# Headers that cannot be compiled as C++; their symbols come from MANUAL
SKIP_HEADERS = {"private/nhttp.h"}

# Where to look for declarations, most authoritative first
HEADER_ROOTS = [
    # aurora's declarations win for modules the PC build forwards to it (revolution/ai.h etc.)
    ("extern/aurora/include/dolphin", "dolphin"),
    ("include/lib/revolution", "revolution"),
    ("lib/RevoEX/include/revolution", "revolution"),
    ("include/lib/dwc", "dwc"),
    ("include/lib/private", "private"),
    ("lib/RevoEX/include/private", "private"),
]


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", " ", t, flags=re.S)
    return re.sub(r"//[^\n]*", " ", t)


headers = []
for rel, inc in HEADER_ROOTS:
    base = os.path.join(root, rel)
    for dp, _, fs in os.walk(base):
        if "BTE" in dp:
            continue
        for f in sorted(fs):
            if f.endswith(".h"):
                path = os.path.join(dp, f)
                include = inc + "/" + os.path.relpath(path, base).replace(os.sep, "/")
                if include in SKIP_HEADERS:
                    continue
                parts = include.split("/")
                if len(parts) == 3 and os.path.exists(os.path.join(base, parts[1] + ".h")):
                    include = parts[0] + "/" + parts[1] + ".h"
                headers.append((include, strip_comments(open(path, encoding="latin-1").read()), rel))


def find_decl(name):
    func = re.compile(r"(?:^|[;{}\n])\s*((?:extern\s+)?[A-Za-z_][\w\s\*]*?[\s\*])" + re.escape(name) + r"\s*\(([^;{]*?)\)\s*;", re.S)
    data = re.compile(r"(?:^|[;{}\n])\s*extern\s+([^;()]*?)\b" + re.escape(name) + r"\b\s*(\[[^\]]*\])?\s*;", re.S)
    for include, text, rel in headers:
        m = func.search(text)
        if m:
            return include, "func", " ".join(m.group(1).replace("extern", "").split()), " ".join(m.group(2).split()), rel
    for include, text, rel in headers:
        m = data.search(text)
        if m:
            return include, "data", " ".join(m.group(1).split()), (m.group(2) or ""), rel
    return None


# Specific results for some stubs. NAND: until host saves exist, every file is missing, as on a console
# that never saved (the game and RVLFaceLib then create their data fresh).
RESULTS = {
    "NANDOpen": "NAND_RESULT_NOEXISTS",
    "NANDPrivateOpen": "NAND_RESULT_NOEXISTS",
    "NANDPrivateSafeOpenAsync": "NAND_RESULT_NOEXISTS",
    "NANDSimpleSafeOpen": "NAND_RESULT_NOEXISTS",
    "NANDGetType": "NAND_RESULT_NOEXISTS",
    "NANDGetLengthAsync": "NAND_RESULT_NOEXISTS",
    "NANDDelete": "NAND_RESULT_NOEXISTS",
    "NANDPrivateDeleteAsync": "NAND_RESULT_NOEXISTS",
}


# Asynchronous NAND calls accept the request and report the result through their callback, later
# and on another thread (src/cf/sdk/nand_async.h); the game waits for that callback.
def is_nand_async(name, params):
    return name.startswith("NAND") and "NANDCallback callback" in params and "NANDCommandBlock* block" in params


def body(ret, policy, name=None, params=""):
    if is_nand_async(name, params):
        result = RESULTS.get(name, "cf::sdk::StubFail<s32>()")
        return f"    return cf::sdk::NandCompleteAsync(callback, block, {result});\n"
    if name in RESULTS:
        return f"    return {RESULTS[name]};\n"
    r = ret.replace("static", "").replace("inline", "").strip()
    if r == "void":
        return ""
    if r == "BOOL":
        return "    return FALSE;\n"
    if r == "bool":
        return "    return false;\n"
    if policy == "zero" or r.endswith("*"):
        return "    return {};\n"
    return f"    return cf::sdk::StubFail<{r}>();\n"


syms = [s.strip() for s in open(symfile) if s.strip() and not s.startswith("?")]
files = {}
unplaced = []
for s in sorted(syms):
    mod = next((m for m in MODULES if s.lstrip("_").startswith(m[0])), None)
    if mod is None:
        continue
    d = (None, "func") + MANUAL[s] + (None,) if s in MANUAL else find_decl(s)
    if d is None:
        unplaced.append(s)
        continue
    include, kind, ret, params, rel = d
    # RevoEX's headers clash with the decomp's (same names, and on Windows the same directories),
    # so their stubs get their own file, built with lib/RevoEX/include first (CMakeLists.txt)
    name = mod[1].replace(".cpp", "_revoex.cpp") if rel and rel.startswith("lib/RevoEX") else mod[1]
    entry = files.setdefault(name, {"what": mod[3], "includes": set(), "local_includes": set(), "defs": []})
    if include:
        entry["includes"].add(include)
    if kind == "func":
        entry["defs"].append(f"{ret} {s}({params}) {{\n{body(ret, mod[2], s, params)}}}\n")
        if is_nand_async(s, params):
            entry["local_includes"].add("nand_async.h")
    else:
        entry["defs"].append(f"{ret} {s}{params} = {{}};\n")

for name, e in files.items():
    lines = [
        f"// Link stubs for {e['what']}. Generated by tools/gen_pc_sdk_stubs.py; edit the generator,",
        "// not this file. Stubs report failure (or zero) so the game takes its own fallback paths.",
    ]
    lines += [f"#include <{i}>" for i in sorted(e["includes"])]
    lines += ['#include "stub_util.h"'] + [f'#include "{i}"' for i in sorted(e["local_includes"])]
    lines += ["", 'extern "C" {', ""]
    lines += e["defs"]
    lines += ['} // extern "C"', ""]
    with open(os.path.join(out_dir, name), "w", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"{name}: {len(e['defs'])} stubs, includes {sorted(e['includes'])}")
if unplaced:
    print("no declaration:", " ".join(unplaced))
