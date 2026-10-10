# Switch Auto V3 — Testes e critérios de aceite

## 1. Baseline

O DHU oficial é o controle funcional. Toda comparação relevante usa:

- mesmo telefone;
- mesma versão do Android Auto;
- mesma rede;
- mesmos aplicativos e ações;
- janela de teste equivalente.

O baseline deve registrar data, versões, resolução, FPS configurado e condições
da rede. As condições incluem banda Wi-Fi, RSSI, latência e perda entre os
dispositivos; sem isso, resultados de DHU e Switch não são comparáveis.

## 2. Vocabulário de métricas

Não misturar:

- bytes TCP;
- frames externos AA;
- mensagens remontadas;
- access units H.264;
- outputs do decoder;
- superfícies publicadas;
- frames apresentados.

Cada contador possui nome e unidade explícitos.

## 3. Testes unitários desktop

### Transporte/framing

- um byte por leitura;
- header fragmentado;
- payload fragmentado;
- múltiplos frames em uma leitura;
- short read/write;
- EOF no meio do frame;
- tamanho zero, máximo e acima do limite;
- flags e tipos inválidos;
- cancelamento idempotente.

### Relay bidirecional

- tráfego simultâneo nos dois sentidos;
- short reads e short writes;
- backpressure sem crescimento ilimitado;
- half-close de cada lado;
- cancelamento durante leitura e escrita;
- reconnect após EOF e timeout;
- carga sustentada sem starvation;
- coleta de métricas fora do hot path.

### Sessão/scheduler

- todas as transições válidas;
- transições inválidas;
- timeout por estado;
- prioridade de keepalive, ACK e input;
- ausência de starvation;
- reconnect e stop concorrentes;
- shutdown repetido.

### Input

- golden bytes de Down, Move, Up e Cancel;
- limites e rotação de coordenadas;
- coalescência somente de Move consecutivo;
- overflow termina gesto com Cancel;
- Down/Up nunca ficam órfãos.

### Vídeo

- codec config separado de mídia;
- PTS válido/inválido;
- access unit fragmentado;
- múltiplas NAL units;
- fila cheia;
- decoder lento;
- surface mailbox entrega realmente a mais recente;
- reset preserva ordem de destruição.

## 4. Fakes obrigatórios

- `FakeTransport` com falha após N bytes;
- `FakeTlsEngine` com ciphertext atrasado e parcial;
- `FakeClock` para timeouts determinísticos;
- `FakeDecoder` lento, travado ou com erro;
- `FakePresenter` sem superfície ou com vblank atrasado;
- `FakeInputSource` com rajadas e gestos incompletos;
- `InMemoryMetricsSink`.

## 5. Replay

Fixtures devem ser pequenas, anonimizadas e determinísticas. Não versionar
chaves privadas adicionais, tokens, destinos, nomes, IPs pessoais ou logs brutos.

Conjuntos mínimos:

- negociação/version response;
- handshake encapsulado sanitizado quando aplicável;
- service discovery;
- abertura dos canais;
- configuração H.264;
- vídeo com PTS;
- sequência completa de toque;
- encerramento e reconnect;
- H.264 conhecido para NVTEGRA.

## 6. Testes físicos

### Smoke — 60 segundos

- conectar;
- mostrar vídeo;
- um toque;
- um arrasto;
- fechar NRO.

Falha em smoke bloqueia qualquer teste maior.

### Interação — 5 minutos

- abrir mapa;
- digitar destino;
- arrastar mapa;
- alternar para Spotify;
- controlar mídia;
- voltar ao mapa.

### Soak — 30 minutos

- manter navegação ativa;
- realizar interações periódicas;
- observar filas, memória, FPS e reconnects;
- fechar normalmente ao final.

Executar três vezes para qualificação.

### Resiliência

- desligar/ligar Wi-Fi;
- encerrar/reabrir relay;
- reiniciar Head Unit Server;
- fechar socket no meio do frame;
- bloquear/desbloquear telefone;
- encerrar NRO durante streaming;
- provocar erro de decoder;
- tornar SD indisponível para diagnóstico.

## 7. Orçamento 720p30

| Métrica | Meta inicial |
| --- | ---: |
| Receive + dispatch p95 | ≤ 1 ms |
| Unidade de mídia reconhecida → ACK enfileirado p95 | ≤ 2 ms |
| Input enqueue → socket write p95 | ≤ 10 ms |
| Input enqueue → socket write máximo | ≤ 30 ms |
| Toque físico → resposta visual p95 | ≤ 150 ms inicialmente |
| Decode submit → output p95 | ≤ 8 ms; alvo ≤ 5 ms |
| Surface pronta → apresentação p95 | ≤ 16,67 ms |
| Shutdown completo | ≤ 2 s |
| Reconnect após rede disponível | ≤ 5 s |

FPS é medido somente durante conteúdo animado. Tela estática não é classificada
como queda de FPS.

Critério de experiência:

- pelo menos 95% das janelas animadas de 1 segundo apresentam 28 fps ou mais;
- frame interval p95 menor ou igual a 50 ms;
- nenhum freeze superior a 250 ms atribuível ao host;
- nenhuma tendência crescente de fila ou memória em 30 minutos.

## 8. Gates

### Gate de protocolo

- fixtures passam;
- malformed input não causa crash;
- socket/TLS possui owner único;
- stop e timeout são determinísticos.

### Gate de vídeo

- somente access units completos chegam ao decoder;
- nenhum CPU readback no caminho de produção;
- fila limitada e estável;
- 30 minutos sem corrupção ou restart.

### Gate de input

- toque funciona sob carga de vídeo;
- nenhuma queda após 500 eventos sintéticos;
- gestos preservam ordem;
- orçamento de latência aprovado.

### Gate de produto

- três soaks aprovados;
- zero panic do Atmosphère;
- zero restart inesperado de Companion/Gearhead;
- fechamento limpo;
- comparação com DHU documentada;
- latência toque-fóton medida com instrumentação de alta velocidade;
- overhead p95 atribuível ao host de no máximo 30 ms contra o DHU;
- problemas remanescentes classificados e aceitos.

## 9. Regra para regressões

Toda falha encontrada em hardware gera, quando possível:

1. fixture ou fake que reproduz o comportamento;
2. teste falhando;
3. correção no módulo owner;
4. teste verde;
5. repetição do smoke e do gate afetado.

Uma correção que só pode ser validada fisicamente deve documentar roteiro,
resultado, logs e métrica antes/depois.

## 10. 720p60

O teste de 60 fps só começa após Gate de Produto em 30 fps. Deve comprovar 60 fps
por PTS, access units, decoder outputs e apresentações — não por configuração
solicitada ou pacotes TCP.
