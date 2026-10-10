# Switch Auto V3 — Status

Este é o único documento operacional mutável durante a execução. Arquitetura,
ADRs e critérios de aceite só mudam por decisão explícita.

## Marco atual

`PREP — preservação e baseline`

## Tarefas

| ID | Estado | Owner | Dependências | Observação |
| --- | --- | --- | --- | --- |
| PREP-01 | done | Integrador | — | V2 preservada; NRO reproduzido byte a byte; APK reconstruído e hashes registrados |
| PREP-02 | done | Integrador | PREP-01 | Branch `codex/v3-aa-foundation`; documentação aprovada por Quality |
| PREP-03 | review | Verification | PREP-01 | Android, apps, Switch, Atmosphère e condições de rede ainda precisam de valores exatos |
| PREP-04 | review | Integrador/Quality | PREP-01 | ADR proposto; escolha da licença pendente |
| M0-01 | ready | Integrador | PREP-02 | Build e targets |
| M0-02 | ready | Contracts | PREP-02 | Tipos e erros |
| M0-03 | blocked | Contracts | M0-02 | Interfaces runtime |
| M0-04 | blocked | Verification | M0-02 | Harness e fixtures |

Estados permitidos: `blocked`, `ready`, `in_progress`, `review`, `done`.

## Decisões pendentes

- licença do projeto e limites para uso de referências GPL;
- formato dos testes desktop e biblioteca de testes;
- formato de fixtures pós-descriptografia;
- mecanismo de wakeup do Protocol I/O thread no Switch.

## Riscos ativos

- a V3 ainda não possui build próprio; a V2 reproduz, mas reutiliza fontes legadas;
- certificados e logs precisam de revisão antes de qualquer publicação;
- comportamento mínimo obrigatório dos canais de áudio/microfone ainda precisa
  de teste de contrato.

## Último gate

Ainda não executado.
