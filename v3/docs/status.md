# Switch Auto V3 — Status

Este é o único documento operacional mutável durante a execução. Arquitetura,
ADRs e critérios de aceite só mudam por decisão explícita.

## Marco atual

`PREP — preservação e baseline`

## Tarefas

| ID | Estado | Owner | Dependências | Observação |
| --- | --- | --- | --- | --- |
| PREP-01 | review | Integrador | — | Commit/tag prontos; APK reproduzido; rebuild NRO/toolchain pendentes |
| PREP-02 | in_progress | Integrador | PREP-01 | Branch `codex/v3-aa-foundation`; documentação aguardando review |
| PREP-03 | review | Verification | PREP-01 | Android/Switch/devkitPro ainda precisam de versões exatas |
| PREP-04 | review | Integrador/Quality | PREP-01 | ADR proposto; escolha da licença pendente |
| M0-01 | blocked | Integrador | PREP-02 | Build e targets |
| M0-02 | blocked | Contracts | PREP-02 | Tipos e erros |
| M0-03 | blocked | Contracts | M0-02 | Interfaces runtime |
| M0-04 | blocked | Verification | M0-02 | Harness e fixtures |

Estados permitidos: `blocked`, `ready`, `in_progress`, `review`, `done`.

## Decisões pendentes

- licença do projeto e limites para uso de referências GPL;
- formato dos testes desktop e biblioteca de testes;
- formato de fixtures pós-descriptografia;
- mecanismo de wakeup do Protocol I/O thread no Switch.

## Riscos ativos

- build V2 ainda reutiliza fontes legadas;
- certificados e logs precisam de revisão antes de qualquer publicação;
- comportamento mínimo obrigatório dos canais de áudio/microfone ainda precisa
  de teste de contrato.

## Último gate

Ainda não executado.
