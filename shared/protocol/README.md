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

O Core representa esse fluxo em `CompanionSession`, uma máquina de estados sem
dependências de socket ou plataforma. O transporte específico do Switch deve
implementar `CompanionService` e alimentar a sessão com mensagens completas.

A implementação atual do Switch usa uma thread de background, timeout de 10
segundos, heartbeat a cada 5 segundos e nova tentativa após 3 segundos. Nenhuma
operação de rede bloqueia a thread da interface Borealis.

Exemplos concretos estão em [`examples/`](examples/). O token retornado ainda não
é persistido e o transporte ainda não é criptografado. TCP será substituído ou
encapsulado por um transporte autenticado após validar a comunicação física.
