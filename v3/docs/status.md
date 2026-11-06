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
| M0-01 | review | Integrador | PREP-02 | Desktop build/test verde; NRO mínimo compilado, aguardando smoke físico de abrir/fechar |
| M0-02 | done | Contracts | PREP-02 | Tipos/erros aprovados; CTest 2/2 e controle negativo comprovado |
| M0-03 | in_progress | Contracts | M0-02 | Interfaces runtime em implementação pelo workstream Contracts |
| M0-04 | in_progress | Verification | M0-02 | Harness/fakes isolados em implementação pelo workstream Verification |

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
