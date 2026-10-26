#ifndef NW4R_G3D_RES_RES_ANM_TEX_PAT_H
#define NW4R_G3D_RES_RES_ANM_TEX_PAT_H
#include <nw4r/types_nw4r.h>
#include <port/endian.h>

#include <nw4r/g3d/res/g3d_resanm.h>
#include <nw4r/g3d/res/g3d_rescommon.h>
#include <nw4r/g3d/res/g3d_resdict.h>
#include <nw4r/g3d/res/g3d_resfile.h>
#include <nw4r/g3d/res/g3d_respltt.h>
#include <nw4r/g3d/res/g3d_restex.h>

namespace nw4r {
namespace g3d {

/******************************************************************************
 *
 * TexPatAnmResult
 *
 ******************************************************************************/
struct TexPatAnmResult {
    static const int NUM_OF_ANMS = 8;

    u8 bTexExist;              // at 0x0
    u8 bPlttExist;             // at 0x1
    u8 PADDING_0x2[0x4 - 0x2]; // at 0x2
    ResTex tex[NUM_OF_ANMS];   // at 0x4
    ResPltt pltt[NUM_OF_ANMS]; // at 0x24
};

/******************************************************************************
 *
 * ResAnmTexPat
 *
 ******************************************************************************/
struct ResAnmTexPatFrmData {
    BE(f32) frame;     // at 0x0
    BE(u16) texIndex;  // at 0x4
    BE(u16) plttIndex; // at 0x6
};

struct ResAnmTexPatAnmData {
    BE(u16) numFrameValues;                 // at 0x0
    u8 PADDING_0x2[0x4 - 0x2];          // at 0x2
    BE(f32) invKeyFrameRange;               // at 0x4
    ResAnmTexPatFrmData frameValues[1]; // at 0x8
};

struct ResAnmTexPatMatData {
    enum Flag {
        FLAG_ANM_EXISTS = (1 << 0),
        FLAG_ANM_CONST = (1 << 1),
        FLAG_ANM_TEX = (1 << 2),
        FLAG_ANM_PLTT = (1 << 3),

        // Four bits in 'flags' for each animation
        NUM_OF_FLAGS = 4
    };

    union AnmData {
        BE(s32) toResAnmTexPatAnmData; // at 0x0

        struct {
            BE(u16) texIndex;  // at 0x0
            BE(u16) plttIndex; // at 0x2
        } constValue;
    };

    BE(s32) name;        // at 0x0
    BE(u32) flags;       // at 0x4
    AnmData anms[1]; // at 0x8
};

struct ResAnmTexPatInfoData {
    BE(u16) numFrame;     // at 0x0
    BE(u16) numMaterial;  // at 0x2
    BE(u16) numTexture;   // at 0x4
    BE(u16) numPalette;   // at 0x6
    BE(AnmPolicy) policy; // at 0x8
};

struct ResAnmTexPatData {
    ResBlockHeaderData header; // at 0x0
    BE(u32) revision;              // at 0x8
    BE(s32) toResFileData;         // at 0xC
    BE(s32) toTexPatDataDic;       // at 0x10
    BE(s32) toTexNameArray;        // at 0x14
    BE(s32) toPlttNameArray;       // at 0x18
    BE(s32) toResTexArray;         // at 0x1C
    BE(s32) toResPlttArray;        // at 0x20
    BE(s32) toResUserData;         // at 0x24
    BE(s32) name;                  // at 0x28
    BE(s32) original_path;         // at 0x2C
    ResAnmTexPatInfoData info; // at 0x30
};

class ResAnmTexPat : public ResCommon<ResAnmTexPatData> {
public:
    static const ulong SIGNATURE = 'PAT0';
    static const int REVISION = 3;

public:
    NW4R_G3D_RESOURCE_FUNC_DEF(ResAnmTexPat);

    bool Bind(const ResFile file);
    void Release();
    void ReleaseTex(const char* pName);

    ResFile GetParent() const;

    ulong GetRevision() const {
        return ref().revision;
    }

    bool CheckRevision() const {
        return GetRevision() == REVISION;
    }

    void GetAnmResult(TexPatAnmResult* pResult, ulong idx, f32 frame) const;

    const ResAnmTexPatMatData* GetMatAnm(int idx) const {
        return static_cast<ResAnmTexPatMatData*>(
            ofs_to_obj<ResDic>(ref().toTexPatDataDic)[idx]);
    }
    const ResAnmTexPatMatData* GetMatAnm(ulong idx) const {
        return static_cast<ResAnmTexPatMatData*>(
            ofs_to_obj<ResDic>(ref().toTexPatDataDic)[idx]);
    }

    int GetNumFrame() const {
        return ref().info.numFrame;
    }

    int GetNumMaterial() const {
        return ref().info.numMaterial;
    }

    AnmPolicy GetAnmPolicy() const {
        return ref().info.policy;
    }
};

} // namespace g3d
} // namespace nw4r

#endif
