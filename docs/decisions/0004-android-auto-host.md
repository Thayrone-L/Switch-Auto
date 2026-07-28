# ADR 0004 — Android Auto como fonte de projeção

- Estado: Aceita
- Data: 2026-07-28
- Escopo: Switch, Android Companion, navegação, mídia e comunicação

## Contexto

O Switch Auto validou a comunicação bilateral entre o Nintendo Switch e o
Android, incluindo pareamento, descoberta, autenticação, reconexão, telemetria,
localização, mídia, mapas e transferência de imagens.

Como prova de conceito, o Companion também passou a:

- pesquisar endereços e calcular rotas com MapLibre e OSRM;
- abrir o destino correto no Waze por deep link;
- capturar a tela do Android com `MediaProjection`;
- transmitir quadros JPEG em 640 × 360 para o widget GPS.

O experimento comprovou que é possível exibir o Waze no Switch, mas também
evidenciou limitações estruturais:

- o Android encerra a projeção ao bloquear a tela;
- a captura preserva a orientação e a proporção do display físico;
- `MediaProjection` não fornece um canal de entrada;
- notificações, chamadas, áudio, microfone e comandos de voz exigiriam
  integrações paralelas;
- reproduzir a experiência automotiva completa dessa forma duplicaria grande
  parte das responsabilidades do Android Auto.

## Decisão

O Switch Auto passa a tratar o Android Auto como a fonte preferencial de
projeção automotiva.

O Nintendo Switch deverá atuar como um host Android Auto não certificado,
inicialmente experimental, responsável por:

- descoberta, negociação e sessão do protocolo;
- recepção e decodificação de vídeo;
- reprodução dos canais de áudio;
- envio de toque e controles físicos;
- integração futura de microfone, chamadas e comandos de voz.

O Android Companion permanece como plano de controle do Switch Auto. Ele não
será substituído pelo Android Auto e continuará responsável por:

- pareamento e configuração do Switch Auto;
- temas, layouts, wallpapers e telas de descanso;
- widgets e integrações exclusivas da plataforma;
- diagnóstico, logs, sincronização e recuperação;
- recursos futuros, como mapas offline.

O Core do Switch compõe a projeção com a interface Borealis. A projeção poderá
ocupar toda a tela ou uma região do dashboard, acompanhada de widgets locais.

MapLibre permanece como fallback local quando o Android Auto estiver
indisponível. A captura por `MediaProjection` permanece no código apenas como
prova de conceito e ferramenta de diagnóstico, desativada por padrão em uma
versão futura.

## Limites

- temas do Switch Auto não modificam internamente Waze, Google Maps ou a
  interface projetada pelo Android Auto;
- o projeto não se apresenta como implementação certificada pelo Google;
- nenhuma dependência comunitária será incorporada antes da análise de licença,
  manutenção e compatibilidade com devkitPro/libnx;
- recursos nativos continuam funcionando de forma degradável sem Android Auto
  ou sem Companion.

## Próxima prova de conceito

A viabilidade será avaliada em quatro marcos:

1. anunciar ou disponibilizar o Switch como head unit;
2. concluir handshake e abertura da sessão com um telefone Android;
3. receber e exibir o primeiro quadro de vídeo no NRO;
4. devolver um toque do Switch ao Android Auto.

Somente após esses marcos serão integrados áudio, microfone, chamadas e
reconexão automática.

## Consequências

- o trabalho deixa de criar integrações específicas para cada app de navegação;
- Waze, Maps, chamadas, notificações e mídia passam a usar a experiência
  automotiva fornecida pelo telefone;
- o NRO ganha responsabilidades de protocolo e decodificação de mídia;
- o Companion se concentra na camada extra que diferencia o Switch Auto;
- referências comunitárias como AASDK, Headunit Revived e OpenAutoLink devem ser
  estudadas, mas o Core preservará interfaces próprias para evitar acoplamento.
