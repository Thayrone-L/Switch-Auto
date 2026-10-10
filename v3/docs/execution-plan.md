# Switch Auto V3 — Plano de execução

Este documento transforma a arquitetura em tarefas integráveis. Cada tarefa tem
owner único, dependências, saída e gate. Um agente não inicia trabalho cujas
dependências ainda não foram integradas.

## 0. Preparação do repositório

### `PREP-01` — Preservar V2

- **Dependências:** nenhuma.
- **Ações:** revisar mudanças não commitadas; registrar o experimento final;
  criar commit e tag da V2.
- **Saída:** ponto reproduzível contendo NRO, APK e documentação usada no último
  teste.
- **Gate:** checkout limpo ou mudanças intencionalmente documentadas.

### `PREP-02` — Criar linha V3

- **Dependências:** `PREP-01`.
- **Ações:** criar branch V3; manter `v1/` e `v2/` imutáveis; configurar ignores
  para builds Gradle/CMake.
- **Saída:** branch e diretórios V3.
- **Gate:** build da V2 continua reproduzível e nenhum artefato gerado é
  rastreado por engano.

### `PREP-03` — Congelar baseline

- **Dependências:** `PREP-01`.
- **Ações:** registrar modelo do telefone, versão do Android Auto, firmware do
  Switch, rede, DHU e resultados conhecidos.
- **Saída:** `v3/docs/baseline.md` com roteiro repetível.
- **Gate:** outra pessoa consegue repetir o teste do DHU sem contexto oral.

### `PREP-04` — Licença e procedência

- **Dependências:** `PREP-01`.
- **Ações:** definir licença pretendida; registrar quais referências são apenas
  comportamentais; definir política clean-room para GPL; atribuir procedência a
  schemas, payloads e fixtures.
- **Saída:** ADR de licença/procedência.
- **Gate:** nenhum código ou fixture de origem ambígua entra na V3.

## M0 — Fundação e contratos

Após `PREP-02`, somente `M0-01`, `M0-02`, o scaffolding de `M0-04` e o esqueleto
isolado de `M1-05` podem avançar em paralelo. Trabalho que consome interfaces
compartilhadas aguarda `M0-02` e `M0-03` integrados.

### `M0-01` — Build e targets

- **Owner:** build/platform.
- **Escopo:** CMake da V3, target desktop de testes e target Switch mínimo.
- **Saída:** executável de teste no desktop e NRO que abre/fecha limpo.
- **Gate:** CI/local executa build sem importar fontes monolíticas da V1/V2.

### `M0-02` — Tipos e modelo de erro

- **Owner:** core contracts.
- **Escopo:** byte spans, `Result`, erros de transporte/protocolo/sessão e
  cancelamento.
- **Saída:** headers sem dependências de plataforma.
- **Gate:** testes unitários e nenhuma exceção no hot path.

### `M0-03` — Contratos de runtime

- **Owner:** core contracts.
- **Dependências:** `M0-02`.
- **Escopo:** `ITransport`, `ITlsEngine`, `IVideoDecoder`, `IVideoPresenter`,
  `IInputSource`, `IMetricsSink`.
- **Gate:** interfaces compilam com fakes desktop e adapters Switch vazios.

### `M0-04` — Harness e fixtures

- **Owner:** verification.
- **Dependências:** `M0-02`.
- **Diretórios:** `v3/tests/support/`, `v3/tests/fixtures/`,
  `v3/tests/faults/`.
- **Escopo:** runner de testes, fixtures de frames e H.264, relógio falso,
  transporte duplex falso e fault injection.
- **Gate:** testes simulam fragmentação, leitura parcial, timeout e fechamento.

### Gate M0

- build desktop e Switch verde;
- NRO abre e fecha sem panic;
- contratos aprovados;
- nenhum módulo V3 depende de Borealis, MapLibre ou Companion V1.

## M1 — Framing, TLS e sessão mínima

### `M1-01` — Frame codec

- **Owner:** protocol.
- **Dependências:** `PREP-04`, `M0-02`, `M0-04`.
- **Escopo:** encode/decode, leitura parcial, múltiplos frames por leitura,
  limites e fragmentação.
- **Gate:** fixtures válidas passam; headers, tamanhos e flags inválidos falham
  deterministicamente.

### `M1-02` — TLS engine adapter

- **Owner:** Switch platform.
- **Dependências:** `M0-03`.
- **Escopo:** encapsular a API SSL/libnx atrás de `ITlsEngine`; garantir que um
  único owner faça read/write.
- **Gate:** handshake controlado e shutdown cancelável, sem polling indefinido.

### `M1-03` — Outbound scheduler

- **Owner:** protocol.
- **Dependências:** `M0-03`, `M0-04`.
- **Escopo:** prioridades, wakeup, mensagens completas e coalescência de Move.
- **Gate:** testes provam ordem, ausência de starvation e preservação de Down/Up.

### `M1-04` — Session state machine

- **Owner:** session.
- **Dependências:** `M1-01`, `M1-03`.
- **Escopo:** estados, eventos, timeouts, cancelamento e reconnect policy.
- **Gate:** tabela de transições coberta por testes.

### `M1-05` — Android relay mínimo

- **Owner:** Android.
- **Dependências:** `PREP-02`.
- **Escopo:** foreground service, duas cópias TCP, health e contadores de bytes.
- **Gate:** teste byte-for-byte confirma conteúdo e ordem em fluxo duplex, com
  short reads/writes, backpressure bilateral, half-close, cancelamento bloqueado,
  reconnect e carga sustentada; métricas/log não executam no caminho de cópia.

### `M1-06` — Service discovery e canais mínimos

- **Owner:** Protocol/Session.
- **Dependências:** `M1-02`, `M1-04`.
- **Escopo:** negociação, discovery, abertura, sensores iniciais e compatibilidade
  necessária para canais de mídia.
- **Gate:** telefone mostra “conectado ao veículo” e mantém sessão sem vídeo por
  10 minutos.

### `M1-07` — Caracterizar ACK de mídia

- **Owner:** Verification, revisão por Protocol/Session.
- **Dependências:** `PREP-04`, `M1-01`, `M1-06`.
- **Diretórios:** `v3/tests/fixtures/`, `v3/tests/replay/`.
- **Escopo:** produzir captura/fixture sanitizada e mapear framing AA, mensagens
  de mídia, NAL/access units e ACKs esperados.
- **Gate:** teste de contrato demonstra qual unidade é reconhecida e em qual
  momento; ADR 0004 é aceito ou corrigido. Sem esse gate, integração de vídeo
  real permanece bloqueada.

### Gate M1

- conexão repetível;
- shutdown do NRO em menos de 2 segundos;
- nenhum log em disco no Protocol I/O thread;
- três ciclos conectar/desconectar sem panic ou processo órfão.

## M2 — Vídeo 720p30

### `M2-01` — Video message parser

- **Owner:** video core.
- **Dependências:** `M1-01`, `M0-04`, `M1-07`.
- **Escopo:** configuração de codec, mídia com PTS e montagem de access units.
- **Gate:** fixtures geram exatamente os access units esperados.

### `M2-02` — Fila de access units

- **Owner:** video core.
- **Dependências:** `M2-01`.
- **Escopo:** fila limitada, métricas e recovery explícito.
- **Gate:** overflow não cresce memória e nunca entrega access unit parcial.

### `M2-03` — NVTEGRA decoder adapter

- **Owner:** Switch video.
- **Dependências:** `M0-03`, `M2-02`.
- **Escopo:** portar somente o decoder comprovado da V2.
- **Gate:** sample H.264 decodifica sem fallback por CPU e libera todos os
  recursos no shutdown.

### `M2-04` — Presenter NV12

- **Owner:** Switch render.
- **Dependências:** `M0-03`.
- **Escopo:** Deko3D, superfície mais recente e lifecycle do swapchain.
- **Gate:** sample apresenta continuamente e fecha sem panic.

### `M2-05` — Video sink integrado

- **Owner:** Protocol/Session.
- **Dependências:** `M1-07`, `M2-01`, `M2-02`.
- **Escopo:** rotear mídia validada ao contrato `IVideoSink` e emitir ACK no
  limite comprovado por `M1-07`.
- **Gate:** ACK independe de decode/render e não cria deadlock/backpressure.

### `M2-06` — Compor pipeline de vídeo

- **Owner:** Integrador, limitado à composição.
- **Dependências:** `M2-03`, `M2-04`, `M2-05`.
- **Escopo:** conectar interfaces já aprovadas sem alterar implementações dos
  workstreams.
- **Gate:** primeiro quadro, vídeo contínuo e nenhum acesso cruzado entre
  Protocol I/O, decoder e presenter.

### Gate M2

- 30 minutos de projeção sem queda;
- fila codificada sem crescimento contínuo;
- nenhum fallback por CPU;
- sem corrupção persistente;
- encerramento limpo em todos os testes.

## M3 — Input de baixa latência

### `M3-01` — Serialização e coordenadas

- **Owner:** input core.
- **Dependências:** `M0-04`, `M1-01`.
- **Gate:** golden tests de Down, Move, Up e Cancel.

### `M3-02` — Gesture queue

- **Owner:** input core.
- **Dependências:** `M1-03`, `M3-01`.
- **Gate:** Move é coalescido; limites nunca deixam ponteiro pressionado.

### `M3-03` — Touch Switch

- **Owner:** Switch input.
- **Dependências:** `M0-03`, `M3-02`.
- **Gate:** um toque, arrasto longo e sequência realista funcionam sem queda.

### Gate M3

- input enqueue → socket write p95 menor que 10 ms;
- nenhuma dependência do ritmo de vídeo;
- 15 minutos de interação com Waze/Maps e Spotify sem reinício.

## M4 — Diagnóstico e recuperação

### `M4-01` — Métricas em memória

- **Owner:** observability.
- **Dependências:** M1 e M2.
- **Escopo:** timestamps por estágio, contadores, percentis e snapshots.
- **Gate:** habilitar/desabilitar diagnóstico não muda a taxa de frames de forma
  mensurável.

### `M4-02` — Relatório assíncrono

- **Owner:** observability.
- **Dependências:** `M4-01`.
- **Gate:** gravação em SD ocorre fora do hot path e suporta arquivo indisponível.

### `M4-03` — Recovery de vídeo

- **Owner:** video core/session.
- **Dependências:** `M2-05`, `M4-01`.
- **Gate:** backlog/corrupção reinicia o pipeline necessário sem panic; reinício
  total da sessão só ocorre quando o protocolo exige.

### `M4-04` — Recovery de conexão

- **Owner:** session/Android.
- **Dependências:** `M1-04`, `M1-05`.
- **Gate:** testes de Wi-Fi, relay e Head Unit Server recuperam automaticamente.

## M5 — Qualificação 720p30

Executar integralmente o [plano de testes](test-plan.md). Nenhuma funcionalidade
nova entra durante a qualificação; apenas correções acompanhadas de regressão.

Gate final:

- todos os cenários críticos aprovados;
- zero panic do Atmosphère;
- zero restart inesperado do Companion/Gearhead;
- métricas dentro do orçamento;
- problemas conhecidos documentados e classificados.

## M6 — 720p60 experimental

Só pode iniciar após Gate M5. Mudanças de FPS, buffers ou janelas de ACK devem
seguir o formato hipótese → medição → resultado → decisão. O caminho 30 fps
permanece disponível para comparação e rollback.

## Pós-qualificação

Ordem recomendada:

1. shell Borealis mínimo;
2. status de conexão e diagnóstico;
3. overlays não interativos;
4. widgets oficiais;
5. temas/layouts pelo Companion;
6. recursos futuros.
