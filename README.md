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
- [Roadmap](docs/roadmap.md).

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
│   └── switch-auto/        Homebrew para Nintendo Switch
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
- [ ] definição final da stack;
- [ ] projeto compilável do Switch;
- [ ] projeto compilável do Android Companion;
- [ ] primeira versão do protocolo;
- [ ] primeiro widget funcional.

## Desenvolvimento

As stacks consideradas inicialmente são:

- Switch: C++, devkitPro, libnx e SDL2;
- Android: Kotlin e Jetpack Compose;
- comunicação: WebSocket com mensagens JSON versionadas.

Essas escolhas ainda serão confirmadas antes da criação dos projetos de build.
Instruções de configuração e compilação serão adicionadas quando o primeiro
esqueleto executável estiver disponível.

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
