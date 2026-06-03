#ifndef __RTXPT_TONE_MAPPING_SHARED_H__
#define __RTXPT_TONE_MAPPING_SHARED_H__

#define RTXPT_TONEMAPPING_AUTOEXPOSURE_CPU 1
#define RTXPT_TONEMAPPING_EXPOSURE_KEY     0.042

enum RTXPTToneMapperOperator
{
    RTXPTToneMapperOperator_Linear           = 0,
    RTXPTToneMapperOperator_Reinhard         = 1,
    RTXPTToneMapperOperator_ReinhardModified = 2,
    RTXPTToneMapperOperator_HejiHableAlu     = 3,
    RTXPTToneMapperOperator_HableUc2         = 4,
    RTXPTToneMapperOperator_Aces             = 5
};

struct RTXPTToneMappingConstants
{
    float    WhiteScale;
    float    WhiteMaxLuminance;
    uint     ToneMapOperator;
    uint     Clamped;
    uint     AutoExposure;
    float    AvgLuminance;
    float    AutoExposureLumValueMin;
    float    AutoExposureLumValueMax;
    float3x4 ColorTransform;
    uint     Enabled;
    uint     _Padding0;
    uint     _Padding1;
    uint     _Padding2;
};

#endif
