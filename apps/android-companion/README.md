# Switch Auto Android Companion — handshake prototype

APK mínimo que abre um servidor TCP local para validar o pareamento com o NRO.

## Requisitos

- Android Studio compatível com Android Gradle Plugin 9.3;
- JDK 17 ou superior (o build foi validado com o JDK 21 do Android Studio);
- Android SDK 36.1 e Build Tools 36.0.0;
- dispositivo Android 8.0 (API 26) ou superior.

## Build

Abra este diretório no Android Studio, sincronize o Gradle e execute o módulo
`app` em um telefone físico. Pela linha de comando, após instalar o toolchain:

```text
gradlew.bat assembleDebug
```

O APK será criado como `app/build/outputs/apk/debug/app-debug.apk`.

## Teste

1. Abra a tela de conexão no Switch e anote o código estável de seis dígitos.
2. Digite esse código no Companion e toque em **Start server**.
3. Conecte o Switch à mesma rede ou ao hotspot do telefone.
4. Toque em **Localizar e conectar** no NRO; o IP é descoberto automaticamente.
5. Observe os eventos `discover.request`, `pair.request`, `hello` e `ping` no log.

Depois do primeiro pareamento, o Companion memoriza o identificador do Switch e
o código correspondente. Não é preciso redigitar o código nas conexões seguintes.

Este protótipo ainda não oferece criptografia de transporte e não deve ser
tratado como o protocolo final.
