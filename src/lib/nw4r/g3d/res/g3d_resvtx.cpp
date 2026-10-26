#include <nw4r/g3d.h>

namespace nw4r {
namespace g3d {

/******************************************************************************
 *
 * ResVtxPos
 *
 ******************************************************************************/
void ResVtxPos::SetArray() {
    if (IsValid()) {
#if TARGET_PC
        GXSETARRAY(GX_VA_POS, GetData(), ref().numPos * ref().stride, ref().stride, false);
#else
        GXSetArray(GX_VA_POS, GetData(), ref().stride);
#endif
    }
}

void ResVtxPos::GetArray(const void** ppBase, u8* pStride) const {
    if (ppBase != NULL) {
        *ppBase = GetData();
    }

    if (pStride) {
        *pStride = ref().stride;
    }
}

void ResVtxPos::CopyTo(void* pDst) const {
    if (!IsValid()) {
        return;
    }

    u32 size = GetSize();
    detail::Copy32ByteBlocks(pDst, ptr(), size);
    DCStoreRange(pDst, size);
}

/******************************************************************************
 *
 * ResVtxNrm
 *
 ******************************************************************************/
void ResVtxNrm::SetArray() {
    if (IsValid()) {
#if TARGET_PC
        GXSETARRAY(GX_VA_NRM, GetData(), ref().numNrm * ref().stride, ref().stride, false);
#else
        GXSetArray(GX_VA_NRM, GetData(), ref().stride);
#endif
    }
}

void ResVtxNrm::GetArray(const void** ppBase, u8* pStride) const {
    if (ppBase != NULL) {
        *ppBase = GetData();
    }

    if (pStride) {
        *pStride = ref().stride;
    }
}

void ResVtxNrm::CopyTo(void* pDst) const {
    if (!IsValid()) {
        return;
    }

    u32 size = GetSize();
    detail::Copy32ByteBlocks(pDst, ptr(), size);
    DCStoreRange(pDst, size);
}

/******************************************************************************
 *
 * ResVtxClr
 *
 ******************************************************************************/
void ResVtxClr::SetArray(GXAttr attr) {
    if (IsValid() && (attr == GX_VA_CLR0 || attr == GX_VA_CLR1)) {
#if TARGET_PC
        GXSETARRAY(attr, GetData(), ref().numClr * ref().stride, ref().stride, false);
#else
        GXSetArray(attr, GetData(), ref().stride);
#endif
    }
}

void ResVtxClr::GetArray(const void** ppBase, u8* pStride) const {
    if (ppBase != NULL) {
        *ppBase = GetData();
    }

    if (pStride) {
        *pStride = ref().stride;
    }
}

void ResVtxClr::CopyTo(void* pDst) const {
    if (!IsValid()) {
        return;
    }

    u32 size = GetSize();
    detail::Copy32ByteBlocks(pDst, ptr(), size);
    DCStoreRange(pDst, size);
}

/******************************************************************************
 *
 * ResVtxTexCoord
 *
 ******************************************************************************/
void ResVtxTexCoord::GetArray(const void** ppBase, u8* pStride) const {
    if (ppBase != NULL) {
        *ppBase = GetData();
    }

    if (pStride) {
        *pStride = ref().stride;
    }
}

void ResVtxFurPos::SetArray(u16 idx) {
    if (IsValid()) {
        void* base_ptr = GetData(idx);
#if TARGET_PC
        GXSETARRAY(GX_VA_POS, base_ptr, ref().numFurPos * ref().stride, ref().stride, false);
#else
        GXSetArray(GX_VA_POS, base_ptr, ref().stride);
#endif
    }
}

/******************************************************************************
 *
 * DCStore
 *
 ******************************************************************************/
void ResVtxPos::DCStore(bool sync) {
    void* pBase = &ref();
    u32 size = GetSize();

    if (sync) {
        DC::StoreRange(pBase, size);
    } else {
        DC::StoreRangeNoSync(pBase, size);
    }
}

void ResVtxNrm::DCStore(bool sync) {
    void* pBase = &ref();
    u32 size = GetSize();

    if (sync) {
        DC::StoreRange(pBase, size);
    } else {
        DC::StoreRangeNoSync(pBase, size);
    }
}

void ResVtxClr::DCStore(bool sync) {
    void* pBase = &ref();
    u32 size = GetSize();

    if (sync) {
        DC::StoreRange(pBase, size);
    } else {
        DC::StoreRangeNoSync(pBase, size);
    }
}

void ResVtxTexCoord::DCStore(bool sync) {
    void* pBase = &ref();
    u32 size = GetSize();

    if (sync) {
        DC::StoreRange(pBase, size);
    } else {
        DC::StoreRangeNoSync(pBase, size);
    }
}

void ResVtxFurVec::DCStore(bool sync) {
    void* pBase = &ref();
    u32 size = GetSize();

    if (sync) {
        DC::StoreRange(pBase, size);
    } else {
        DC::StoreRangeNoSync(pBase, size);
    }
}

void ResVtxFurPos::DCStore(bool sync) {
    void* pBase = &ref();
    u32 size = GetSize();

    if (sync) {
        DC::StoreRange(pBase, size);
    } else {
        DC::StoreRangeNoSync(pBase, size);
    }
}

} // namespace g3d
} // namespace nw4r
