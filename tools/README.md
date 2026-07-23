# Ferramentas

Os builds nativos usam CMake e o Borealis versionado em `extern/borealis`. Ao
clonar o projeto, inicialize os submódulos com:

```powershell
git submodule update --init --recursive
```

Scripts auxiliares de build, formatação, validação do protocolo e empacotamento
serão mantidos aqui quando as stacks forem definidas.

## Dispositivos sem fio

Copie `devices.example.json` para `devices.local.json` e preencha o endereço dos
dois aparelhos. O arquivo local é ignorado pelo Git.

### Android

No Android 11 ou superior, ative **Opções do desenvolvedor > Depuração sem fio**.
Na primeira vez, escolha **Parear dispositivo com código** e execute:

```powershell
.\tools\connect-android.ps1 -PairEndpoint IP:PORT -PairCode CODIGO -Endpoint IP:PORT
```

As portas de pareamento e conexão mostradas pelo Android podem ser diferentes.
Depois, para compilar e reinstalar o APK:

```powershell
.\tools\deploy-android.ps1
```

### Nintendo Switch

Conecte o Switch à mesma rede, abra o Homebrew Menu e pressione **Y** para iniciar
o NetLoader. Depois execute:

```powershell
.\tools\deploy-switch.ps1
```

O script compila, envia e abre o NRO sem gravá-lo permanentemente no cartão. O
terminal permanece ligado ao `nxlink` e recebe os logs da execução. Para atualizar
a cópia permanente em `/switch/switch-auto/`, ainda é necessário usar FTP ou
copiar o arquivo manualmente.
