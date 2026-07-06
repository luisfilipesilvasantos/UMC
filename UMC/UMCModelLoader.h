#pragma once

#include "VirtualGpuMemory.h"
#include <string>

class UMCModelLoader {
public:
    explicit UMCModelLoader(VirtualGpuMemory& virtualMem);
    ~UMCModelLoader() = default;

    // Load binary file sequentially into the virtual memory space in chunks
    bool loadModel(const std::string& path, size_t& outBlocksLoaded);

private:
    VirtualGpuMemory& m_virtualMem;
};
