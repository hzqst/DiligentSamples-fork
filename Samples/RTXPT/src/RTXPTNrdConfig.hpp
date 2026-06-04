#pragma once

#include "RTXPTRealtimeSettings.hpp"

namespace Diligent
{

const char* RTXPTGetNrdUnavailableReason();
bool        RTXPTIsNrdAvailable();

#if RTXPT_HAS_NRD

#    include "NRD.h"
#    include "NRDSettings.h"

nrd::Denoiser      RTXPTToNrdDenoiser(RTXPTNrdMethod Method);
nrd::RelaxSettings RTXPTMakeRelaxSettings(const RTXPTNrdRelaxUiSettings& Ui);
nrd::ReblurSettings RTXPTMakeReblurSettings(const RTXPTNrdReblurUiSettings& Ui);

#endif

} // namespace Diligent
