#pragma once

#include <dwc/dwc_memfunc.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DWCi_MEMFUNC_SIGNATURE 'DWCM'

typedef struct {
  u32 magic;
  u32 size;
  u32 _[6];
} DWCi_AllocateHeader;

DWCi_AllocateHeader* DWCi_GetAllocateHeader(void* block);
void* DWCi_SetAllocateHeader(DWCi_AllocateHeader* blockHeader, u32 size);
u32 DWCi_GetAllocateSize(void* block);

void DWCi_SetMemFunc(DWCAllocEx allocator, DWCFreeEx freer);

// size_t, as GameSpy's gsMallocCB and the rest take (the Wii's u32; 64 bits on 64-bit hosts)
void* DWCi_GsMalloc(size_t size);
void* DWCi_GsRealloc(void* block, size_t size);
void DWCi_GsFree(void* block);
void* DWCi_GsMemalign(size_t align, size_t size);

#ifdef __cplusplus
}
#endif
