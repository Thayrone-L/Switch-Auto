# ADR 0005 — Licença e procedência do código

## Estado

Proposta — bloqueia a importação ou adaptação de implementações externas.

## Contexto

A V3 precisa consultar especificações, projetos de head unit e experimentos da
V1/V2 sem introduzir código incompatível com a licença que o projeto escolher.
Uma reimplementação baseada em comportamento observado não é equivalente a
copiar ou adaptar uma implementação protegida.

## Decisão proposta

- escolher e adicionar a licença raiz antes de `M1-01`;
- manter um inventário de dependências, versões, licenças e URLs de origem;
- registrar a procedência de toda fixture e trecho portado;
- não copiar nem adaptar código GPL para um componente que se pretenda publicar
  sob licença incompatível;
- quando uma referência tiver licença incompatível, usar especificação pública,
  captura sanitizada e testes de comportamento em um processo clean-room;
- exigir revisão de qualidade para toda nova dependência ou portabilidade.

## Escolha pendente

O proprietário do projeto deve optar entre:

1. uma licença permissiva para a implementação própria, preservando a V3 como
   reimplementação clean-room; ou
2. GPL-3.0 para permitir derivação consciente de referências compatíveis,
   aceitando as obrigações de distribuição correspondentes.

Até essa escolha, referências externas são somente material de estudo e nenhum
código delas entra na V3.

## Consequências

O início do protocolo pode exigir um pouco mais de documentação, mas o projeto
evita uma reescrita jurídica posterior e mantém clara a origem de cada artefato.
