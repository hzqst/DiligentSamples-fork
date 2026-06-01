/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

#include <cmath>

#include "BasicMath.hpp"

namespace Diligent
{

struct RTXPTCameraBasis
{
    float3 Right   = float3{1.0f, 0.0f, 0.0f};
    float3 Up      = float3{0.0f, 1.0f, 0.0f};
    float3 Forward = float3{0.0f, 0.0f, -1.0f};
};

inline float3 NormalizeRTXPTCameraAxisOrFallback(const float3& Axis, const float3& Fallback)
{
    const float AxisLength = length(Axis);
    return AxisLength > 1e-5f ? Axis / AxisLength : Fallback;
}

inline float3 GetRTXPTCameraFallbackUp(const float3& Forward)
{
    return std::abs(Forward.y) < 0.9f ? float3{0.0f, 1.0f, 0.0f} : float3{1.0f, 0.0f, 0.0f};
}

inline RTXPTCameraBasis MakeRTXPTDonutCameraBasis(const QuaternionF& Rotation)
{
    RTXPTCameraBasis Basis;

    Basis.Forward = NormalizeRTXPTCameraAxisOrFallback(Rotation.RotateVector(float3{0.0f, 0.0f, -1.0f}), Basis.Forward);
    const float3 RotatedUp = NormalizeRTXPTCameraAxisOrFallback(Rotation.RotateVector(float3{0.0f, 1.0f, 0.0f}), Basis.Up);

    Basis.Right = cross(Basis.Forward, RotatedUp);
    const float RightLength = length(Basis.Right);
    if (RightLength > 1e-5f)
    {
        Basis.Right = Basis.Right / RightLength;
    }
    else
    {
        Basis.Right = NormalizeRTXPTCameraAxisOrFallback(
            cross(Basis.Forward, GetRTXPTCameraFallbackUp(Basis.Forward)),
            Basis.Right);
    }

    Basis.Up = NormalizeRTXPTCameraAxisOrFallback(cross(Basis.Right, Basis.Forward), Basis.Up);
    return Basis;
}

} // namespace Diligent
