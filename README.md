# Switch Auto

Uma plataforma automotiva modular para Nintendo Switch desbloqueado, integrada a
um dispositivo Android.

O Switch Auto transforma o console em uma central customizável para veículos. O
Nintendo Switch executa a interface, os widgets, temas e recursos locais; o
Android Companion fornece GPS, mídia, conectividade e integrações do telefone.

> [!IMPORTANT]
> O projeto está em fase inicial de arquitetura e prototipagem. Ainda não existe
> uma versão pronta para instalação ou uso cotidiano.

## Visão

O projeto não pretende ser apenas um launcher ou uma cópia visual do Android
Auto. A proposta é criar uma plataforma extensível para projetos de customização
automotiva:

- interface otimizada para a tela touchscreen do Switch;
- widgets oficiais e, futuramente, widgets da comunidade;
- temas completos para diferentes carros e estilos;
- integração com mídia, localização e serviços Android;
- possibilidade futura de telemetria OBD-II e hardware adicional;
- funcionamento local e degradável quando o telefone estiver desconectado.

## Princípios

```text
Core fornece capacidades.
Widgets implementam funcionalidades.
Temas definem aparência.
Traduções definem idioma.
O SDK simplifica o desenvolvimento.
```

A arquitetura é modular desde o início, mas evita implementar antecipadamente
recursos que pertencem ao futuro. Widgets oficiais utilizam o mesmo SDK e os
mesmos limites previstos para futuros widgets externos.

## Como funciona

```text
┌──────────────────────────┐         ┌──────────────────────────┐
│     Nintendo Switch      │         │      Android Phone       │
│                          │         │                          │
│  UI e touchscreen        │         │  GPS e localização       │
│  Core e widgets          │◄───────►│  Sessão de mídia         │
│  Temas e traduções       │  rede   │  APIs e conectividade    │
│  Cache e sensores locais │  local  │  Processamento externo   │
└──────────────────────────┘         └──────────────────────────┘
        Switch Auto                     Android Companion
```

O Switch deve iniciar e manter seus recursos locais mesmo sem o Companion. Uma
desconexão afeta somente as funcionalidades dependentes do telefone.

## Arquitetura

O Core será organizado em serviços pequenos, acessados por interfaces estáveis e
por uma camada de abstração de hardware (HAL). Widgets nunca acessam libnx ou o
hardware diretamente.

```text
Widget → SDK → Service interface → Platform implementation → libnx → Switch
```

Serviços planejados incluem:

- tempo e energia;
- movimento e input;
- rede, mídia e localização;
- temas, traduções e configurações.

Consulte:

- [Arquitetura e princípios](docs/architecture.md);
- [ADR 0001 — Fundação do Core](docs/decisions/0001-core-foundation.md);
- [ADR 0003 — Borealis como camada de interface](docs/decisions/0003-borealis-ui.md);
- [ADR 0004 — Android Auto como fonte de projeção](docs/decisions/0004-android-auto-host.md);
- [Roadmap](docs/roadmap.md).

## Direção atual

Os primeiros protótipos validaram pareamento seguro, descoberta automática,
reconexão, telemetria, localização, mídia, mapas e transporte de imagens entre
Android e Switch.

Também foi validada uma integração experimental com o Waze: o Companion abre o
destino correto e consegue espelhar a tela por `MediaProjection`. Essa abordagem
é mantida como prova de conceito, mas não será a base da experiência final
porque depende do display físico ligado, preserva sua orientação e não fornece
entrada remota.

A direção adotada é tornar o Switch um host Android Auto experimental. O Android
Auto fornece navegação, chamadas, notificações, mídia, áudio e comandos de voz.
O Companion continua como a camada extra do Switch Auto para temas, layouts,
telas de descanso, widgets, diagnósticos, sincronização e futuros mapas offline.
O MapLibre permanece como fallback quando a projeção não estiver disponível.

## Escopo da V1

- Core e SDK interno;
- Hardware Abstraction Layer;
- Widget Registry e primeiros widgets oficiais;
- tema padrão e internacionalização `en-US`/`pt-BR`;
- Android Companion;
- comunicação local versionada;
- logs, persistência e fallbacks;
- exibição de velocidade e estado da mídia;
- comandos de reprodução pelo Switch.

## Futuro

- SDK público e widgets externos;
- Widget Hub, Theme Hub e marketplace;
- sistema de permissões e sandbox;
- navegação integrada;
- telemetria OBD-II;
- temas e layouts compartilháveis;
- integração com acessórios e iluminação do veículo.

## Estrutura do repositório

```text
switch-auto/
├── apps/
│   ├── android-companion/  Aplicativo auxiliar para Android
│   ├── simulator/          Artefatos do simulador Windows
│   └── switch-auto/        Homebrew para Nintendo Switch
│   └── ui/                 Interface Borealis compartilhada
├── core/                   Core, SDK interno e widgets
├── extern/borealis/        UI Kit versionado como submódulo
├── resources/              Layouts e traduções do Switch Auto
├── docs/
│   ├── decisions/          Registros de decisões arquiteturais
│   ├── architecture.md     Arquitetura normativa
│   └── roadmap.md          Marcos do projeto
├── shared/
│   └── protocol/           Contratos entre Switch e Android
└── tools/                  Ferramentas de desenvolvimento
```

## Estado do desenvolvimento

O projeto está no **Marco 0 — Fundação**:

- [x] visão e princípios arquiteturais;
- [x] lifecycle, modelo de erros e fronteiras entre módulos;
- [x] persistência, versionamento e estratégia de fallback;
- [x] definição inicial da stack;
- [x] projeto compilável do Switch;
- [x] projeto compilável do Android Companion;
- [x] primeira versão do protocolo;
- [x] primeiros widgets executados pelo Core compartilhado.
- [x] pareamento seguro e reconexão automática em rede local/hotspot;
- [x] telemetria bilateral de bateria, GPS e mídia;
- [x] MapLibre, busca por endereço, rota e interação por toque;
- [x] prova de conceito de abertura e espelhamento do Waze;
- [x] definição do Android Auto Host como próxima direção arquitetural.

## Desenvolvimento

As stacks adotadas inicialmente são:

- Core e Switch: C++17, devkitPro e libnx;
- interface e simulador: Borealis v2.1.0, XML, Yoga e NanoVG;
- Android: Kotlin e Jetpack Compose;
- renderização: a mesma árvore Borealis no Switch e no Windows;
- comunicação experimental: TCP com mensagens JSON versionadas.

Consulte os READMEs de cada aplicação para instruções de build e execução.

## Segurança

O Switch Auto é destinado a consoles capazes de executar homebrew. O projeto não
fornece instruções para desbloqueio, chaves, firmware ou conteúdo protegido.

Durante a condução, a interface deve priorizar baixa distração, alvos grandes e
operações simples. O usuário é responsável por instalar e utilizar o equipamento
de acordo com a legislação local e de forma segura.

## Contribuições e licença

As regras de contribuição e a licença serão definidas antes da primeira versão
pública. Até que um arquivo de licença seja adicionado, nenhum direito de uso,
modificação ou redistribuição é concedido automaticamente.
