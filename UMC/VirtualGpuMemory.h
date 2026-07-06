#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <windows.h>

enum class MemoryTier {
    Vram,
    Ram,
    Pagefile
};

std::string tierToString(MemoryTier tier);

struct BlockLocation {
    MemoryTier tier;
    int deviceId;           // GPU index for VRAM, -1 for RAM/Pagefile
    size_t physicalOffset;  // Physical address (pointer) or file mapping offset
    size_t size;
};

struct GpuDeviceInfo {
    int deviceId;
    std::string name;
    size_t totalVram;
    size_t freeVram;
};

class VirtualGpuMemory {
public:
    explicit VirtualGpuMemory(
        size_t blockSizeBytes = 4 * 1024 * 1024,
        size_t maxVramBudget = 0,
        size_t maxRamBudget = 0,
        size_t maxPagefileBudget = 0
    );
    ~VirtualGpuMemory();

    // Prevent copying and moving
    VirtualGpuMemory(const VirtualGpuMemory&) = delete;
    VirtualGpuMemory& operator=(const VirtualGpuMemory&) = delete;
    VirtualGpuMemory(VirtualGpuMemory&&) = delete;
    VirtualGpuMemory& operator=(VirtualGpuMemory&&) = delete;

    // Initialize hardware configuration (NVML & Windows APIs)
    bool initialize();

    // Configuration accessors
    size_t getBlockSize() const { return m_blockSizeBytes; }
    size_t getTotalVirtualCapacity() const { return m_totalVirtualCapacity; }

    const std::vector<GpuDeviceInfo>& getGpuDevices() const { return m_gpuDevices; }
    size_t getTotalHostRam() const { return m_totalHostRam; }
    size_t getAvailableHostRam() const { return m_availableHostRam; }
    size_t getTotalPagefile() const { return m_totalPagefile; }
    size_t getAvailablePagefile() const { return m_availablePagefile; }

    // Synchronous raw read and write operations
    bool writeBlock(size_t blockId, const void* data, size_t size);
    bool readBlock(size_t blockId, void* outData, size_t size);

    // Byte-granular logical memory access
    bool write(size_t logicalOffset, const void* data, size_t size);
    bool read(size_t logicalOffset, void* outData, size_t size);

    // Introspection
    bool getBlockLocation(size_t blockId, BlockLocation& outLocation) const;
    void dumpLayout() const;

private:
    // Core physical allocation dispatch
    bool allocateBlockPhysical(size_t blockId, BlockLocation& outLocation);
    bool allocateVram(size_t blockId, BlockLocation& outLocation);
    bool allocateRam(size_t blockId, BlockLocation& outLocation);
    bool allocatePagefile(size_t blockId, BlockLocation& outLocation);

    void cleanup();

private:
    const size_t m_blockSizeBytes;
    size_t m_totalVirtualCapacity;

    // Hardware status
    std::vector<GpuDeviceInfo> m_gpuDevices;
    size_t m_totalHostRam;
    size_t m_availableHostRam;
    size_t m_totalPagefile;
    size_t m_availablePagefile;

    // Synchronization
    mutable std::mutex m_mutex;
    std::unordered_map<size_t, BlockLocation> m_blockMap;

    // Pagefile file mapping state
    HANDLE m_hPagefileMapping;
    size_t m_pagefileAllocatedBytes;
    size_t m_pagefileLimitBytes;
    bool m_nvmlInitialized;

    // Allocation trackers (for tier capacity enforcement)
    std::vector<size_t> m_gpuAllocatedBytes;
    std::vector<size_t> m_gpuLimitBytes;
    size_t m_hostAllocatedBytes;
    size_t m_ramLimitBytes;

    // Budget override limits for testing
    const size_t m_maxVramBudgetOverride;
    const size_t m_maxRamBudgetOverride;
    const size_t m_maxPagefileBudgetOverride;
};
