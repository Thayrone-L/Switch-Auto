# Switch Auto V3

A V3 é uma reconstrução focada da head unit Android Auto para Nintendo Switch.
Ela usa a V1 e a V2 como fonte de aprendizado e fixtures, mas não continua o
host monolítico existente.

## Objetivo do primeiro release

Entregar uma sessão Android Auto em `1280x720 @ 30 fps` que seja estável,
responsiva e recuperável:

- vídeo H.264 decodificado por NVTEGRA/NVDEC;
- apresentação NV12 sem cópia de quadros pela CPU;
- toque simples e arrasto sem depender do ritmo do vídeo;
- navegação, mídia e troca de aplicativos sem reiniciar a sessão;
- encerramento limpo, sem panic do Atmosphère;
- reconexão automática após perda do telefone, relay ou Wi-Fi.

O experimento de `720p60` só começa depois da qualificação completa de
`720p30`.

## Limite de produto

Fazem parte da primeira entrega:

- Android relay transparente;
- transporte, framing, TLS e sessão Android Auto;
- canais mínimos exigidos pelo protocolo;
- vídeo, input, lifecycle, diagnóstico e recuperação;
- aplicação Switch fullscreen mínima.

Ficam adiados:

- widgets, temas, animações e dashboard;
- MapLibre, OSRM e mapas offline;
- `MediaProjection`;
- áudio e microfone no Switch;
- personalização pelo Companion;
- protocolo e serviços do Companion V1;
- SDK público e plugins.

Áudio pode ser anunciado e reconhecido apenas quando necessário para manter a
sessão Android Auto saudável. O som permanece no telefone.

## Documentos normativos

- [Arquitetura](docs/architecture.md)
- [Plano de execução](docs/execution-plan.md)
- [Guia para múltiplos agentes](docs/agent-playbook.md)
- [Plano de testes e critérios de aceite](docs/test-plan.md)
- [Matriz de migração V1/V2](docs/migration-matrix.md)
- [Baseline DHU e V2](docs/baseline.md)
- [Inventário de procedência](docs/provenance.md)
- [ADR 0001 — Projection first](docs/decisions/0001-projection-first.md)
- [ADR 0002 — Relay Android transparente](docs/decisions/0002-transparent-android-relay.md)
- [ADR 0003 — Owner único do protocolo](docs/decisions/0003-single-protocol-owner.md)
- [ADR 0004 — Filas por access unit](docs/decisions/0004-complete-access-unit-queues.md)
- [ADR 0005 — Licença e procedência](docs/decisions/0005-license-and-provenance.md)

Em caso de conflito, a ordem de autoridade é:

1. ADRs aceitos da V3;
2. arquitetura da V3;
3. plano de testes da V3;
4. plano de execução;
5. código e documentação de V1/V2, usados somente como referência.

## Como iniciar

1. Preservar o estado atual da V2 em commit e tag próprios.
2. Criar uma branch dedicada para a fundação da V3.
3. Executar o marco `M0` do [plano de execução](docs/execution-plan.md).
4. Só abrir trabalho paralelo depois que contratos e ownership estiverem
   aprovados.
5. Integrar cada tarefa apenas quando seus testes e evidências estiverem
   anexados ao handoff.

## Regra de ouro

Nenhuma otimização por parâmetro substitui uma fronteira arquitetural.
Alterações de FPS, tamanho de buffer, janela de ACK ou pacing precisam partir de
uma medição, declarar uma hipótese e possuir condição explícita de reversão.
