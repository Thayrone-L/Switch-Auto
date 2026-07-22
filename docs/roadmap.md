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
- carregar tema padrão e traduções `en-US`/`pt-BR`;
- executar um widget oficial pelo lifecycle e Registry;
- [x] conexão manual por endereço IP;
- [x] transporte TCP assíncrono com heartbeat e reconexão;
- [x] estado da conexão exibido na dashboard Borealis;
- [x] identidade persistente gerada pelo Switch;
- [x] descoberta automática do Companion, sem IP manual;
- migrar o transporte experimental de TCP para WebSocket;
- dados simulados nos dois lados.

## Marco 2 — Mídia e GPS

- velocidade obtida pelo GPS do Android;
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
