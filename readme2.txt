Quero que evoluas este projeto UMC para que possa funcionar como uma **camada central de gestão de memória para aplicações de IA no Ubuntu Linux**, com capacidade de gerir e coordenar VRAM NVIDIA, RAM do sistema e armazenamento NVMe/SSD, permitindo que várias aplicações de IA utilizem modelos maiores do que a VRAM disponível.

O README atual descreve o UMC como uma MMU definida por software que combina:

* GPU VRAM;
* RAM física;
* tier de overflow em disco;

num espaço lógico de memória GPU virtual.

Quero evoluir essa arquitetura para que o UMC possa funcionar não apenas como uma biblioteca incorporada numa aplicação, mas também como um **serviço/daemon Linux central**, disponibilizando uma API para diferentes aplicações.

---

# 1. OBJETIVO

O objetivo final é chegar a uma arquitetura semelhante a:

```text
                         UBUNTU LINUX
                              │
                              ▼
                    ┌───────────────────┐
                    │    UMC DAEMON     │
                    │                   │
                    │ Global Memory     │
                    │ Manager           │
                    └─────────┬─────────┘
                              │
             ┌────────────────┼────────────────┐
             │                │                │
             ▼                ▼                ▼
        NVIDIA VRAM       SYSTEM RAM       NVMe / SSD
             │                │                │
             └────────────────┼────────────────┘
                              │
                     UMC MEMORY POOL
                              │
          ┌───────────────────┼───────────────────┐
          │                   │                   │
          ▼                   ▼                   ▼
       ComfyUI              Gradio             PyTorch
          │                   │                   │
          └───────────────────┼───────────────────┘
                              │
                    outras aplicações IA
```

O UMC deve tornar possível que várias aplicações utilizem a infraestrutura de memória disponibilizada pelo UMC.

IMPORTANTE:

Não assumes que é possível interceptar automaticamente qualquer aplicação Linux.

Se uma aplicação utiliza PyTorch, CUDA, llama.cpp, ONNX Runtime, etc., determina qual é a forma correta de integração.

O sistema deve suportar diferentes mecanismos de integração.

---

# 2. NÃO QUERO APENAS UMA BIBLIOTECA

A implementação atual funciona essencialmente como uma biblioteca/controlador de memória dentro da aplicação.

Quero acrescentar uma segunda camada:

```text
UMC Core
   │
   ├── Library API
   │
   └── UMC Daemon
          │
          ├── IPC / Unix Socket
          ├── Memory Manager
          ├── GPU Manager
          ├── RAM Manager
          ├── Storage Manager
          └── Process Manager
```

A biblioteca atual deve continuar a existir.

Não destruas a API pública atual sem necessidade.

Cria uma arquitetura em que:

```text
Aplicação
    │
    ▼
UMC Client Library
    │
    ▼
Unix Domain Socket
    │
    ▼
UMC Daemon
    │
    ├── VRAM
    ├── RAM
    └── NVMe
```

---

# 3. UMC DAEMON

Criar um daemon Linux:

```text
umcd
```

ou nome equivalente apropriado.

O daemon deve:

* iniciar no Ubuntu;
* detetar GPUs NVIDIA;
* detetar VRAM;
* detetar RAM disponível;
* detetar armazenamento disponível;
* criar e gerir o pool de memória UMC;
* aceitar pedidos de clientes;
* controlar quotas por processo;
* controlar quotas por aplicação;
* gerir prioridades;
* libertar memória quando necessário;
* movimentar dados entre tiers;
* fornecer métricas;
* permitir shutdown limpo.

Não executar o daemon como root sem necessidade.

Utilizar o mínimo de privilégios possível.

---

# 4. IPC

Implementar comunicação entre aplicações e o UMC através de:

```text
Unix Domain Socket
```

por exemplo:

```text
/run/user/<uid>/umc/umcd.sock
```

ou outra localização Linux apropriada.

Criar uma API IPC clara.

Exemplo conceptual:

```text
ALLOCATE
RELEASE
UPLOAD
DOWNLOAD
PREFETCH
EVICT
PIN
UNPIN
QUERY
STATS
REGISTER_PROCESS
UNREGISTER_PROCESS
```

Não é necessário implementar exatamente estes nomes.

Define uma API limpa e versionada.

---

# 5. CLIENT LIBRARY

Criar uma biblioteca:

```text
libumc.so
```

que permita às aplicações comunicar facilmente com o daemon.

Exemplo conceptual:

```cpp
UMCClient client;

client.connect();

auto model = client.allocate(size);

client.upload(model, data);

client.prefetch(model);

client.release(model);
```

A API real deve ser desenhada de acordo com o código existente.

A biblioteca não deve obrigar a aplicação a conhecer detalhes internos do daemon.

---

# 6. MEMORY TIERS

Manter o conceito atual:

```text
Tier 1 = GPU VRAM
Tier 2 = System RAM
Tier 3 = NVMe/SSD
```

O README atual já define precisamente esta arquitetura de três tiers.

Mas evoluir o gestor para uma política dinâmica.

Exemplo:

```text
                    MODELO 30 GB
                         │
             ┌───────────┼───────────┐
             ▼           ▼           ▼
           VRAM         RAM         NVMe
           8 GB        14 GB         8 GB
```

O sistema deve decidir dinamicamente onde colocar cada bloco.

---

# 7. NÃO TRATAR NVMe COMO VRAM

É muito importante distinguir:

```text
VRAM
RAM
NVMe
```

O NVMe não deve ser apresentado como se tivesse a mesma latência da VRAM.

Criar políticas inteligentes para:

* hot pages;
* cold pages;
* prefetch;
* eviction;
* caching;
* read-ahead;
* write-back.

Os dados utilizados intensivamente devem permanecer preferencialmente na VRAM.

Dados menos utilizados podem permanecer em RAM.

Dados frios podem permanecer em NVMe.

---

# 8. MODELOS DE IA

O sistema deve ser pensado especificamente para modelos de IA.

Exemplos:

```text
LLM
Diffusion
Stable Diffusion
Flux
LLama
Transformers
ComfyUI models
PyTorch models
ONNX models
Tensor models
Safetensors
GGUF
```

Não assumes que todos estes formatos funcionam diretamente com o UMC.

Determina onde é necessário um adaptador.

---

# 9. PYTORCH

Criar uma integração correta com PyTorch.

Investigar a possibilidade de:

```text
PyTorch
   ↓
UMC allocator / extension
   ↓
UMC daemon
```

ou outra arquitetura tecnicamente adequada.

O objetivo é permitir que modelos PyTorch possam utilizar a memória gerida pelo UMC.

Não modificar PyTorch internamente de forma frágil.

Preferir:

* C++ extension;
* custom allocator;
* CUDA integration;
* Python bindings;
* mecanismos oficiais de extensão.

O README já identifica `pybind11` como uma possibilidade de integração com PyTorch/ComfyUI.

---

# 10. COMFYUI

Criar uma integração opcional para ComfyUI.

Idealmente:

```text
ComfyUI
   │
   ▼
UMC Python Extension
   │
   ▼
libumc.so
   │
   ▼
umcd
```

O objetivo é permitir que modelos grandes possam utilizar:

```text
VRAM + RAM + NVMe
```

sem modificar profundamente o ComfyUI.

Investiga a arquitetura real do ComfyUI antes de implementar.

Não assumes que simplesmente substituir `torch.cuda` resolverá o problema.

---

# 11. GRADIO

Gradio normalmente funciona como interface/web UI e não é necessariamente o componente que possui diretamente o modelo.

Por isso:

```text
Gradio
   │
   ▼
Python application
   │
   ▼
PyTorch / Transformers / CUDA
   │
   ▼
UMC
```

O UMC deve integrar-se no nível correto.

Não criar uma integração artificial específica para a interface Gradio se o verdadeiro consumidor de memória for PyTorch/CUDA.

---

# 12. OUTRAS APLICAÇÕES

Criar uma arquitetura extensível.

No futuro devem poder existir adaptadores para:

```text
PyTorch
ComfyUI
Transformers
llama.cpp
ONNX Runtime
TensorRT
vLLM
KoboldCpp
outras aplicações CUDA
```

Não implementar todos imediatamente.

Criar primeiro uma arquitetura de plugins/adapters.

Exemplo:

```text
plugins/
    pytorch/
    comfyui/
    llama_cpp/
    onnx/
```

---

# 13. DETEÇÃO DE PROCESSOS

O daemon deve saber quais processos estão ligados ao UMC.

Exemplo:

```text
PID
Process name
UID
GPU
VRAM usage
UMC memory
RAM usage
NVMe usage
Priority
```

Criar uma estrutura interna semelhante a:

```text
ProcessManager
ApplicationManager
MemoryManager
GpuManager
StorageManager
PolicyManager
```

---

# 14. MULTI-APPLICATION

O UMC deve suportar simultaneamente:

```text
ComfyUI
+
LLM
+
Gradio application
+
PyTorch application
```

Exemplo:

```text
VRAM = 12 GB

ComfyUI       → 7 GB
LLM           → 4 GB
outra app     → 1 GB
```

Se uma aplicação precisar de mais memória:

```text
ComfyUI pede +4 GB
```

o UMC deve avaliar:

```text
VRAM
   ↓
há espaço?
   │
   ├── sim → alocar
   │
   └── não
        ↓
     procurar páginas/blocos frios
        ↓
     VRAM → RAM
        ↓
     se necessário
        ↓
     RAM → NVMe
```

---

# 15. PRIORIDADES

Criar prioridades por aplicação.

Exemplo:

```text
HIGH
NORMAL
LOW
BACKGROUND
```

Mas não implementar prioridades arbitrárias sem necessidade.

Criar uma política configurável.

Exemplo:

```yaml
applications:
  comfyui:
    priority: high

  ollama:
    priority: normal

  background:
    priority: low
```

---

# 16. PREFETCH

Para modelos IA isto é extremamente importante.

Implementar prefetch.

Exemplo:

```text
GPU está a processar camada N

CPU/RAM/NVMe
       │
       └── prefetch camada N+1
                 │
                 ▼
               VRAM
```

O objetivo é reduzir:

```text
VRAM ↔ RAM
RAM ↔ NVMe
```

durante a inferência.

Se for possível utilizar streams CUDA e operações assíncronas, investigar essa possibilidade.

Não introduzir sincronizações bloqueantes desnecessárias.

---

# 17. DOUBLE BUFFERING

Investigar uma arquitetura:

```text
GPU
 │
 ├── buffer A → computação
 │
 └── buffer B → prefetch
```

enquanto:

```text
RAM/NVMe
     │
     ▼
prefetch
     │
     ▼
VRAM
```

O README já sugere double buffering e prefetch para integração com pipelines de IA.

Transforma isto numa implementação real se a arquitetura permitir.

---

# 18. DETEÇÃO AUTOMÁTICA DE GPU

O daemon deve detetar:

```text
GPU model
VRAM total
VRAM available
CUDA version
driver version
GPU count
```

Utilizar CUDA/NVML.

O README indica que NVML Linux deve utilizar:

```text
libnvidia-ml.so.1
```

e não `nvml.dll`.

---

# 19. MULTI-GPU

Preparar a arquitetura para:

```text
GPU 0
GPU 1
GPU 2
...
```

O UMC deve conhecer a memória disponível em cada GPU.

No futuro deverá poder decidir:

```text
Model A → GPU 0
Model B → GPU 1
```

ou distribuir determinados dados entre GPUs.

Não implementar sharding complexo se não for necessário para a primeira versão.

Mas não bloquear a arquitetura futura.

---

# 20. MONITORIZAÇÃO

Criar CLI:

```bash
umc status
```

Exemplo:

```text
UMC STATUS

GPU 0
VRAM:       10.2 GB / 12 GB

SYSTEM RAM
Used:       21.4 GB / 64 GB

NVMe CACHE
Used:       18.7 GB / 500 GB

APPLICATIONS

PID      APP          VRAM    RAM     UMC
1234     ComfyUI      7.1G    4.3G    11.4G
5678     LLM          3.0G    6.2G     9.2G
```

Criar também:

```bash
umc stats
umc processes
umc gpu
umc memory
umc config
```

se fizer sentido.

---

# 21. MONITOR GUI

Não é necessário criar uma GUI inicialmente.

Mas a arquitetura deve permitir posteriormente:

```text
UMC daemon
     │
     ▼
REST API / WebSocket
     │
     ▼
Web UI
```

para visualizar:

```text
VRAM
RAM
NVMe
processos
modelos
transfers
prefetch
evictions
```

---

# 22. SYSTEMD

Criar serviço:

```text
umcd.service
```

para Ubuntu.

Permitir:

```bash
systemctl --user enable umcd
systemctl --user start umcd
```

se a arquitetura permitir.

Não executar como root por defeito.

---

# 23. SEGURANÇA

O daemon deve validar:

* UID;
* PID;
* permissões;
* tamanho das alocações;
* pedidos IPC;
* acesso aos buffers;
* limites de armazenamento.

Uma aplicação não deve conseguir aceder à memória pertencente a outra aplicação simplesmente porque conhece um ID.

---

# 24. CRASH RECOVERY

Se uma aplicação morrer:

```text
SIGKILL
crash
segmentation fault
```

o UMC deve conseguir recuperar os recursos dessa aplicação.

O daemon não pode ficar com:

```text
VRAM perdida
RAM bloqueada
NVMe temporário abandonado
handles inválidos
```

Criar mecanismo de:

```text
client disconnect
heartbeat
cleanup
```

---

# 25. CONFIGURAÇÃO

Criar configuração:

```text
/etc/umc/
```

ou configuração por utilizador quando apropriado.

Exemplo conceptual:

```yaml
memory:
  vram_reserve: 512MB
  ram_limit: auto
  nvme_limit: 200GB

policy:
  prefetch: true
  compression: false

applications:
  comfyui:
    priority: high
```

Não copiar este YAML cegamente.

Define uma configuração adequada ao projeto.

---

# 26. COMPRESSÃO

Investigar, mas NÃO implementar automaticamente.

Uma futura camada poderá permitir:

```text
RAM
 ↓
compression
 ↓
NVMe
```

ou:

```text
VRAM → compressed RAM
```

Mas primeiro implementar corretamente o sistema sem compressão.

---

# 27. CUDA MEMORY

Não substituir:

```cpp
cudaMalloc
cudaMemcpy
cudaFree
```

simplesmente porque queremos UMC.

Determina onde o UMC deve intervir.

O objetivo é:

```text
UMC
 │
 ├── gestão lógica
 ├── localização dos blocos
 ├── prefetch
 ├── eviction
 └── transferência
       │
       ▼
CUDA
```

Preservar a utilização da CUDA para VRAM.

---

# 28. PERFORMANCE

Não sacrificar drasticamente desempenho apenas para conseguir abstração.

Medir:

```text
VRAM → VRAM
VRAM → RAM
RAM → VRAM
RAM → NVMe
NVMe → RAM
NVMe → VRAM
```

Criar benchmarks.

Comparar:

```text
UMC
vs
memória convencional
```

quando possível.

Medir:

* throughput;
* latency;
* CPU usage;
* GPU utilization;
* PCIe transfer;
* NVMe throughput.

---

# 29. COMPATIBILIDADE

O alvo inicial deve ser:

```text
Ubuntu 24.04 LTS
64-bit
GNOME
GCC 13+
C++23
CMake 3.20+
NVIDIA Driver
CUDA 12.x / 13.x
```

O README atual já estabelece Ubuntu 24.04, C++23, CUDA 12.x/13.x e CMake 3.20+ como ambiente de referência.

Não assumir que outras distribuições funcionam sem testes.

---

# 30. NÃO ALTERAR O KERNEL

Não criar inicialmente:

* kernel module;
* driver NVIDIA;
* patch ao kernel;
* substituição do virtual memory manager Linux;
* filesystem especial.

O primeiro objetivo deve ser uma solução:

```text
userspace
+
daemon
+
shared library
+
IPC
+
CUDA/NVML
+
mmap
```

Se futuramente for necessário um componente kernel, documentar isso separadamente.

---

# 31. NÃO PROMETER "MEMÓRIA GLOBAL" MÁGICA

É fundamental não afirmar que o UMC consegue automaticamente controlar qualquer memória de qualquer programa Linux.

Existem três níveis:

### Nível 1

Aplicação integrada diretamente:

```text
Application
   ↓
libumc
   ↓
umcd
```

### Nível 2

Aplicação suportada através de adapter:

```text
ComfyUI
   ↓
UMC adapter
   ↓
libumc
   ↓
umcd
```

### Nível 3

Aplicação sem integração:

```text
Application
   ↓
Linux/CUDA normal
```

Neste último caso o UMC não deve fingir que controla automaticamente a memória da aplicação.

Documenta claramente esta diferença.

---

# 32. PRIMEIRA VERSÃO FUNCIONAL

Não tentes implementar tudo de uma vez.

A primeira versão deve conseguir:

```text
Ubuntu
   ↓
umcd
   ↓
detect NVIDIA GPU
   ↓
detect VRAM
   ↓
detect RAM
   ↓
create NVMe backing store
   ↓
accept IPC client
   ↓
allocate memory
   ↓
move blocks
   ↓
release memory
```

Depois:

```text
libumc
```

Depois:

```text
PyTorch adapter
```

Depois:

```text
ComfyUI adapter
```

Depois outras aplicações.

---

# 33. TESTE REAL

O projeto só deve ser considerado funcional depois de testar:

```text
1 GPU
2 GPUs
RAM limitada
NVMe overflow
modelo maior que VRAM
múltiplos clientes
processo termina inesperadamente
daemon reinicia
CUDA error
NVML error
NVMe cheio
RAM insuficiente
```

Criar testes automatizados sempre que possível.

---

# 34. README

Reescrever o README para explicar claramente:

```text
UMC Core
UMC Daemon
UMC Client
Memory tiers
IPC
CUDA
NVML
PyTorch integration
ComfyUI integration
Installation
Configuration
CLI
Troubleshooting
Performance
Limitations
```

Não afirmar que o UMC controla todas as aplicações Linux.

Explicar exatamente quais aplicações possuem integração.

---

# 35. PRINCÍPIO FUNDAMENTAL

Não quero uma simples adaptação cosmética.

Quero transformar o projeto de:

```text
Virtual GPU Memory Library
```

para uma arquitetura:

```text
Linux AI Memory Management Platform
```

mantendo o núcleo UMC existente sempre que possível.

A arquitetura final pretendida é:

```text
                         UBUNTU
                           │
                           ▼
                    ┌─────────────┐
                    │    UMCD     │
                    │             │
                    │ Memory Core │
                    └──────┬──────┘
                           │
          ┌────────────────┼────────────────┐
          │                │                │
          ▼                ▼                ▼
        VRAM              RAM              NVMe
          │                │                │
          └────────────────┼────────────────┘
                           │
                    UMC Memory Pool
                           │
             ┌─────────────┼─────────────┐
             │             │             │
             ▼             ▼             ▼
          PyTorch       ComfyUI       outros
             │             │             │
             └─────────────┼─────────────┘
                           │
                       libumc.so
                           │
                       UMC IPC
```

O mais importante é que **não inventes uma solução apenas para satisfazer o pedido**.

Antes de modificar o código:

1. analisa todo o repositório;
2. identifica a arquitetura atual;
3. identifica o que já existe;
4. identifica o que falta;
5. verifica compatibilidade das dependências;
6. implementa por etapas;
7. compila depois de cada alteração importante;
8. executa testes reais;
9. corrige os erros encontrados;
10. só declara uma funcionalidade como concluída quando tiver sido realmente testada.

Se uma funcionalidade não puder ser implementada de forma segura em userspace Linux, indica exatamente a limitação e propõe a arquitetura tecnicamente necessária, sem mascarar o problema.

Prioridade absoluta:

**funcionar realmente no Ubuntu Linux com NVIDIA CUDA, gerir VRAM + RAM + NVMe de forma eficiente e disponibilizar essa infraestrutura a várias aplicações de IA através de uma API/daemon comum.**
