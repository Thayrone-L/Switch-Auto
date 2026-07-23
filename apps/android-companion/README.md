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

As sessões possuem timeout de I/O de 8 segundos. As esperas de clientes e
descoberta são verificadas a cada segundo para que parar o servidor ou fechar o
aplicativo cancele o transporte rapidamente.

O log do Android fica no diretório externo privado do aplicativo, no arquivo
`switch-auto.log`. O arquivo é rotacionado ao atingir 1 MiB e nunca registra o
token de pareamento.

As linhas de uma conexão incluem `sessionId`. O último resultado também fica em
`diagnostics/latest.json`, com schema versionado e sem segredos. Credenciais com
tipo ou formato inválido são removidas durante a recuperação das preferências.

## Capacidades e permissões

O Companion centraliza permissões em `AndroidPermissionManager`. Localização,
estado e controle de chamadas, histórico de chamadas, notificações e execução
em segundo plano possuem estados normalizados para o Core. Nenhuma permissão
sensível é solicitada ao abrir o aplicativo; cada recurso deverá explicar a
necessidade e pedir acesso somente quando o usuário o ativar. Acesso de mídia
que depende de uma tela especial do sistema é informado como
`requires_settings`.

## Widgets do Companion

Os recursos fornecidos pelo celular também seguem um contrato de widgets. O
`CompanionWidgetRegistry` registra os widgets, controla seu ciclo de vida e reúne
as contribuições de telemetria antes de enviá-las ao Switch. Widgets não escrevem
diretamente no socket.

- `core.battery`: lê a bateria do Android, recebe a bateria do Switch e mantém o
  campo `phoneBatteryPercent` compatível com versões anteriores.
- `core.gps`: solicita localização somente quando habilitado, combina os
  provedores GPS e rede e publica posição, precisão, velocidade, direção e horário.

O GPS começa desabilitado. Nesta fase ele funciona enquanto o processo do
Companion está ativo. A etapa seguinte será mover transporte e localização para
um foreground service, permitindo operação previsível com a tela bloqueada.

O mapa inicial usa MapLibre Native e `MapSnapshotter`. O Android renderiza um
quadro 640×360 centrado na posição atual, comprime em JPEG e o envia ao Switch
como payload binário do protocolo v3. O JSON transporta apenas metadados e limita
cada quadro a 512 KiB. O estilo público `demotiles` é exclusivo do protótipo e
deve ser substituído por um provedor de tiles apropriado antes de produção.

Este protótipo ainda não oferece criptografia de transporte e não deve ser
tratado como o protocolo final.
