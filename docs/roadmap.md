# Roadmap

## Marco 0 — Fundação

- [x] implementar `Result` e o contrato inicial de `CompanionService`;
- criar o Core, HAL, SDK interno e Widget Registry mínimos;
- definir concorrência, tarefas assíncronas e cancelamento;
- definir stack e requisitos de desenvolvimento;
- [x] criar os projetos mínimos para Android e Switch;
- [x] definir o protocolo experimental de handshake;
- [x] criar Core, Widget Registry e renderer compartilhado;
- [x] criar simulador Windows com serviços falsos;
- [x] compilar e validar o APK e o NRO nos dispositivos físicos;
- [x] validar o handshake usando o hotspot do Android.

## Marco 1 — Conexão

- [x] `ServiceRegistry` tipado e estados separados;
- [x] lifecycle de widgets com ativação, suspensão e retomada;
- [x] fila de eventos do Core;
- [x] widget de bateria bilateral Switch ↔ Android;
- [x] inicialização automática após o primeiro pareamento;
- [x] redescoberta após queda ou mudança de IP;
- [x] backoff exponencial cancelável limitado a 16 segundos;
- [x] jitter de reconexão, timeout de I/O e conexão TCP cancelável;
- [x] autenticação mútua desafio–resposta com HMAC-SHA256;
- [x] protocolo v2 sem transmissão da credencial de pareamento;
- [x] Companion Android inicia o servidor em todo startup;
- [x] revogação de credenciais e encerramento de sessões ativas;
- [x] estado de pareamento versionado com migração e fallback de corrupção;
- [x] carregar traduções `en-US`/`pt-BR` por `LocalizationService`;
- [x] fallback de tradução `locale escolhido → en-US → chave`;
- [x] validar integridade e paridade dos catálogos;
- [x] definir capacidades e `PermissionService` independente de plataforma;
- [x] implementar broker Android sem pedidos sensíveis no startup;
- [x] implementar Registry e lifecycle dos widgets do Companion;
- [x] migrar a telemetria de bateria do Android para `core.battery`;
- carregar tema padrão;
- executar um widget oficial pelo lifecycle e Registry;
- [x] conexão manual por endereço IP;
- [x] transporte TCP assíncrono com heartbeat e reconexão;
- [x] estado da conexão exibido na dashboard Borealis;
- [x] Event Queue integrada ao transporte sem polling de estado pela UI;
- [x] logs correlacionados por sessão e relatório de diagnóstico versionado;
- [x] testes de queda, rejeição, reconexão e protocolo incompatível;
- [x] identidade persistente gerada pelo Switch;
- [x] descoberta automática do Companion, sem IP manual;
- migrar o transporte experimental de TCP para WebSocket;
- dados simulados nos dois lados.

## Marco 2 — Mídia e GPS

- [x] widget GPS inicial com posição, precisão, velocidade e direção;
- [x] receber e apresentar telemetria GPS na dashboard Borealis;
- definir framing binário e canais separados para mapas/áudio;
- foreground service para manter transporte e GPS com a tela bloqueada;
- metadados da sessão de mídia;
- play, pause, faixa anterior e próxima;
- permissões e serviço em primeiro plano no Android.

## Marco 3 — Experiência automotiva

- endurecer o pareamento com desafio criptográfico e transporte autenticado;
- temas claro e escuro;
- perfis e layouts customizáveis;
- otimizações para inicialização e uso prolongado.

## Futuro

- SDK público e widgets externos;
- Widget Hub, Theme Hub e marketplace;
- sistema efetivo de permissões e sandbox;
- instruções de navegação;
- telemetria OBD-II;
- comandos de voz;
- temas compartilháveis;
- integração com iluminação e acessórios do veículo.
