# Switch Auto V3 — Baseline inicial

- Data do registro: 2026-10-09
- Commit preservado da V2: `0eef28c438f1dc33e0696079139c20864d52b987`
- Tag: `v2-aa-prototype-final`
- Telefone validado: Motorola edge 60
- Android Auto observado no teste original: `17.2.662634-release`
- DHU: versão `2.0-windows`, build `2022-03-30-438482292`
- Perfil: `1280x720`, `30 fps`, `160 dpi`, touch

## Ambiente ainda bloqueante

| Item | Estado |
| --- | --- |
| Versão Android/API | pendente; ADB estava desconectado em 2026-10-09 |
| Firmware do Switch | pendente de leitura no aparelho |
| Atmosphère | pendente de leitura no aparelho |
| devkitPro/devkitA64/libnx | bloqueado; `C:\devkitPro` não estava acessível em 2026-10-09 |
| Java | OpenJDK `21.0.10` do Android Studio |
| ADB | `1.0.41`, platform-tools `37.0.0-14910828` |
| Gradle/AGP | Gradle `9.5.0`, Android Gradle Plugin `9.3.0` |
| Waze | versão pendente via `dumpsys package` |
| Google Maps | versão pendente via `dumpsys package` |
| Spotify | versão pendente via `dumpsys package` |
| Rede do teste comparativo | banda, RSSI, RTT e perda pendentes |

PREP-03 permanece em `review` até que todos os itens pendentes sejam
preenchidos. Versões e condições de rede não devem ser inferidas da memória.

## Artefatos V2 preservados

| Artefato | Papel | SHA-256 |
| --- | --- | --- |
| `v2/apps/switch/switch-auto-v2.nro` | último artefato testado; ainda não reproduzido após `0eef28c` | `28AF1BD161FC50094092281B4E7E22F9813DB521A18D6B04F735EEC5AA55D7D3` |
| APK testado, preservado somente pelo hash | último artefato instalado/testado antes do rebuild | `C4BB5AD12FBFD5EDD50B4F5D3750264A91B635786E1C567A661D7028B8244785` |
| `v2/apps/android-companion/app/build/outputs/apk/debug/app-debug.apk` | rebuild limpo a partir de `0eef28c` | `3CE975F0412DD3029C32647C9D79327F8CC9767143C23BD7EEB7FC1A2F2CD97B` |

O rebuild limpo do APK foi executado com:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
.\apps\android-companion\gradlew.bat `
  -p .\v2\apps\android-companion clean :app:assembleDebug --no-daemon
```

Resultado: sucesso em `1m11s`, acima do teto operacional de um minuto. O hash
divergiu do artefato testado, portanto o APK debug não é tratado como build
reprodutível byte a byte; assinatura/timestamps do pacote precisam ser
investigados se reprodutibilidade binária for exigida.

O NRO ainda precisa ser reconstruído com:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\build-switch.ps1 -Generation v2
```

PREP-01 permanece em `review` até registrar toolchain, duração e hash desse
rebuild. Os binários são evidência local e não precisam ser versionados.

## Controle funcional — DHU

### Pré-condições

1. PC e telefone na mesma rede IPv4. No teste original o telefone usou
   `192.168.15.5/24`; o endereço não é requisito do produto.
2. Depuração por Wi-Fi ativa.
3. Na tela de pareamento, registrar endpoint temporário e código; fora do modal,
   registrar o endpoint principal de conexão.
4. Parear e conectar explicitamente:

   ```powershell
   $adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
   & $adb -P 5038 pair '<telefone>:<porta-pareamento>'
   & $adb -P 5038 connect '<telefone>:<porta-conexao>'
   & $adb -P 5038 devices -l
   ```

5. Selecionar o serial mostrado por `devices`; não assumir o primeiro quando
   houver mais de um.
6. Coletar versões Android e Android Auto:

   ```powershell
   & $adb -P 5038 -s '<serial>' shell getprop ro.build.version.release
   & $adb -P 5038 -s '<serial>' shell getprop ro.build.version.sdk
   & $adb -P 5038 -s '<serial>' shell dumpsys package `
     com.google.android.projection.gearhead
   & $adb -P 5038 -s '<serial>' shell dumpsys package com.waze
   & $adb -P 5038 -s '<serial>' shell dumpsys package com.google.android.apps.maps
   & $adb -P 5038 -s '<serial>' shell dumpsys package com.spotify.music
   ```

7. Android Auto em modo desenvolvedor.
8. “Iniciar servidor da unidade principal” ativo.
9. Companion e NRO fechados para evitar duas head units concorrentes.
10. Configuração DHU disponível em `tools/dhu-720p30.ini` ou copiada para
    `%USERPROFILE%\.android\headunit.ini`.
11. Registrar banda Wi-Fi, RSSI do telefone e do Switch, RTT e perda durante a
    mesma janela de teste. A execução não é comparativa enquanto esses campos
    estiverem ausentes.

### Túnel e inicialização

```powershell
$adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
& $adb -P 5038 devices -l
& $adb -P 5038 -s '<serial>' forward tcp:5277 tcp:5277
& "$env:LOCALAPPDATA\Android\Sdk\extras\google\auto\desktop-head-unit.exe"
```

Quando um servidor ADB alternativo for usado, o DHU e o comando `forward`
precisam compartilhar a mesma instância/porta do servidor ADB.

### Roteiro

1. confirmar conexão e aguardar 10 segundos na tela inicial;
2. abrir Waze ou Maps;
3. arrastar o mapa por 15–20 segundos;
4. pesquisar ou selecionar um destino;
5. alternar entre mapa e Spotify;
6. executar controles de mídia;
7. realizar uma sequência realista de toques;
8. observar por pelo menos 5 minutos e registrar quedas, freezes e atraso de
   input.

### Resultado observado

No DHU oficial, com o mesmo telefone e rede usados no Switch Auto, vídeo e
input apresentaram a fluidez esperada para uso real. Não foram observados os
congelamentos recorrentes nem as quedas provocadas pelo host V2.

## Baseline V2

Capacidades comprovadas:

- handshake e service discovery Android Auto;
- notificação “conectado ao veículo”;
- vídeo H.264 720p;
- decode NVTEGRA e apresentação Deko3D/NV12;
- toque simples e arrasto;
- relay Android;
- reconexão;
- encerramento sem panic após correções de lifecycle.

Limitações observadas:

- cadência de vídeo instável e em rajadas;
- atrasos e queda de sessão sob interação;
- responsabilidades concentradas no host monolítico;
- métricas confundindo pacotes, access units e apresentações;
- políticas de pacing/buffer/ACK usadas durante experimentos;
- comportamento diferente e inferior ao DHU no mesmo ambiente.

## Hipótese de trabalho da V3

Telefone, Gearhead, rede e hardware de decode são capazes. A V3 deve corrigir
ownership, ordem e independência temporal entre protocolo, input, decode e
apresentação. Esta hipótese deve ser reavaliada por métricas, não tratada como
verdade imutável.
