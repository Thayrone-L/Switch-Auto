# ADR 0002 — Renderer de software compartilhado

- Estado: Aceita
- Data: 2026-07-21
- Escopo: Core, Switch e simulador desktop

## Contexto

Um simulador anterior mantido separadamente apresentou diferenças de posição em
relação ao Switch. O projeto precisa de iteração visual rápida sem criar duas
implementações da interface.

## Decisão

O Core produz um framebuffer lógico RGBA de `1280×720` por meio de um renderer de
software compartilhado. Layout, primitivas, fonte bitmap e widgets são idênticos
nos dois alvos.

- o Switch apresenta o buffer com o `Framebuffer` do libnx;
- o Windows converte RGBA para BGRA e apresenta com `StretchDIBits`;
- serviços e input permanecem específicos de plataforma;
- o simulador oferece estados mutáveis para desenvolvimento;
- snapshots determinísticos podem ser gerados sem abrir uma janela.

SDL2 ou aceleração por GPU poderão substituir a apresentação ou o renderer no
futuro, mas não poderão introduzir uma segunda árvore de layout.

## Consequências

- posições e rasterização são determinadas pelo mesmo código;
- o simulador não depende de firmware, chaves ou emulador de Switch;
- o renderer inicial tem menos recursos tipográficos e gráficos;
- desempenho e comportamento de hardware ainda exigem teste no console;
- assets e fontes futuras precisarão ter processamento determinístico.

