#include <nw4r/g3d.h>
#include <nw4r/math.h>
#include <nw4r/ut.h>

#include <cstring>

#if TARGET_PC
#include <port/side_slot.h>
#endif

namespace nw4r {
namespace g3d {

ResFile ResAnmTexPat::GetParent() const {
    return ofs_to_obj<ResFile>(ref().toResFileData);
}

namespace {

const ResAnmTexPatFrmData* SearchFrame(const ResAnmTexPatAnmData* pAnmData,
                                       f32 frame) {
    const ResAnmTexPatFrmData& rFirst = pAnmData->frameValues[0];
    const ResAnmTexPatFrmData& rLast =
        pAnmData->frameValues[pAnmData->numFrameValues - 1];

    if (frame <= rFirst.frame) {
        return &rFirst;
    }

    if (rLast.frame <= frame) {
        return &rLast;
    }

    f32 frameOffset = frame - rFirst.frame;
    f32 numKeyFrame = math::U16ToF32(pAnmData->numFrameValues);

    f32 fEstimate = frameOffset * numKeyFrame * pAnmData->invKeyFrameRange;
    u16 iEstimate = math::F32ToU16(fEstimate);

    const ResAnmTexPatFrmData* pFrmData = &pAnmData->frameValues[iEstimate];

    if (frame < pFrmData->frame) {
        do {
            pFrmData--;
        } while (frame < pFrmData->frame);
    } else {
        do {
            pFrmData++;
        } while (pFrmData->frame <= frame);

        pFrmData--;
    }

    return pFrmData;
}

inline ResName GetResNameFromOffsetArray(BE(s32)* pStringArray, int idx) {
    s32 offset = pStringArray[idx];
    return NW4R_G3D_OFS_TO_RESNAME(pStringArray, offset);
}

// The texture and palette arrays are 4-byte slots in the file that Bind fills in. On PC a pointer does
// not fit, so the slots are SidePtrs (port/side_slot.h); on the Wii they are the ResTex/ResPltt themselves.
#if TARGET_PC
typedef SidePtr<ResTexData> ResTexSlot;
typedef SidePtr<ResPlttData> ResPlttSlot;

inline ResTex SlotToRes(const ResTexSlot& rSlot) {
    return ResTex(static_cast<ResTexData*>(rSlot));
}
inline ResPltt SlotToRes(const ResPlttSlot& rSlot) {
    return ResPltt(static_cast<ResPlttData*>(rSlot));
}
inline void SetSlot(ResTexSlot& rSlot, ResTex tex) {
    rSlot = tex.ptr();
}
inline void SetSlot(ResPlttSlot& rSlot, ResPltt pltt) {
    rSlot = pltt.ptr();
}
#else
// Macros, so the Wii code is the original's
typedef ResTex ResTexSlot;
typedef ResPltt ResPlttSlot;
#define SlotToRes(SLOT) (SLOT)
#define SetSlot(SLOT, RES) ((SLOT) = (RES))
#endif

} // namespace

void ResAnmTexPat::GetAnmResult(TexPatAnmResult* pResult, u32 idx,
                                f32 frame) const {
    (void)GetParent(); // unused

    const ResAnmTexPatMatData* pMatData = GetMatAnm(idx);
    const ResAnmTexPatMatData::AnmData* pAnmDataImpl = pMatData->anms;
    u32 flags = pMatData->flags;

    const ResAnmTexPatData& r = ref();
    const ResTexSlot* pResTexArray = ofs_to_ptr<ResTexSlot>(r.toResTexArray);
    const ResPlttSlot* pResPlttArray = ofs_to_ptr<ResPlttSlot>(r.toResPlttArray);

    pResult->bTexExist = 0;
    pResult->bPlttExist = 0;

    for (int i = 0; i < TexPatAnmResult::NUM_OF_ANMS;
         flags >>= ResAnmTexPatMatData::NUM_OF_FLAGS, i++) {

        if (!(flags & ResAnmTexPatMatData::FLAG_ANM_EXISTS)) {
            continue;
        }

        int texIndex, plttIndex;
        if (flags & ResAnmTexPatMatData::FLAG_ANM_CONST) {
            texIndex = pAnmDataImpl->constValue.texIndex;
            plttIndex = pAnmDataImpl->constValue.plttIndex;
        } else {
            const ResAnmTexPatAnmData* pAnmData =
                static_cast<const ResAnmTexPatAnmData*>(ut::AddOffsetToPtr(
                    pMatData, pAnmDataImpl->toResAnmTexPatAnmData));

            const ResAnmTexPatFrmData* pFrmData = SearchFrame(pAnmData, frame);
            texIndex = pFrmData->texIndex;
            plttIndex = pFrmData->plttIndex;
        }

        u32 targetBit = 1 << i;

        if (flags & ResAnmTexPatMatData::FLAG_ANM_TEX) {
            ResTex tex(SlotToRes(pResTexArray[texIndex]));

            if (tex.IsValid()) {
                pResult->tex[i] = tex;
                pResult->bTexExist |= targetBit;
            }
        }

        if (flags & ResAnmTexPatMatData::FLAG_ANM_PLTT) {
            ResPltt pltt(SlotToRes(pResPlttArray[plttIndex]));

            if (pltt.IsValid()) {
                pResult->pltt[i] = pltt;
                pResult->bPlttExist |= targetBit;
            }
        }

        pAnmDataImpl++;
    }
}

bool ResAnmTexPat::Bind(const ResFile file) {
    const ResAnmTexPatInfoData& rInfoData = ref().info;
    int numTexture = rInfoData.numTexture;
    int numPalette = rInfoData.numPalette;

    BE(s32)* pTexNameArray = ofs_to_ptr<BE(s32)>(ref().toTexNameArray);
    BE(s32)* pPlttNameArray = ofs_to_ptr<BE(s32)>(ref().toPlttNameArray);

    ResTexSlot* pResTexArray = ofs_to_ptr<ResTexSlot>(ref().toResTexArray);
    ResPlttSlot* pResPlttArray = ofs_to_ptr<ResPlttSlot>(ref().toResPlttArray);

    int numBound = 0;

    for (int i = 0; i < numTexture; i++) {
        ResTex tex(SlotToRes(pResTexArray[i]));

        if (tex.IsValid()) {
            numBound++;
        } else {
            ResTex found =
                file.GetResTex(GetResNameFromOffsetArray(pTexNameArray, i));

            if (found.IsValid()) {
                SetSlot(pResTexArray[i], found);
                numBound++;
            }
        }
    }

    for (int i = 0; i < numPalette; i++) {
        ResPltt pltt(SlotToRes(pResPlttArray[i]));

        if (pltt.IsValid()) {
            numBound++;
        } else {
            ResPltt found =
                file.GetResPltt(GetResNameFromOffsetArray(pPlttNameArray, i));

            if (found.IsValid()) {
                SetSlot(pResPlttArray[i], found);
                numBound++;
            }
        }
    }

    return numBound == numTexture + numPalette;
}

void ResAnmTexPat::Release() {
    const ResAnmTexPatInfoData& rInfoData = ref().info;
    int numTexture = rInfoData.numTexture;
    int numPalette = rInfoData.numPalette;

    ResTexSlot* pResTexArray = ofs_to_ptr<ResTexSlot>(ref().toResTexArray);
    ResPlttSlot* pResPlttArray = ofs_to_ptr<ResPlttSlot>(ref().toResPlttArray);

    for (int i = 0; i < numTexture; i++) {
        SetSlot(pResTexArray[i], ResTex(NULL));
    }

    for (int i = 0; i < numPalette; i++) {
        SetSlot(pResPlttArray[i], ResPltt(NULL));
    }
}

void ResAnmTexPat::ReleaseTex(const char* pName) {
    const ResAnmTexPatInfoData& rInfoData = ref().info;
    int numTexture = rInfoData.numTexture;
    u32 len = std::strlen(pName);

    BE(s32)* pTexNameArray = ofs_to_ptr<BE(s32)>(ref().toTexNameArray);
    ResTexSlot* pResTexArray = ofs_to_ptr<ResTexSlot>(ref().toResTexArray);

    for (int i = 0; i < numTexture; i++) {
        ResName name = GetResNameFromOffsetArray(pTexNameArray, i);
        bool same = false;

        if (len == name.GetLength() &&
            std::strcmp(name.GetName(), pName) == 0) {
            same = true;
        }

        if (same) {
            SetSlot(pResTexArray[i], ResTex(NULL));
            return;
        }
    }
}

} // namespace g3d
} // namespace nw4r
