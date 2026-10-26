// TPL texture palettes on PC (lib/RVL_SDK/src/tpl/TPL.c on the Wii). The Wii's TPLBind turns the
// palette's 32-bit offsets into pointers in place; on PC they stay offsets, relocated to be relative to
// their own field (OFFSET_PTR, include/lib/revolution/tpl.h). Binding twice is harmless: each field is
// marked once relocated.
#include <revolution/os.h>
#include <revolution/tpl.h>

extern "C" {

void TPLBind(TPLPalette* pal) {
    if (pal->versionNumber != TPL_MAGIC) {
        OSPanic(__FILE__, __LINE__, "invalid version number for texture palette");
    }
    if (pal->descriptorArray.isRelocated()) {
        return;
    }
    pal->descriptorArray.setBase(pal);

    TPLDescriptor* descriptors = pal->descriptorArray;
    for (u32 i = 0; i < pal->numDescriptors; i++) {
        TPLDescriptor& desc = descriptors[i];
        if (desc.textureHeader) {
            desc.textureHeader.setBase(pal);
            TPLHeader* header = desc.textureHeader;
            if (!header->unpacked) {
                header->data.setBase(pal);
                header->unpacked = 1;
            }
        }
        if (desc.CLUTHeader) {
            desc.CLUTHeader.setBase(pal);
            TPLClutHeader* clut = desc.CLUTHeader;
            if (!clut->unpacked) {
                clut->data.setBase(pal);
                clut->unpacked = 1;
            }
        }
    }
}

TPLDescriptor* TPLGet(TPLPalette* pal, u32 id) {
    TPLDescriptor* descriptors = pal->descriptorArray;
    return &descriptors[id % pal->numDescriptors];
}

} // extern "C"
