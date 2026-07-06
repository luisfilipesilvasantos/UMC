#include "VirtualGpuMemory.h"
#include "UMCModelLoader.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

// Prototypes
bool createDummyModelFile(const std::string& path, size_t sizeBytes);
bool verifyModelData(VirtualGpuMemory& virtualMem, size_t sizeBytes);
bool testByteGranularAccess(VirtualGpuMemory& virtualMem);

int main() {
    std::cout << "========================================================\n";
    std::cout << "   Unified Memory Controller (UMC) - Windows & CUDA     \n";
    std::cout << "========================================================\n\n";

    // 1. Create the VirtualGpuMemory space with 4 MB blocks
    const size_t blockSize = 4 * 1024 * 1024;
    // Enforce 16 MB limits per tier to demonstrate fallback (4 blocks VRAM, 4 blocks RAM, 4 blocks Pagefile)
    VirtualGpuMemory virtualMem(blockSize, 16 * 1024 * 1024, 16 * 1024 * 1024, 16 * 1024 * 1024);

    // Initialize hardware configuration (NVML, system RAM, pagefile stats)
    if (!virtualMem.initialize()) {
        std::cerr << "[FATAL] Failed to initialize VirtualGpuMemory. Exiting.\n";
        return -1;
    }

    // 2. Generate a dummy model file.
    // 48 MB dummy file will distribute perfectly across the tiers.
    const size_t dummyModelSize = 48 * 1024 * 1024; 
    const std::string modelPath = "dummy_model.bin";

    std::cout << "[DEMO INFO] Creating dummy model file on disk...\n";
    if (!createDummyModelFile(modelPath, dummyModelSize)) {
        return -1;
    }

    // 3. Load model using UMCModelLoader
    UMCModelLoader loader(virtualMem);
    size_t blocksLoaded = 0;
    if (!loader.loadModel(modelPath, blocksLoaded)) {
        std::cerr << "[DEMO ERROR] Model loading failed.\n";
        std::remove(modelPath.c_str());
        return -1;
    }

    // 4. Introspect where each block is stored
    virtualMem.dumpLayout();

    // 5. Query specific block mapping
    BlockLocation queryLoc;
    size_t queryBlockId = 0; // First block
    if (virtualMem.getBlockLocation(queryBlockId, queryLoc)) {
        std::cout << "[DEMO INFO] Introspected Block " << queryBlockId << ":\n"
                  << "   - Tier:            " << tierToString(queryLoc.tier) << "\n"
                  << "   - Device ID:       " << ((queryLoc.deviceId == -1) ? "Host" : std::to_string(queryLoc.deviceId)) << "\n"
                  << "   - Physical Offset: 0x" << std::hex << queryLoc.physicalOffset << std::dec << "\n"
                  << "   - Size:            " << queryLoc.size / (1024 * 1024) << " MB\n\n";
    }

    // 6. Verify consistency and ensure no data corruption occurred
    if (!verifyModelData(virtualMem, dummyModelSize)) {
        std::cerr << "[DEMO ERROR] Verification failed: data corruption detected.\n";
        std::remove(modelPath.c_str());
        return -1;
    }

    // 7. Test byte-granular logical read/write spanning across block boundaries
    if (!testByteGranularAccess(virtualMem)) {
        std::cerr << "[DEMO ERROR] Byte-granular boundary test failed.\n";
        std::remove(modelPath.c_str());
        return -1;
    }

    // 8. Clean up disk file and release virtual memory allocations
    std::cout << "\n[DEMO INFO] Cleaning up resources...\n";
    std::remove(modelPath.c_str());
    std::cout << "[DEMO SUCCESS] Unified Memory Controller demo completed successfully!\n";

    return 0;
}

bool createDummyModelFile(const std::string& path, size_t sizeBytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[DEMO ERROR] Failed to create dummy model file: " << path << "\n";
        return false;
    }

    std::vector<char> buffer(1024 * 1024); // 1 MB chunk buffer
    size_t written = 0;
    while (written < sizeBytes) {
        size_t currentChunk = (std::min)(sizeBytes - written, buffer.size());
        for (size_t i = 0; i < currentChunk; ++i) {
            // Pattern: deterministic modulo pattern based on absolute file offset
            buffer[i] = static_cast<char>((written + i) % 256);
        }
        file.write(buffer.data(), currentChunk);
        if (!file) {
            std::cerr << "[DEMO ERROR] File write failed.\n";
            return false;
        }
        written += currentChunk;
    }
    return true;
}

bool verifyModelData(VirtualGpuMemory& virtualMem, size_t sizeBytes) {
    std::cout << "[DEMO INFO] Verifying model integrity byte-by-byte...\n";
    size_t blockSize = virtualMem.getBlockSize();
    std::vector<char> readBuffer(blockSize);

    size_t verifiedBytes = 0;
    size_t blockId = 0;

    while (verifiedBytes < sizeBytes) {
        size_t currentSize = (std::min)(sizeBytes - verifiedBytes, blockSize);
        if (!virtualMem.readBlock(blockId, readBuffer.data(), currentSize)) {
            std::cerr << "[DEMO ERROR] Failed to read block " << blockId << " from virtual memory.\n";
            return false;
        }

        for (size_t i = 0; i < currentSize; ++i) {
            char expected = static_cast<char>((verifiedBytes + i) % 256);
            if (readBuffer[i] != expected) {
                std::cerr << "[DEMO ERROR] Data mismatch at absolute offset " << verifiedBytes + i 
                          << " (Block " << blockId << ", offset " << i << "). "
                          << "Expected: " << static_cast<int>(static_cast<unsigned char>(expected)) 
                          << ", Got: " << static_cast<int>(static_cast<unsigned char>(readBuffer[i])) << "\n";
                return false;
            }
        }

        verifiedBytes += currentSize;
        blockId++;
    }

    std::cout << "[DEMO SUCCESS] All " << verifiedBytes << " bytes of the model matched expectations. No corruption detected.\n";
    return true;
}

bool testByteGranularAccess(VirtualGpuMemory& virtualMem) {
    std::cout << "[DEMO INFO] Starting byte-granular boundary cross test...\n";
    
    // Choose an offset close to the block boundary:
    // Block size = 4MB = 4,194,304 bytes. Let's write starting at 4,194,300 (4 bytes before block 1 start)
    size_t boundaryOffset = virtualMem.getBlockSize() - 4;
    std::string testPattern = "TESTING_BOUNDARY_CROSS_OVER_UNIFIED_MEMORY_CONTROLLER_12345";
    size_t patternLen = testPattern.length();

    // Perform logical write
    if (!virtualMem.write(boundaryOffset, testPattern.data(), patternLen)) {
        std::cerr << "[DEMO ERROR] Byte-granular boundary write failed.\n";
        return false;
    }
    std::cout << "[DEMO INFO] Wrote '" << testPattern << "' across block boundary at offset " << boundaryOffset << ".\n";

    // Read back
    std::vector<char> readPattern(patternLen + 1, 0);
    if (!virtualMem.read(boundaryOffset, readPattern.data(), patternLen)) {
        std::cerr << "[DEMO ERROR] Byte-granular boundary read failed.\n";
        return false;
    }

    std::string readStr(readPattern.data(), patternLen);
    if (readStr != testPattern) {
        std::cerr << "[DEMO ERROR] Byte-granular boundary data mismatch.\n"
                  << "   Expected: " << testPattern << "\n"
                  << "   Got:      " << readStr << "\n";
        return false;
    }

    std::cout << "[DEMO SUCCESS] Byte-granular boundary read/write matched. Read back: '" << readStr << "'\n";
    return true;
}
