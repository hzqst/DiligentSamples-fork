cmake_minimum_required(VERSION 3.10)

project(RTXPT CXX)

set(SOURCE
    src/RTXPTSample.cpp
    src/RTXPTScene.cpp
    src/RTXPTMaterials.cpp
    src/RTXPTLights.cpp
    src/RTXPTAccelerationStructures.cpp
)

set(INCLUDE
    src/RTXPTSample.hpp
    src/RTXPTScene.hpp
    src/RTXPTMaterials.hpp
    src/RTXPTLights.hpp
    src/RTXPTAccelerationStructures.hpp
)

set(SHADERS
    assets/shaders/RTXPTCommon.fxh
    assets/shaders/RTXPTMinimal.rgen
    assets/shaders/RTXPTMinimal.rmiss
    assets/shaders/RTXPTMinimal.rchit
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
)

