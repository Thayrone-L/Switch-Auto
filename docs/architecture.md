# Switch Auto — Arquitetura e princípios

Este documento define a direção arquitetural normativa do Switch Auto. As
decisões detalhadas de lifecycle, erros, persistência e fallback estão no
[ADR 0001](decisions/0001-core-foundation.md).

## 1. Filosofia

O Switch Auto é uma plataforma modular para Nintendo Switch. Seu objetivo é
fornecer um núcleo sólido e extensível, permitindo evolução gradual sem grandes
refatorações.

Princípios fundamentais:

- o Core fornece capacidades;
- widgets implementam funcionalidades;
- temas definem aparência;
- traduções definem idioma;
- o SDK simplifica o desenvolvimento;
- contratos do Core são desacoplados de suas implementações;
- a arquitetura nasce preparada para expansão, sem implementar antecipadamente
  tudo o que pertence ao futuro.

O Switch Auto não é apenas um launcher. É uma plataforma modular construída para
evoluir.

## 2. Visão dos componentes

```text
┌─────────────────────────────────────────────────────────────┐
│                         Widgets                             │
│             oficiais na V1; externos no futuro             │
└───────────────┬─────────────────────────────────────────────┘
                │ Widget SDK + UI Kit
┌───────────────▼─────────────────────────────────────────────┐
│                           Core                              │
│ Registry │ Lifecycle │ State │ Events │ Themes │ Locales   │
└───────┬───────────────────┬───────────────────────┬─────────┘
        │ interfaces        │ interfaces            │ protocolo
┌───────▼────────┐  ┌───────▼────────────────┐  ┌───▼───────────┐
│ Serviços/HAL  │  │ Persistência e logs    │  │ Companion     │
└───────┬────────┘  └────────────────────────┘  │ Client        │
        │                                        └───┬───────────┘
┌───────▼────────┐                                  │ WebSocket
│ libnx/platform │                          ┌───────▼───────────┐
└────────────────┘                          │ Android Companion │
                                            └───────────────────┘
```

Widgets, temas, traduções, Companion e serviços do Core permanecem independentes
sempre que possível. O acoplamento ocorre por interfaces estáveis.

## 3. Core

O Core coordena a aplicação e fornece capacidades. Ele é responsável por:

- startup e shutdown;
- estado da aplicação;
- registro e lifecycle dos widgets;
- acesso a serviços por interfaces;
- temas e localização;
- persistência e configuração;
- eventos relevantes;
- logs, diagnóstico e isolamento de falhas.

O Core não contém funcionalidades que pertencem a widgets e não conhece detalhes
de APIs Android. Implementações de hardware ficam atrás da HAL.

## 4. Widgets

Todos os widgets utilizam a mesma API. Widgets oficiais e futuros widgets da
comunidade seguem o mesmo modelo.

Na V1, somente widgets compilados internamente serão suportados. Mesmo assim,
eles devem usar exclusivamente o SDK interno e as interfaces públicas concedidas
pelo contexto do widget. Nenhum widget oficial recebe acesso privilegiado à
implementação do Core.

O contrato de lifecycle é:

```text
initialize → activate → update/render ⇄ suspend/resume → dispose
```

As transições e regras completas estão definidas no ADR 0001.

## 5. Widget Registry

O Core possui um registro central responsável por:

- registrar widgets;
- localizar widgets por identificador estável;
- controlar seu lifecycle;
- rejeitar identificadores duplicados;
- isolar falhas por widget;
- preservar uma rota de evolução para descoberta externa.

Registro inicial:

```cpp
registry.registerWidget(std::make_unique<ClockWidget>());
```

Descoberta automática, carregamento dinâmico e Widget Hub não fazem parte da V1.
O Registry não deve assumir, porém, que widgets serão internos para sempre.

## 6. SDK interno

O SDK existe para simplificar o desenvolvimento e proteger contratos estáveis.
Ele não deve esconder APIs simples sem necessidade.

Uma abstração é justificada quando:

- reduz complexidade;
- evita duplicação;
- desacopla uma implementação;
- melhora de forma concreta a experiência do desenvolvedor.

Os widgets oficiais são os primeiros consumidores do SDK. Isso força o contrato
a amadurecer antes de uma futura publicação para a comunidade.

## 7. Hardware Abstraction Layer

Widgets nunca acessam hardware ou libnx diretamente:

```text
Widget → SDK → Service interface → Platform implementation → libnx → Switch
```

Isso permite trocar a origem de uma capacidade sem alterar o widget. Por exemplo,
`LocationService` pode receber dados do Companion hoje, de um GPS USB no futuro
ou de um módulo dedicado posteriormente, enquanto o consumidor continua usando:

```cpp
location.current();
```

## 8. Serviços

Não haverá um `SystemService` monolítico. Serviços são separados por
responsabilidade, inicialmente em interfaces como:

- `TimeService`;
- `PowerService`;
- `MotionService`;
- `InputService`;
- `NetworkService`;
- `MediaService`;
- `LocationService`;
- `ThemeService`;
- `LocalizationService`;
- `SettingsService`.

Um serviço coleta e normaliza um dado uma única vez sempre que possível. Widgets
consomem o estado compartilhado ou eventos relevantes, evitando polling e leitura
duplicada do hardware.

## 9. Temas

Temas controlam somente apresentação. Podem definir:

- cores, fontes e ícones;
- sons e wallpapers;
- layouts e animações.

Temas nunca implementam lógica da aplicação, acessam serviços ou alteram regras
de negócio. O tema padrão usa o mesmo contrato dos demais temas e funciona como
fallback empacotado.

### Layout dos widgets

A barra de sistema (relógio, conexão e baterias) é fixa. A área abaixo usa uma
grade lógica de 12 colunas por 8 linhas. Cada `WidgetPlacement` define o
identificador, posição, tamanho, limites mínimos e visibilidade.

O `WidgetLayout` rejeita posições fora da grade, sobreposição, duplicatas e
tamanhos abaixo do mínimo. `config/widget-layout.json` possui `schemaVersion`;
conteúdo inválido é movido para `.corrupt` e o layout padrão é restaurado.

O padrão reserva 8×8 para GPS, 4×2 para velocidade e 4×6 para mídia. O editor
manipula somente esse modelo; widgets não conhecem gestos nem coordenadas de tela.

## 10. Internacionalização

Todo texto visível ao usuário utiliza uma chave de tradução. Textos de UI não
podem ficar hardcoded no Core ou nos widgets.

Estrutura inicial:

```text
locales/
├── en-US/
└── pt-BR/
```

Ordem de resolução:

```text
idioma escolhido → en-US → chave de tradução visível
```

Logs técnicos e identificadores de erro não passam pelo sistema de tradução.

O Core expõe `LocalizationService`; widgets recebem essa capacidade pelo
`ServiceRegistry` e não conhecem a biblioteca de UI. A aplicação Borealis usa um
adaptador desse contrato. `CatalogLocalizationService` mantém catálogos
independentes e aplica o fallback sem lançar exceções. Os JSON empacotados são
validados antes da build quanto a sintaxe, valores vazios e paridade de chaves
com `en-US`.

## 11. Companion

O Android Companion integra o Switch Auto com capacidades externas e do telefone.
O Switch continua responsável pela UI, widgets, sensores locais e cache. O
Companion fornece GPS, mídia, APIs externas, sincronização, downloads e trabalho
pesado apropriado ao telefone.

O contrato compartilhado contém apenas mensagens e tipos serializáveis. A perda
do Companion degrada recursos conectados, mas não encerra o Switch Auto.

## 12. Estado e comunicação

O estado é dividido em `AppState`, `WidgetState`, `PersistentSettings` e
`RuntimeCache`. Não haverá um singleton global mutável.

Widgets não conversam diretamente entre si. Chamadas diretas são preferidas
quando a dependência é clara. Eventos são usados para mudanças importantes e
distribuição para múltiplos consumidores; o Event Bus não será usado para todas
as operações.

## 13. Logs e observabilidade

Uma funcionalidade difícil de diagnosticar não está pronta. Cada registro deve
ter timestamp, severidade, módulo e mensagem, além de contexto estruturado quando
útil.

Níveis:

| Build/configuração | Níveis ativos |
| --- | --- |
| Release | `ERROR`, `FATAL` |
| Debug | `DEBUG`, `WARN`, `ERROR`, `FATAL` |
| Trace opcional | `TRACE`, `DEBUG`, `WARN`, `ERROR`, `FATAL` |

Logs devem ser filtráveis por severidade e módulo. Exportação e relatórios de
diagnóstico ficam previstos para evolução futura.

Dados sensíveis, tokens e conteúdo pessoal não devem aparecer em logs.

## 14. Crash reports

Falhas inesperadas sem recuperação segura produzem um relatório contendo, quando
disponível:

- versão do Core, SDK e schema;
- plataforma e versão do sistema;
- tema e idioma ativos;
- módulo e operação em execução;
- últimos logs relevantes;
- stack trace;
- informações de diagnóstico que não exponham dados sensíveis.

Crash reports são armazenados localmente. Envio automático não faz parte da V1.

## 15. Performance e concorrência

O Core deve evitar alocações desnecessárias, processamento duplicado, leitura
repetitiva de hardware e polling redundante.

Rede, disco, downloads e processamento pesado não podem bloquear a thread da UI.
A implementação terá conceitos explícitos de UI thread, tarefas em background,
fila de eventos, cancelamento e timeout. O modelo concreto será definido em ADR
antes da implementação assíncrona.

O renderer inicial usa um framebuffer lógico compartilhado de `1280×720`,
conforme o ADR 0002. Switch e simulador executam a mesma árvore de widgets e o
mesmo código de layout; somente a apresentação do buffer é específica da
plataforma.

## 16. Filosofia de APIs

APIs devem ser pequenas, intuitivas, consistentes e previsíveis. Formas concisas
são preferidas quando realmente reduzem complexidade:

```cpp
time.now();
battery.level();
motion.accelerometer();
```

Não se cria uma camada apenas para encapsular outra API. Cada abstração precisa
ter uma responsabilidade e um benefício verificáveis.

## 17. Compatibilidade e segurança futura

Core, SDK, schemas, temas e widgets possuem metadados de versão conforme o ADR
0001. Manifestos de widgets serão preparados para declarar permissões como
`network`, `motion`, `location` e `storage`.

O Core representa permissões como capacidades estáveis (`network`, `location`,
`media_control`, `phone_state`, `answer_calls`, `call_log`, `notifications`,
`background_execution` e outras). Widgets consultam `PermissionService` pelo
SDK; nunca chamam APIs de permissão do Android ou da plataforma diretamente.

O Companion Android funciona como broker: traduz capacidades para permissões
da versão instalada e distingue `granted`, `denied`, `permanently_denied`,
`requires_settings`, `not_required` e `unavailable`. Pedidos sensíveis são
sempre contextuais e iniciados por ação do usuário — nunca em lote no startup.
Declarar uma capacidade não equivale a concedê-la.

Sandbox, carregamento de código externo e marketplace só serão implementados
quando houver um modelo de segurança adequado.

## 18. Escopo evolutivo

### V1

- Core e SDK interno;
- Widget Registry e widgets oficiais;
- serviços e HAL;
- temas e traduções;
- Android Companion;
- logs e base para crash reports;
- persistência, versionamento e fallbacks.

### Futuro

- SDK público;
- widgets externos e plugins;
- Widget Hub e Theme Hub;
- marketplace;
- sistema efetivo de permissões e sandbox;
- integrações adicionais de hardware.

Preparar a arquitetura para crescer é diferente de implementar todas as
funcionalidades desde o início.

## 19. Distribuição de temas e layouts

O Android Companion é a fonte de verdade para catálogos, favoritos e edição. O
Switch conserva apenas o snapshot ativo necessário para iniciar sem o telefone e
um tema padrão embutido para recuperação.

Uma aplicação de personalização segue o fluxo:

1. o Android gera um manifesto versionado com tema, layout e revisão;
2. o Switch valida identificadores, schema e limites da grade;
3. arquivos válidos são gravados primeiro em staging e depois promovidos;
4. o Switch responde com a revisão efetivamente aplicada;
5. o Android só apresenta a sincronização como concluída após essa confirmação.

Falha de validação ou gravação preserva a última configuração válida. Falha ao
carregá-la no startup ativa o tema e o layout padrão embutidos. A transferência
futura de assets de tema seguirá o mesmo processo de staging, validação e
confirmação.
