cmake_minimum_required(VERSION 3.10)

project(RTXPT CXX)

add_sample_app(RTXPT
    IDE_FOLDER
        DiligentSamples/Samples
    SOURCES
        src/RTXPTSample.cpp
    INCLUDES
        src/RTXPTSample.hpp
    ASSETS
        assets/README.md
    DXC_REQUIRED
        YES
)

target_link_libraries(RTXPT
PRIVATE
    Diligent-AssetLoader
)
