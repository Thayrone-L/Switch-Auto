# ADR 0003 — Borealis como camada de interface

- Estado: Aceita
- Data: 2026-07-21
- Escopo: Core, Switch e simulador desktop

## Contexto

O renderer de software inicial comprovou o compartilhamento do layout, mas não
oferecia tipografia, animações, navegação, toque e componentes suficientes para a
evolução do Switch Auto.

## Decisão

O projeto usa o fork mantido `xfangfang/borealis` v2.1.0 como camada de UI. A
biblioteca é versionada como submódulo Git e a aplicação compartilha código C++ e
recursos XML entre o NRO e o simulador Windows.

O Borealis pertence à camada de apresentação. Serviços e estado do Core não
dependem dele. Integrações de hardware permanecem atrás de serviços específicos
da plataforma.

## Consequências

- Switch e simulador usam a mesma árvore de layout;
- controles, toque, Yoga, NanoVG e localização ficam disponíveis desde o início;
- o build passa a usar CMake e inclui os recursos Borealis no NRO;
- diferenças de driver e hardware ainda precisam ser validadas no console real;
- a ADR 0002 e seu renderer manual deixam de definir a UI dos executáveis.
