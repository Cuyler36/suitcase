#include <lib/egg/core/eggMsgRes.h>

#if TARGET_PC
#include <string.h>
#endif

#define FOURCC(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

namespace EGG {

const u32 MsgRes::cDataBlkKindCodeSet[DATABLK_MAX] = {
    FOURCC('I', 'N', 'F', '1'), // DATABLK_MSGINFO
    FOURCC('D', 'A', 'T', '1'), // DATABLK_MSGDATA
    FOURCC('S', 'T', 'R', '1'), // DATABLK_STRATTR
    FOURCC('M', 'I', 'D', '1'), // DATABLK_MSGID
    FOURCC('F', 'L', 'W', '1'), // DATABLK_FLOWCHART
    FOURCC('F', 'L', 'I', '1')  // DATABLK_FLOWLABEL
};

const u32 MsgRes::cMainMsgIdBitNumSet[cFormSupplementMax] = {
    32, 24, 16, 8, 0,
};

const MsgRes::MsgIDMask MsgRes::cMsgIdMaskSet[cFormSupplementMax] = {
    {0xFFFFFFFF, 0x00000000}, {0xFFFFFF00, 0x000000FF}, {0xFFFF0000, 0x0000FFFF},
    {0xFF000000, 0x00FFFFFF}, {0x00000000, 0xFFFFFFFF},
};

#if TARGET_PC
void MsgRes::convertTextToHost(const void *data) {
    // The data is the loaded file, which the game only ever reads
    MessageHeader *header = const_cast<MessageHeader *>(static_cast<const MessageHeader *>(data));
    // Marks converted data: the last byte of the header's padding, zero in the files
    u8 &converted = reinterpret_cast<u8 *>(header)[sizeof(MessageHeader) - 1];
    if (converted != 0 || header->mEncoding != MessageHeader::ENCODING_2BYTE) {
        return;
    }
    converted = 1;

    u8 *cursor = reinterpret_cast<u8 *>(header) + sizeof(MessageHeader);
    for (u32 i = 0; i < header->mNumBlocks; i++) {
        DataBlock *block = reinterpret_cast<DataBlock *>(cursor);
        if (block->mKind == cDataBlkKindCodeSet[DATABLK_MSGDATA]) {
            u8 *text = cursor + sizeof(DataBlock);
            u8 *end = cursor + block->mDataSize;
            while (text + 2 <= end) {
                u16 code;
                memcpy(&code, text, sizeof(code));
                code = RES_U16(code);
                memcpy(text, &code, sizeof(code));
                text += 2;
                if (code == cTagMark && text < end) {
                    // A tag: its first byte is its size, including the mark; skip the rest as is
                    u8 size = *text;
                    text += size > 2 ? size - 2 : 0;
                }
            }
        }
        cursor += block->mDataSize;
    }
}
#endif

MsgRes::MsgRes(const void *data) {
#if TARGET_PC
    convertTextToHost(data);
#endif
    mMsgHeader = NULL;
    mMsgInfoDataBlk = NULL;
    mMsgDataBlk = NULL;
    mMsgStrDataBlk = NULL;
    mMsgIDDataBlk = NULL;
    mMsgFlowChartBlk = NULL;
    mMsgFlowLabelBlk = NULL;

    const u8 *cursor = static_cast<const u8 *>(data);
    extractMsgHeader(cursor);

    const void *current = cursor + sizeof(MessageHeader);

    u32 numBlocks = mMsgHeader->mNumBlocks;

    for (int i = 0; i < numBlocks; i++) {
        EDataBlkKind kind = analyzeDataBlkKind(getDataBlkKind(current));
        const DataBlock *pDataBlk = NULL;

        switch (kind) {
        case DATABLK_MSGINFO: {
            extractMsgInfoDataBlk(current);
            pDataBlk = mMsgInfoDataBlk;
            break;
        }
        case DATABLK_MSGDATA: {
            extractMsgDataBlk(current);
            pDataBlk = mMsgDataBlk;
            break;
        }
        case DATABLK_STRATTR: {
            extractStrAttrDataBlk(current);
            pDataBlk = mMsgStrDataBlk;
            break;
        }
        case DATABLK_MSGID: {
            extractMsgIDDataBlk(current);
            pDataBlk = mMsgIDDataBlk;
            break;
        }
        case DATABLK_FLOWCHART: {
            extractFlowChartInfoDataBlk(current);
            pDataBlk = mMsgFlowChartBlk;
            break;
        }
        case DATABLK_FLOWLABEL: {
            extractFlowLabelInfoDataBlk(current);
            pDataBlk = mMsgFlowLabelBlk;
            break;
        }
        default: {
            break;
        }
        }

        current = static_cast<const u8 *>(current) + getDataBlkSize(pDataBlk);
    }
}

MsgRes::~MsgRes() {}

void MsgRes::analyzeTag(u16 code, const wchar16_t *tag, u8 *tagLength, u32 *tagID, void **param) {
    *tagLength = *reinterpret_cast<const u8 *>(tag) - 2;
    *tagID = RES_U32(*reinterpret_cast<const u32 *>(tag)) & 0x00FFFFFF; // tag bodies stay big-endian

    if (*tagLength > 4) {
        *param = const_cast<wchar16_t *>(tag + 2);
    } else {
        *param = NULL;
    }
}

const wchar16_t *MsgRes::getMsg(u32 group, u32 idx) {
    const MessageInfoDataBlock::Entry *pEntry = getMsgEntry(group, idx);

    return reinterpret_cast<const wchar16_t *>(reinterpret_cast<const u8 *>(mMsgDataBlk->mContent) +
                                             pEntry->mDataOffset);
}

BOOL MsgRes::isExistMsg(u32 group, u32 idx) {
    return getMsgEntry(group, idx) != NULL;
}

void MsgRes::extractMsgHeader(const void *data) {
    mMsgHeader = static_cast<const MessageHeader *>(data);
}

void MsgRes::extractMsgInfoDataBlk(const void *data) {
    mMsgInfoDataBlk = static_cast<const MessageInfoDataBlock *>(data);
}

void MsgRes::extractMsgDataBlk(const void *data) {
    mMsgDataBlk = static_cast<const MessageDataBlock *>(data);
}

void MsgRes::extractStrAttrDataBlk(const void *data) {
    mMsgStrDataBlk = static_cast<const StringAttributeDataBlock *>(data);
}

void MsgRes::extractMsgIDDataBlk(const void *data) {
    mMsgIDDataBlk = static_cast<const MessageIDDataBlock *>(data);
}

void MsgRes::extractFlowChartInfoDataBlk(const void *data) {
    mMsgFlowChartBlk = static_cast<const FlowChartInfoDataBlock *>(data);
}

void MsgRes::extractFlowLabelInfoDataBlk(const void *data) {
    mMsgFlowLabelBlk = static_cast<const FlowLabelInfoDataBlock *>(data);
}

MsgRes::EDataBlkKind MsgRes::analyzeDataBlkKind(u32 kind) {
    for (int i = 0; i < DATABLK_MAX; i++) {
        if (kind == cDataBlkKindCodeSet[i]) {
            return static_cast<EDataBlkKind>(i);
        }
    }

    return DATABLK_MAX;
}

const MsgRes::MessageInfoDataBlock::Entry *MsgRes::getMsgEntry(u32 group, u32 idx) {
    u32 idBitNum = 32 - cMainMsgIdBitNumSet[mMsgIDDataBlk->mFormSupplement];
    const MsgIDMask &rMask = cMsgIdMaskSet[mMsgIDDataBlk->mFormSupplement];

    for (u16 i = 0; i < mMsgIDDataBlk->mNumEntries; i++) {
        u32 msgId = getMsgID(i);

        if (group == (msgId & rMask.groupMask) >> idBitNum && idx == (msgId & rMask.indexMask)) {
            return reinterpret_cast<const MessageInfoDataBlock::Entry *>(
                reinterpret_cast<const u8 *>(mMsgInfoDataBlk->mContent) +
                mMsgInfoDataBlk->mEntrySize * i);
        }
    }

    return NULL;
}

u32 MsgRes::getMsgID(u16 idx) {
    return mMsgIDDataBlk->mContent[idx];
}

} // namespace EGG
