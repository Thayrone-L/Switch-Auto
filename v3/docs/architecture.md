# Switch Auto V3 — Arquitetura normativa

## 1. Princípios

1. Android Auto projection é o produto principal da V3.
2. Cada recurso mutável possui um único owner.
3. Transporte, protocolo, decode e apresentação não bloqueiam uns aos outros.
4. Filas são limitadas, mensuráveis e têm política explícita.
5. O hot path não executa I/O em disco nem formatação pesada de logs.
6. O Companion não interpreta Android Auto.
7. Código da V1/V2 é referência, não dependência implícita.
8. A arquitetura deve ser verificável no desktop antes do teste físico sempre
   que a plataforma não for essencial.

## 2. Visão de runtime

```text
Android/Gearhead
      │ localhost:5277
      ▼
Android Relay ─────────── pipe TCP transparente
      │ Wi-Fi
      ▼
ITransport
      │ bytes
      ▼
ProtocolRuntime (owner único)
  ├── FrameCodec
  ├── TlsEngine
  ├── SessionStateMachine
  ├── OutboundScheduler
  └── ChannelRouter
        ├── ControlChannel
        ├── SensorChannel
        ├── InputChannel
        ├── VideoSink
        └── CompatibilityMediaSink
                    │ access units
                    ▼
              IVideoDecoder
                    │ surfaces NV12
                    ▼
              SurfaceMailbox
                    │ latest complete
                    ▼
              IVideoPresenter
```

## 3. Módulos e dependências

### `transport`

Responsável por conexão, leitura/escrita de bytes, timeout, cancelamento e
detecção de fechamento. Não conhece TLS, canais ou vídeo.

Contrato inicial:

```cpp
class ITransport {
public:
    virtual Result<ReadResult, TransportError> read(
        ByteSpan target, Deadline deadline, CancellationToken cancel) = 0;
    virtual Result<void, TransportError> writeAll(
        ConstByteSpan bytes, Deadline deadline,
        CancellationToken cancel) = 0;
    virtual Result<void, TransportError> shutdown(ShutdownMode mode) = 0;
};
```

`M0-03` deve congelar a semântica de EOF, short read/write, half-close,
deadline, cancelamento, thread affinity e wakeup do Protocol I/O. Até esse gate,
esta assinatura é ilustrativa e não um header definitivo.

### `protocol`

Contém framing Android Auto, ids de mensagens, codificação mínima de protobuf e
adaptação TLS. Não conhece NVTEGRA, Deko3D, Borealis ou Android/Kotlin.

Responsabilidades:

- validar headers e limites;
- remontar mensagens fragmentadas;
- encapsular/desencapsular TLS;
- garantir ordem global de escrita;
- rotear mensagens completas por canal;
- nunca bloquear esperando decode ou renderização.

### `session`

Máquina de estados explícita:

```text
Stopped
  → Connecting
  → VersionNegotiation
  → TlsHandshake
  → ServiceDiscovery
  → OpeningChannels
  → Projecting
  → Recovering
  → Stopping
  → Stopped
```

Cada transição declara evento de entrada, timeout, saída e erro. Reconexão é
política da sessão; canais não iniciam reconexão diretamente.

### `input`

Normaliza coordenadas e preserva a sequência de gesto.

- `Down` nunca é descartado;
- `Up` e `Cancel` nunca são descartados;
- apenas `Move` consecutivo do mesmo ponteiro pode ser substituído;
- input entra no scheduler de saída, sem escrever diretamente no socket;
- timestamps são convertidos somente na borda do protocolo.

### `video`

`VideoSink` recebe mensagens AA completas e produz access units H.264 completos.
Ele não decodifica e não apresenta.

Pipeline:

```text
AA media message
  → parse session/PTS
  → characterize/emit ACK at the protocol-defined boundary
  → assemble complete access unit
  → accept into bounded queue
  → decode worker
  → completed NV12 surface
  → latest-surface mailbox
  → presenter
```

Configuração de codec e dados temporizados são tratados separadamente. Nunca se
descarta parte de um access unit. Em backlog sustentado, a política de recovery
do vídeo entra em ação; a fila não cresce para esconder o problema.

### `diagnostics`

O hot path publica números em contadores atômicos ou ring buffer prealocado. Um
consumidor de baixa prioridade cria snapshots e, opcionalmente, grava no SD.

Diagnóstico não pode modificar timing, pacing ou ordem de protocolo.
A qualificação mede também latência toque-fóton; medir apenas o tempo até a
escrita no socket não representa a experiência percebida pelo usuário.

## 4. Modelo de concorrência

### Protocol I/O thread

Único owner de:

- socket de transporte;
- conexão TLS;
- codec de frames;
- máquina de sessão;
- sequência de escrita.

Ela alterna leitura e drenagem da fila de saída. Um wakeup explícito notifica a
chegada de input/controle, evitando esperar o próximo frame de vídeo.

### Decode thread

Único owner do decoder NVTEGRA e de seus buffers. Recebe access units por fila
limitada. Nunca escreve no protocolo.

### Render/UI thread

Único owner de Deko3D e do swapchain mínimo. Consome somente superfícies
concluídas. Nunca espera rede ou decode.

Borealis só poderá envolver esse presenter depois do Gate M5.

### Android relay

Duas cópias direcionais independentes:

```text
read(Gearhead) → write(Switch)
read(Switch)   → write(Gearhead)
```

Não há parsing, pacing, reordenação ou remontagem.

## 5. Scheduler de saída

Há um único scheduler, drenado pelo Protocol I/O thread.

Prioridades:

1. encerramento, ping e respostas de sessão;
2. ACKs de mídia e mensagens de controle obrigatórias;
3. input `Down`, `Up` e `Cancel`;
4. input `Move` coalescível;
5. sensores periódicos e mensagens não urgentes.

Prioridade não permite reordenar bytes já entregues ao TLS. Cada mensagem é
criptografada e enviada integralmente antes da próxima.

## 6. Filas

Toda fila declara:

- unidade armazenada;
- capacidade;
- owner produtor/consumidor;
- comportamento em overflow;
- métrica de profundidade e high-water mark.

| Fila | Unidade | Política |
| --- | --- | --- |
| Saída de protocolo | mensagem completa | nunca perder controle/ACK; coalescer `Move` |
| Vídeo codificado | access unit completo | limitada; recovery explícito em backlog |
| Superfícies | superfície concluída | preservar a mais recente apresentável |
| Diagnóstico | evento compacto | descartar evento de baixa severidade em overflow |

Valores numéricos iniciais são definidos por teste, não por esta arquitetura.

## 7. Tratamento de canais não usados

O Switch não reproduzirá áudio nem capturará microfone na primeira entrega.
Porém, canais exigidos pelo telefone podem precisar ser anunciados, abertos e
reconhecidos para manter a sessão saudável.

Essa compatibilidade fica isolada em `CompatibilityMediaSink`:

- descarta payload após validação mínima;
- envia somente respostas exigidas;
- não cria pipeline PCM;
- não altera o roteamento de áudio do telefone além do indispensável;
- possui teste e documentação próprios.

## 8. Fronteiras de plataforma

Código portável:

- framing;
- protobuf mínimo;
- máquina de sessão;
- scheduler;
- regras de filas;
- serialização de input;
- montagem de access units;
- métricas em memória.

Código Switch:

- socket/libnx;
- TLS/libnx;
- touch;
- NVTEGRA/FFmpeg;
- Deko3D;
- SD e lifecycle do NRO.

Código Android:

- foreground service;
- conexão ao servidor Head Unit;
- relay;
- permissões e estado visível.

## 9. Regras de dependência

```text
apps → platform adapters → core contracts
platform adapters → platform SDK
core modules → core contracts only
```

Proibido:

- `protocol` incluir headers de NVTEGRA/Deko3D;
- `video` acessar socket/TLS;
- renderer enviar ACK;
- Companion interpretar flags ou ids AA;
- UI escrever diretamente no transporte;
- diagnóstico controlar pacing;
- módulo V3 depender de widget, MapLibre ou protocolo V1.

## 10. Referências externas

AASDK/OpenAuto podem ser usados para comparar comportamento e schemas. Código
GPL não será incorporado sem decisão explícita de licença. O DHU oficial é a
baseline funcional, não uma dependência de runtime.
