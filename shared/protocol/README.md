# Protocolo Switch Auto

Este diretório armazenará os schemas e exemplos versionados das mensagens
trocadas entre o companion Android e o homebrew.

Formato inicialmente proposto:

- transporte: WebSocket;
- codificação: JSON UTF-8;
- envelope com `version`, `type`, `timestamp` e `payload`;
- heartbeat e identificação explícita da sessão.

Os contratos concretos serão adicionados junto do primeiro protótipo de conexão.

