#include "RTXPTNrdConfig.hpp"

namespace Diligent
{

const char* RTXPTGetNrdUnavailableReason()
{
#if RTXPT_HAS_NRD
    return "Standalone NRD is available.";
#else
    return "Standalone denoiser disabled: NRD/NRI submodules are missing or RTXPT_NRD_ROOT was cleared.";
#endif
}

bool RTXPTIsNrdAvailable()
{
#if RTXPT_HAS_NRD
    return true;
#else
    return false;
#endif
}

#if RTXPT_HAS_NRD

namespace
{

nrd::HitDistanceReconstructionMode ToNrdHitDistanceMode(RTXPTNrdHitDistanceReconstructionMode Mode)
{
    switch (Mode)
    {
        case RTXPTNrdHitDistanceReconstructionMode::Off:
            return nrd::HitDistanceReconstructionMode::OFF;
        case RTXPTNrdHitDistanceReconstructionMode::Area3x3:
            return nrd::HitDistanceReconstructionMode::AREA_3X3;
        case RTXPTNrdHitDistanceReconstructionMode::Area5x5:
            return nrd::HitDistanceReconstructionMode::AREA_5X5;
        default:
            return nrd::HitDistanceReconstructionMode::OFF;
    }
}

} // namespace

nrd::Denoiser RTXPTToNrdDenoiser(RTXPTNrdMethod Method)
{
    return Method == RTXPTNrdMethod::RELAX ?
        nrd::Denoiser::RELAX_DIFFUSE_SPECULAR :
        nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;
}

nrd::RelaxSettings RTXPTMakeRelaxSettings(const RTXPTNrdRelaxUiSettings& Ui)
{
    nrd::RelaxSettings Settings;
    Settings.enableAntiFirefly                  = Ui.EnableAntiFirefly;
    Settings.hitDistanceReconstructionMode      = ToNrdHitDistanceMode(Ui.HitDistanceReconstructionMode);
    Settings.diffusePrepassBlurRadius           = Ui.DiffusePrepassBlurRadius;
    Settings.specularPrepassBlurRadius          = Ui.SpecularPrepassBlurRadius;
    Settings.diffuseMaxAccumulatedFrameNum      = Ui.DiffuseMaxAccumulatedFrameNum;
    Settings.specularMaxAccumulatedFrameNum     = Ui.SpecularMaxAccumulatedFrameNum;
    Settings.diffuseMaxFastAccumulatedFrameNum  = Ui.DiffuseMaxFastAccumulatedFrameNum;
    Settings.specularMaxFastAccumulatedFrameNum = Ui.SpecularMaxFastAccumulatedFrameNum;
    Settings.historyFixFrameNum                 = Ui.HistoryFixFrameNum;
    Settings.atrousIterationNum                 = Ui.AtrousIterationNum;
    Settings.lobeAngleFraction                  = Ui.LobeAngleFraction;
    Settings.specularLobeAngleSlack             = Ui.SpecularLobeAngleSlack;
    Settings.depthThreshold                     = Ui.DepthThreshold;
    Settings.antilagSettings.accelerationAmount = Ui.AntilagAccelerationAmount;
    Settings.antilagSettings.spatialSigmaScale  = Ui.AntilagSpatialSigmaScale;
    Settings.antilagSettings.temporalSigmaScale = Ui.AntilagTemporalSigmaScale;
    Settings.antilagSettings.resetAmount        = Ui.AntilagResetAmount;
    return Settings;
}

nrd::ReblurSettings RTXPTMakeReblurSettings(const RTXPTNrdReblurUiSettings& Ui)
{
    nrd::ReblurSettings Settings;
    Settings.enableAntiFirefly             = Ui.EnableAntiFirefly;
    Settings.hitDistanceReconstructionMode = ToNrdHitDistanceMode(Ui.HitDistanceReconstructionMode);
    Settings.maxAccumulatedFrameNum        = Ui.MaxAccumulatedFrameNum;
    Settings.maxFastAccumulatedFrameNum    = Ui.MaxFastAccumulatedFrameNum;
    Settings.historyFixFrameNum            = Ui.HistoryFixFrameNum;
    Settings.diffusePrepassBlurRadius      = Ui.DiffusePrepassBlurRadius;
    Settings.specularPrepassBlurRadius     = Ui.SpecularPrepassBlurRadius;
    return Settings;
}

#endif

} // namespace Diligent
