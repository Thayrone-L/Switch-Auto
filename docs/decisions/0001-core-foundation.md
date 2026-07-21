# ADR 0001 — Fundação do Core

- Estado: Aceita
- Data: 2026-07-21
- Escopo: Core do Switch Auto, widgets oficiais e Android Companion

## Contexto

Antes de implementar o Core, o projeto precisa de contratos estáveis para evitar
dependências acidentais, estado global e falhas isoladas que encerrem toda a
aplicação. Esta decisão fecha somente a fundação necessária para iniciar o código.

## 1. Ciclo de vida

### Aplicação

Os estados da aplicação são:

```text
Created
   ↓
LoadingSettings
   ↓
LoadingLocale
   ↓
LoadingTheme
   ↓
InitializingServices
   ↓
RegisteringWidgets
   ↓
StartingUi
   ↓
Running
   ↓
ShuttingDown
   ↓
Stopped
```

`Failed` é um estado terminal reservado para uma falha fatal durante startup ou
execução. Antes de entrar nele, a aplicação tenta os fallbacks definidos neste
documento. O encerramento normal sempre passa por `ShuttingDown`.

Na V1, a sequência é síncrona durante o startup. Depois de `Running`, rede, disco
e processamento pesado não podem bloquear a thread da interface.

### Widget

Os estados de um widget são:

```text
Registered → Initialized → Active ⇄ Suspended → Disposed
                    \______________________________/
```

Contrato de ciclo de vida:

```cpp
initialize(context) -> Result<void, WidgetError>
activate()           -> Result<void, WidgetError>
update(deltaTime)    -> Result<void, WidgetError>
render(canvas)       -> Result<void, WidgetError>
suspend()            -> void
resume()             -> Result<void, WidgetError>
dispose()            -> void
```

Regras:

- `initialize` ocorre uma única vez após o registro;
- somente um widget `Active` recebe `update` e `render`;
- `suspend` preserva estado transitório quando possível;
- `resume` retorna o widget para `Active`;
- `dispose` deve ser idempotente e libera recursos do widget;
- falha recuperável desativa somente o widget afetado;
- a V1 pode implementar apenas parte das transições, mas não pode criar estados
  incompatíveis com este contrato.

## 2. Modelo de erros

O Core adota três mecanismos, cada um com um significado distinto:

- `Result<T, E>`: operação pode falhar de uma forma esperada e tratável;
- `std::optional<T>`: ausência de valor é válida e não representa falha;
- exceção: invariante quebrada ou falha inesperada de programação.

Exemplo:

```cpp
Result<BatteryInfo, PowerError> getBattery();
std::optional<TrackInfo> currentTrack() const;
```

Todo erro estruturado deve conter ao menos:

```text
code        identificador estável e legível por máquina
message     descrição para diagnóstico
cause       causa encadeada opcional
recoverable indica se existe recuperação segura
```

Exceções não atravessam fronteiras públicas do SDK de widgets. O Core as captura
na fronteira do componente, registra o contexto e tenta isolar a falha. Quando
não houver recuperação segura, produz um relatório fatal e entra em `Failed`.

`Result` será um tipo próprio e pequeno na V1. A API não dependerá inicialmente
de uma biblioteca externa para representar resultados.

## 3. Fronteiras entre módulos

Dependências permitidas:

```text
Widgets ──────→ Widget SDK ──────→ Core interfaces
    ├────────→ UI Kit
    ├────────→ Theme API
    └────────→ Localization API

Platform/libnx ──→ implementa Core interfaces
Companion Client ─→ implementa Core interfaces
Core ─────────────→ coordena serviços, registry e UI
```

Widgets podem conhecer:

- Widget SDK e tipos de domínio públicos;
- UI Kit;
- Theme API;
- Localization API;
- interfaces de serviços concedidas pelo `WidgetContext`.

Widgets não podem conhecer:

- libnx ou detalhes da plataforma;
- implementação do Android Companion;
- caminhos internos ou filesystem privado do Core;
- instâncias globais mutáveis;
- implementação de outros widgets.

Widgets não conversam diretamente entre si. Serviços expõem leitura direta de
estado quando a dependência é clara; mudanças relevantes podem ser publicadas na
fila de eventos. O Event Bus não substitui chamadas diretas de serviço.

O estado será separado em quatro categorias:

- `AppState`: estado coordenado e observável da aplicação;
- `WidgetState`: estado privado de cada widget;
- `PersistentSettings`: configuração validada e persistida;
- `RuntimeCache`: dados descartáveis e reconstruíveis.

Não haverá um singleton global de estado. Dependências entram por construtor ou
por contextos de escopo limitado.

## 4. Persistência e versionamento

O diretório padrão no Switch é:

```text
/switch/switch-auto/
├── config/
├── themes/
├── locales/
├── widgets/
├── cache/
├── logs/
└── crash-reports/
```

Arquivos de configuração, manifestos, temas e traduções usam JSON UTF-8. Todo
arquivo persistente que possa evoluir contém `schemaVersion` inteiro:

```json
{
  "schemaVersion": 1
}
```

Regras de persistência:

- defaults ficam empacotados no aplicativo e são imutáveis;
- escrita usa arquivo temporário e substituição somente após validação;
- antes de migrar, o arquivo anterior é preservado com sufixo `.bak`;
- migrações são sequenciais e testáveis (`v1 → v2 → v3`);
- versão futura desconhecida não é sobrescrita automaticamente;
- cache nunca é fonte única de dados e pode ser removido com segurança;
- conteúdo corrompido é preservado para diagnóstico e substituído por defaults.

Compatibilidade pública será declarada por:

```json
{
  "schemaVersion": 1,
  "minCoreVersion": "0.1.0",
  "sdkVersion": 1
}
```

`CoreVersion` segue SemVer. `sdkVersion` e `schemaVersion` são inteiros e mudam
quando seus respectivos contratos ficam incompatíveis. A primeira implementação
começa com Core `0.1.0`, SDK `1` e schema `1`.

## 5. Responsabilidades Switch × Companion

### Switch Auto

- ciclo de vida e estado da interface;
- renderização, input e sensores locais disponíveis;
- widgets, temas e traduções locais;
- cache local e configurações da interface;
- apresentação de dados recebidos;
- funcionamento degradado sem telefone.

### Android Companion

- GPS e dados originados no telefone;
- integração com sessão de mídia e serviços Android;
- APIs externas, downloads e autenticação externa;
- processamento inadequado ao hardware ou à thread de UI do Switch;
- sincronização e publicação de dados para o Switch;
- aplicação das permissões do Android.

### Compartilhado

Somente contratos de protocolo e tipos serializáveis. Regras de apresentação não
entram no Companion; detalhes de APIs Android não entram no Switch.

O Switch não depende do Companion para iniciar. A desconexão muda o estado do
serviço para offline, mas não encerra o aplicativo.

## 6. Degradação e fallback

Princípio normativo:

> Uma falha isolada não derruba toda a aplicação quando houver uma recuperação
> segura possível.

Tabela mínima de recuperação:

| Falha | Comportamento |
| --- | --- |
| Tema inválido | Registrar erro e carregar o tema padrão empacotado |
| Tradução ausente | Tentar `en-US`; depois usar a chave visível |
| Widget falhou | Suspender, registrar e desativar somente o widget |
| Companion offline | Manter UI e recursos locais; tentar reconectar |
| Configuração corrompida | Preservar cópia, carregar defaults e informar usuário |
| Cache corrompido | Descartar e reconstruir |
| Migração falhou | Restaurar backup e não sobrescrever o original |
| Serviço local indisponível | Marcar dado indisponível e manter o loop da UI |

Um fallback deve ser determinístico, observável em log e limitado para não entrar
em repetição infinita. Falhas fatais ficam restritas a casos como impossibilidade
de iniciar a renderização, corrupção de memória ou violação de uma invariante do
Core sem recuperação segura.

## Consequências

- o Core poderá ser testado no computador usando implementações falsas;
- widgets oficiais e futuros plugins ficam desacoplados do hardware;
- existe trabalho inicial para definir interfaces e tipos de erro;
- a evolução de formatos exige migrações explícitas;
- recursos externos não podem tornar o Companion obrigatório para iniciar.

## Decisões adiadas

Serão fechadas em ADRs posteriores:

- executor de tarefas, cancelamento, timeouts e fila de eventos;
- formato completo do manifesto e aplicação real de permissões;
- framework Android e versão mínima suportada;
- biblioteca de renderização e detalhes do build homebrew;
- estratégia completa de testes e automação de CI;
- descoberta automática e segurança do protocolo local.

