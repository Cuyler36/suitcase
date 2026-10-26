// Bit-fields in big-endian data (the PC port).
//
// MWCC allocates bit-fields from the most significant bit of their storage unit; MSVC (and GCC/Clang
// on little-endian targets) from the least significant. With 1-byte storage units (u8/s8 bit-fields)
// the units are the same bytes on both, so declaring each byte's fields in reverse order on PC gives
// the Wii's layout, and data read from disc (or written by the Wii) needs no conversion.
//
// BITFIELD_BYTE_n(f1, ..., fn) declares the fields of one byte, f1 first, in the Wii's order (f1 in
// the most significant bits). The fields must fill the byte; name the unused bits (or add an unnamed
// `u8 : n` field) when they do not. Wider storage units would also need a byte swap and are not
// covered.
#ifndef PORT_BITFIELD_H
#define PORT_BITFIELD_H

#if TARGET_PC
#define BITFIELD_BYTE_2(a, b) b; a
#define BITFIELD_BYTE_3(a, b, c) c; b; a
#define BITFIELD_BYTE_4(a, b, c, d) d; c; b; a
#define BITFIELD_BYTE_5(a, b, c, d, e) e; d; c; b; a
#define BITFIELD_BYTE_6(a, b, c, d, e, f) f; e; d; c; b; a
#define BITFIELD_BYTE_7(a, b, c, d, e, f, g) g; f; e; d; c; b; a
#define BITFIELD_BYTE_8(a, b, c, d, e, f, g, h) h; g; f; e; d; c; b; a
#else
#define BITFIELD_BYTE_2(a, b) a; b
#define BITFIELD_BYTE_3(a, b, c) a; b; c
#define BITFIELD_BYTE_4(a, b, c, d) a; b; c; d
#define BITFIELD_BYTE_5(a, b, c, d, e) a; b; c; d; e
#define BITFIELD_BYTE_6(a, b, c, d, e, f) a; b; c; d; e; f
#define BITFIELD_BYTE_7(a, b, c, d, e, f, g) a; b; c; d; e; f; g
#define BITFIELD_BYTE_8(a, b, c, d, e, f, g, h) a; b; c; d; e; f; g; h
#endif

#endif // PORT_BITFIELD_H
