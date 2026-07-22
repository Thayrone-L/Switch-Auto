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
