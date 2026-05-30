cmake_minimum_required(VERSION 3.10)

project(RTXPT CXX)

set(SOURCE
    src/RTXPTSample.cpp
    src/RTXPTScene.cpp
    src/RTXPTMaterials.cpp
    src/RTXPTLights.cpp
    src/RTXPTAccelerationStructures.cpp
    src/RTXPTSkinnedGeometry.cpp
    src/RTXPTRenderTargets.cpp
    src/RTXPTRayTracingPass.cpp
    src/RTXPTComputePass.cpp
    src/RTXPTBlitPass.cpp
)

set(INCLUDE
    src/RTXPTSample.hpp
    src/RTXPTScene.hpp
    src/RTXPTMaterials.hpp
    src/RTXPTLights.hpp
    src/RTXPTAccelerationStructures.hpp
    src/RTXPTSkinnedGeometry.hpp
    src/RTXPTRenderTargets.hpp
    src/RTXPTRayTracingPass.hpp
    src/RTXPTComputePass.hpp
    src/RTXPTBlitPass.hpp
    RTXPT_FORK_MAPPING.md
)

set(SHADERS
    assets/shaders/RTXPTCommon.fxh
    assets/shaders/PathTracer/PathTracer.hlsli
    assets/shaders/PathTracer/Config.h
    assets/shaders/PathTracer/PathTracerHelpers.hlsli
    assets/shaders/PathTracer/PathTracerShared.h
    assets/shaders/PathTracer/PathTracerBridge.hlsli
    assets/shaders/PathTracer/Rendering/Materials/MaterialBridge.hlsli
    assets/shaders/PathTracer/Lighting/EnvMap.hlsli
    assets/shaders/PathTracer/Lighting/PolymorphicLight.hlsli
    assets/shaders/PathTracer/Utils/SampleGenerators.hlsli
    assets/shaders/PathTracer/Rendering/Materials/BxDF.hlsli
    assets/shaders/PathTracer/PathTracerSample.rgen
    assets/shaders/PathTracer/PathTracerMiss.rmiss
    assets/shaders/PathTracer/PathTracerClosestHit.rchit
    assets/shaders/PathTracer/PathTracerAnyHit.rahit
    assets/shaders/PathTracer/SkinnedVertexBuild.csh
    assets/shaders/RTXPTDebugCompute.csh
    assets/shaders/RTXPTBlit.vsh
    assets/shaders/RTXPTBlit.psh
)

add_sample_app(RTXPT
    IDE_FOLDER
        DiligentSamples/Samples
    SOURCES
        ${SOURCE}
    INCLUDES
        ${INCLUDE}
    SHADERS
        ${SHADERS}
    DXC_REQUIRED
        YES
)

target_link_libraries(RTXPT
PRIVATE
    Diligent-AssetLoader
    Diligent-JSON
)

