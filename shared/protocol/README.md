# Protocolo Switch Auto

Este diretório armazenará os schemas e exemplos versionados das mensagens
trocadas entre o companion Android e o homebrew.

Formato do protótipo de handshake:

- transporte: TCP na porta `38471`;
- codificação: JSON UTF-8;
- framing: uma mensagem JSON por linha (`LF`);
- descoberta do Companion por broadcast UDP na porta `38472`;
- identidade aleatória criada e persistida pelo Switch;
- bootstrap por QR contendo `deviceId` e token aleatório de 256 bits;
- o Android persiste o token e só responde ao Switch previamente lido;
- heartbeat por mensagens `ping` e `pong`.

Fluxo implementado:

```text
discover.request → discover.reply → TCP
pair.request → pair.accepted → hello → hello.ack → ping ⇄ pong
```

O Core representa esse fluxo em `CompanionSession`, uma máquina de estados sem
dependências de socket ou plataforma. O transporte específico do Switch deve
implementar `CompanionService` e alimentar a sessão com mensagens completas.

A implementação atual do Switch usa uma thread de background, timeout de 10
segundos, heartbeat a cada 5 segundos e nova tentativa após 3 segundos. Nenhuma
operação de rede bloqueia a thread da interface Borealis.

Exemplos concretos estão em [`examples/`](examples/). O Companion persiste a
associação entre `deviceId` e token depois da leitura do QR. Credenciais e tokens
de sessão são omitidos dos logs. O transporte ainda não é criptografado e o token
é enviado no primeiro frame TCP; esse passo será substituído por desafio-resposta
antes de uma versão de produção. TCP será substituído ou
encapsulado por um transporte autenticado após validar a comunicação física.
