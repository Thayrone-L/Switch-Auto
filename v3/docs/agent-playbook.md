# Switch Auto V3 — Guia para múltiplos agentes

## 1. Objetivo

Permitir execução paralela sem duplicar decisões, alterar contratos de forma
silenciosa ou concentrar novamente responsabilidades em um integrador.

Após `PREP-02`, só podem avançar em paralelo trabalhos sem dependência nos
contratos compartilhados: scaffolding de build, definição dos próprios
contratos, harness inicial e esqueleto do relay Android. Protocol, Video, Input
e adapters que consomem contratos aguardam `M0-02` e `M0-03` integrados.

## 2. Papéis

Com quatro agentes simultâneos, usar no máximo:

1. **Integrador** — coordenação, build, composição e gates;
2. **Protocol/Session** — framing, TLS orchestration, scheduler e sessão;
3. **Video/Switch** — access units, NVTEGRA, superfícies e presenter;
4. **Android/Input/Verification** — escolhido conforme o marco atual.

O integrador não assume trabalho de outro workstream enquanto coordena uma onda.
Falhas voltam ao owner original.

## 3. Ownership de diretórios

| Workstream | Escrita exclusiva |
| --- | --- |
| Integração | `v3/CMakeLists.txt`, `v3/apps/switch/`, scripts `*v3*`, `v3/docs/status.md` |
| Base/Contracts | `v3/core/base/`, headers de contrato compartilhados |
| Protocol/Session | `v3/core/transport/`, `v3/core/protocol/`, `v3/core/session/`, testes correspondentes |
| Switch Platform | `v3/platform/switch/transport/`, `v3/platform/switch/tls/`, `v3/platform/switch/lifecycle/` |
| Video/Switch | `v3/core/video/`, `v3/platform/switch/video/`, `v3/platform/switch/render/`, `v3/tests/video/` |
| Input/Switch | `v3/core/input/`, `v3/platform/switch/input/`, `v3/tests/input/` |
| Android Relay | `v3/apps/android-relay/`, testes do relay |
| Diagnóstico | `v3/core/diagnostics/`, `v3/tests/diagnostics/` |
| Verification | `v3/tests/support/`, `v3/tests/fixtures/`, `v3/tests/replay/`, `v3/tests/faults/`, `v3/tests/integration/` |

Apenas o integrador edita arquivos CMake globais e `status.md`. Um agente que
precisa de mudança fora de seu ownership registra o pedido no handoff; não faz
uma correção lateral.

Fixtures compartilhadas são escritas apenas por Verification. O owner de um
módulo pode propor a captura e consumir a fixture aprovada, mas não altera o
artefato compartilhado durante a implementação.

## 4. Modelo obrigatório de tarefa

```md
### M1-02 — Implementar frame codec

Estado: ready
Owner: Protocol
Dependências: M0-03

Diretórios permitidos:
- v3/core/protocol/
- v3/tests/protocol/

Objetivo:
Decodificar frames AA a partir de um stream arbitrariamente fragmentado.

Entradas:
- contratos congelados
- fixtures anonimizadas

Entregáveis:
- FrameDecoder
- erros tipados
- testes unitários

Não inclui:
- TLS
- socket real
- máquina de sessão

Critérios de aceite:
- aceita byte a byte e múltiplos frames por leitura;
- rejeita tamanho inválido;
- memória permanece limitada;
- testes desktop passam.

Comando de validação:
<comando determinístico>
```

Estados permitidos:

```text
blocked → ready → in_progress → review → done
```

`done` exige código, teste, evidência e gate aprovados.

## 5. Regras de trabalho

1. Uma tarefa possui um único owner.
2. Cada agente edita apenas os diretórios listados na tarefa.
3. Interfaces compartilhadas são congeladas por marco.
4. Mudança de interface exige proposta curta ao integrador e atualização de
   contrato antes da implementação dependente.
5. Testes vivem ao lado do workstream; não criar um novo arquivo monolítico de
   testes.
6. Não copiar arquivos inteiros da V2. Portar funções pequenas com teste de
   caracterização.
7. Não alterar constantes de performance sem hipótese e métricas antes/depois.
8. Experimentos usam flag explícita e são removidos ou promovidos antes do gate.
9. Um commit representa uma tarefa e inclui seu ID.
10. Integração segue dependências, nunca a ordem em que agentes terminaram.

## 6. Handoff obrigatório

Todo agente encerra a tarefa com:

```md
Tarefa: Mx-yy
Estado: review
Commit: <sha ou working tree declarado>
Arquivos alterados: <lista>
Contratos usados/alterados: <lista>
Testes executados: <comandos e resultado>
Evidência: <logs/métricas/fixture>
Procedência: <código, fixture e dependências adicionadas ou "não se aplica">
Riscos conhecidos: <lista>
Próxima dependência desbloqueada: <id>
```

Comentários como “funcionou aqui” não substituem evidência reproduzível.

## 7. Fluxo do integrador

Para cada onda:

1. confirmar que dependências estão `done`;
2. congelar interfaces da onda;
3. atribuir diretórios sem sobreposição;
4. iniciar agentes;
5. receber handoffs sem fazer merge por ordem de chegada;
6. revisar ownership, bloqueios, alocações por frame, disco e shutdown;
7. integrar na ordem das dependências;
8. executar testes completos e gate do marco;
9. atualizar `status.md`;
10. só então abrir a onda seguinte.

## 8. Ondas recomendadas

### Onda A — Fundação

- Integrador: build V3 e targets.
- Contracts: tipos base e interfaces.
- Verification: fakes e harness.
- Android: APK relay mínimo compilável.

### Onda B — Sessão

- Protocol: framing e reassembly.
- Session: scheduler, state machine e timeouts.
- Switch platform: transporte e TLS adapter.
- Android: relay duplex e teste byte-for-byte.

### Onda C — Vídeo

- Protocol: handler e ACK de vídeo.
- Video core: access units e fila.
- Switch video: NVTEGRA.
- Switch render: Deko3D e mailbox.

### Onda D — Input e observabilidade

- Input core: serialização e gesture queue.
- Switch input: touch/HID.
- Diagnostics: métricas em memória.
- Verification: testes de carga e fault injection.

### Onda E — Resiliência e qualificação

- Um agente executa cenário e coleta evidência.
- Um agente analisa métricas sem editar código.
- Owners corrigem falhas nos próprios módulos.
- Integrador repete o gate completo.

## 9. Política de conflito

Se dois agentes precisam do mesmo arquivo:

1. parar a edição concorrente;
2. extrair um contrato menor controlado pelo integrador;
3. integrar o contrato;
4. retomar tarefas em diretórios separados.

Não resolver conflito Git escolhendo “ours/theirs” sem revisar semântica.

## 10. Uso de V1/V2

Permitido:

- consultar comportamento;
- extrair fixture anonimizada;
- portar função pequena com teste;
- comparar performance;
- reutilizar script após adaptação explícita.

Proibido:

- linkar fontes legadas silenciosamente;
- copiar o host monolítico;
- importar widget/dashboard na fundação;
- usar logs brutos com dados pessoais;
- transformar parâmetro experimental da V2 em default da V3 sem validação.
