# ADR 0003 — Owner único de socket, TLS e escrita

- Estado: Aceita
- Data: 2026-10-09

## Contexto

ACKs, keepalive e toque compartilham a mesma sequência TLS. Escritas diretas por
handlers ou múltiplas threads tornam ordem e shutdown difíceis de provar.

## Decisão

Uma única Protocol I/O thread possui transporte, TLS, framing e sequência de
escrita. Outros módulos publicam mensagens completas em um scheduler priorizado.

Input, vídeo, UI e diagnóstico nunca chamam `send()` ou a API SSL diretamente.
Um mecanismo de wakeup permite enviar mensagens sem esperar nova entrada de
rede.

## Consequências

- ordem TLS é determinística;
- concorrência fica testável com fakes;
- ACK e input não dependem de decode/render;
- handlers precisam ser rápidos e não bloqueantes;
- todo shutdown passa por um único owner.
