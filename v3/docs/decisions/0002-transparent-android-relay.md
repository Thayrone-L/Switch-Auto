# ADR 0002 — Android Companion como relay transparente

- Estado: Aceita
- Data: 2026-10-09

## Contexto

Experimentos da V2 adicionaram parsing e pacing ao relay Android para tentar
regularizar vídeo. Isso aumentou a diferença em relação ao DHU e introduziu mais
um lugar capaz de atrasar, reagrupar ou interpretar dados Android Auto.

## Decisão

O Companion V3 executa somente cópia TCP duplex entre o Head Unit Server local e
o Switch. Ele não conhece framing, TLS, canais, vídeo, ACK ou input.

Contadores de bytes e lifecycle são permitidos desde que não alterem ordem,
conteúdo ou timing intencionalmente. Logs persistentes ocorrem fora das threads
de cópia.

## Consequências

- bugs de protocolo ficam localizados no host;
- o relay pode ser testado byte-for-byte;
- pacing, parsing e remontagem Android são removidos;
- configuração de endpoint e reconnect continuam responsabilidades Android.
