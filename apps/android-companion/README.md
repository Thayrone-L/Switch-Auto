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

1. Abra a tela de conexão no Switch para exibir o QR de pareamento.
2. Toque em **Scan Switch QR** no Companion e leia o código.
3. O servidor Android inicia automaticamente após validar o QR.
4. Conecte o Switch à mesma rede ou ao hotspot do telefone.
5. Toque em **Localizar e conectar** no NRO; o IP é descoberto automaticamente.
6. Observe os eventos `discover.request`, `pair.request`, `hello` e `ping` no log.

Depois do primeiro pareamento, o Companion memoriza o identificador e o token do
Switch. Não é preciso ler novamente o QR nas conexões seguintes. O servidor é
sempre iniciado automaticamente quando o aplicativo for aberto, mesmo sem um
pareamento; dispositivos desconhecidos continuam sendo rejeitados.

O botão **Forget paired Switch** revoga todas as credenciais conhecidas e encerra
as sessões ativas. Um novo QR precisa ser lido para permitir outra conexão.

O log do Android fica no diretório externo privado do aplicativo, no arquivo
`switch-auto.log`. O arquivo é rotacionado ao atingir 1 MiB e nunca registra o
token de pareamento.

Este protótipo ainda não oferece criptografia de transporte e não deve ser
tratado como o protocolo final.
