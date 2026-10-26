#ifndef NW4R_G3D_RES_RES_MDL_H
#define NW4R_G3D_RES_RES_MDL_H
#include <nw4r/types_nw4r.h>
#include <port/endian.h>

#include <nw4r/g3d/res/g3d_rescommon.h>
#include <nw4r/g3d/res/g3d_resdict.h>
#include <nw4r/g3d/res/g3d_resmat.h>
#include <nw4r/g3d/res/g3d_resnode.h>
#include <nw4r/g3d/res/g3d_resshp.h>
#include <nw4r/g3d/res/g3d_resvtx.h>

#include <nw4r/math.h>

namespace nw4r {
namespace g3d {

/******************************************************************************
 *
 * ResMdlInfo
 *
 ******************************************************************************/
struct ResMdlInfoDataTypedef {
    enum TexMatrixMode {
        TEXMATRIXMODE_MAYA,
        TEXMATRIXMODE_XSI,
        TEXMATRIXMODE_3DSMAX
    };

    enum EnvelopeMatrixMode {
        EVPMATRIXMODE_NORMAL,
        EVPMATRIXMODE_APPROX,
        EVPMATRIXMODE_EXACT
    };
};

enum ScalingRule {
    SCALINGRULE_STANDARD,
    SCALINGRULE_SOFTIMAGE,
    SCALINGRULE_MAYA
};

struct ResMtxIDToNodeIDData {
    BE(u32) numMtxID; // at 0x0
};

struct ResMdlInfoData : ResMdlInfoDataTypedef {
    BE(u32) size;                   // at 0x0
    BE(s32) toResMdlData;           // at 0x4
    BE(ScalingRule) scaling_rule;   // at 0x8
    BE(TexMatrixMode) tex_mtx_mode; // at 0xC
    BE(s32) vertex_size;            // at 0x10
    BE(s32) triangle_size;          // at 0x14
    BE(s32) original_path;          // at 0x18
    BE(u32) numViewMtx;             // at 0x1C
    bool need_nrm_mtx_array;    // at 0x20
    bool need_tex_mtx_array;    // at 0x21
    bool is_valid_volume;       // at 0x22
    u8 envelope_mtx_mode;       // at 0x23
    BE(s32) toMtxIDToNodeID;        // at 0x24
    BE(math::_VEC3) volume_min;     // at 0x28
    BE(math::_VEC3) volume_max;     // at 0x34
};

class ResMdlInfo : public ResCommon<ResMdlInfoData>,
                   public ResMdlInfoDataTypedef {
public:
    NW4R_G3D_RESOURCE_FUNC_DEF(ResMdlInfo);

    ScalingRule GetScalingRule() const {
        return ref().scaling_rule;
    }

    ulong GetNumViewMtx() const {
        return ref().numViewMtx;
    }

    EnvelopeMatrixMode GetEnvelopeMatrixMode() const {
        return static_cast<EnvelopeMatrixMode>(ref().envelope_mtx_mode);
    }

    ulong GetNumPosNrmMtx() const {
        const ResMtxIDToNodeIDData* pData =
            reinterpret_cast<const ResMtxIDToNodeIDData*>(
                reinterpret_cast<const u8*>(&ref()) + ref().toMtxIDToNodeID);

        return pData->numMtxID;
    }

    s32 GetNodeIDFromMtxID(ulong id) const {
        const BE(s32)* pArray = reinterpret_cast<const BE(s32)*>(
            reinterpret_cast<const u8*>(&ref()) + ref().toMtxIDToNodeID +
            sizeof(ResMtxIDToNodeIDData));

        return pArray[id];
    }
};

/******************************************************************************
 *
 * ResMdl
 *
 ******************************************************************************/
struct ResMdlData {
    ResBlockHeaderData header;         // at 0x0
    BE(u32) revision;                      // at 0x8
    BE(s32) toResFileData;                 // at 0xC
    BE(s32) toResByteCodeDic;              // at 0x10
    BE(s32) toResNodeDic;                  // at 0x14
    BE(s32) toResVtxPosDic;                // at 0x18
    BE(s32) toResVtxNrmDic;                // at 0x1C
    BE(s32) toResVtxClrDic;                // at 0x20
    BE(s32) toResVtxTexCoordDic;           // at 0x24
    BE(s32) toResVtxFurVecDic;             // at 0x28
    BE(s32) toResVtxFurPosDic;             // at 0x2C
    BE(s32) toResMatDic;                   // at 0x30
    BE(s32) toResTevDic;                   // at 0x34
    BE(s32) toResShpDic;                   // at 0x38
    BE(s32) toResTexNameToTexPlttInfoDic;  // at 0x3C
    BE(s32) toResPlttNameToTexPlttInfoDic; // at 0x40
    BE(s32) toResUserData;                 // at 0x44
    BE(s32) name;                          // at 0x48
    ResMdlInfoData info;               // at 0x4C
};

class ResMdl : public ResCommon<ResMdlData> {
public:
    static const ulong SIGNATURE = 'MDL0';
    static const int REVISION = 9;

public:
    NW4R_G3D_RESOURCE_FUNC_DEF(ResMdl);

    void Init();
    void Terminate();

    bool Bind(const ResFile file);
    void Release();
    void ReleaseTex(const char* pName);
    void ReleasePltt(const char* pName);

    ulong GetRevision() const {
        return ref().revision;
    }

    bool CheckRevision() const {
        return GetRevision() == REVISION;
    }

    const u8* GetResByteCode(const char* pName) const;

    ResNode GetResNode(const char* pName) const;
    ResNode GetResNode(const ResName name) const;
    ResNode GetResNode(int idx) const;
    ResNode GetResNode(ulong idx) const;
#if TARGET_PC
    ResNode GetResNode(size_t idx) const {
        return GetResNode(static_cast<ulong>(idx));
    }
#endif
    ulong GetResNodeNumEntries() const;

    ResVtxPos GetResVtxPos(const ResName name) const;
    ResVtxPos GetResVtxPos(int idx) const;
    ResVtxPos GetResVtxPos(ulong idx) const;
    ulong GetResVtxPosNumEntries() const;

    ResVtxNrm GetResVtxNrm(const ResName name) const;
    ResVtxNrm GetResVtxNrm(int idx) const;
    ResVtxNrm GetResVtxNrm(ulong idx) const;
    ulong GetResVtxNrmNumEntries() const;

    ResVtxClr GetResVtxClr(const ResName name) const;
    ResVtxClr GetResVtxClr(int idx) const;
    ResVtxClr GetResVtxClr(ulong idx) const;
    ulong GetResVtxClrNumEntries() const;

    ResVtxTexCoord GetResVtxTexCoord(int idx) const;
    ulong GetResVtxTexCoordNumEntries() const;

    ResVtxFurVec GetResVtxFurVec(ulong idx) const;
    ulong GetResVtxFurVecNumEntries() const;

    ResVtxFurPos GetResVtxFurPos(int idx) const;
    ResVtxFurPos GetResVtxFurPos(ulong idx) const;
    ulong GetResVtxFurPosNumEntries() const;

    ResMat GetResMat(const char* pName) const;
    ResMat GetResMat(const ResName name) const;
    ResMat GetResMat(int idx) const;
    ResMat GetResMat(ulong idx) const;
#if TARGET_PC
    // size_t is 64-bit on PC, so it matches neither the int nor the ulong overload
    ResMat GetResMat(size_t idx) const {
        return GetResMat(static_cast<ulong>(idx));
    }
#endif
    ulong GetResMatNumEntries() const;

    ResShp GetResShp(const char* pName) const;
    ResShp GetResShp(int idx) const;
    ResShp GetResShp(ulong idx) const;
#if TARGET_PC
    ResShp GetResShp(size_t idx) const {
        return GetResShp(static_cast<ulong>(idx));
    }
#endif
    ulong GetResShpNumEntries() const;

    ResTexPlttInfoOffset GetResTexPlttInfoOffsetFromTexName(int idx) const;
    ulong GetResTexPlttInfoOffsetFromTexNameNumEntries() const;

    ResMdlInfo GetResMdlInfo() {
        return ResMdlInfo(&ref().info);
    }
    ResMdlInfo GetResMdlInfo() const {
        return ResMdlInfo(const_cast<ResMdlInfoData*>(&ref().info));
    }
};

} // namespace g3d
} // namespace nw4r

#endif
