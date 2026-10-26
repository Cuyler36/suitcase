#include <nw4r/ef.h>

#include <cstring>

#if TARGET_PC
#include <nw4r/ef/ef_field.h>
#include <nw4r/ef/ef_postfield.h>
#include <port/endian.h>
#include <port/side_slot.h>
#endif

namespace nw4r {
namespace ef {

#if TARGET_PC
/******************************************************************************
 *
 * PC: project lists
 *
 ******************************************************************************/
// A project's list link is an 8-byte slot in the file, too small for a host ut::Link, so the lists
// link nodes kept beside the file (port/side_slot.h); each node points back at its project.
namespace {

struct ProjectNode {
    ut::Link link;
    void *project;
};

template <typename T> ProjectNode *GetProjectNode(const T *pProject) {
    return static_cast<ProjectNode *>(
        port::SideSlotObject(pProject->projectlink, sizeof(ProjectNode)));
}

void *ProjectOf(void *pNode) {
    return pNode != NULL ? static_cast<ProjectNode *>(pNode)->project : NULL;
}

void *ProjectList_GetFirst(const ut::List *pList) {
    return ProjectOf(ut::List_GetFirst(pList));
}

void *ProjectList_GetLast(const ut::List *pList) {
    return ProjectOf(ut::List_GetLast(pList));
}

template <typename T>
void *ProjectList_GetNext(const ut::List *pList, const T *pProject) {
    return ProjectOf(ut::List_GetNext(pList, GetProjectNode(pProject)));
}

template <typename T>
void *ProjectList_GetPrev(const ut::List *pList, const T *pProject) {
    return ProjectOf(ut::List_GetPrev(pList, GetProjectNode(pProject)));
}

template <typename T> void ProjectList_Append(ut::List *pList, T *pProject) {
    ProjectNode *pNode = GetProjectNode(pProject);
    std::memset(&pNode->link, 0, sizeof(ut::Link));
    pNode->project = pProject;
    ut::List_Append(pList, pNode);
}

template <typename T> void ProjectList_Remove(ut::List *pList, T *pProject) {
    ut::List_Remove(pList, GetProjectNode(pProject));
}

} // namespace

#define EF_PROJECT_LIST(fn) ProjectList_##fn
#define EF_PROJECT_LINK_OFFSET(T) offsetof(ProjectNode, link)
#else
#define EF_PROJECT_LIST(fn) ut::List_##fn
#define EF_PROJECT_LINK_OFFSET(T) offsetof(T, projectlink)
#endif

Resource Resource::mResource;

Resource *Resource::GetInstance() {
    return &mResource;
}

Resource::Resource() {
    Initialize();
}

void Resource::Initialize() {
    ut::List_Init(&mBREFFList, EF_PROJECT_LINK_OFFSET(EffectProject));
    ut::List_Init(&mBREFTList, EF_PROJECT_LINK_OFFSET(TextureProject));
    mNumEmitter = 0;
    mNumTexture = 0;
}

/******************************************************************************
 *
 * Binary resource implementation
 *
 ******************************************************************************/
struct FileHeader {
    char signature[4]; // at 0x0
    u8 byteorder[2];   // at 0x4
    u16 version;       // at 0x6
    u32 filesize;      // at 0x8
    u16 headersize;    // at 0xC
    u16 blocknum;      // at 0xE
};

struct BlockHeader {
    char type[4];   // at 0x0
    u32 headersize; // at 0x4
};

struct NameTable {
    u32 tablesize;   // at 0x0
    u16 numEntry;    // at 0x5
    u16 PADDING_0x4; // at 0x6
};

#if TARGET_PC
/******************************************************************************
 *
 * PC: byte order
 *
 ******************************************************************************/
namespace {

// BREFF and BREFT are big-endian, and nw4r ef reads them in place through raw pointers, so on PC
// each file is converted to host order once, when it is added (as EGG::MsgRes converts BMG text).
// The file header's byte-order mark records the conversion. The layouts follow the readers:
// EmitterResource (ef_res_emitter.h), ParticleParameterDesc (ef_particle.h), the animation curves
// (ef_animcurve.cpp, ef_particlemanager.cpp), InfoField (ef_field.h), PostFieldInfo
// (ef_postfield.h) and TextureData (ef_res_texture.h). Name tables are read byte by byte and stay
// as they are.
void SwapU16At(u8 *p) {
    u16 v;
    std::memcpy(&v, p, sizeof(v));
    v = RES_U16(v);
    std::memcpy(p, &v, sizeof(v));
}

void SwapU32At(u8 *p) {
    u32 v;
    std::memcpy(&v, p, sizeof(v));
    v = RES_U32(v);
    std::memcpy(p, &v, sizeof(v));
}

void SwapU32Range(u8 *p, u32 count) {
    for (u32 i = 0; i < count; i++) {
        SwapU32At(p + i * sizeof(u32));
    }
}

bool ConvertFileHeader(FileHeader *pHeader) {
    // Big-endian files carry FE FF; a converted file is marked FF FE
    if (pHeader->byteorder[0] == 0xFF && pHeader->byteorder[1] == 0xFE) {
        return false;
    }
    pHeader->byteorder[0] = 0xFF;
    pHeader->byteorder[1] = 0xFE;
    be_swap(pHeader->version);
    be_swap(pHeader->filesize);
    be_swap(pHeader->headersize);
    be_swap(pHeader->blocknum);
    return true;
}

enum TrackKind {
    TRACK_U8,
    TRACK_F32,
    TRACK_ROTATE,
    TRACK_TEXTURE,
    TRACK_CHILD,
    TRACK_FIELD,
    TRACK_POSTFIELD,
    TRACK_UNKNOWN,
};

TrackKind GetTrackKind(const AnimCurveHeader *pHeader, bool emitterTrack) {
    if (emitterTrack) {
        return TRACK_F32; // Emitter::CalcEmitter
    }
    switch (pHeader->curveFlag) { // ParticleManager::CalcParticle
    case 0: return TRACK_U8;
    case 3: return TRACK_F32;
    case 6: return TRACK_ROTATE;
    case 4: return TRACK_TEXTURE;
    case 5: return TRACK_CHILD;
    case 7: return TRACK_FIELD;
    case 2: return TRACK_POSTFIELD;
    default: return TRACK_UNKNOWN;
    }
}

int CountBits(u8 x) {
    int n = 0;
    for (; x != 0; x >>= 1) {
        n += x & 1;
    }
    return n;
}

// Swaps a table section (AnimCurveRandom / AnimCurveRandomTable: u16 count, then entries)
void ConvertEntryTable(u8 *pSection, u32 sectionSize, u32 entrySize, TrackKind kind,
                       int elemCount, bool randomTable) {
    if (sectionSize < 4 || entrySize == 0) {
        return;
    }
    SwapU16At(pSection);
    u16 count;
    std::memcpy(&count, pSection, sizeof(count));
    u32 maxCount = (sectionSize - 4) / entrySize;
    if (count > maxCount) {
        count = static_cast<u16>(maxCount);
    }

    for (u32 i = 0; i < count; i++) {
        u8 *e = pSection + 4 + i * entrySize;
        switch (kind) {
        case TRACK_F32:
        case TRACK_FIELD:
        case TRACK_POSTFIELD:
        case TRACK_ROTATE:
            // (base, range) f32 pairs per element; rotate entries end in a direction byte
            SwapU32Range(e, elemCount * 2);
            break;
        case TRACK_TEXTURE:
            SwapU16At(e + 2); // KeyTexture::nameIdx (RndKey starts with a KeyTexture)
            break;
        case TRACK_CHILD:
            if (randomTable) {
                SwapU16At(e + 0);  // ChildData: EmitterInheritSetting::speed
                SwapU16At(e + 10); // ChildData::name
            }
            break;
        default:
            break; // U8 values are bytes
        }
    }
}

void ConvertTrack(u8 *pTrack, u32 size, bool emitterTrack) {
    if (size < sizeof(AnimCurveHeader)) {
        return;
    }
    AnimCurveHeader *pHeader = reinterpret_cast<AnimCurveHeader *>(pTrack);
    if (pHeader->magic != NW4R_EF_MAGIC_ANIMCURVE && pHeader->magic != NW4R_EF_MAGIC_ANIMBAKED) {
        return; // the game skips these tracks
    }
    be_swap(pHeader->randomSeed);
    be_swap(pHeader->frameLength);
    be_swap(pHeader->keyTable);
    be_swap(pHeader->rangeTable);
    be_swap(pHeader->randomTable);
    be_swap(pHeader->nameTable);
    be_swap(pHeader->infoTable);

    const u32 total = sizeof(AnimCurveHeader) + pHeader->keyTable + pHeader->rangeTable +
                      pHeader->randomTable + pHeader->nameTable + pHeader->infoTable;
    if (total > size) {
        OSReport("[nw4r::ef] animation curve sections overrun the track (%u > %u)\n", total, size);
        return;
    }

    const TrackKind kind = GetTrackKind(pHeader, emitterTrack);
    if (kind == TRACK_UNKNOWN) {
        return;
    }
    const int elemCount = CountBits(pHeader->kindEnable);
    const bool f32Values = kind == TRACK_F32 || kind == TRACK_ROTATE || kind == TRACK_FIELD ||
                           kind == TRACK_POSTFIELD;

    u8 *pKey = pTrack + sizeof(AnimCurveHeader);
    u8 *pRange = pKey + pHeader->keyTable;
    u8 *pRandom = pRange + pHeader->rangeTable;
    u8 *pName = pRandom + pHeader->randomTable;
    u8 *pInfo = pName + pHeader->nameTable;

    if (pHeader->magic == NW4R_EF_MAGIC_ANIMBAKED) {
        // One value per enabled element per frame (AnimBakedExecuteU8 / F32)
        if (f32Values) {
            SwapU32Range(pKey, pHeader->keyTable / sizeof(u32));
        }
    } else if (pHeader->keyTable >= 4) {
        // Keys: u16 frame, u8 flag, u8, u8 curve types[8], then the values from KEY_PARAM1 (12),
        // or a u16 random index when the flag is set
        u32 framesize;
        switch (kind) {
        case TRACK_U8: framesize = (12 + elemCount + 1) & ~1; break;
        case TRACK_TEXTURE: framesize = 16; break;
        case TRACK_CHILD: framesize = 24; break;
        default: framesize = 12 + 4 * elemCount; break;
        }

        SwapU16At(pKey);
        u16 count;
        std::memcpy(&count, pKey, sizeof(count));
        u32 maxCount = (pHeader->keyTable - 4) / framesize;
        if (count > maxCount) {
            count = static_cast<u16>(maxCount);
        }

        for (u32 i = 0; i < count; i++) {
            u8 *k = pKey + 4 + i * framesize;
            SwapU16At(k); // frame
            if (k[2] != 0) {
                SwapU16At(k + 12); // random index
                continue;
            }
            switch (kind) {
            case TRACK_TEXTURE:
                SwapU16At(k + 12 + 2); // KeyTexture::nameIdx
                break;
            case TRACK_CHILD:
                SwapU16At(k + 12 + 0);  // ChildData: EmitterInheritSetting::speed
                SwapU16At(k + 12 + 10); // ChildData::name
                break;
            case TRACK_U8:
                break;
            default:
                SwapU32Range(k + 12, elemCount);
                break;
            }
        }
    }

    // Random ranges and random sets
    u32 rangeEntry, setEntry;
    switch (kind) {
    case TRACK_U8: rangeEntry = setEntry = 2 * elemCount; break;
    case TRACK_ROTATE: rangeEntry = setEntry = 8 * elemCount + 4; break;
    case TRACK_TEXTURE: rangeEntry = 8; setEntry = 4; break;
    case TRACK_CHILD: rangeEntry = 0; setEntry = 12; break;
    default: rangeEntry = setEntry = 8 * elemCount; break;
    }
    ConvertEntryTable(pRange, pHeader->rangeTable, rangeEntry, kind, elemCount, false);
    ConvertEntryTable(pRandom, pHeader->randomTable, setEntry, kind, elemCount, true);

    // Name table: u16 count, the 4-byte work slots, then (u16 length, name) per entry
    if (pHeader->nameTable >= 4) {
        u8 *pEnd = pName + pHeader->nameTable;
        SwapU16At(pName);
        u16 count;
        std::memcpy(&count, pName, sizeof(count));
        u8 *p = pName + 4 + count * sizeof(AnimCurveNameTable::Name);
        for (u32 i = 0; i < count && p + sizeof(u16) <= pEnd; i++) {
            SwapU16At(p);
            u16 len;
            std::memcpy(&len, p, sizeof(len));
            p += sizeof(u16) + len;
        }
    }

    if (kind == TRACK_FIELD && pHeader->infoTable > 4) {
        // InfoField: four bytes, then the field's parameters (all f32 but InfoFieldRandom::interval)
        u32 infoSize = pHeader->infoTable < sizeof(InfoField) ? pHeader->infoTable : sizeof(InfoField);
        if (pHeader->kindType == 7) {
            InfoField *pField = reinterpret_cast<InfoField *>(pInfo);
            if (infoSize >= offsetof(InfoField, u) + sizeof(InfoFieldRandom)) {
                be_swap(pField->u.random.power);
                be_swap(pField->u.random.diffusion);
                be_swap(pField->u.random.interval);
            }
        } else {
            SwapU32Range(pInfo + 4, (infoSize - 4) / sizeof(u32));
        }
    }

    if (kind == TRACK_POSTFIELD && pHeader->infoTable != 0) {
        if (pHeader->infoTable < sizeof(PostFieldInfo)) {
            OSReport("[nw4r::ef] postfield info is %u bytes, expected %u\n", pHeader->infoTable,
                     static_cast<u32>(sizeof(PostFieldInfo)));
            return;
        }
        PostFieldInfo *pInfoPF = reinterpret_cast<PostFieldInfo *>(pInfo);
        be_swap(pInfoPF->mAnimatableParams.mSize);
        be_swap(pInfoPF->mAnimatableParams.mRotate);
        be_swap(pInfoPF->mAnimatableParams.mTranslate);
        be_swap(pInfoPF->mReferenceSpeed);
        be_swap(pInfoPF->mCollisionOption);
        be_swap(pInfoPF->mStartFrame);
        be_swap(pInfoPF->mSpeedFactor);
        be_swap(pInfoPF->mChildOption.mInheritSetting.speed);
        be_swap(pInfoPF->mChildOption.mNameIdx);
        be_swap(pInfoPF->mWrapScale);
        be_swap(pInfoPF->mWrapRotate);
        be_swap(pInfoPF->mWrapTranslate);
    }
}

void ConvertEmitterDesc(EmitterDesc *d) {
    be_swap(d->commonFlag);
    be_swap(d->emitFlag);
    be_swap(d->emitLife);
    be_swap(d->ptclLife);
    be_swap(d->emitEmit);
    be_swap(d->emitEmitStart);
    be_swap(d->emitEmitPast);
    be_swap(d->emitEmitInterval);
    be_swap(d->commonParam);
    be_swap(d->emitEmitDiv);
    be_swap(d->velPowerRadiationDir);
    be_swap(d->velPowerYAxis);
    be_swap(d->velPowerRandomDir);
    be_swap(d->velPowerNormalDir);
    be_swap(d->velDiffusionEmitterNormal);
    be_swap(d->velPowerSpecDir);
    be_swap(d->velDiffusionSpecDir);
    be_swap(d->velSpecDir);
    be_swap(d->scale);
    be_swap(d->rotate);
    be_swap(d->translate);
    be_swap(d->randomSeed);

    EmitterDrawSetting &s = d->drawSetting;
    be_swap(s.mFlags);
    be_swap(s.mAlphaFlickCycle);
    be_swap(s.mLighting.mRadius);
    be_swap(s.mLighting.mPosition);
    be_swap(s.mIndTexOffsetMtx);
    be_swap(s.zOffset);
}

void ConvertParticleDesc(ParticleParameterDesc *d) {
    be_swap(d->size);
    be_swap(d->scale);
    be_swap(d->rotate);
    be_swap(d->textureScale);
    be_swap(d->textureRotate);
    be_swap(d->textureTranslate);
    be_swap(d->textureWrap);
    be_swap(d->rotateOffset);

    // Texture names: (u16 length, name) per layer (Resource::RelocateCommand)
    u8 *p = d->textureNames;
    for (int i = 0; i < TEX_LAYER_MAX; i++) {
        SwapU16At(p);
        u16 len;
        std::memcpy(&len, p, sizeof(len));
        p += sizeof(u16) + len;
    }
}

void ConvertEmitter(EmitterResource *pResource) {
    u8 *pBase = reinterpret_cast<u8 *>(pResource);
    SwapU32At(pBase + 4); // headersize (after the 4-byte name slot)
    u32 headersize;
    std::memcpy(&headersize, pBase + 4, sizeof(headersize));
    if (headersize < sizeof(EmitterDesc)) {
        OSReport("[nw4r::ef] emitter descriptor is %u bytes, expected %u\n", headersize,
                 static_cast<u32>(sizeof(EmitterDesc)));
    }
    ConvertEmitterDesc(pResource->GetEmitterDesc());

    SwapU32At(pResource->SkipEmitterDesc()); // particle descriptor size
    ConvertParticleDesc(pResource->GetParticleParameterDesc());

    // Track tables: (u16 count, u16 init count, u32 pointers[count], u32 sizes[count]) for the
    // particle tracks, then for the emitter tracks; the tracks follow
    u8 *pTbl = pResource->SkipParticleParameterDesc();
    SwapU16At(pTbl);
    SwapU16At(pTbl + 2);
    u16 numPtcl = pResource->NumPtclTrack();
    SwapU32Range(pTbl + 4 + numPtcl * 4, numPtcl);

    u8 *pEmitTbl = pTbl + numPtcl * 8 + 4;
    SwapU16At(pEmitTbl);
    SwapU16At(pEmitTbl + 2);
    u16 numEmit = pResource->NumEmitTrack();
    SwapU32Range(pEmitTbl + 4 + numEmit * 4, numEmit);

    u8 *pTail = pEmitTbl + numEmit * 8 + 4;
    const u32 *pPtclSize = reinterpret_cast<const u32 *>(pTbl + 4) + numPtcl;
    for (u16 i = 0; i < numPtcl; i++) {
        ConvertTrack(pTail, pPtclSize[i], false);
        pTail += pPtclSize[i];
    }
    const u32 *pEmitSize = reinterpret_cast<const u32 *>(pEmitTbl + 4) + numEmit;
    for (u16 i = 0; i < numEmit; i++) {
        ConvertTrack(pTail, pEmitSize[i], true);
        pTail += pEmitSize[i];
    }
}

template <typename T> NameTable *ConvertProject(T *pProject) {
    be_swap(pProject->headersize);
    be_swap(pProject->projectnameLen);
    NameTable *pNameTable =
        reinterpret_cast<NameTable *>(reinterpret_cast<u8 *>(pProject) + pProject->headersize);
    be_swap(pNameTable->tablesize);
    be_swap(pNameTable->numEntry);
    return pNameTable;
}

// Calls func(name table, offset) for each entry (the entries are read byte by byte)
template <typename F> void ForEachEntry(NameTable *pNameTable, F func) {
    u8 *pData = reinterpret_cast<u8 *>(pNameTable) + sizeof(NameTable);
    for (int i = 0; i < pNameTable->numEntry; i++) {
        int len = (pData[0] << 8) + pData[1];
        pData += sizeof(u16) + len;
        u32 offset = (pData[0] << 24) + (pData[1] << 16) + (pData[2] << 8) + pData[3];
        pData += sizeof(u32) * 2; // offset, size
        func(reinterpret_cast<u8 *>(pNameTable) + offset);
    }
}

void ConvertBREFF(u8 *pData) {
    FileHeader *pFileHeader = reinterpret_cast<FileHeader *>(pData);
    if (!ConvertFileHeader(pFileHeader)) {
        return;
    }
    BlockHeader *pBlockHeader = reinterpret_cast<BlockHeader *>(pData + pFileHeader->headersize);
    be_swap(pBlockHeader->headersize);
    EffectProject *pProject = reinterpret_cast<EffectProject *>(pBlockHeader + 1);
    ForEachEntry(ConvertProject(pProject), [](u8 *pEntry) {
        ConvertEmitter(reinterpret_cast<EmitterResource *>(pEntry));
    });
}

void ConvertBREFT(u8 *pData) {
    FileHeader *pFileHeader = reinterpret_cast<FileHeader *>(pData);
    if (!ConvertFileHeader(pFileHeader)) {
        return;
    }
    BlockHeader *pBlockHeader = reinterpret_cast<BlockHeader *>(pData + pFileHeader->headersize);
    be_swap(pBlockHeader->headersize);
    TextureProject *pProject = reinterpret_cast<TextureProject *>(pBlockHeader + 1);
    ForEachEntry(ConvertProject(pProject), [](u8 *pEntry) {
        // The image and palette data stay in GX (big-endian) order, as on the Wii
        TextureData *pTex = reinterpret_cast<TextureData *>(pEntry);
        be_swap(pTex->width);
        be_swap(pTex->height);
        be_swap(pTex->dataSize);
        be_swap(pTex->tlutEntries);
        be_swap(pTex->tlutSize);
    });
}

} // namespace
#endif

/* static */ void breffRelocate(EffectProject *pEffProject) {
    u8 *pPtr = reinterpret_cast<u8 *>(pEffProject);
    pPtr += pEffProject->headersize;

    u8 *pData = pPtr;
    NameTable *pNameTable = reinterpret_cast<NameTable *>(pPtr);

    pData += sizeof(NameTable);

    for (int i = 0; i < pNameTable->numEntry; i++) {
        int len = (pData[0] << 8) + pData[1];
        pData += sizeof(u16);

        char *pName = reinterpret_cast<char *>(pData);
        pData += len;

        u32 offset =
            (pData[0] << 24) + (pData[1] << 16) + (pData[2] << 8) + pData[3];
        pData += sizeof(u32);

        u32 size =
            (pData[0] << 24) + (pData[1] << 16) + (pData[2] << 8) + pData[3];
        pData += sizeof(u32);

        EmitterResource *pResource = reinterpret_cast<EmitterResource *>(
            reinterpret_cast<u8 *>(pNameTable) + offset);

        // I think they really did this, the member is private...
#if TARGET_PC
        pResource->SetName(pName);
#else
        *reinterpret_cast<char **>(pResource) = pName;
#endif

        pResource->LocateTracks();
    }
}

static u16 breffNumEmitter(EffectProject *pEffProject) {
    u8 *pPtr = reinterpret_cast<u8 *>(pEffProject);
    pPtr += pEffProject->headersize;

    NameTable *pNameTable = reinterpret_cast<NameTable *>(pPtr);
    return pNameTable->numEntry;
}

static u16 breftNumTexture(TextureProject *pTexProject) {
    u8 *pPtr = reinterpret_cast<u8 *>(pTexProject);
    pPtr += pTexProject->headersize;

    NameTable *pNameTable = reinterpret_cast<NameTable *>(pPtr);
    return pNameTable->numEntry;
}

static EmitterResource *breffSearchName(EffectProject *pEffProject,
                                        const char *pEffName) {

    u8 *pPtr = reinterpret_cast<u8 *>(pEffProject);
    pPtr += pEffProject->headersize;

    u8 *pData = pPtr;
    NameTable *pNameTable = reinterpret_cast<NameTable *>(pPtr);

    pData += sizeof(NameTable);

    for (int i = 0; i < pNameTable->numEntry; i++) {
        int len = (pData[0] << 8) + pData[1];
        pData += sizeof(u16);

        char *pName = reinterpret_cast<char *>(pData);
        pData += len;

        u32 offset =
            (pData[0] << 24) + (pData[1] << 16) + (pData[2] << 8) + pData[3];
        pData += sizeof(u32);

        // Skip size
        pData += sizeof(u32);

        if (std::strcmp(pEffName, pName) == 0) {
            return reinterpret_cast<EmitterResource *>(
                reinterpret_cast<u8 *>(pNameTable) + offset);
        }
    }

    return NULL;
}

static EmitterResource *breffIndexOf(EffectProject *pEffProject, u16 idx) {
    u8 *pPtr = reinterpret_cast<u8 *>(pEffProject);
    pPtr += pEffProject->headersize;

    u8 *pData = pPtr;
    NameTable *pNameTable = reinterpret_cast<NameTable *>(pPtr);

    if (idx >= pNameTable->numEntry) {
        return NULL;
    }

    pData += sizeof(NameTable);

    for (int i = 0; i < pNameTable->numEntry; i++) {
        int len = (pData[0] << 8) + pData[1];
        pData += sizeof(u16);

        char *pEffName = reinterpret_cast<char *>(pData);
        pData += len;

        u32 offset =
            (pData[0] << 24) + (pData[1] << 16) + (pData[2] << 8) + pData[3];
        pData += sizeof(u32);

        // Skip size
        pData += sizeof(u32);

        if (i == idx) {
            return reinterpret_cast<EmitterResource *>(
                reinterpret_cast<u8 *>(pNameTable) + offset);
        }
    }

    return NULL;
}

static void breftRelocate(TextureProject *pTexProject) {
    u8 *pPtr = reinterpret_cast<u8 *>(pTexProject);
    pPtr += pTexProject->headersize;

    u8 *pData = pPtr;
    NameTable *pNameTable = reinterpret_cast<NameTable *>(pPtr);

    pData += sizeof(NameTable);

    for (int i = 0; i < pNameTable->numEntry; i++) {
        int len = (pData[0] << 8) + pData[1];
        pData += sizeof(u16);

        char *pName = reinterpret_cast<char *>(pData);
        pData += len;

        u32 offset =
            (pData[0] << 24) + (pData[1] << 16) + (pData[2] << 8) + pData[3];
        pData += sizeof(u32);

        u32 size =
            (pData[0] << 24) + (pData[1] << 16) + (pData[2] << 8) + pData[3];
        pData += sizeof(u32);

        TextureData *pTexData = reinterpret_cast<TextureData *>(
            reinterpret_cast<u8 *>(pNameTable) + offset);

        pTexData->name = pName;
        pTexData->texture =
            reinterpret_cast<u8 *>(pTexData) + sizeof(TextureData);
        pTexData->tlut = reinterpret_cast<u8 *>(pTexData) +
                         sizeof(TextureData) + pTexData->dataSize;
    }
}

static TextureData *breftSearchName(TextureProject *pTexProject,
                                    const char *pTexName) {

    u8 *pPtr = reinterpret_cast<u8 *>(pTexProject);
    pPtr += pTexProject->headersize;

    u8 *pData = pPtr;
    NameTable *pNameTable = reinterpret_cast<NameTable *>(pPtr);

    pData += sizeof(NameTable);

    for (int i = 0; i < pNameTable->numEntry; i++) {
        int len = (pData[0] << 8) + pData[1];
        pData += sizeof(u16);

        char *pName = reinterpret_cast<char *>(pData);
        pData += len;

        u32 offset =
            (pData[0] << 24) + (pData[1] << 16) + (pData[2] << 8) + pData[3];
        pData += sizeof(u32);

        // Skip size
        pData += sizeof(u32);

        if (std::strcmp(pTexName, pName) == 0) {
            return reinterpret_cast<TextureData *>(
                reinterpret_cast<u8 *>(pNameTable) + offset);
        }
    }

    return NULL;
}

/******************************************************************************
 *
 * Resource interface
 *
 ******************************************************************************/
EffectProject *Resource::Add(u8 *pData) {
#if TARGET_PC
    ConvertBREFF(pData);
#endif
    u8 *pPtr = pData;

    FileHeader *pFileHeader = reinterpret_cast<FileHeader *>(pPtr);
    pPtr += pFileHeader->headersize;

    BlockHeader *pBlockHeader = reinterpret_cast<BlockHeader *>(pPtr);
    pPtr += sizeof(BlockHeader);

    EffectProject *pEffProject = reinterpret_cast<EffectProject *>(pPtr);

#if TARGET_PC
    ProjectList_Append(&mBREFFList, pEffProject);
#else
    memset(&pEffProject->projectlink, 0, sizeof(ut::Link));
    ut::List_Append(&mBREFFList, pEffProject);
#endif

    mNumEmitter += breffNumEmitter(pEffProject);
    breffRelocate(pEffProject);

    return pEffProject;
}

ResEmitter Resource::FindEmitter(const char *pName,
                                 EffectProject *pEffProject) const {

    EmitterResource *const pResource = _FindEmitter(pName, pEffProject);
    return ResEmitter(pResource);
}

EmitterResource *Resource::_FindEmitter(const char *pName,
                                        EffectProject *pEffProject) const {

    EmitterResource *pResource;

    if (pEffProject != NULL) {
        pResource = breffSearchName(pEffProject, pName);
        if (pResource != NULL) {
            return pResource;
        }
    }

    pEffProject = static_cast<EffectProject *>(EF_PROJECT_LIST(GetLast)(&mBREFFList));

    while (pEffProject != NULL) {
        pResource = breffSearchName(pEffProject, pName);
        if (pResource != NULL) {
            return pResource;
        }

        pEffProject = static_cast<EffectProject *>(
            EF_PROJECT_LIST(GetPrev)(&mBREFFList, pEffProject));
    }

    return NULL;
}

ResTexture Resource::FindTexture(const char *pName,
                                 TextureProject *pTexProject) const {

    TextureData *pTexData = _FindTexture(pName, pTexProject);
    return ResTexture(pTexData);
}

TextureData *Resource::_FindTexture(const char *pName,
                                    TextureProject *pTexProject) const {

    TextureData *pTexData;

    if (pName[0] == 0) {
        return NULL;
    }

    if (pTexProject != NULL) {
        pTexData = breftSearchName(pTexProject, pName);
        if (pTexData != NULL) {
            return pTexData;
        }
    }

    pTexProject = static_cast<TextureProject *>(EF_PROJECT_LIST(GetLast)(&mBREFTList));

    while (pTexProject != NULL) {
        pTexData = breftSearchName(pTexProject, pName);
        if (pTexData != NULL) {
            return pTexData;
        }

        pTexProject = static_cast<TextureProject *>(
            EF_PROJECT_LIST(GetPrev)(&mBREFTList, pTexProject));
    }

    return NULL;
}

u32 Resource::NumEmitter(EffectProject *pEffProject) const {
    if (pEffProject != NULL) {
        return breffNumEmitter(pEffProject);
    }

    return mNumEmitter;
}

EmitterResource *
Resource::_GetEmitterIndexOf(u32 idx, EffectProject *pEffProject) const {
    if (pEffProject != NULL) {
        u16 numEmitter = breffNumEmitter(pEffProject);

        if (idx < numEmitter) {
            return breffIndexOf(pEffProject, idx);
        }

        return NULL;
    }

    pEffProject = static_cast<EffectProject *>(EF_PROJECT_LIST(GetFirst)(&mBREFFList));

    while (pEffProject != NULL) {
        u16 numEmitter = breffNumEmitter(pEffProject);

        if (idx < numEmitter) {
            return breffIndexOf(pEffProject, idx);
        }

        idx -= numEmitter;

        pEffProject = static_cast<EffectProject *>(
            EF_PROJECT_LIST(GetNext)(&mBREFFList, pEffProject));
    }

    return NULL;
}

TextureProject *Resource::AddTexture(u8 *pData) {
#if TARGET_PC
    ConvertBREFT(pData);
#endif
    u8 *pPtr = pData;

    FileHeader *pFileHeader = reinterpret_cast<FileHeader *>(pPtr);
    pPtr += pFileHeader->headersize;

    BlockHeader *pBlockHeader = reinterpret_cast<BlockHeader *>(pPtr);
    pPtr += sizeof(BlockHeader);

    TextureProject *pTexProject = reinterpret_cast<TextureProject *>(pPtr);

#if TARGET_PC
    ProjectList_Append(&mBREFTList, pTexProject);
#else
    memset(&pTexProject->projectlink, 0, sizeof(ut::Link));
    ut::List_Append(&mBREFTList, pTexProject);
#endif

    mNumTexture += breftNumTexture(pTexProject);
    breftRelocate(pTexProject);

    return pTexProject;
}

u32 Resource::relocateCurveTexture(u8 *pCmdList, const EmitterResource *eh) {
    u32 error = 0;

    AnimCurveHeader *pHeader = reinterpret_cast<AnimCurveHeader *>(pCmdList);

    AnimCurveKey *pKey = reinterpret_cast<AnimCurveKey *>(
        reinterpret_cast<u8 *>(pHeader) + sizeof(AnimCurveHeader));

    AnimCurveRandom *pRandom = reinterpret_cast<AnimCurveRandom *>(
        reinterpret_cast<u8 *>(pKey) + pHeader->keyTable);

    AnimCurveRandomTable *pRandomTable =
        reinterpret_cast<AnimCurveRandomTable *>(
            reinterpret_cast<u8 *>(pRandom) + pHeader->rangeTable);

    AnimCurveNameTable *pNameTable = reinterpret_cast<AnimCurveNameTable *>(
        reinterpret_cast<u8 *>(pRandomTable) + pHeader->randomTable);

    if (pHeader->nameTable == 0) {
        return 0;
    }

    u8 *pName = reinterpret_cast<u8 *>(pNameTable) +
                offsetof(AnimCurveNameTable, datas) +
                pNameTable->count * sizeof(AnimCurveNameTable::Name);

    for (int i = 0; i < pNameTable->count; i++) {
        int size = *reinterpret_cast<u16 *>(pName);
        pName += sizeof(u16);

#if TARGET_PC
        pNameTable->datas[i].work =
            _FindTexture(reinterpret_cast<const char *>(pName), NULL);
#else
        pNameTable->datas[i].work = reinterpret_cast<u32>(
            _FindTexture(reinterpret_cast<const char *>(pName), NULL));
#endif

        if (pName[0] != 0 && pNameTable->datas[i].work == 0) {
            error++;
        }

        pName += size;
    }

    return error;
}

u32 Resource::relocateCurveChild(u8 *pCmdList, const EmitterResource *eh) {
    u32 error = 0;

    AnimCurveHeader *pHeader = reinterpret_cast<AnimCurveHeader *>(pCmdList);

    AnimCurveKey *pKey = reinterpret_cast<AnimCurveKey *>(
        reinterpret_cast<u8 *>(pHeader) + sizeof(AnimCurveHeader));

    AnimCurveRandom *pRandom = reinterpret_cast<AnimCurveRandom *>(
        reinterpret_cast<u8 *>(pKey) + pHeader->keyTable);

    AnimCurveRandomTable *pRandomTable =
        reinterpret_cast<AnimCurveRandomTable *>(
            reinterpret_cast<u8 *>(pRandom) + pHeader->rangeTable);

    AnimCurveNameTable *pNameTable = reinterpret_cast<AnimCurveNameTable *>(
        reinterpret_cast<u8 *>(pRandomTable) + pHeader->randomTable);

    if (pHeader->nameTable == 0) {
        return 0;
    }

    u8 *pName = reinterpret_cast<u8 *>(pNameTable) +
                offsetof(AnimCurveNameTable, datas) +
                pNameTable->count * sizeof(AnimCurveNameTable::Name);

    for (int i = 0; i < pNameTable->count; i++) {
        int size = *reinterpret_cast<u16 *>(pName);
        pName += sizeof(u16);

#if TARGET_PC
        pNameTable->datas[i].work =
            _FindEmitter(reinterpret_cast<char *>(pName), NULL);
#else
        pNameTable->datas[i].work = reinterpret_cast<u32>(
            _FindEmitter(reinterpret_cast<char *>(pName), NULL));
#endif

        if (pNameTable->datas[i].work == 0) {
            error++;
        }

        pName += size;
    }

    return error;
}

u32 Resource::RelocateCommand() {
    u32 error = 0;

    for (u32 idx = 0; idx < NumEmitter(NULL); idx++) {
        u8 currentError = 0;
        EmitterResource *pResource = _GetEmitterIndexOf(idx, NULL);
        if (pResource == NULL) {
            continue;
        }

        ParticleParameterDesc *pPtclDesc =
            pResource->GetParticleParameterDesc();
#if TARGET_PC
        SidePtr<TextureData> *ppWork = pPtclDesc->mTexture;
#else
        TextureData **ppWork = pPtclDesc->mTexture;
#endif
        u8 *pPtr = pPtclDesc->textureNames;

        for (int i = 0; i < TEX_LAYER_MAX; i++) {
            u16 size = *reinterpret_cast<u16 *>(pPtr);
            pPtr += sizeof(u16);

            ppWork[i] = _FindTexture(reinterpret_cast<char *>(pPtr), NULL);

            if (pPtr[0] != 0 && ppWork[i] == NULL) {
                currentError++;
            }

            pPtr += size;
        }

        for (u16 pidx = 0; pidx < pResource->NumPtclTrack(); pidx++) {
            u8 *pPtr = pResource->GetPtclTrack(pidx);

#define pTrackAsHeader reinterpret_cast<AnimCurveHeader *>(pPtr)

            if (static_cast<u8>(pTrackAsHeader->magic - static_cast<s8>(0xAB)) >
                1) {
                continue;
            }

            u8 curveType = pTrackAsHeader->curveFlag;
            switch (curveType) {
            case 4:
                currentError += relocateCurveTexture(pPtr, pResource);
                break;
            case 5:
            case 2:
                currentError += relocateCurveChild(pPtr, pResource);
                break;
            }

#undef pTrackAsHeader
        }
        EmitterDesc *ed = pResource->GetEmitterDesc();
        if (currentError) {
            ed->commonFlag &= ~EmitterDesc::CMN_FLAG_RELOCATED;
        } else {
            ed->commonFlag |= EmitterDesc::CMN_FLAG_RELOCATED;
        }
        error += currentError;
    }

    return error;
}

bool Resource::RemoveEffectProject(EffectProject *pEffProject) {
    EffectProject *pIt =
        static_cast<EffectProject *>(EF_PROJECT_LIST(GetFirst)(&mBREFFList));

    for (; pIt != NULL; pIt = static_cast<EffectProject *>(
                            EF_PROJECT_LIST(GetNext)(&mBREFFList, pIt))) {

        if (pIt != pEffProject) {
            continue;
        }

        EF_PROJECT_LIST(Remove)(&mBREFFList, pEffProject);

        mNumEmitter -=
            pEffProject != NULL ? breffNumEmitter(pEffProject) : mNumEmitter;

        return true;
    }

    return false;
}

bool Resource::RemoveTextureProject(TextureProject *pTexProject) {
    TextureProject *pIt =
        static_cast<TextureProject *>(EF_PROJECT_LIST(GetFirst)(&mBREFTList));

    for (; pIt != NULL; pIt = static_cast<TextureProject *>(
                            EF_PROJECT_LIST(GetNext)(&mBREFTList, pIt))) {

        if (pIt != pTexProject) {
            continue;
        }

        EF_PROJECT_LIST(Remove)(&mBREFTList, pTexProject);

        mNumTexture -=
            pTexProject != NULL ? breftNumTexture(pTexProject) : mNumTexture;

        return true;
    }

    return false;
}

} // namespace ef
} // namespace nw4r
