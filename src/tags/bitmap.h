#ifndef BITMAP_DEFINITION_H
#define BITMAP_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bitmap_format {
    FORMAT_DXT1_BC1             = 0, // Opaque color & packed masks. 4 bpp. Use MAP_SRGB for color.
    FORMAT_DXT5_BC3             = 1, // Color with smooth alpha. 8 bpp. Use MAP_SRGB for color.
    FORMAT_RGTC1_BC4            = 2, // Single-channel data (height, roughness). 4 bpp.
    FORMAT_RGTC2_BC5            = 3, // Normal maps. 8 bpp. Use MAP_NORMAL for Y-flip.
    FORMAT_RGBA16F_UNCOMPRESSED = 4, // HDR environment maps. 64 bpp. Keep low-res.
    FORMAT_R8_UNCOMPRESSED      = 5, // Grayscale & SDF fonts. 8 bpp.
    FORMAT_RGBA8_UNCOMPRESSED   = 6, // Colored glyphs. 32 bpp. Use MAP_SRGB.

    MAP_SRGB               = 1 << 3, // Apply gamma correction to RGB
    MAP_CUBE               = 1 << 4, // Texture is a cubemap (6 faces)
    MAP_NORMAL             = 1 << 5, // Normal map: triggers Y-flip for DX11
    MAP_ARRAY              = 1 << 6, // Texture is an array (multiple layers)
    MAP_GENERATE_MIPS      = 1 << 7, // Generate mipmaps on load
    MAP_PREMULTIPLY_ALPHA  = 1 << 8  // RGB is pre-multiplied by Alpha (UI, Fonts, Particles).
} bitmap_format;

TAG_BITFIELD32_BEGIN(bitmap_format, 3)
    TAG_BITFIELD32_ENTRY(FORMAT_DXT1_BC1,             "DXT1_BC1")
    TAG_BITFIELD32_ENTRY(FORMAT_DXT5_BC3,             "DXT5_BC3")
    TAG_BITFIELD32_ENTRY(FORMAT_RGTC2_BC5,            "RGTC2_BC5")
    TAG_BITFIELD32_ENTRY(FORMAT_RGBA16F_UNCOMPRESSED, "RGBA16F_UNCOMPRESSED")
TAG_BITFIELD32_END(bitmap_format)


TAG_BLOCK_BEGIN(bitmap_data_block, -1, sizeof(u8))
    FIELD_TERMINATOR
TAG_BLOCK_END(bitmap_data_block, -1, sizeof(u8))

typedef struct bitmap_definition {
    u32  bitmap_format;
    struct tag_block data;
} bitmap_definition;

TAG_GROUP_BEGIN(bitmap, TAG_MAGIC_PACK(bitm), sizeof(struct bitmap_definition))
    FIELD_BITFIELD32("bitmap_format", bitmap_format_bitfield, bitmap_format_ENUM_BITS),
    FIELD_BLOCK("data", bitmap_data_block),
    FIELD_TERMINATOR
TAG_GROUP_END(bitmap, sizeof(struct bitmap_definition))

#ifdef __cplusplus
}
#endif
#endif /* BITMAP_DEFINITION_H */