cmake_minimum_required(VERSION 3.10)

project(RTXPT CXX)

set(SOURCE
    src/RTXPTSample.cpp
    src/RTXPTScene.cpp
)

set(INCLUDE
    src/RTXPTSample.hpp
    src/RTXPTScene.hpp
)

add_sample_app(RTXPT
    IDE_FOLDER
        DiligentSamples/Samples
    SOURCES
        ${SOURCE}
    INCLUDES
        ${INCLUDE}
    DXC_REQUIRED
        YES
)

target_link_libraries(RTXPT
PRIVATE
    Diligent-AssetLoader
)

