# Unified Memory Controller (UMC) — Porte Linux

Porte para Linux do [UMC original](https://github.com/luisfilipesilvasantos/UMC), uma
Memory Management Unit (MMU) definida por software, escrita em C++23 para NVIDIA CUDA.
O UMC agrupa VRAM multi-GPU, RAM física do sistema, e uma tier de overflow em disco
(equivalente ao pagefile do Windows) num único espaço lógico de "memória GPU virtual",
permitindo carregar e consultar ficheiros grandes (como pesos de modelos de IA) que
excedem os limites físicos de VRAM.

Este porte mantém a **mesma arquitetura e a mesma API pública** do projeto original —
apenas as chamadas específicas do Windows foram substituídas pelos equivalentes POSIX/Linux.
Todo o código foi efetivamente compilado e testado neste porte (ver secção "Estado do porte").

---

## Arquitetura

O UMC particiona o espaço de memória virtual lógico em blocos de tamanho fixo (por
omissão 4 MB). Cada bloco é alocado dinamicamente na tier de memória mais alta disponível:

```
  Espaço de Endereços Lógico (0 ... N-1 Bytes)
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
    GPU VRAM     System RAM     Overflow
    (CUDA DMA)   (mmap anon)   (ficheiro mmap)
```

### Tabela de correspondência Windows → Linux

| Componente               | Windows (original)                                    | Linux (este porte)                                                        |
|---------------------------|--------------------------------------------------------|-----------------------------------------------------------------------------|
| Tier 1: VRAM               | `cudaMalloc`/`cudaMemcpy`/`cudaFree`                    | **Idêntico** — API CUDA Runtime é multiplataforma                          |
| Monitorização de VRAM       | NVML (`nvml.dll`)                                        | NVML (`libnvidia-ml.so.1`) — mesma API, ver secção NVML abaixo             |
| Tier 2: System RAM          | `VirtualAlloc(MEM_COMMIT\|MEM_RESERVE, PAGE_READWRITE)` | `mmap(MAP_PRIVATE\|MAP_ANONYMOUS)`                                          |
| Libertação da RAM            | `VirtualFree(ptr, 0, MEM_RELEASE)`                      | `munmap(ptr, blockSize)`                                                    |
| Monitorização de RAM          | `GlobalMemoryStatusEx`                                  | `/proc/meminfo` (`MemTotal` / `MemAvailable`)                                |
| Tier 3: Pagefile/Overflow     | `CreateFileMapping(INVALID_HANDLE_VALUE, ...)`          | Ficheiro real em `/var/tmp/umc_pagefile.bin`, aberto e imediatamente `unlink()`ado (fica "anónimo": fd válido, espaço reservado, sem entrada no diretório) |
| Acesso a blocos do overflow    | `MapViewOfFile`/`UnmapViewOfFile` (janelas deslizantes) | `mmap`/`munmap` por bloco (mesma janela deslizante, mesmo footprint baixo) |
| Monitorização do overflow      | `GetPerformanceInfo` (`CommitLimit`/`CommitTotal`)      | `/proc/meminfo` (`CommitLimit`/`Committed_AS`) — métrica equivalente de overcommit |
| Códigos de erro              | `GetLastError()`                                        | `errno` / `strerror(errno)`                                                 |

### Como funciona

* **Registo thread-safe**: um `std::unordered_map<size_t, BlockLocation>` mapeia IDs de
  blocos lógicos para localizações físicas — exatamente como no original.
* **Transferências DMA síncronas**: escrever/ler na tier de GPU executa uma transferência
  bloqueante host↔device via `cudaMemcpy`.
* **Janelas de memória do overflow**: para aceder a um bloco no ficheiro de overflow, o
  UMC mapeia apenas o offset desse bloco e desmapeia logo a seguir — mesmo comportamento
  de baixo footprint do original.
* **Acesso byte-a-byte**: leituras/escritas lógicas que atravessam fronteiras de blocos
  são divididas automaticamente em operações físicas correspondentes.

---

## Pré-requisitos

* **Sistema operativo**: Linux 64-bit (testado o build em Ubuntu 24.04).
* **Compilador**: GCC ou Clang com suporte a **C++23**.
* **CUDA SDK**: CUDA Toolkit 12.x/13.x instalado (com `nvcc` e a biblioteca `cudart`).
* **Driver NVIDIA**: necessário para NVML (`libnvidia-ml.so.1`, instalado com o driver).
* **Sistema de build**: CMake 3.20+.

```bash
sudo apt update
sudo apt install -y build-essential cmake
# CUDA Toolkit: segue as instruções oficiais da NVIDIA para a tua distro
# (https://developer.nvidia.com/cuda-downloads) — necessário para cudart e os
# headers cuda_runtime.h/nvml.h.
```

---

## Compilar

```bash
cd UMC-Linux/UMC
cmake -B build -S .
cmake --build build --config Release
```

Se o CUDA Toolkit não estiver em `/usr/local/cuda`, indica o caminho:

```bash
cmake -B build -S . -DCUDA_SDK_DIR=/caminho/para/o/teu/cuda
```

---

## Correr a demo

```bash
cd build
./umc_demo
```

### O que a demo faz (`main.cpp`, inalterado face ao original)

1. Deteta a(s) GPU(s) física(s), a RAM do sistema, e o commit limit do sistema.
2. Restringe o orçamento de cada tier a **16 MB** para fins de teste.
3. Escreve um ficheiro binário fictício de **48 MB** (`dummy_model.bin`) em disco.
4. Faz streaming do ficheiro para o espaço virtual. O controlador coloca automaticamente:
   * **Blocos 0–3** (16 MB) em **VRAM**.
   * **Blocos 4–7** (16 MB) em **RAM do sistema**.
   * **Blocos 8–11** (16 MB) no **overflow** (equivalente ao pagefile).
5. Imprime o **Virtual GPU Layout Map** com os ponteiros/offsets ativos.
6. Faz verificação de integridade byte-a-byte para garantir zero corrupção de dados.
7. Executa uma escrita/leitura lógica que atravessa a fronteira de 4 MB de um bloco.
8. Liberta em segurança todas as alocações de GPU e de memória do processo.

---

## Estado do porte

Este porte foi **compilado, ligado e corrido com sucesso** neste ambiente (GCC 13,
C++23, CMake 3.28), usando stubs mínimos de `cuda_runtime.h`/`nvml.h` no lugar do CUDA
Toolkit real, porque este ambiente de desenvolvimento não tem GPU NVIDIA nem o toolkit
instalado. Com os stubs (0 GPUs detetadas, exatamente como reportaria uma máquina real
sem NVIDIA), a demo:

* inicializa corretamente, lê `/proc/meminfo` e calcula os orçamentos das 3 tiers;
* aloca corretamente os blocos 0–3 em **RAM** (`mmap` anónimo) e os blocos 4–7 no
  **ficheiro de overflow** (`mmap` de ficheiro);
* falha, como esperado, no bloco 8 — porque sem GPU só há 32 MB de orçamento total
  (16 MB RAM + 16 MB overflow) para um ficheiro de 48 MB. Numa máquina real com GPU,
  os blocos 0–3 iriam para VRAM em vez de RAM, libertando espaço para os 12 blocos
  completarem exatamente como no demo original.

Isto confirma que a lógica de tiers, o `mmap`/`munmap` de RAM, e as janelas deslizantes
de `mmap` do ficheiro de overflow estão corretos. **Não foi testado contra CUDA/NVML
reais** — antes de correres a demo a sério numa máquina com GPU, confirma que:

1. `nvidia-smi` funciona e reporta a(s) tua(s) GPU(s);
2. `nvcc --version` funciona (CUDA Toolkit instalado);
3. `ldconfig -p | grep nvidia-ml` mostra `libnvidia-ml.so.1` (parte do driver).

Se o link falhar por não encontrar `libnvidia-ml`, o CMakeLists.txt já tenta o stub em
`${CUDA_SDK_DIR}/lib64/stubs/`; caso o teu toolkit não o tenha, passa manualmente:
`cmake -B build -S . -DNVML_LIB=/caminho/para/libnvidia-ml.so`.

---

## Integração em pipelines de IA (ex.: ComfyUI / PyTorch / C++)

Para runtimes personalizados ou bindings Python:
* **Runtimes C++**: integra `VirtualGpuMemory` como o alocador subjacente para as
  camadas do tensor. Usa double buffering, em que a thread de execução da inferência
  corre a multiplicação de matrizes no buffer de staging da GPU, enquanto uma thread de
  prefetch em segundo plano carrega a próxima camada de RAM/overflow para a VRAM.
* **PyTorch/ComfyUI**: compila o código como uma extensão C++ do PyTorch usando
  `pybind11`. Descarrega tensores pesados do PyTorch para o armazenamento do UMC.
  Quando o ComfyUI pede a execução de uma camada, copia os pesos diretamente dos
  offsets rastreados do UMC para tensores CUDA do PyTorch.

## Estrutura do repositório

```
UMC-Linux/
└── UMC/
    ├── CMakeLists.txt
    ├── VirtualGpuMemory.h      # inalterado na API pública, portado internamente
    ├── VirtualGpuMemory.cpp    # tiers VRAM/RAM/overflow — ver tabela de correspondência acima
    ├── UMCModelLoader.h        # inalterado (já era portável)
    ├── UMCModelLoader.cpp      # inalterado (já era portável)
    └── main.cpp                # demo original, inalterada (já era portável)
```
