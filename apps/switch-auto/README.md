# Switch Auto NRO

Aplicação homebrew do Switch Auto, renderizada pelo Borealis com a mesma
interface XML usada no simulador Windows.

## Build

Na raiz do repositório:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-switch.ps1
```

O artefato é gerado em `apps/switch-auto/switch-auto.nro`. Copie-o para:

```text
/switch/switch-auto/switch-auto.nro
```

A dashboard atual lê horário e bateria reais do console. O handshake TCP será
executado de forma assíncrona atrás de `CompanionService`, sem acoplar a UI à
rede ou ao Android Companion. Na inicialização, informe o IPv4 exibido no app
Android e o código de seis dígitos. A dashboard mostrará as etapas da conexão e
tentará reconectar automaticamente se o hotspot oscilar.

Logs de inicialização e erros fatais são gravados em
`/switch/switch-auto/switch-auto.log` no cartão SD.

O transporte publica mudanças de estado pela Event Queue, sem polling da UI.
Logs de conexão possuem `sessionId`, e o último relatório versionado fica em
`/switch/switch-auto/diagnostics/latest.json`. A identidade persistida é migrada
para `identity/device.identity`; arquivos inválidos são preservados com sufixo
`.corrupt` antes da recuperação.

Textos do Core e da UI passam por `LocalizationService`, implementado no NRO por
um adaptador Borealis. Os catálogos `en-US` e `pt-BR` são empacotados no RomFS;
ausências usam `en-US` e, por último, exibem a própria chave para tornar o erro
diagnosticável.
