#ifndef __MATERIAL_DATA_HLSLI__
#define __MATERIAL_DATA_HLSLI__

#define EXTRACT_BITS(bits, offset, value) (((value) >> (offset)) & ((1u << (bits)) - 1u))
#define PACK_BITS(bits, offset, flags, value) ((((value) & ((1u << (bits)) - 1u)) << (offset)) | ((flags) & (~(((1u << (bits)) - 1u) << (offset)))))

struct MaterialHeader
{
    uint packedData;

    static const uint kNestedPriorityBits  = 4u;
    static const uint kLobeTypeBits        = 8u;
    static const uint kNestedPriorityOffset = 0u;
    static const uint kLobeTypeOffset       = kNestedPriorityOffset + kNestedPriorityBits;
    static const uint kThinSurfaceFlagOffset = kLobeTypeOffset + kLobeTypeBits;

    static MaterialHeader make()
    {
        MaterialHeader header;
        header.packedData = 0u;
        return header;
    }

    void setNestedPriority(uint priority) { packedData = PACK_BITS(kNestedPriorityBits, kNestedPriorityOffset, packedData, priority); }
    uint getNestedPriority() { return EXTRACT_BITS(kNestedPriorityBits, kNestedPriorityOffset, packedData); }

    void setActiveLobes(uint activeLobes) { packedData = PACK_BITS(kLobeTypeBits, kLobeTypeOffset, packedData, activeLobes); }
    uint getActiveLobes() { return EXTRACT_BITS(kLobeTypeBits, kLobeTypeOffset, packedData); }

    void setThinSurface(bool thinSurface) { packedData = PACK_BITS(1u, kThinSurfaceFlagOffset, packedData, thinSurface ? 1u : 0u); }
    bool isThinSurface() { return (packedData & (1u << kThinSurfaceFlagOffset)) != 0u; }
};

#undef PACK_BITS
#undef EXTRACT_BITS

#endif // __MATERIAL_DATA_HLSLI__
