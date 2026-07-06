# Unified Memory Controller (UMC)

A high-performance, software-defined Memory Management Unit (MMU) written in C++23 for Windows 11 and NVIDIA CUDA. UMC pools multi-GPU VRAM, physical System RAM, and the SSD-backed System Pagefile into a single logical "virtual GPU memory" space, allowing you to load and query huge files (such as AI model weights) that exceed physical VRAM limits.

---

## Architecture Overview

UMC partitions the logical virtual memory space into fixed-size blocks (default 4 MB). Each block is dynamically allocated on the highest available memory tier:

```
  Logical Offset / Address Space (0 ... N-1 Bytes)
                      |
                      v
      +-------------------------------+
      |  VirtualGpuMemory Controller  |
      +-------------------------------+
                      |
        +-------------+-------------+
        |             |             |
        v             v             v
   [ Tier 1 ]    [ Tier 2 ]    [ Tier 3 ]
    GPU VRAM     System RAM     Pagefile
    (CUDA DMA)   (VirtualAlloc) (Win32 Mapped File)
```

### Allocation Tiers

1. **Tier 1: GPU VRAM (Multi-GPU)**
   * **Monitoring**: Enlisted via the **NVIDIA Management Library (NVML)**.
   * **Orchestration**: Managed via CUDA Runtime APIs (`cudaMalloc`, `cudaMemcpy`, `cudaFree`). 
2. **Tier 2: System RAM**
   * **Monitoring**: Monitored via Windows API (`GlobalMemoryStatusEx`).
   * **Orchestration**: Allocated using low-level Win32 `VirtualAlloc` (`MEM_COMMIT | MEM_RESERVE` with `PAGE_READWRITE`).
3. **Tier 3: Pagefile (SSD-backed)**
   * **Monitoring**: Budget calculated using system performance statistics (`GetPerformanceInfo`).
   * **Orchestration**: Allocated via a file mapping backed by the system paging file (`CreateFileMapping` with `INVALID_HANDLE_VALUE`). Operations use short-lived sliding views (`MapViewOfFile` / `UnmapViewOfFile`) to prevent virtual address space exhaustion.

---

## How It Works

### Software-Defined Block Mapping
UMC stores a thread-safe registry (`std::unordered_map<size_t, BlockLocation>`) matching logical block IDs to physical locations:
```cpp
struct BlockLocation {
    MemoryTier tier;
    int deviceId;           // GPU Index for VRAM, -1 for RAM/Pagefile
    size_t physicalOffset;  // Physical pointer address or file mapping offset
    size_t size;
};
```

* **Synchronous DMA Transfers**: Writing to/reading from the GPU tier executes a blocking host-to-device/device-to-host DMA transfer (`cudaMemcpy`).
* **Pagefile Memory Windows**: To access a block in the pagefile, UMC maps only that block's offset. After copying data, it instantly unmaps the view, maintaining an extremely low memory footprint.
* **Byte-Granular Access**: Logical reads/writes spanning block boundaries are intercepted and split into corresponding physical operations.

---

## Prerequisites

* **Operating System**: Windows 11 (64-bit).
* **Compiler**: MSVC (Visual Studio 2022) with **C++23** support.
* **CUDA SDK**: CUDA Toolkit 12.x / 13.x (with `nvcc` and `cudart` library).
* **NVIDIA Driver**: Required for NVML (`nvml.dll` in `C:\Windows\System32`).
* **Build System**: CMake 3.20+.

---

## Build Instructions

1. Open PowerShell or Command Prompt.
2. Navigate to the project root:
   ```cmd
   cd C:\Users\luisf\UMC
   ```
3. Run the configuration command:
   ```cmd
   cmake -B build -S .
   ```
4. Build the executable in Release configuration:
   ```cmd
   cmake --build build --config Release
   ```

---

## Running the Demo

Navigate to the release build folder and run the executable:
```cmd
cd build\Release
umc_demo.exe
```

### Demonstration Operations
The demo executable (`main.cpp`):
1. Detects your physical RTX 3060 graphics card, host RAM, and system commit limit.
2. Restricts the allocation budget of each tier to **16 MB** for testing purposes.
3. Writes a **48 MB** dummy binary model file (`dummy_model.bin`) to disk.
4. Streams the file into the virtual space. The controller automatically places:
   * **Blocks 0–3** (16 MB) in **VRAM**.
   * **Blocks 4–7** (16 MB) in **System RAM**.
   * **Blocks 8–11** (16 MB) in the **Pagefile**.
5. Prints the **Virtual GPU Layout Map** showing active pointers/offsets.
6. Performs byte-by-byte integrity verification to ensure zero data corruption.
7. Executes a logical write/read crossing the 4 MB block boundary contiguous offset.
8. Safely unallocates all GPU and Win32 handles.

---

## Integration in AI Pipelines (e.g. ComfyUI / PyTorch / C++)

For custom runtimes or Python bindings:
* **C++ Runtimes**: Integrate `VirtualGpuMemory` as the underlying allocator for tensor layers. Use double buffering where the inference execution thread runs matrix multiplication on the GPU staging buffer, while a background prefetch thread loads the next layer from system RAM/Pagefile into VRAM.
* **PyTorch/ComfyUI**: Compile the code as a PyTorch C++ extension using `pybind11`. Offload heavyweight PyTorch tensors into UMC storage. When ComfyUI requests layer execution, copy weights directly from UMC's tracked offsets into PyTorch CUDA tensors, bypassing Windows OS coarse pagefile freezes.
