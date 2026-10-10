# ADR 0004 — Filas de vídeo usam access units completos

- Estado: Proposta — bloqueada por caracterização do ACK de mídia
- Data: 2026-10-09

## Contexto

Bytes TCP, frames AA, mensagens remontadas e quadros H.264 possuem fronteiras
diferentes. Tratar fragmentos arbitrários como “frames” produz métricas erradas e
torna descarte/backpressure inseguros para referências H.264.

## Decisão proposta

A fronteira entre protocolo e pipeline de vídeo é `EncodedAccessUnit` completo,
validado e associado ao PTS quando disponível.

A fila é limitada e nunca contém fragmentos parciais.

O momento e a unidade do ACK ainda não estão decididos. Antes de aceitar este
ADR, uma fixture real deve mapear:

```text
framing AA → mensagem de mídia → NAL/access unit → ACK correspondente
```

Se o ACK reconhecer uma mensagem de mídia antes da conclusão do access unit, o
pipeline deverá separar `MediaMessageAccepted` de `AccessUnitAccepted`. Esperar
o AU completo não pode bloquear a janela de mensagens não reconhecidas do
telefone.

Backlog sustentado aciona recovery explícito do vídeo; não aumenta buffers
indefinidamente e não descarta referências silenciosamente.

O mailbox posterior ao decoder pode substituir apresentações obsoletas e deve
entregar semanticamente a superfície mais recente.

## Consequências

- a decisão final depende da tarefa `M1-07`;
- métricas passam a distinguir protocolo, mídia, decode e apresentação;
- testes de fila usam unidades semanticamente válidas;
- recovery de vídeo pode ser isolado da sessão;
- a montagem precisa estar concluída antes do decoder.
