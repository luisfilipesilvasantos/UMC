cmake_minimum_required(VERSION 3.20)
project(umc_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add CUDA include and library paths directly to ensure deterministic linking
set(CUDA_SDK_DIR "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3")

include_directories("${CUDA_SDK_DIR}/include")
link_directories("${CUDA_SDK_DIR}/lib/x64")

add_executable(umc_demo
    main.cpp
    VirtualGpuMemory.h
    VirtualGpuMemory.cpp
    UMCModelLoader.h
    UMCModelLoader.cpp
)

# Link Windows system libraries (for VirtualAlloc, FileMapping, GetPerformanceInfo) and NVIDIA libraries
target_link_libraries(umc_demo PRIVATE
    Psapi.lib
    cudart.lib
    nvml.lib
)

# Optional MSVC configurations for cleaner output and warnings
if(MSVC)
    target_compile_options(umc_demo PRIVATE /W4 /WX /utf-8)
endif()
