#pragma once

// The forward headers' u32 without the SDK's integer typedefs: unsigned long on the Wii, but that is
// 64-bit on LP64 hosts (Linux, macOS), where the definitions' u32 is unsigned int
#if TARGET_PC
#define DWC_FWD_U32 unsigned int
#else
#define DWC_FWD_U32 unsigned long
#endif
