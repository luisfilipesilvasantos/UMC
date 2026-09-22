Quero que analises e corrijas este projeto para que deixe de depender de qualquer implementação, biblioteca, caminho, API ou ferramenta específica do Windows e funcione nativamente em Ubuntu Linux com ambiente gráfico GNOME.

O objetivo NÃO é criar uma solução híbrida Windows/Linux. O objetivo é tornar o projeto Linux-first, especificamente compatível com Ubuntu 24.04 64-bit, mantendo a arquitetura, API pública e funcionalidade original sempre que tecnicamente possível.

O README indica que o projeto já foi parcialmente portado para Linux, usando C++23, CMake, CUDA e APIs POSIX/Linux. Usa essa implementação como ponto de partida e verifica o código real antes de alterar qualquer coisa.

### OBJETIVO PRINCIPAL

Converter/corrigir todas as dependências Windows para equivalentes Linux/Ubuntu.

Procura sistematicamente por:

* Windows.h
* windows.h
* WinAPI
* VirtualAlloc
* VirtualFree
* CreateFile
* CreateFileMapping
* MapViewOfFile
* UnmapViewOfFile
* HANDLE
* DWORD
* BOOL
* GetLastError
* FormatMessage
* GlobalMemoryStatusEx
* GetPerformanceInfo
* PAGE_READWRITE
* MEM_COMMIT
* MEM_RESERVE
* MEM_RELEASE
* DLL
* LoadLibrary
* GetProcAddress
* .dll
* .lib
* caminhos `C:\...`
* caminhos `C:/...`
* `%APPDATA%`
* `%TEMP%`
* `%USERPROFILE%`
* comandos PowerShell
* comandos CMD
* bibliotecas específicas `.lib`
* bibliotecas `.dll`
* pragmas ou flags MSVC
* `cl.exe`
* MSVC
* Visual Studio
* `nvcc` configurado exclusivamente para Windows
* qualquer dependência que assuma Windows.

Não faças apenas alterações cosméticas. Faz uma auditoria completa do código e das dependências.

### SISTEMA ALVO

Considera como ambiente principal:

Ubuntu 24.04 LTS 64-bit
GNOME
GCC 13+
C++23
CMake 3.20+
NVIDIA Driver
CUDA Toolkit 12.x ou 13.x
NVML
POSIX/Linux

O projeto deve compilar através de:

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config
```

e, depois de CUDA estar instalado:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Não assumes que existe Visual Studio, MSVC, PowerShell ou qualquer ferramenta Windows.

### CUDA

CUDA é multiplataforma e deve continuar a ser utilizada.

Verifica corretamente:

```bash
nvcc --version
nvidia-smi
ldconfig -p | grep nvidia-ml
```

O projeto deve localizar corretamente:

* cuda_runtime.h
* nvml.h
* libcudart
* libnvidia-ml.so.1

Não assumes cegamente `/usr/local/cuda`.

Se CUDA estiver instalado noutro local, o CMake deve permitir configurar o caminho.

Preferencialmente utiliza mecanismos modernos do CMake para localizar CUDA em vez de caminhos hardcoded.

### NVML

No Linux utiliza:

```text
libnvidia-ml.so.1
```

e não:

```text
nvml.dll
```

Confirma que o CMake consegue localizar NVML.

Se não conseguir encontrar NVML automaticamente, cria uma deteção robusta através do CMake.

Não copies DLLs Windows para o projeto.

### MEMÓRIA DO SISTEMA

Para a tier de RAM utiliza APIs Linux/POSIX.

O equivalente esperado para a implementação atual é:

```cpp
mmap(...)
munmap(...)
```

com:

```cpp
MAP_PRIVATE | MAP_ANONYMOUS
```

Para informações de memória utiliza:

```text
/proc/meminfo
```

e trata corretamente:

```text
MemTotal
MemAvailable
CommitLimit
Committed_AS
```

Não utilizes:

```cpp
VirtualAlloc
VirtualFree
GlobalMemoryStatusEx
GetPerformanceInfo
```

### OVERFLOW / PAGEFILE

O equivalente Linux do mecanismo de overflow deve continuar baseado em ficheiro + mmap.

O comportamento documentado atualmente utiliza um ficheiro temporário em:

```text
/var/tmp/umc_pagefile.bin
```

e depois faz `unlink()` mantendo o file descriptor aberto.

Mantém esse comportamento se for seguro e apropriado.

Garante:

* criação segura do ficheiro;
* tratamento correto de erros;
* tamanho correto;
* `ftruncate`;
* `mmap`;
* `munmap`;
* `close`;
* `unlink`;
* tratamento de `errno`.

Não introduzas mecanismos Windows.

### TRATAMENTO DE ERROS

Substitui:

```cpp
GetLastError()
```

por mecanismos Linux apropriados:

```cpp
errno
strerror(errno)
```

ou `std::system_error` quando fizer sentido.

As mensagens de erro devem indicar claramente:

* operação que falhou;
* errno;
* caminho/função envolvida;
* dimensão do bloco quando relevante.

### CMAKE

Analisa completamente o `CMakeLists.txt`.

Remove:

* bibliotecas `.lib`;
* DLLs;
* flags exclusivamente MSVC;
* caminhos Windows;
* comandos Windows;
* configuração específica do Visual Studio.

Utiliza GCC/Clang no Linux.

Mantém C++23:

```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

Se forem necessários links adicionais no Linux, configura-os corretamente.

Evita caminhos absolutos hardcoded sempre que possível.

Não assumes que CUDA está obrigatoriamente em `/usr/local/cuda`.

### PORTABILIDADE

Se houver código que precise de funcionar tanto em Windows como Linux, utiliza:

```cpp
#ifdef _WIN32
    // Windows
#elif defined(__linux__)
    // Linux
#endif
```

Mas NÃO cries uma camada de compatibilidade desnecessária se o objetivo deste repositório for exclusivamente Linux.

Se a implementação Windows já não for necessária, prefere código Linux limpo em vez de manter código morto.

### DEPENDÊNCIAS

Cria uma lista explícita das dependências necessárias para Ubuntu.

Se forem necessárias bibliotecas adicionais, determina exatamente quais são e adiciona-as ao processo de instalação.

Por exemplo:

```bash
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config
```

Não inventes pacotes.

Só adiciona uma dependência se o código realmente precisar dela.

Para cada dependência externa, verifica:

1. se existe no Ubuntu 24.04;
2. qual pacote `apt` fornece os headers;
3. qual biblioteca fornece o runtime;
4. se o CMake consegue encontrá-la;
5. se existe conflito de versões.

### PYTHON / PYTORCH / COMFYUI

Se existirem bindings Python, extensões PyTorch ou integração com ComfyUI, verifica também se existem dependências Windows escondidas.

Procura:

```text
torch
torch.utils.cpp_extension
pybind11
CUDA
cuDNN
Python
```

Não assumes uma versão específica de Python sem verificar o projeto.

Não mistures pacotes `pip` Windows com Linux.

Se for necessário criar ambiente Python, utiliza:

```bash
python3 -m venv .venv
source .venv/bin/activate
```

e instala apenas as dependências realmente necessárias.

Se houver conflito entre versões de PyTorch/CUDA/Python, identifica o conflito concretamente e propõe uma combinação compatível em vez de simplesmente atualizar tudo.

### CAMINHOS

Substitui qualquer lógica deste género:

```text
C:\...
C:/...
```

por caminhos Linux.

Não uses caminhos absolutos desnecessários.

Utiliza APIs C++/POSIX apropriadas para:

* HOME;
* TEMP;
* cache;
* ficheiros temporários;
* bibliotecas;
* configuração.

### THREADS

Verifica se existe código dependente de:

* Windows threads;
* Windows synchronization;
* CriticalSection;
* SRWLock;
* Event;
* Mutex Windows;
* WaitForSingleObject;
* CreateThread.

Substitui por:

```cpp
std::thread
std::mutex
std::recursive_mutex
std::condition_variable
std::unique_lock
std::lock_guard
```

quando apropriado.

### BUILD LIMPO

Depois das alterações, elimina artefactos anteriores:

```bash
rm -rf build
```

e executa:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Não declares que o problema está resolvido apenas porque o CMake configura.

É obrigatório verificar:

1. configuração CMake;
2. compilação;
3. link;
4. execução;
5. carregamento das bibliotecas;
6. CUDA;
7. NVML;
8. deteção da GPU;
9. alocação de RAM;
10. alocação de VRAM;
11. overflow;
12. libertação de memória.

### DIAGNÓSTICO DE BIBLIOTECAS

Se o executável compilar mas não iniciar, verifica:

```bash
ldd ./build/umc_demo
```

e identifica qualquer:

```text
not found
```

Para CUDA/NVIDIA verifica também:

```bash
ldconfig -p | grep -E 'cuda|nvidia-ml'
```

e:

```bash
nvidia-smi
```

Se necessário, verifica dependências dinâmicas sem assumir que o problema está no código.

### GPU

O comportamento esperado é:

```text
GPU VRAM
    ↓
System RAM
    ↓
Overflow
```

A tier CUDA deve continuar utilizando:

```cpp
cudaMalloc
cudaMemcpy
cudaFree
```

porque a CUDA Runtime API é multiplataforma.

Não substituas CUDA por uma implementação CPU apenas para fazer o programa compilar.

Se não existir GPU NVIDIA, o programa deve falhar de forma controlada ou operar nas tiers disponíveis, conforme a arquitetura original.

### TESTES

Cria/usa testes para verificar:

* 0 GPUs;
* 1 GPU;
* múltiplas GPUs;
* RAM insuficiente;
* overflow;
* ficheiro inexistente;
* ficheiro grande;
* erro de `mmap`;
* erro de `munmap`;
* erro de CUDA;
* erro de NVML;
* CUDA Toolkit ausente;
* NVIDIA Driver ausente.

Testa especialmente operações que atravessam fronteiras dos blocos de 4 MB.

### NÃO FAZER

Não faças estas alterações:

* não substituir CUDA por CPU;
* não remover funcionalidades apenas para conseguir compilar;
* não desativar NVML;
* não ignorar erros de CUDA;
* não adicionar DLLs;
* não adicionar `.lib`;
* não instalar Wine;
* não usar Docker como solução para um problema de portabilidade;
* não recomendar Windows;
* não assumir Visual Studio;
* não atualizar todas as dependências indiscriminadamente;
* não alterar a API pública sem necessidade;
* não reescrever completamente o projeto sem primeiro diagnosticar o problema.

### PROCESSO OBRIGATÓRIO

Primeiro analisa todo o repositório.

Depois cria uma tabela interna:

```text
Componente
Dependência atual
Windows/Linux
Problema
Substituição Linux
Ficheiro
Linha
Alteração necessária
```

Depois corrige os problemas por ordem:

1. sistema operativo;
2. compilador;
3. CMake;
4. CUDA;
5. NVML;
6. APIs de memória;
7. APIs de ficheiros;
8. threads/sincronização;
9. bibliotecas;
10. Python/PyTorch, se existirem;
11. testes;
12. execução.

Não faças alterações aleatórias.

Sempre que encontrares um erro de compilação, corrige a causa real e volta a compilar.

Se uma correção provocar um novo erro, analisa a compatibilidade entre as versões em vez de mascarar o erro.

### RESULTADO FINAL

No final quero um projeto que possa ser clonado numa instalação limpa do Ubuntu 24.04 e preparado sem depender de Windows.

Deve ser possível fazer aproximadamente:

```bash
git clone <REPOSITORY>
cd <REPOSITORY>

sudo apt update
sudo apt install -y build-essential cmake pkg-config

# CUDA/NVIDIA devem estar instalados

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/umc_demo
```

Se forem necessárias dependências adicionais, documenta-as claramente no README.

Atualiza o `README.md` para refletir exclusivamente o processo Linux real.

Não afirmes que algo foi testado se não foi realmente executado.

Se o ambiente disponível não tiver GPU NVIDIA/CUDA real, distingue claramente:

```text
TESTADO REALMENTE
```

de:

```text
NÃO TESTADO — requer GPU/CUDA real
```

O resultado final deve incluir:

* código corrigido;
* CMake corrigido;
* dependências Linux;
* comandos de instalação;
* comandos de compilação;
* comandos de execução;
* diagnóstico de CUDA/NVML;
* testes realizados;
* problemas ainda existentes, se houver.

Mantém a API pública e a arquitetura do UMC sempre que possível. O README atual afirma explicitamente que a arquitetura e a API pública devem permanecer iguais, enquanto as chamadas específicas do Windows são substituídas pelos equivalentes POSIX/Linux.

A prioridade é: **compatibilidade real com Ubuntu 24.04 + NVIDIA CUDA + GNOME, sem dependências Windows e sem soluções improvisadas.**
