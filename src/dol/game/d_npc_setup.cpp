// The villager setup pack (dNpcSetup). .text 800F9EF4..800FA08C, .ctors 804656EC, .rodata 80475748..80475768,
// .bss 805CEAC8..805CEAF0. See include/game/game/d_npc_setup.hpp.
#include <game/game/d_npc_setup.hpp>
#include <game/game/d_animal.hpp>
#include <game/game/d_heap.hpp>

#if TARGET_PC
#include <port/endian.h>
#endif

static const char l_fileName[] = "/Npc/Normal/Setup/pack.bin";

namespace dNpcSetup {

Pack_c sPack;

#if TARGET_PC
// pack.bin is big-endian: a u32 count, then the templates from 0x20. On PC it is converted to host
// order once, when loaded (the bit-fields need no conversion, port/bitfield.h).
static_assert(sizeof(dAnimalTemplate_c) == 0x198, "dAnimalTemplate_c must match pack.bin");

static void convertPackToHost(u8 *data) {
    u32 *num = (u32 *)data;
    be_swap(*num);
    dAnimalTemplate_c *tmpl = (dAnimalTemplate_c *)(data + 0x20);
    for (u32 i = 0; i < *num; i++, tmpl++) {
        be_swap(tmpl->mNpcIdx);
        be_swap(tmpl->mCloth);
        be_swap(tmpl->mCarpet);
        be_swap(tmpl->mWall);
        be_swap(tmpl->mUmbrella);
        be_swap(tmpl->mFurniture);
        be_swap(tmpl->mMusic);
        be_swap(tmpl->mSoundId);
        be_swap(tmpl->mNames);
        be_swap(tmpl->mHabits);
    }
}
#endif

// 800F9EF4
BOOL load() {
    Pack_c *pack = &sPack;
    if (pack->mpData == NULL && dHeap::npcNmlPackHeap_p != NULL) {
        pack->mpData = pack->mLoader.request(l_fileName, 0, dHeap::npcNmlPackHeap_p);
        if (pack->mpData == NULL) {
            return FALSE;
        }
#if TARGET_PC
        convertPackToHost((u8 *)pack->mpData);
#endif
    }
    return TRUE;
}

// 800F9F64
dAnimalTemplate_c *getList(u32 *num) {
    Pack_c *pack = &sPack;
    if (pack->mpData != NULL) {
        u8 *data = (u8 *)pack->mpData;
        *num = *(u32 *)data;
        return (dAnimalTemplate_c *)(data + 0x20);
    }
    return NULL;
}

// 800F9F94
dAnimalTemplate_c *find(u16 npcIdx, dAnimalTemplate_c *list, u32 num) {
    dAnimalTemplate_c *found = NULL;
    if (list != NULL && npcIdx != 0xFFFF) {
        for (u32 i = 0; i < num; i++, list++) {
            int idx = list->mNpcIdx;
            if (npcIdx == (u16)idx) {
                found = list;
                break;
            }
        }
    }
    return found;
}

} // namespace dNpcSetup
