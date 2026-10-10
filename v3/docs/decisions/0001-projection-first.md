# ADR 0001 — Android Auto projection first

- Estado: Aceita
- Data: 2026-10-09
- Escopo: Switch Auto V3

## Contexto

A V1 validou pareamento, Companion, widgets, GPS, mídia, mapas e customização. A
V2 validou handshake Android Auto, vídeo H.264, NVTEGRA, apresentação Deko3D,
toque e reconexão. Porém, a evolução incremental acumulou responsabilidades em
um host monolítico e tornou difícil distinguir gargalos de transporte,
protocolo, decode e renderização.

O teste com o Desktop Head Unit oficial, usando o mesmo telefone e a mesma rede,
foi fluido. Isso demonstra que telefone, Gearhead e rede são capazes de entregar
a experiência esperada; a diferença está na implementação da head unit.

## Decisão

A V3 terá um único objetivo até sua qualificação: ser uma head unit Android Auto
estável em `720p30`.

O projeto será reconstruído em componentes pequenos e testáveis. O Android
Companion será um relay TCP transparente. O Switch terá um único proprietário
do protocolo/TLS, decode assíncrono e apresentação direta de superfícies NV12.

V1 e V2 permanecem preservadas como histórico, referência e fonte de fixtures.
Código antigo só será portado quando houver:

1. responsabilidade claramente definida;
2. teste de contrato ou integração;
3. ausência de dependência em funcionalidades adiadas;
4. evidência de que a implementação é necessária.

## Consequências

- a V3 começa com menos funcionalidades que a V2;
- widgets e personalização não bloqueiam a projeção;
- o primeiro alvo é 30 fps estáveis, não picos de 60 fps;
- diagnóstico passa a fazer parte do desenho, sem I/O em disco no hot path;
- parâmetros de desempenho deixam de ser usados como correção arquitetural;
- o host monolítico não será copiado para a V3.

## Fora de escopo

- certificação Google ou apresentação do projeto como produto certificado;
- reprodução de áudio no Switch no primeiro release;
- captura de microfone no primeiro release;
- compatibilidade binária com componentes internos da V1/V2;
- carregamento de widgets externos.
