#include "VirtualGpuMemory.h"
#include <nvml.h>
#include <cuda_runtime.h>
#include <psapi.h>
#include <algorithm>
#include <iomanip>

std::string tierToString(MemoryTier tier) {
    switch (tier) {
        case MemoryTier::Vram:     return "VRAM";
        case MemoryTier::Ram:      return "RAM";
        case MemoryTier::Pagefile: return "Pagefile";
    }
    return "Unknown";
}

VirtualGpuMemory::VirtualGpuMemory(
    size_t blockSizeBytes,
    size_t maxVramBudget,
    size_t maxRamBudget,
    size_t maxPagefileBudget
)
    : m_blockSizeBytes(blockSizeBytes)
    , m_totalVirtualCapacity(0)
    , m_totalHostRam(0)
    , m_availableHostRam(0)
    , m_totalPagefile(0)
    , m_availablePagefile(0)
    , m_hPagefileMapping(nullptr)
    , m_pagefileAllocatedBytes(0)
    , m_pagefileLimitBytes(0)
    , m_nvmlInitialized(false)
    , m_hostAllocatedBytes(0)
    , m_ramLimitBytes(0)
    , m_maxVramBudgetOverride(maxVramBudget)
    , m_maxRamBudgetOverride(maxRamBudget)
    , m_maxPagefileBudgetOverride(maxPagefileBudget)
{
}

VirtualGpuMemory::~VirtualGpuMemory() {
    cleanup();
}

bool VirtualGpuMemory::initialize() {
    // 1. Initialize NVML and query GPUs
    nvmlReturn_t nvmlRes = nvmlInit();
    if (nvmlRes != NVML_SUCCESS) {
        std::cerr << "[UMC WARNING] NVML initialization failed. Error code: " << nvmlRes 
                  << ". Operating in GPU-less mode.\n";
        m_nvmlInitialized = false;
    } else {
        m_nvmlInitialized = true;
        unsigned int deviceCount = 0;
        nvmlRes = nvmlDeviceGetCount(&deviceCount);
        if (nvmlRes == NVML_SUCCESS) {
            m_gpuAllocatedBytes.resize(deviceCount, 0);
            for (unsigned int i = 0; i < deviceCount; ++i) {
                nvmlDevice_t device;
                if (nvmlDeviceGetHandleByIndex(i, &device) != NVML_SUCCESS) continue;

                char name[96] = {0};
                std::string deviceName = "Unknown GPU";
                if (nvmlDeviceGetName(device, name, sizeof(name)) == NVML_SUCCESS) {
                    deviceName = name;
                }

                nvmlMemory_t memInfo;
                size_t totalVram = 0;
                size_t freeVram = 0;
                if (nvmlDeviceGetMemoryInfo(device, &memInfo) == NVML_SUCCESS) {
                    totalVram = memInfo.total;
                    freeVram = memInfo.free;
                }

                m_gpuDevices.push_back({ static_cast<int>(i), deviceName, totalVram, freeVram });
                std::cout << "[UMC INFO] Detected GPU " << i << ": " << deviceName 
                          << " | VRAM Total: " << totalVram / (1024 * 1024) << " MB"
                          << " | Free: " << freeVram / (1024 * 1024) << " MB\n";
            }
        } else {
            std::cerr << "[UMC WARNING] Failed to query GPU device count from NVML. Error: " << nvmlRes << "\n";
        }
    }

    // 2. Query System RAM using GlobalMemoryStatusEx
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        m_totalHostRam = memStatus.ullTotalPhys;
        m_availableHostRam = memStatus.ullAvailPhys;
        std::cout << "[UMC INFO] System RAM: Total = " << m_totalHostRam / (1024 * 1024) << " MB"
                  << " | Available = " << m_availableHostRam / (1024 * 1024) << " MB\n";
    } else {
        std::cerr << "[UMC ERROR] GlobalMemoryStatusEx failed. Error code: " << GetLastError() << "\n";
        return false;
    }

    // 3. Query Pagefile using GetPerformanceInfo
    PERFORMANCE_INFORMATION perfInfo;
    perfInfo.cb = sizeof(perfInfo);
    if (GetPerformanceInfo(&perfInfo, sizeof(perfInfo))) {
        size_t pageSize = perfInfo.PageSize;
        m_totalPagefile = perfInfo.CommitLimit * pageSize;
        m_availablePagefile = (perfInfo.CommitLimit - perfInfo.CommitTotal) * pageSize;
        std::cout << "[UMC INFO] Pagefile Commit Limit: Total = " << m_totalPagefile / (1024 * 1024) << " MB"
                  << " | Available = " << m_availablePagefile / (1024 * 1024) << " MB\n";
    } else {
        std::cerr << "[UMC ERROR] GetPerformanceInfo failed. Error code: " << GetLastError() << "\n";
        return false;
    }

    // 4. Calculate budgets and set limits
    m_gpuLimitBytes.resize(m_gpuDevices.size(), 0);
    size_t totalGpuVramAllowed = 0;
    for (size_t i = 0; i < m_gpuDevices.size(); ++i) {
        if (m_maxVramBudgetOverride > 0) {
            m_gpuLimitBytes[i] = m_maxVramBudgetOverride;
        } else {
            m_gpuLimitBytes[i] = static_cast<size_t>(m_gpuDevices[i].freeVram * 0.80);
        }
        totalGpuVramAllowed += m_gpuLimitBytes[i];
    }

    if (m_maxRamBudgetOverride > 0) {
        m_ramLimitBytes = m_maxRamBudgetOverride;
    } else {
        m_ramLimitBytes = static_cast<size_t>(m_availableHostRam * 0.50);
    }
    size_t totalRamAllowed = m_ramLimitBytes;

    if (m_maxPagefileBudgetOverride > 0) {
        m_pagefileLimitBytes = m_maxPagefileBudgetOverride;
    } else {
        m_pagefileLimitBytes = (std::min)(size_t(256) * 1024 * 1024, m_availablePagefile / 2);
        if (m_pagefileLimitBytes < m_blockSizeBytes) {
            m_pagefileLimitBytes = m_blockSizeBytes * 16;
        }
    }
    size_t totalPagefileAllowed = m_pagefileLimitBytes;

    m_totalVirtualCapacity = totalGpuVramAllowed + totalRamAllowed + totalPagefileAllowed;

    std::cout << "[UMC INFO] Unified Memory Controller initialized successfully.\n"
              << "   - VRAM Budget:     " << totalGpuVramAllowed / (1024 * 1024) << " MB\n"
              << "   - Host RAM Budget: " << totalRamAllowed / (1024 * 1024) << " MB\n"
              << "   - Pagefile Budget: " << totalPagefileAllowed / (1024 * 1024) << " MB\n"
              << "   - Total virtual GPU memory capacity: " << m_totalVirtualCapacity / (1024 * 1024) << " MB\n";

    return true;
}

void VirtualGpuMemory::cleanup() {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& [blockId, loc] : m_blockMap) {
        if (loc.tier == MemoryTier::Vram) {
            cudaSetDevice(loc.deviceId);
            cudaFree(reinterpret_cast<void*>(loc.physicalOffset));
        } else if (loc.tier == MemoryTier::Ram) {
            VirtualFree(reinterpret_cast<void*>(loc.physicalOffset), 0, MEM_RELEASE);
        }
    }
    m_blockMap.clear();

    if (m_hPagefileMapping) {
        CloseHandle(m_hPagefileMapping);
        m_hPagefileMapping = nullptr;
    }

    if (m_nvmlInitialized) {
        nvmlShutdown();
        m_nvmlInitialized = false;
    }
}

bool VirtualGpuMemory::writeBlock(size_t blockId, const void* data, size_t size) {
    if (size > m_blockSizeBytes) {
        std::cerr << "[UMC ERROR] Write block " << blockId << " failed: size " << size 
                  << " exceeds block limit " << m_blockSizeBytes << ".\n";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Locate or allocate physical block location
    BlockLocation loc;
    auto it = m_blockMap.find(blockId);
    if (it == m_blockMap.end()) {
        if (!allocateBlockPhysical(blockId, loc)) {
            return false;
        }
    } else {
        loc = it->second;
    }

    // Synchronous write based on chosen tier
    if (loc.tier == MemoryTier::Vram) {
        cudaError_t err = cudaSetDevice(loc.deviceId);
        if (err != cudaSuccess) {
            std::cerr << "[UMC ERROR] Failed to set GPU device " << loc.deviceId << ".\n";
            return false;
        }
        err = cudaMemcpy(reinterpret_cast<void*>(loc.physicalOffset), data, size, cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            std::cerr << "[UMC ERROR] VRAM memcpy failed on device " << loc.deviceId 
                      << ". CUDA Error: " << cudaGetErrorString(err) << "\n";
            return false;
        }
    } else if (loc.tier == MemoryTier::Ram) {
        memcpy(reinterpret_cast<void*>(loc.physicalOffset), data, size);
    } else if (loc.tier == MemoryTier::Pagefile) {
        if (!m_hPagefileMapping) {
            std::cerr << "[UMC ERROR] Pagefile mapping handle is null.\n";
            return false;
        }
        void* ptr = MapViewOfFile(
            m_hPagefileMapping,
            FILE_MAP_WRITE,
            static_cast<DWORD>(loc.physicalOffset >> 32),
            static_cast<DWORD>(loc.physicalOffset & 0xFFFFFFFF),
            m_blockSizeBytes
        );
        if (!ptr) {
            std::cerr << "[UMC ERROR] MapViewOfFile failed for block " << blockId 
                      << ". Win32 Error: " << GetLastError() << "\n";
            return false;
        }
        memcpy(ptr, data, size);
        UnmapViewOfFile(ptr);
    }

    return true;
}

bool VirtualGpuMemory::readBlock(size_t blockId, void* outData, size_t size) {
    if (size > m_blockSizeBytes) {
        std::cerr << "[UMC ERROR] Read block " << blockId << " failed: size " << size 
                  << " exceeds block limit " << m_blockSizeBytes << ".\n";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_blockMap.find(blockId);
    if (it == m_blockMap.end()) {
        std::cerr << "[UMC ERROR] Read block " << blockId << " failed: block not allocated.\n";
        return false;
    }
    const BlockLocation& loc = it->second;

    // Synchronous read based on chosen tier
    if (loc.tier == MemoryTier::Vram) {
        cudaError_t err = cudaSetDevice(loc.deviceId);
        if (err != cudaSuccess) return false;
        err = cudaMemcpy(outData, reinterpret_cast<void*>(loc.physicalOffset), size, cudaMemcpyDeviceToHost);
        if (err != cudaSuccess) {
            std::cerr << "[UMC ERROR] VRAM read-back failed. CUDA Error: " << cudaGetErrorString(err) << "\n";
            return false;
        }
    } else if (loc.tier == MemoryTier::Ram) {
        memcpy(outData, reinterpret_cast<void*>(loc.physicalOffset), size);
    } else if (loc.tier == MemoryTier::Pagefile) {
        if (!m_hPagefileMapping) return false;
        void* ptr = MapViewOfFile(
            m_hPagefileMapping,
            FILE_MAP_READ,
            static_cast<DWORD>(loc.physicalOffset >> 32),
            static_cast<DWORD>(loc.physicalOffset & 0xFFFFFFFF),
            m_blockSizeBytes
        );
        if (!ptr) {
            std::cerr << "[UMC ERROR] MapViewOfFile failed. Win32 Error: " << GetLastError() << "\n";
            return false;
        }
        memcpy(outData, ptr, size);
        UnmapViewOfFile(ptr);
    }

    return true;
}

bool VirtualGpuMemory::write(size_t logicalOffset, const void* data, size_t size) {
    if (size == 0) return true;
    const char* src = reinterpret_cast<const char*>(data);
    size_t bytesWritten = 0;

    while (bytesWritten < size) {
        size_t currentOffset = logicalOffset + bytesWritten;
        size_t blockId = currentOffset / m_blockSizeBytes;
        size_t blockOffset = currentOffset % m_blockSizeBytes;
        size_t bytesToCopy = (std::min)(m_blockSizeBytes - blockOffset, size - bytesWritten);

        std::lock_guard<std::mutex> lock(m_mutex);

        BlockLocation loc;
        auto it = m_blockMap.find(blockId);
        if (it == m_blockMap.end()) {
            if (!allocateBlockPhysical(blockId, loc)) {
                return false;
            }
        } else {
            loc = it->second;
        }

        if (loc.tier == MemoryTier::Vram) {
            cudaError_t err = cudaSetDevice(loc.deviceId);
            if (err != cudaSuccess) return false;
            err = cudaMemcpy(reinterpret_cast<void*>(loc.physicalOffset + blockOffset), 
                             src + bytesWritten, bytesToCopy, cudaMemcpyHostToDevice);
            if (err != cudaSuccess) return false;
        } else if (loc.tier == MemoryTier::Ram) {
            memcpy(reinterpret_cast<char*>(loc.physicalOffset) + blockOffset, 
                   src + bytesWritten, bytesToCopy);
        } else if (loc.tier == MemoryTier::Pagefile) {
            void* ptr = MapViewOfFile(
                m_hPagefileMapping,
                FILE_MAP_WRITE,
                static_cast<DWORD>(loc.physicalOffset >> 32),
                static_cast<DWORD>(loc.physicalOffset & 0xFFFFFFFF),
                m_blockSizeBytes
            );
            if (!ptr) return false;
            memcpy(reinterpret_cast<char*>(ptr) + blockOffset, src + bytesWritten, bytesToCopy);
            UnmapViewOfFile(ptr);
        }

        bytesWritten += bytesToCopy;
    }
    return true;
}

bool VirtualGpuMemory::read(size_t logicalOffset, void* outData, size_t size) {
    if (size == 0) return true;
    char* dest = reinterpret_cast<char*>(outData);
    size_t bytesRead = 0;

    while (bytesRead < size) {
        size_t currentOffset = logicalOffset + bytesRead;
        size_t blockId = currentOffset / m_blockSizeBytes;
        size_t blockOffset = currentOffset % m_blockSizeBytes;
        size_t bytesToCopy = (std::min)(m_blockSizeBytes - blockOffset, size - bytesRead);

        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_blockMap.find(blockId);
        if (it == m_blockMap.end()) {
            std::cerr << "[UMC ERROR] Read failed: block " << blockId << " is not allocated.\n";
            return false;
        }
        const BlockLocation& loc = it->second;

        if (loc.tier == MemoryTier::Vram) {
            cudaError_t err = cudaSetDevice(loc.deviceId);
            if (err != cudaSuccess) return false;
            err = cudaMemcpy(dest + bytesRead, 
                             reinterpret_cast<const void*>(loc.physicalOffset + blockOffset), 
                             bytesToCopy, cudaMemcpyDeviceToHost);
            if (err != cudaSuccess) return false;
        } else if (loc.tier == MemoryTier::Ram) {
            memcpy(dest + bytesRead, 
                   reinterpret_cast<const char*>(loc.physicalOffset) + blockOffset, 
                   bytesToCopy);
        } else if (loc.tier == MemoryTier::Pagefile) {
            void* ptr = MapViewOfFile(
                m_hPagefileMapping,
                FILE_MAP_READ,
                static_cast<DWORD>(loc.physicalOffset >> 32),
                static_cast<DWORD>(loc.physicalOffset & 0xFFFFFFFF),
                m_blockSizeBytes
            );
            if (!ptr) return false;
            memcpy(dest + bytesRead, reinterpret_cast<const char*>(ptr) + blockOffset, bytesToCopy);
            UnmapViewOfFile(ptr);
        }

        bytesRead += bytesToCopy;
    }
    return true;
}

bool VirtualGpuMemory::getBlockLocation(size_t blockId, BlockLocation& outLocation) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_blockMap.find(blockId);
    if (it != m_blockMap.end()) {
        outLocation = it->second;
        return true;
    }
    return false;
}

void VirtualGpuMemory::dumpLayout() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "\n================ VIRTUAL GPU LAYOUT MAP ================\n";
    std::cout << std::left << std::setw(10) << "Block ID"
              << std::setw(12) << "Tier"
              << std::setw(12) << "Device ID"
              << std::setw(18) << "Physical Offset"
              << std::setw(12) << "Size (MB)" << "\n";
    std::cout << "--------------------------------------------------------\n";
    
    std::vector<size_t> sortedBlockIds;
    sortedBlockIds.reserve(m_blockMap.size());
    for (const auto& [id, _] : m_blockMap) {
        sortedBlockIds.push_back(id);
    }
    std::sort(sortedBlockIds.begin(), sortedBlockIds.end());

    for (size_t id : sortedBlockIds) {
        const auto& loc = m_blockMap.at(id);
        std::string devStr = (loc.deviceId == -1) ? "Host" : std::to_string(loc.deviceId);
        std::cout << std::left << std::setw(10) << id
                  << std::setw(12) << tierToString(loc.tier)
                  << std::setw(12) << devStr
                  << "0x" << std::hex << std::setw(16) << std::setfill('0') << loc.physicalOffset 
                  << std::dec << std::setfill(' ')
                  << std::setw(12) << static_cast<double>(loc.size) / (1024.0 * 1024.0) << "\n";
    }
    std::cout << "========================================================\n\n";
}

bool VirtualGpuMemory::allocateBlockPhysical(size_t blockId, BlockLocation& outLocation) {
    // Priority 1: VRAM
    if (allocateVram(blockId, outLocation)) {
        m_blockMap[blockId] = outLocation;
        return true;
    }
    // Priority 2: host RAM
    if (allocateRam(blockId, outLocation)) {
        m_blockMap[blockId] = outLocation;
        return true;
    }
    // Priority 3: Pagefile
    if (allocatePagefile(blockId, outLocation)) {
        m_blockMap[blockId] = outLocation;
        return true;
    }
    return false;
}

bool VirtualGpuMemory::allocateVram(size_t blockId, BlockLocation& outLocation) {
    for (size_t i = 0; i < m_gpuDevices.size(); ++i) {
        size_t gpuLimit = m_gpuLimitBytes[i];
        if (m_gpuAllocatedBytes[i] + m_blockSizeBytes <= gpuLimit) {
            cudaError_t err = cudaSetDevice(static_cast<int>(i));
            if (err != cudaSuccess) continue;

            void* devPtr = nullptr;
            err = cudaMalloc(&devPtr, m_blockSizeBytes);
            if (err == cudaSuccess) {
                // Initialize to zero for consistency with VirtualAlloc
                cudaMemset(devPtr, 0, m_blockSizeBytes);

                outLocation.tier = MemoryTier::Vram;
                outLocation.deviceId = static_cast<int>(i);
                outLocation.physicalOffset = reinterpret_cast<size_t>(devPtr);
                outLocation.size = m_blockSizeBytes;

                m_gpuAllocatedBytes[i] += m_blockSizeBytes;
                std::cout << "[UMC ALLOC] Block " << blockId << " -> VRAM (GPU " << i << ") "
                          << "at 0x" << std::hex << outLocation.physicalOffset << std::dec << "\n";
                return true;
            } else {
                cudaGetLastError(); // Clear error state
            }
        }
    }
    return false;
}

bool VirtualGpuMemory::allocateRam(size_t blockId, BlockLocation& outLocation) {
    size_t hostLimit = m_ramLimitBytes;
    if (m_hostAllocatedBytes + m_blockSizeBytes <= hostLimit) {
        void* ramPtr = VirtualAlloc(nullptr, m_blockSizeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (ramPtr) {
            outLocation.tier = MemoryTier::Ram;
            outLocation.deviceId = -1;
            outLocation.physicalOffset = reinterpret_cast<size_t>(ramPtr);
            outLocation.size = m_blockSizeBytes;

            m_hostAllocatedBytes += m_blockSizeBytes;
            std::cout << "[UMC ALLOC] Block " << blockId << " -> System RAM "
                      << "at 0x" << std::hex << outLocation.physicalOffset << std::dec << "\n";
            return true;
        }
    }
    return false;
}

bool VirtualGpuMemory::allocatePagefile(size_t blockId, BlockLocation& outLocation) {
    if (m_pagefileAllocatedBytes + m_blockSizeBytes <= m_pagefileLimitBytes) {
        if (!m_hPagefileMapping) {
            m_hPagefileMapping = CreateFileMappingW(
                INVALID_HANDLE_VALUE,
                nullptr,
                PAGE_READWRITE,
                static_cast<DWORD>(m_pagefileLimitBytes >> 32),
                static_cast<DWORD>(m_pagefileLimitBytes & 0xFFFFFFFF),
                nullptr
            );
            if (!m_hPagefileMapping) {
                std::cerr << "[UMC ERROR] Failed to create system pagefile-backed mapping. Error: " 
                          << GetLastError() << "\n";
                return false;
            }
        }

        outLocation.tier = MemoryTier::Pagefile;
        outLocation.deviceId = -1;
        outLocation.physicalOffset = m_pagefileAllocatedBytes;
        outLocation.size = m_blockSizeBytes;

        m_pagefileAllocatedBytes += m_blockSizeBytes;
        std::cout << "[UMC ALLOC] Block " << blockId << " -> Pagefile Segment "
                  << "at offset 0x" << std::hex << outLocation.physicalOffset << std::dec << "\n";
        return true;
    }
    return false;
}
