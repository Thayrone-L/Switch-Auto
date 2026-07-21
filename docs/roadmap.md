# Roadmap

## Marco 0 — Fundação

- implementar as interfaces definidas no ADR 0001;
- criar o Core, HAL, SDK interno e Widget Registry mínimos;
- definir concorrência, tarefas assíncronas e cancelamento;
- definir stack e requisitos de desenvolvimento;
- criar os projetos compiláveis para Android e Switch;
- definir a primeira versão do protocolo.

## Marco 1 — Conexão

- carregar tema padrão e traduções `en-US`/`pt-BR`;
- executar um widget oficial pelo lifecycle e Registry;
- conexão manual por endereço IP;
- WebSocket com heartbeat e reconexão;
- tela de estado da conexão;
- dados simulados nos dois lados.

## Marco 2 — Mídia e GPS

- velocidade obtida pelo GPS do Android;
- metadados da sessão de mídia;
- play, pause, faixa anterior e próxima;
- permissões e serviço em primeiro plano no Android.

## Marco 3 — Experiência automotiva

- descoberta automática na rede;
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
