# Protocolo Switch Auto

Este diretório armazenará os schemas e exemplos versionados das mensagens
trocadas entre o companion Android e o homebrew.

Formato do protocolo v2:

- transporte: TCP na porta `38471`;
- codificação: JSON UTF-8;
- framing: uma mensagem JSON por linha (`LF`);
- descoberta do Companion por broadcast UDP na porta `38472`;
- identidade aleatória criada e persistida pelo Switch;
- bootstrap por QR contendo `deviceId` e token aleatório de 256 bits;
- o Android persiste o token e só responde ao Switch previamente lido;
- heartbeat por mensagens `ping` e `pong`.
- telemetria bilateral de bateria embutida no heartbeat.

Fluxo implementado:

```text
discover.request → discover.reply → TCP
pair.request → pair.challenge → pair.proof → pair.accepted
             → hello → hello.ack → ping ⇄ pong
```

O token obtido pelo QR nunca é enviado pela rede. Cada sessão usa nonces
aleatórios de 128 bits e provas HMAC-SHA256 separadas por papel (`server|...` e
`client|...`). A comparação das provas evita curto-circuito e uma prova de uma
sessão não é reutilizável em outra com nonces diferentes.

O `ping` enviado pelo Switch inclui `switchBatteryPercent`; o `pong` devolvido
pelo Companion inclui `phoneBatteryPercent`. Ambos são inteiros entre 0 e 100.

O Core representa esse fluxo em `CompanionSession`, uma máquina de estados sem
dependências de socket ou plataforma. O transporte específico do Switch deve
implementar `CompanionService` e alimentar a sessão com mensagens completas.

A implementação atual do Switch usa uma thread de background, timeout de 10
segundos e heartbeat a cada 5 segundos. Após uma queda, volta à descoberta para
aceitar mudanças de IP e usa backoff de 1, 2, 4, 8 e no máximo 16 segundos.
Todas as esperas são canceláveis e nenhuma operação de rede bloqueia a thread da
interface Borealis.

Exemplos concretos estão em [`examples/`](examples/). O Companion persiste a
associação entre `deviceId` e token depois da leitura do QR. Credenciais, provas e
tokens de sessão são omitidos dos logs. A autenticação é mútua, mas o conteúdo da
sessão ainda não é criptografado. TCP será substituído ou
encapsulado por um transporte autenticado após validar a comunicação física.

## Persistência e revogação

O Android mantém `schemaVersion = 1` nas preferências de pareamento. O Switch
persiste seu marcador em `identity/companion.json`, migra o marcador legado e
move arquivos inválidos para `companion.json.corrupt`. Revogar no Android remove
os tokens confiáveis e fecha imediatamente os sockets de clientes ativos.
