#ifndef __RTXPT_BLOOM_SHARED_H__
#define __RTXPT_BLOOM_SHARED_H__

struct RTXPTBloomConstants
{
    float2 PixStep;
    float  ArgumentScale;
    float  NormalizationScale;

    float3 Padding;
    float  NumSamples;
};

#endif // __RTXPT_BLOOM_SHARED_H__
