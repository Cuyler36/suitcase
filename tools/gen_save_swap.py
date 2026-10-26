# Generates the save file's byte-order map (src/cf/game/save_codec.cpp uses it).
#
# Usage: python tools/gen_save_swap.py <repo root> <output .cpp>   (run by CMake at build time)
#
# The save file (rvforest.dat) is the bytes of dSaveData_c in the Wii's layout, big-endian. The PC
# keeps the save native in memory and converts at the file boundary, so it needs to know where every
# multi-byte value in dSaveData_c is. This script reads the save structs' declarations from the
# headers (starting at dSaveData_c and following member types) and emits, per struct, a visitor that
# names each member: the compiler supplies offsets, array sizes and value widths (offsetof /
# decltype), so nothing here evaluates macros or computes layouts. Each struct also gets a
# compile-time completeness check: a gap between consecutive members larger than the next member's
# alignment padding means a member was missed, and fails the build.
#
# Bit-fields: one-byte storage units declared with BITFIELD_BYTE_n (include/lib/port/bitfield.h)
# already have the Wii's layout in memory and are bytes here; any other bit-field fails generation.
# Unions are visited through the member named in UNION_VIEWS (or as bytes when every member is
# byte-sized).
import os
import re
import sys

root, out_path = sys.argv[1], sys.argv[2]
ROOT_TYPES = ["dSaveData_c"]
HEADER_DIRS = ["include/game", "include/lib"]

# Named union types the file stores as bytes
UNION_VIEWS = {
    # dMD_c: { u8 month, day } over u16 date; the bytes are month then day on the Wii
    "dMD_c": "BYTES",
}

# Anonymous unions whose views are not all bytes need a decision. Owner class -> the members the file
# uses (named members of the union or of its anonymous structs), visited in this order
ANON_UNION_VIEWS = {
    # { dItem::BITM mBITM; struct { u8 mData[0x1FFC]; u32 mChecksum; }; }: the item definition (read
    # natively by the game), the archive after it (bytes) and the slot's checksum
    "dSaveDLItem_c": ["mBITM", "mChecksum"],
}

# Anonymous unions whose layout depends on the data: owner class -> handled by VisitUnion<owner>
# (src/cf/game/save_codec.cpp), which reads the data to pick the view
UNION_HANDLERS = {
    # mPlayer / mAnimal / mText by mType
    "dMailAddress_c",
}

# Byte arrays that hold one multi-byte value (copied in and out with memcpy, so they can sit at
# unaligned save offsets): class -> {member: the value's size}
BYTES_AS_VALUES = {
    "dTimeStamp_c": {"mData": 8},                  # an OSTime (d_time_stamp.cpp)
    "dSvWeek::Week_c": {"mOffset": 8, "mWeek": 2}, # s64, s16 (d_sv_week.cpp)
}

# Members that hold GPU data, which stays big-endian in memory as the GPU (and aurora) reads it: the
# file's bytes as they are. class -> members
GPU_DATA = {
    # RGB5A3 entries copied from the item palettes (disc data) and handed to GX as a TLUT
    "dDesign_c": {"mPalette"},
}

BYTE_TYPES = {"u8", "s8", "char", "bool", "unsigned char", "signed char"}


class Error(Exception):
    pass


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0).split("\n")[0]) + "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def preprocess(text):
    # Keeps what the PC build compiles: TARGET_PC is 1; `#if 0` is dropped; other conditions keep
    # their first branch
    out = []
    stack = []  # (active, taken)
    for line in text.split("\n"):
        s = line.strip()
        m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", s)
        if not m:
            if all(a for a, _ in stack):
                out.append(line)
            else:
                out.append("")
            continue
        kind, cond = m.group(1), m.group(2).strip()
        if kind in ("if", "ifdef", "ifndef"):
            if kind == "if" and cond in ("0", "!TARGET_PC", "!(TARGET_PC)"):
                val = False
            elif kind == "if" and cond in ("TARGET_PC", "TARGET_PC == 1"):
                val = True
            elif kind == "ifndef":
                val = True  # include guards and "#ifndef X / #define X"
            else:
                val = True
            stack.append([val, val])
        elif kind == "elif":
            top = stack[-1]
            top[0] = not top[1] and False
        elif kind == "else":
            top = stack[-1]
            top[0] = not top[1]
            top[1] = True
        elif kind == "endif":
            stack.pop()
        out.append("")
    return "\n".join(out)


def match_brace(text, i):
    depth = 0
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise Error("unbalanced braces")


class Class:
    def __init__(self, name, kind, bases, body, path):
        self.name, self.kind, self.bases, self.body, self.path = name, kind, bases, body, path
        self.raw_body = body


classes = {}  # qualified name -> Class


def scan(text, path, scope):
    i = 0
    pat = re.compile(r"\b(namespace|class|struct|union)\s+((?:PC_ALIGNAS|ALIGN)\s*\([^)]*\)\s+)?([A-Za-z_]\w*)?\s*(:[^{;:][^{;]*)?\{|\bnamespace\s*\{", re.S)
    while True:
        m = pat.search(text, i)
        if not m:
            return
        kind = m.group(1)
        brace = m.end() - 1
        end = match_brace(text, brace)
        if kind == "namespace" or kind is None:
            name = m.group(3) or ""
            # `class PC_ALIGNAS(32) X` puts the macro in group 2; for namespaces group 3 is the name
            inner = scope + [name] if name else scope
            scan(text[brace + 1 : end], path, inner)
        else:
            name = m.group(3)
            if name:
                q = "::".join(scope + [name])
                bases = []
                if m.group(4):
                    for b in m.group(4)[1:].split(","):
                        b = re.sub(r"\b(public|protected|private|virtual)\b", "", b).strip()
                        if b:
                            bases.append(b)
                body = text[brace + 1 : end]
                if q not in classes or len(body) > len(classes[q].body):
                    classes[q] = Class(q, kind, bases, body, path)
                scan(body, path, scope + [name])
        i = end + 1


for d in HEADER_DIRS:
    for dp, _, fs in os.walk(os.path.join(root, d)):
        for f in sorted(fs):
            if f.endswith((".h", ".hpp")):
                p = os.path.join(dp, f)
                text = preprocess(strip_comments(open(p, encoding="utf-8", errors="replace").read()))
                try:
                    scan(text, p, [])
                except Error:
                    pass


def statements(body):
    # Splits a class body at depth 0: member declarations end with ';'; function bodies and nested
    # type definitions are kept whole
    out = []
    i, start = 0, 0
    while i < len(body):
        c = body[i]
        if c == "{":
            j = match_brace(body, i)
            head = body[start:i]
            k = j + 1
            while k < len(body) and body[k] in " \t\r\n":
                k += 1
            if re.search(r"\)\s*(const\s*)?(noexcept\s*)?(override\s*)?(:.*)?$", head.strip(), re.S) and not re.search(r"\b(union|struct|class|enum)\b", head):
                out.append(("func", head))  # inline function: no ';' after its body
                i = j + 1
                start = i
                continue
            # a nested type (with or without declarators before its ';')
            semi = body.find(";", j)
            out.append(("type", body[start : semi + 1]))
            i = semi + 1
            start = i
            continue
        if c == ";":
            out.append(("decl", body[start:i]))
            start = i + 1
        i += 1
    return out


def resolve(name, scope_cls):
    # Finds a member type's class: the owner's own scope outwards, then globally
    if name in ("u8", "s8", "u16", "s16", "u32", "s32", "u64", "s64", "f32", "f64", "bool", "BOOL", "char", "int", "wchar16_t"):
        return None
    parts = scope_cls.split("::")
    for k in range(len(parts), -1, -1):
        q = "::".join(parts[:k] + [name])
        if q in classes:
            return q
    if name in classes:
        return name
    return None


def parse_decl(cls, s):
    # One member declaration -> [(kind, name, type, width)]: kind "member" or "bit"
    s = re.sub(r"\bALIGN\s*\([^)]*\)", "", s).strip()
    m = re.match(r"(.*?)\s*([A-Za-z_]\w*)?\s*:\s*(\d+)$", s)
    if m and "::" not in s[s.rfind(":") - 1 : s.rfind(":") + 1]:
        return [("bit", m.group(2), m.group(1).strip(), int(m.group(3)))]
    m = re.match(r"(.*?)\s*([A-Za-z_]\w*)\s*((?:\[[^\]]*\]\s*)*)$", s)
    if not m:
        raise Error(f"{cls.name}: cannot read `{s}`")
    typ = m.group(1)
    names = [m.group(2)]
    if "," in typ:  # `u8 a, b`
        first, *rest = typ.split(",")
        typ = first.rsplit(None, 1)[0]
        names = [first.rsplit(None, 1)[1]] + [r.strip() for r in rest] + names
    if "*" in typ:
        raise Error(f"{cls.name}::{names[0]}: a pointer in save data")
    return [("member", n, typ.strip(), 0) for n in names]


def items_of(cls, body):
    # A class (or anonymous struct) body -> [(kind, name, type, extra)]:
    #   "member"; "bit" (extra: width); "bitbyte" (a BITFIELD_BYTE_n byte, already the Wii's layout);
    #   "anon_union" (extra: views, each ("member", name, type) or ("struct", items));
    #   anonymous structs are flattened in (their members are the owner's)
    out = []
    for kind, st in statements(body):
        s = " ".join(st.split())
        if kind == "func" or not s:
            continue
        if kind == "type":
            m = re.match(r"(union|struct|class)\s*(\w+)?\s*\{(.*)\}\s*(\w+)?\s*(\[[^\]]*\])?\s*;?$", s)
            if not m:
                continue
            if m.group(1) == "union" and not m.group(2) and not m.group(4):
                views = []
                for k2, st2 in statements(m.group(3)):
                    s2 = " ".join(st2.split())
                    if not s2 or k2 == "func":
                        continue
                    if k2 == "type":
                        mm = re.match(r"(?:struct|union)\s*\{(.*)\}\s*(\w+)?\s*;?$", s2)
                        if mm and not mm.group(2):
                            views.append(("struct", None, None, items_of(cls, mm.group(1))))
                        elif mm:
                            views.append(("named_struct", mm.group(2), None, items_of(cls, mm.group(1))))
                        continue
                    if re.search(r"\bBITFIELD_BYTE_\d\s*\(", s2):
                        views.append(("struct", None, None, [("bitbyte", None, "u8", 8)]))
                        continue
                    views.append(parse_decl(cls, s2)[0])
                out.append(("anon_union", None, None, views))
            elif m.group(1) in ("struct", "class") and not m.group(2) and not m.group(4):
                out.extend(items_of(cls, m.group(3)))  # an anonymous struct
            elif m.group(4) and m.group(2):
                out.append(("member", m.group(4), m.group(2), 0))
            elif m.group(4):
                # a member of an unnamed type: its views (a union's members, or the struct itself)
                inner = items_of(cls, m.group(3))
                views = inner if m.group(1) == "union" else [("struct", None, None, inner)]
                out.append(("unnamed_type_member", m.group(4), m.group(1), views))
            continue
        s = re.sub(r"^(public|protected|private)\s*:\s*", "", s)
        if not s:
            continue
        if re.search(r"\bBITFIELD_BYTE_\d\s*\(", s):
            out.append(("bitbyte", None, "u8", 8))
            continue
        s = re.sub(r"\b(ALIGN|PC_ALIGNAS)\s*\([^)]*\)", "", s).strip()
        if re.search(r"\b(static|typedef|using|friend|enum|template|operator)\b", s) or "(" in s:
            continue
        out.extend(parse_decl(cls, s))
    return out


def members(cls):
    return items_of(cls, cls.body)


def type_class(typ, scope):
    base = re.sub(r"\b(const|volatile|mutable|unsigned|signed)\b", "", typ or "").strip()
    base = re.sub(r"<.*>", "", base).strip()
    if not base:
        return None
    return resolve(base, scope) or resolve(base.split("::")[-1], scope)


def byte_only(items, scope, depth=0):
    # Whether these items are all bytes (no byte order, no bit order to convert)
    for kind, name, typ, extra in items:
        if kind == "bitbyte":
            continue
        if kind == "bit":
            return False
        if kind in ("struct", "named_struct"):
            if not byte_only(extra, scope, depth + 1):
                return False
            continue
        if kind in ("anon_union", "unnamed_type_member"):
            if not all(byte_only([v], scope, depth + 1) for v in extra):
                return False
            continue
        t = " ".join((typ or "").split())
        if t in BYTE_TYPES:
            continue
        tq = type_class(t, scope)
        if tq is None or depth > 8 or classes[tq].kind == "union" and tq not in UNION_VIEWS:
            return False
        if tq not in UNION_VIEWS and not byte_only(members(classes[tq]), tq, depth + 1):
            return False
    return True


BIT_TYPES = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "int": 4, "BOOL": 4, "unsigned": 4, "unsigned int": 4}


def pc_reordered(cls):
    # Bit-fields declared under `#if TARGET_PC` were reordered for the PC to have the Wii's bit
    # positions (e.g. dFdBlockId_c): their units only need a byte swap
    raw = open(cls.path, encoding="utf-8", errors="replace").read()
    short = cls.name.split("::")[-1]
    m = re.search(r"\b(?:struct|class|union)\s+(?:PC_ALIGNAS\s*\([^)]*\)\s+)?" + re.escape(short) + r"\b[^;{]*\{", raw)
    if not m:
        return False
    body = raw[m.end() : match_brace(raw, m.end() - 1)]
    for blk in re.finditer(r"#\s*if\s+TARGET_PC\b(.*?)#\s*(?:else|endif)", body, re.S):
        if re.search(r"[^:]:\s*\d+\s*;", blk.group(1)):
            return True
    return False


def bit_units(cls, ms):
    # Groups consecutive bit-fields into storage units: a new unit when the type's size changes or the
    # unit is full (MWCC and MSVC agree on this; they differ in the bit order inside a unit)
    units = []  # (index of the first item, size, [widths], wii_layout)
    cur = None
    for i, (kind, name, typ, extra) in enumerate(ms):
        if kind == "bitbyte":
            units.append((i, 1, [8], True))
            cur = None
        elif kind == "bit":
            size = BIT_TYPES.get(typ)
            if size is None:
                raise Error(f"{cls.name}: bit-field type `{typ}`")
            if cur is None or cur[1] != size or sum(cur[2]) + extra > size * 8:
                cur = (i, size, [], False)
                units.append(cur)
            cur[2].append(extra)
        else:
            cur = None
    return units


needed = []
seen = set()
errors = []


def need(q):
    if q in seen:
        return
    seen.add(q)
    needed.append(q)
    cls = classes[q]
    for b in cls.bases:
        bq = resolve(b, q)
        if bq is None:
            errors.append(f"{q}: base {b} not found")
        else:
            need(bq)
    try:
        ms = members(cls)
    except Error as e:
        errors.append(str(e))
        return
    try:
        bit_units(cls, ms)
    except Error as e:
        errors.append(str(e))
    def walk(items):
        for k, n, t, extra in items:
            if k == "member":
                tq = type_class(t, q)
                if tq is not None:
                    need(tq)
            elif k == "unnamed_type_member":
                if not byte_only(extra, q):
                    errors.append(f"{q}::{n}: a member of an unnamed type that is not all bytes "
                                  "(declare its bit-fields with BITFIELD_BYTE_n, or name its type)")
            elif k == "anon_union":
                for v in extra:
                    walk([v] if v[0] == "member" else v[3])
                if q not in ANON_UNION_VIEWS and q not in UNION_HANDLERS and not all(byte_only([v], q) for v in extra):
                    errors.append(f"{q}: an anonymous union whose views are not all bytes: pick its view in "
                                  "ANON_UNION_VIEWS, or handle it in UNION_HANDLERS (tools/gen_save_swap.py)")
    walk(ms)


for r in ROOT_TYPES:
    need(r)

if errors:
    for e in errors:
        print("gen_save_swap:", e, file=sys.stderr)
    sys.exit(1)

lines = [
    "// Generated by tools/gen_save_swap.py from the save structs' declarations; do not edit.",
    "// The byte-order map of dSaveData_c (src/cf/game/save_codec.cpp).",
    '#include "save_visit.h"',
    "#include <algorithm>",
]
headers = sorted({os.path.relpath(classes[q].path, os.path.join(root, "include")).replace(os.sep, "/") for q in needed})
for h in headers:
    if h.startswith("game/") or h.startswith("lib/"):
        inc = h[4:] if h.startswith("lib/") else h
        lines.append(f"#include <{inc}>")
lines += ["", "namespace cf {", "namespace save {", ""]
for q in sorted(UNION_HANDLERS & set(needed)):
    lines.append(f"template <> void VisitUnion<{q}>(Visitor& v, u32 structBase, const u8* obj); // save_codec.cpp")
lines.append("")

def view_names(view):
    # The member names a union view makes visible
    if view[0] in ("member", "named_struct"):
        return [view[1]]
    names = []
    for k, n, _, extra in view[3]:
        if k in ("member",):
            names.append(n)
        elif k == "anon_union":
            for v in extra:
                names += view_names(v)
    return names


def emit_items(q, cls, items, out, spans, prev_end, consts):
    units = {u[0]: u for u in bit_units(cls, items)}
    for i, (kind, name, typ, extra) in enumerate(items):
        if kind == "member":
            off = f"offsetof({q}, {name})"
            value = BYTES_AS_VALUES.get(q, {}).get(name)
            if name in GPU_DATA.get(q, ()):
                out.append(f"    // {name}: GPU data, big-endian in memory too")
            elif value:
                out.append(f"    v.Value(base + {off}, {value}); // bytes holding one {value}-byte value")
                out.append(f"    static_assert(sizeof({q}::{name}) == {value}, \"{q}::{name}: not {value} bytes\");")
            else:
                out.append(f"    Visit<decltype({q}::{name})>(v, base + {off});")
            spans.append((off, f"sizeof({q}::{name})", f"alignof(decltype({q}::{name}))"))
            prev_end = f"({off} + sizeof({q}::{name}))"
        elif kind == "unnamed_type_member":
            off = f"offsetof({q}, {name})"
            out.append(f"    // {name}: bytes")
            spans.append((off, f"sizeof({q}::{name})", f"alignof(decltype({q}::{name}))"))
            prev_end = f"({off} + sizeof({q}::{name}))"
        elif kind == "anon_union":
            names = [n for vw in extra for n in view_names(vw)]
            if not names:
                raise Error(f"{q}: an anonymous union without a named member")
            # the union starts where its first member is; it ends after its furthest member
            start = f"offsetof({q}, {names[0]})"
            size = "std::max({" + ", ".join(f"offsetof({q}, {n}) + sizeof({q}::{n})" for n in names) + "}) - " + start
            align = "std::max({" + ", ".join(f"alignof(decltype({q}::{n}))" for n in names) + "})"
            if q in UNION_HANDLERS:
                out.append(f"    v.Dynamic(base, base + {start}, {size}, &VisitUnion<{q}>); // by the data")
            elif q in ANON_UNION_VIEWS:
                out.append(f"    // an anonymous union: {', '.join(ANON_UNION_VIEWS[q])}")
                for n in ANON_UNION_VIEWS[q]:
                    out.append(f"    Visit<decltype({q}::{n})>(v, base + offsetof({q}, {n}));")
            else:
                out.append(f"    // an anonymous union of bytes at {start}")
            spans.append((start, f"({size})", align))
            prev_end = f"({start} + ({size}))"
        elif i in units:
            _, size, widths, wii = units[i]
            cname = "kUnit_" + re.sub(r"\W", "_", q) + f"_{len(consts)}"
            consts.append(f"constexpr size_t {cname} = AlignUp({prev_end if prev_end else '0'}, {size});")
            off = cname
            if wii:
                out.append(f"    // a BITFIELD_BYTE byte at {off}: already the Wii's layout")
            elif pc_reordered(cls):
                out.append(f"    v.Value(base + {off}, {size}); // bit-fields reordered for the PC (#if TARGET_PC): a byte swap")
            else:
                out.append(f"    v.Bitfield(base + {off}, {size}, {{{', '.join(map(str, widths))}}});")
            spans.append((off, str(size), str(size)))
            prev_end = f"({off} + {size})"
    return prev_end


for q in reversed(needed):
    cls = classes[q]
    ms = members(cls)
    view = UNION_VIEWS.get(q)
    lines.append(f"template <> void VisitStruct<{q}>(Visitor& v, u32 base) {{")
    if cls.kind == "union" or view == "BYTES":
        lines.append(f"    // {('a union' if cls.kind == 'union' else q)}: bytes in the file")
        lines.append("}")
        lines.append("")
        continue
    spans = []  # C++ expressions (offset, size, align) for the completeness check
    prev_end = None
    body = []
    consts = []
    for b in cls.bases:
        bq = resolve(b, q)
        body.append(f"    VisitStruct<{bq}>(v, base + BaseOffset<{q}, {bq}>());")
        prev_end = f"(BaseOffset<{q}, {bq}>() + sizeof({bq}))"
    emit_items(q, cls, ms, body, spans, prev_end, consts)
    if consts:
        # the bit-field units' offsets (after the previous member, at their alignment)
        func = lines.pop()
        lines += consts
        lines.append(func)
    lines += body
    lines.append("}")
    if spans:
        chk = ", ".join(f"MemberSpan{{{o}, {z}, {a}}}" for o, z, a in spans)
        start_expr = f"BaseOffset<{q}, {resolve(cls.bases[0], q)}>() + sizeof({resolve(cls.bases[0], q)})" if cls.bases else "0"
        lines.append(f"static_assert(IsComplete<{q}>({start_expr}, {{{chk}}}), \"{q}: a member is missing from the save map\");")
    lines.append("")

lines += ["} // namespace save", "} // namespace cf", ""]


text = "\n".join(lines)
if not os.path.exists(out_path) or open(out_path, encoding="utf-8").read() != text:
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
print(f"gen_save_swap: {len(needed)} structs")
