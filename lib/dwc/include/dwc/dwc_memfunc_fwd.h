#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum {
  DWC_ALLOCTYPE_AUTH,
  DWC_ALLOCTYPE_AC,
  DWC_ALLOCTYPE_BM,
  DWC_ALLOCTYPE_UTIL,
  DWC_ALLOCTYPE_BASE,
  DWC_ALLOCTYPE_LANMATCH,
  DWC_ALLOCTYPE_GHTTP,
  DWC_ALLOCTYPE_RANKING,
  DWC_ALLOCTYPE_ENC,
  DWC_ALLOCTYPE_GS,
  DWC_ALLOCTYPE_ND,
  DWC_ALLOCTYPE_TAC,
  DWC_ALLOCTYPE_OPTION_CF,
  DWC_ALLOCTYPE_NHTTP,
  DWC_ALLOCTYPE_MAIL,
  DWC_ALLOCTYPE_LAST,
  DWC_ALLOCTYPE_NUM = DWC_ALLOCTYPE_LAST
} DWCAllocType;

#if TARGET_PC
// The Wii's unsigned long is 32 bits, as DWC_Alloc/DWC_Free and the game's allocators (dwc.h) take
// it: a 64-bit host's unsigned long makes those incompatible function pointers (an error in Clang)
typedef unsigned int DWCi_MemSize;
#else
typedef unsigned long DWCi_MemSize;
#endif

typedef void* (*DWCAllocEx)(DWCAllocType name, DWCi_MemSize size, int align);
typedef void (*DWCFreeEx)(DWCAllocType name, void* ptr, DWCi_MemSize size);
typedef void* (*DWCAllocFunc)(DWCAllocType name, DWCi_MemSize size);
typedef void (*DWCFreeFunc)(DWCAllocType name, void* ptr, DWCi_MemSize size);

#ifdef __cplusplus
}
#endif
