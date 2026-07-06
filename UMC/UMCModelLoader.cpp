#include "UMCModelLoader.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <algorithm>

UMCModelLoader::UMCModelLoader(VirtualGpuMemory& virtualMem)
    : m_virtualMem(virtualMem)
{
}

bool UMCModelLoader::loadModel(const std::string& path, size_t& outBlocksLoaded) {
    outBlocksLoaded = 0;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[UMC LOADER ERROR] Failed to open file: " << path << "\n";
        return false;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (fileSize == 0) {
        std::cerr << "[UMC LOADER ERROR] File is empty: " << path << "\n";
        return false;
    }

    size_t blockSize = m_virtualMem.getBlockSize();
    std::vector<char> buffer(blockSize);

    std::cout << "[UMC LOADER INFO] Loading model file: " << path << "\n"
              << "   - File size: " << fileSize / (1024 * 1024) << " MB (" << fileSize << " bytes)\n"
              << "   - Block size: " << blockSize / (1024 * 1024) << " MB\n";

    size_t bytesRemaining = fileSize;
    size_t blockId = 0;

    while (bytesRemaining > 0) {
        size_t currentReadSize = (std::min)(bytesRemaining, blockSize);
        file.read(buffer.data(), currentReadSize);
        if (!file) {
            std::cerr << "[UMC LOADER ERROR] Disk read failed at block " << blockId << ".\n";
            return false;
        }

        // Synchronously write block to virtual GPU memory
        if (!m_virtualMem.writeBlock(blockId, buffer.data(), currentReadSize)) {
            std::cerr << "[UMC LOADER ERROR] Write to VirtualGpuMemory failed at block " << blockId << ".\n";
            return false;
        }

        bytesRemaining -= currentReadSize;
        blockId++;
    }

    outBlocksLoaded = blockId;
    std::cout << "[UMC LOADER INFO] Finished loading model. Total blocks: " << outBlocksLoaded << "\n";
    return true;
}
