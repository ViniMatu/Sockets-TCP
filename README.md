# Medição de throughput TCP — Cliente/Servidor em C

Trabalho de Redes de Computadores — UNIFESP.

Este projeto modifica uma aplicação de chat TCP para medir a **capacidade efetiva máxima de transmissão de dados dos clientes para o servidor**. Os clientes enviam dados continuamente durante um intervalo definido, sem digitação manual. O servidor mede a **taxa de transmissão agregada (Mbps)** recebida de 1 a 10 clientes simultâneos.

---

## Arquivos

| Arquivo | Descrição |
|---|---|
| `server.c` | Servidor TCP multithread (uma thread por cliente). Mede os bytes recebidos dentro de uma janela de tempo e calcula o throughput agregado. |
| `client.c` | Cliente TCP que envia blocos de dados continuamente durante um tempo definido. |
| `roda_servidor.sh` | Script que executa automaticamente os cenários de 1 a 10 clientes, com repetições, no servidor. |
| `roda_clientes.sh` | Script que dispara automaticamente os clientes correspondentes a cada cenário. |
| `resultados_rede.csv` | Resultados agregados obtidos (clientes, repetição, Mbps, status). |
| `log_detalhado.txt` | Throughput individual de cada cliente em cada rodada (saída de erro do servidor). |
| `log_clientes.txt` | Total de bytes enviados por cada cliente em cada rodada. |

---

## Requisitos

Ambiente utilizado nos experimentos:

| | Servidor | Cliente |
|---|---|---|
| Processador | Intel Core i9-9900K @ 3,60 GHz (8 núcleos / 16 threads) | AMD Ryzen 5 3500U (4 núcleos / 8 threads) |
| Memória RAM | 16 GB (7,7 GiB disponíveis ao WSL2) | 5,9 GB utilizáveis (2,8 GiB disponíveis ao WSL2) |
| Sistema operacional | Windows 11 Pro 26H2 (build 26300) | Windows 10 Home 22H2 (build 19045.6466) |
| Subsistema Linux | WSL 2.6.1.0, Ubuntu 26.04.1 LTS | WSL 3.0.1.0, Ubuntu 26.04.1 LTS |
| Kernel | 6.6.87.2-microsoft-standard-WSL2 | 6.18.58.1-microsoft-standard-WSL2 |
| Compilador | GCC 15.2.0 | GCC 15.2.0 |
| Interface de rede | Ethernet 1000 Mbps (full-duplex) | Wi-Fi Realtek 8821CE, 802.11ac 1×1, 5 GHz, canal 149, taxa PHY 433,3 Mbps |
| Endereço IP | 192.168.1.101 | 192.168.1.106 |
| Rede do WSL | NAT + `netsh portproxy` | NAT |

Ambas as máquinas estavam conectadas ao mesmo roteador (rede 192.168.1.0/24): o servidor por cabo e o cliente por Wi-Fi.

Instalação do compilador (Ubuntu):

```bash
sudo apt update
sudo apt install -y build-essential
```

---

## Compilação

```bash
gcc -O2 -pthread server.c -o server
gcc -O2 client.c -o client
```

- `-O2`: nível de otimização, igual nas duas máquinas, para garantir comparabilidade.
- `-pthread`: necessário apenas no servidor (`pthread_create`, `pthread_barrier`).

---

## Execução

### Servidor

```bash
./server <porta> <N clientes> <warmup s> <duracao s> [bloco]
```

| Parâmetro | Descrição |
|---|---|
| `porta` | Porta TCP em que o servidor escuta |
| `N clientes` | Número exato de clientes (1 a 10). A medição só começa quando **todos** se conectarem. |
| `warmup s` | Tempo de aquecimento (s) em que os bytes recebidos **não são contados** |
| `duracao s` | Duração da janela de medição (s) |
| `bloco` | Tamanho do buffer de cada `recv()` em bytes (padrão: 65536) |

Exemplo (10 clientes, 2 s de warmup, 30 s de medição, blocos de 64 KB):

```bash
./server 5000 10 2 30 65536
```

### Cliente

```bash
./client <IP do servidor> <porta> <segundos> [bloco]
```

| Parâmetro | Descrição |
|---|---|
| `IP do servidor` | Endereço IPv4 do servidor |
| `porta` | Porta do servidor |
| `segundos` | Tempo de transmissão. Deve ser **maior que warmup + duração** do servidor (recomendado: +2 s de folga). |
| `bloco` | Tamanho de cada `send()` em bytes (padrão: 65536) |

Exemplo (10 clientes simultâneos):

```bash
for i in $(seq 1 10); do ./client 192.168.1.101 5000 34 65536 & done; wait
```

> O cliente envia por 34 s = 2 s de warmup + 30 s de medição + 2 s de folga. Se um cliente encerrar antes do fim da janela, o servidor o marca como **INVALIDO**.

### Saída do servidor

- Na saída de erro (`stderr`): throughput individual de cada cliente.
- Na saída padrão (`stdout`): uma linha CSV `N,Mbps` com o throughput agregado.
- Código de saída: `0` se todos os clientes permaneceram até o fim da janela, `2` se algum caiu antes (medição inválida) e `1` em caso de erro de execução.

---

## Funcionamento

1. O servidor aguarda exatamente **N conexões** (`accept()`), criando uma thread receptora para cada cliente.
2. Quando todos estão conectados, o servidor define a janela de medição (`início = agora + warmup`, `fim = início + duração`) e envia **1 byte (`"S"`)** a cada cliente, liberando a transmissão ao mesmo tempo para todos.
3. Uma **barreira** (`pthread_barrier`) faz todas as threads receptoras começarem juntas.
4. Cada thread executa `recv()` em laço e soma em um **contador próprio** (sem mutex) apenas os bytes recebidos dentro da janela de medição.
5. Ao final, o servidor soma os contadores de todas as threads e calcula a taxa agregada.

### Cálculo da taxa de transmissão agregada

$$
\text{Throughput (Mbps)} = \frac{\left(\sum_{i=1}^{N} \text{bytes}_i\right) \times 8}{\text{duração} \times 10^6}
$$

O tempo é medido com `clock_gettime(CLOCK_MONOTONIC)`, que não sofre ajustes do relógio do sistema.

---

## Experimentos automatizados (1 a 10 clientes, 5 repetições)

**No servidor** (iniciar primeiro):

```bash
chmod +x roda_servidor.sh
./roda_servidor.sh
```

**No cliente:**

```bash
chmod +x roda_clientes.sh
./roda_clientes.sh
```

- Edite a variável `SERVER` em `roda_clientes.sh` com o IP do servidor.
- Cada rodada usa 2 s de warmup + 30 s de medição. O conjunto completo (50 rodadas) leva cerca de 35–40 minutos.
- O cliente tenta reconectar automaticamente (`until ... sleep 0.5`) caso o servidor da próxima rodada ainda não esteja pronto.
- Os resultados são gravados em `resultados_rede.csv` (`clientes,repeticao,mbps,status`). Rodadas com `status` diferente de `0` devem ser descartadas.

---

## Teste local (mesma máquina)

```bash
./server 5000 1 2 5 65536          # terminal 1
./client 127.0.0.1 5000 9 65536    # terminal 2
```

> Esse teste não envolve a placa de rede (loopback). Ele mede o limite de CPU/memória da máquina, não da rede.

---

## Configuração usada entre duas máquinas (Windows + WSL2)

O ambiente de cada máquina está descrito na seção [Requisitos](#requisitos). Como o WSL2 em modo NAT não recebe conexões externas diretamente, foi necessário redirecionar a porta no Windows do servidor (PowerShell como administrador):

```powershell
# IP interno do WSL: obter com "hostname -I" dentro do WSL
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=5000 connectaddress=<IP_DO_WSL> connectport=5000

# Liberar a porta no Firewall do Windows
New-NetFirewallRule -DisplayName "TCP 5000 Trabalho Redes" -Direction Inbound -Protocol TCP -LocalPort 5000 -Action Allow
```

A rede do servidor também precisa estar com perfil **Privado** no Windows.

Para remover depois:

```powershell
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=5000
Remove-NetFirewallRule -DisplayName "TCP 5000 Trabalho Redes"
```

> **Observação:** o modo de rede *mirrored* do WSL2 foi testado no servidor. Nesse modo, o handshake TCP era concluído, mas os segmentos de dados seguintes não chegavam à aplicação (observado com `ss -tanp`: conexão `ESTAB` com `Recv-Q` zerado). Por isso foi adotada a configuração NAT + `portproxy`.

---

## Repositório

- Código-fonte: `<link do repositório GitHub>`
