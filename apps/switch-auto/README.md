# Switch Auto NRO

Aplicação homebrew que executa o Core, o Widget Registry, os widgets oficiais e
o renderer compartilhado do Switch Auto.

## Build

No Windows, execute a partir da raiz do repositório:

```powershell
.\tools\build-switch.ps1
```

O artefato será gerado como `apps/switch-auto/switch-auto.nro`. Copie-o para:

```text
/switch/switch-auto/switch-auto.nro
```

O NRO atual apresenta a primeira dashboard do Core e lê horário e bateria reais
do console. Pressione `+` para encerrar.

O handshake TCP já validado será reintegrado como uma implementação de
`CompanionService`; ele não faz parte temporariamente desta primeira tela do Core.
