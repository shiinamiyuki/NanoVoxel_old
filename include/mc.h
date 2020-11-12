#pragma once

// https://github.com/erich666/Mineways/blob/master/Win/blockInfo.h


// fills whole block
#define BLF_WHOLE            0x0001
// almost a whole block
#define BLF_ALMOST_WHOLE    0x0002
// stairs
#define BLF_STAIRS            0x0004
// half block
#define BLF_HALF            0x0008
// fair-sized, worth rendering, has geometry
#define BLF_MIDDLER            0x0010
// larger billboard object worth rendering
#define BLF_BILLBOARD        0x0020
// billboard flat through middle, usually transparent (portal, glass pane)
#define BLF_PANE            0x0040
// sits on top of a block below it
#define BLF_FLATTEN            0x0080
// flat on a wall: sign, ladder, etc. - normally not shown on the map; to make something visible on map, use BLF_FLATTEN instead, which otherwise is identical
#define BLF_FLATTEN_SMALL        0x0100
// small, not as worth rendering (will disappear if not flattened, etc. when exporting for a 3D print), has geometry - normally not shown on the map
#define BLF_SMALL_MIDDLER    0x0200
// small thing: lever, flower - normally culled out
#define BLF_SMALL_BILLBOARD    0x0400

// has an alpha for the whole block (vs. glass, which often has a frame that's solid)
#define BLF_TRANSPARENT        0x0800
// has cutout parts to its texture, on or off (no semitransparent alpha)
#define BLF_CUTOUTS            0x1000
// trunk
#define BLF_TRUNK_PART      0x2000
// leaf
#define BLF_LEAF_PART       0x4000
// is related to trees - if something is floating and is a tree, delete it for printing
#define BLF_TREE_PART       (BLF_TRUNK_PART|BLF_LEAF_PART)
// is an entrance of some sort, for sealing off building interiors
#define BLF_ENTRANCE        0x8000
// export image texture for this object, as it makes sense - almost everything has this property (i.e. has a texture tile)
// actually, now everything has this property, so it's eliminated
//#define BLF_IMAGE_TEXTURE   0x10000

// this object emits light - affects output material
#define BLF_EMITTER         0x10000
// this object attaches to fences; note that fences do not have this property themselves, so that nether & regular fence won't attach
#define BLF_FENCE_NEIGHBOR    0x20000
// this object outputs its true geometry (not just a block) for rendering
#define BLF_TRUE_GEOMETRY    0x40000
// this object outputs its special non-full-block geometry for 3D printing, if the printer can glue together the bits.
// Slightly different than TRUE_GEOMETRY in that things that are just too thin don't have this bit set.
#define BLF_3D_BIT          0x80000
// this object is a 3D bit, and this bit is set if it can actually glue horizontal neighboring blocks together
// - not really used. TODO - may want to use this to decide whether objects should be grouped together or whatever.
#define BLF_3D_BIT_GLUE     0x100000
// set if the block does not affect fluid height. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_DNE_FLUID        0x200000
// set if the block connects to redstone - do only if there's no orientation to the block, e.g. repeaters attach only on two sides, so don't have this flag
#define BLF_CONNECTS_REDSTONE        0x400000
// has no geometry, on purpose
#define BLF_NONE            0x800000
// is an offset tile, rendered separately: rails, vines, lily pad, redstone, ladder (someday, tripwire? TODO)
#define BLF_OFFSET            0x1000000
// is a billboard or similar that is always underwater, such as seagrass and kelp. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_WATERLOG        0x2000000
// is a billboard or similar that may waterlog, such as coral fans; bit 0x100 is set if waterlogged. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_MAYWATERLOG        0x4000000
// this object is a gate that attachs to fences if oriented properly - like BLF_FENCE_NEIGHBOR, but needs orientation to connect
#define BLF_FENCE_GATE        0x8000000
// this object is a fence that attachs to fences if of the right type - like BLF_FENCE_NEIGHBOR, but needs for types (nether, wood) to match to connect
#define BLF_FENCE            0x10000000

#define BLF_CLASS_SE

typedef struct BlockDefinition {
    const char *name;
    unsigned int read_color;    // r,g,b, locked in place, never written to: used for initial setting of color
    float read_alpha;
    unsigned int color;    // r,g,b, NOT multiplied by alpha - input by the user, result of color scheme application
    unsigned int pcolor;    // r,g,b, premultiplied by alpha (basically, unmultColor * alpha) - used (only) in mapping
    float alpha;
    int txrX;   // column and row, from upper left, of 16x16 tiles in terrainExt.png, for TOP view of block
    int txrY;
    unsigned char subtype_mask;    // bits that are used in the data value to determine whether this is a separate material
    unsigned int flags;
} BlockDefinition;