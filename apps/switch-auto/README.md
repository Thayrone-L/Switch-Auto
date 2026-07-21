# Switch Auto NRO — handshake prototype

Cliente mínimo para validar comunicação entre o Nintendo Switch e o Android
Companion.

## Build

Execute no shell do devkitPro/MSYS2:

```sh
export DEVKITPRO=/opt/devkitpro
make
```

No Windows, o caminho deste repositório contém um espaço e o template do
devkitPro não consegue processá-lo diretamente. Execute a partir da raiz:

```powershell
.\tools\build-switch.ps1
```

O artefato será gerado como `switch-auto.nro`. Copie-o para:

```text
/switch/switch-auto/switch-auto.nro
```

## Teste

1. Abra o Android Companion e inicie o servidor.
2. Conecte o Switch à mesma rede ou ao hotspot do Android.
3. Abra o NRO e informe o IPv4 e o código mostrados no APK.
4. Confirme `PAIR ACCEPTED`, `HELLO ACK` e respostas `PONG`.
5. Pressione `+` para encerrar.

Em caso de falha, o NRO mostra a operação, o código `errno` e sua descrição. Ele
faz três tentativas com espera progressiva e depois oferece:

- `A`: repetir com o mesmo IP e código;
- `X`: editar IP e código;
- `+`: encerrar.

O transporte TCP/JSON desta etapa é provisório e fica isolado do futuro Core.
