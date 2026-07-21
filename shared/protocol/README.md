# Protocolo Switch Auto

Este diretório armazenará os schemas e exemplos versionados das mensagens
trocadas entre o companion Android e o homebrew.

Formato do protótipo de handshake:

- transporte: TCP na porta `38471`;
- codificação: JSON UTF-8;
- framing: uma mensagem JSON por linha (`LF`);
- pareamento por código temporário de seis dígitos;
- heartbeat por mensagens `ping` e `pong`.

Fluxo implementado:

```text
pair.request → pair.accepted → hello → hello.ack → ping ⇄ pong
```

Exemplos concretos estão em [`examples/`](examples/). O token retornado ainda não
é persistido e o transporte ainda não é criptografado. TCP será substituído ou
encapsulado por um transporte autenticado após validar a comunicação física.
