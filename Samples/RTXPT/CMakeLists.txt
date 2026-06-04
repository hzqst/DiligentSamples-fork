cmake_minimum_required(VERSION 3.10)

project(RTXPT CXX)

set(SOURCE
    src/RTXPTSample.cpp
    src/RTXPTScene.cpp
    src/RTXPTSceneJson.cpp
    src/RTXPTSceneGraph.cpp
    src/RTXPTMaterials.cpp
    src/RTXPTLights.cpp
    src/RTXPTLightsBaker.cpp
    src/RTXPTLightsBakerPass.cpp
    src/RTXPTEnvMapBaker.cpp
    src/RTXPTEnvMapBakerPass.cpp
    src/RTXPTAccelerationStructures.cpp
    src/RTXPTSkinnedGeometry.cpp
    src/RTXPTRenderTargets.cpp
    src/RTXPTPostProcessPipeline.cpp
    src/RTXPTPostProcessPass.cpp
    src/RTXPTNrdConfig.cpp
    src/RTXPTNrdIntegration.cpp
    src/RTXPTRayTracingPass.cpp
    src/RTXPTDenoisingGuidesBaker.cpp
    src/RTXPTEmissiveTrianglePass.cpp
    src/RTXPTComputePass.cpp
    src/RTXPTAccumulationPass.cpp
    src/RTXPTBloomPass.cpp
    src/RTXPTSuperResolutionPass.cpp
    src/RTXPTToneMappingPass.cpp
    src/RTXPTBlitPass.cpp
)

set(INCLUDE
    src/RTXPTSample.hpp
    src/RTXPTCameraBasis.hpp
    src/RTXPTScene.hpp
    src/RTXPTSceneJson.hpp
    src/RTXPTSceneGraph.hpp
    src/RTXPTMaterials.hpp
    src/RTXPTLights.hpp
    src/RTXPTLightsBaker.hpp
    src/RTXPTLightsBakerPass.hpp
    src/RTXPTFrameConstants.hpp
    src/RTXPTRealtimeSettings.hpp
    src/RTXPTEnvMapBaker.hpp
    src/RTXPTEnvMapBakerPass.hpp
    src/RTXPTAccelerationStructures.hpp
    src/RTXPTSkinnedGeometry.hpp
    src/RTXPTRenderTargets.hpp
    src/RTXPTPostProcessPipeline.hpp
    src/RTXPTPostProcessPass.hpp
    src/RTXPTNrdConfig.hpp
    src/RTXPTNrdIntegration.hpp
    src/RTXPTRayTracingPass.hpp
    src/RTXPTDenoisingGuidesBaker.hpp
    src/RTXPTEmissiveTrianglePass.hpp
    src/RTXPTComputePass.hpp
    src/RTXPTAccumulationPass.hpp
    src/RTXPTBloomPass.hpp
    src/RTXPTSuperResolutionPass.hpp
    src/RTXPTToneMappingPass.hpp
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
    assets/shaders/PathTracer/PathPayload.hlsli
    assets/shaders/PathTracer/PathState.hlsli
    assets/shaders/PathTracer/PathTracerTypes.hlsli
    assets/shaders/PathTracer/StablePlanes.hlsli
    assets/shaders/PathTracer/PathTracerStablePlanes.hlsli
    assets/shaders/PathTracer/DenoisingGuidesBaker.hlsl
    assets/shaders/PathTracer/PathTracerNestedDielectrics.hlsli
    assets/shaders/PathTracer/Rendering/Materials/MaterialBridge.hlsli
    assets/shaders/PathTracer/Rendering/Materials/LobeType.hlsli
    assets/shaders/PathTracer/Rendering/Materials/InteriorList.hlsli
    assets/shaders/PathTracer/Scene/Material/MaterialData.hlsli
    assets/shaders/PathTracer/Scene/Material/HomogeneousVolumeData.hlsli
    assets/shaders/PathTracer/Rendering/Volumes/HomogeneousVolumeSampler.hlsli
    assets/shaders/PathTracer/Lighting/LightingConfig.h
    assets/shaders/PathTracer/Lighting/LightingTypes.hlsli
    assets/shaders/PathTracer/Lighting/EnvMap.hlsli
    assets/shaders/PathTracer/Lighting/PolymorphicLight.hlsli
    assets/shaders/PathTracer/Lighting/LightSampler.hlsli
    assets/shaders/PathTracer/Lighting/LightsBaker.hlsl
    assets/shaders/PathTracer/Lighting/EnvMapImportanceBaker.hlsl
    assets/shaders/PathTracer/Utils/SampleGenerators.hlsli
    assets/shaders/PathTracer/Utils/StatelessSampleGenerators.hlsli
    assets/shaders/PathTracer/Rendering/Materials/BxDF.hlsli
    assets/shaders/PathTracer/PathTracerSample.rgen
    assets/shaders/PathTracer/PathTracerMiss.rmiss
    assets/shaders/PathTracer/PathTracerClosestHit.rchit
    assets/shaders/PathTracer/EmissiveTriangleBuild.hlsl
    assets/shaders/PathTracer/PathTracerAnyHit.rahit
    assets/shaders/PathTracer/SkinnedVertexBuild.csh
    assets/shaders/PostProcessing/RTXPTFullscreen.vsh
    assets/shaders/PostProcessing/RTXPTAccumulation.csh
    assets/shaders/PostProcessing/RTXPTPostProcess.csh
    assets/shaders/PostProcessing/RTXPTDenoiserNRD.hlsli
    assets/shaders/PostProcessing/RTXPTBloomShared.h
    assets/shaders/PostProcessing/RTXPTBloomCopy.psh
    assets/shaders/PostProcessing/RTXPTBloomBlur.psh
    assets/shaders/PostProcessing/ToneMapper/ToneMappingShared.h
    assets/shaders/PostProcessing/ToneMapper/ToneMapping.hlsl
    assets/shaders/PostProcessing/ToneMapper/ToneMapping.ps.hlsli
    assets/shaders/PostProcessing/ToneMapper/Luminance.psh
    assets/shaders/RTXPTDebugCompute.csh
    assets/shaders/RTXPTBlit.vsh
    assets/shaders/RTXPTBlit.psh
)

set(RTXPT_NRD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/NRD" CACHE PATH "NVIDIA NRD SDK root for the RTXPT standalone denoiser")
set(RTXPT_NRI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/NRI" CACHE PATH "NVIDIA NRI SDK root used by the RTXPT NRD submodule when NRD_NRI is enabled")
set(RTXPT_HAS_NRD OFF)
set(RTXPT_NRD_SHADER_INCLUDE_DIR "")
set(RTXPT_NRD_SHADER_SOURCE_DIR "")
set(RTXPT_NRI_ROOT_NORMALIZED "")
set(RTXPT_NRI_AVAILABLE OFF)
set(NRD_NRI OFF CACHE BOOL "Build NRD NRI integration")

if(RTXPT_NRD_ROOT)
    file(TO_CMAKE_PATH "${RTXPT_NRD_ROOT}" RTXPT_NRD_ROOT_NORMALIZED)
    if(RTXPT_NRI_ROOT)
        file(TO_CMAKE_PATH "${RTXPT_NRI_ROOT}" RTXPT_NRI_ROOT_NORMALIZED)
        if(EXISTS "${RTXPT_NRI_ROOT_NORMALIZED}/CMakeLists.txt")
            set(FETCHCONTENT_SOURCE_DIR_NRI "${RTXPT_NRI_ROOT_NORMALIZED}" CACHE PATH "Use RTXPT's local NRI submodule when NRD_NRI is enabled" FORCE)
            set(RTXPT_NRI_AVAILABLE ON)
        elseif(NRD_NRI)
            message(WARNING "NRD_NRI is ON, but RTXPT_NRI_ROOT does not contain a valid NRI checkout.")
        endif()
    endif()

    set(RTXPT_NRD_INCLUDE_DIR "${RTXPT_NRD_ROOT_NORMALIZED}/Include")
    set(RTXPT_NRD_SHADER_INCLUDE_DIR "${RTXPT_NRD_ROOT_NORMALIZED}/Shaders/Include")
    set(RTXPT_NRD_SHADER_SOURCE_DIR "${RTXPT_NRD_ROOT_NORMALIZED}/Shaders/Source")

    if(EXISTS "${RTXPT_NRD_ROOT_NORMALIZED}/CMakeLists.txt" AND
       EXISTS "${RTXPT_NRD_INCLUDE_DIR}/NRD.h" AND
       EXISTS "${RTXPT_NRD_INCLUDE_DIR}/NRDSettings.h" AND
       EXISTS "${RTXPT_NRD_INCLUDE_DIR}/NRDDescs.h" AND
       EXISTS "${RTXPT_NRD_SHADER_INCLUDE_DIR}/NRD.hlsli" AND
       EXISTS "${RTXPT_NRD_SHADER_SOURCE_DIR}/REBLUR_DiffuseSpecular_TemporalAccumulation.cs.hlsl" AND
       (NOT NRD_NRI OR RTXPT_NRI_AVAILABLE))
        set(NRD_STATIC_LIBRARY ON CACHE BOOL "Build NRD as a static library" FORCE)
        set(NRD_EMBEDS_DXIL_SHADERS OFF CACHE BOOL "RTXPT compiles NRD HLSL through Diligent DXC" FORCE)
        set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "RTXPT compiles NRD HLSL through Diligent DXC" FORCE)
        set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "RTXPT compiles NRD HLSL through Diligent DXC" FORCE)
        add_subdirectory("${RTXPT_NRD_ROOT_NORMALIZED}" "${CMAKE_CURRENT_BINARY_DIR}/NRD")
        set(RTXPT_HAS_NRD ON)
    else()
        message(WARNING "RTXPT_NRD_ROOT is set, but required NRD files were not found, or NRD_NRI is ON without a valid RTXPT_NRI_ROOT. RTXPT standalone NRD will be disabled.")
    endif()
endif()

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

if (NOT SUPER_RESOLUTION_SUPPORTED)
    message(FATAL_ERROR "RTXPT requires Diligent SuperResolution; configure without DILIGENT_NO_SUPER_RESOLUTION or disable the RTXPT sample")
endif()

target_link_libraries(RTXPT
PRIVATE
    Diligent-AssetLoader
    Diligent-JSON
    Diligent-SuperResolution-static
    DiligentFX
)

target_compile_definitions(RTXPT
PRIVATE
    RTXPT_HAS_NRD=$<BOOL:${RTXPT_HAS_NRD}>
    RTXPT_NRD_SHADER_INCLUDE_DIR="${RTXPT_NRD_SHADER_INCLUDE_DIR}"
    RTXPT_NRD_SHADER_SOURCE_DIR="${RTXPT_NRD_SHADER_SOURCE_DIR}"
)

if(RTXPT_HAS_NRD)
    target_link_libraries(RTXPT PRIVATE NRD)
    target_include_directories(RTXPT PRIVATE
        "${RTXPT_NRD_INCLUDE_DIR}"
        "${RTXPT_NRD_SHADER_INCLUDE_DIR}"
    )
endif()

if (DILIGENT_DSR_SUPPORTED)
    target_link_libraries(RTXPT PRIVATE DirectSR-AgilitySDK)
    copy_directsr_dlls(RTXPT)
endif()

if (DILIGENT_DLSS_SUPPORTED)
    copy_dlss_dlls(RTXPT)
endif()
