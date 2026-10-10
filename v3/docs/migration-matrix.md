# Switch Auto V3 — Matriz de migração V1/V2

Nenhum item é migrado apenas porque já existe. As categorias são:

- **Portar:** extrair, adaptar ao contrato V3 e cobrir por teste.
- **Reescrever:** preservar comportamento conhecido, não a implementação.
- **Referência:** consultar, mas não criar dependência.
- **Adiar:** não entra antes da qualificação.
- **Descartar:** experimento que não pertence ao desenho V3.

| Componente existente | Decisão | Condição |
| --- | --- | --- |
| `AndroidAutoFrameCodec` | Portar | Testes de fragmentação, limites e malformed input primeiro |
| Negociação de versão | Portar | Isolar de socket e máquina de sessão |
| Certificados/handshake validado | Portar | Encapsular em `LibnxTlsEngine`; revisar material antes de publicar |
| `socketpair` para SSL libnx | Portar | Ficar invisível ao core e possuir timeout/cancelamento |
| Payloads de service discovery | Portar | Um tipo/teste golden por mensagem |
| Serialização de toque | Portar | Golden tests e timestamps na borda |
| `NvTegraH264Decoder` | Portar | Implementar `IVideoDecoder` e provar cleanup |
| Shader/importação NV12 | Portar | Sem CPU readback e com teste sintético |
| Renderer Deko3D V2 | Portar parcialmente | Separar presenter, lifecycle e diagnóstico |
| Surface mailbox global | Reescrever | Ownership local; `takeLatest()` semanticamente correto |
| `SwitchAndroidAutoHost` | Referência | Nunca copiar inteiro |
| `AsyncVideoDecoder` interno | Reescrever | Fila de access units completos e adapter dedicado |
| Máquina conceitual `AndroidAutoSession` | Referência | Nova state machine com tabela e testes |
| Android relay transparente | Portar | Remover IP fixo, parsing, pacing e log síncrono |
| `V2Log.appendText()` no relay | Descartar | Substituir por contadores/ring e snapshot assíncrono |
| Métricas agregadas no loop AA | Descartar | Reimplementar fora do hot path |
| Pacing do Companion | Descartar | Relay não altera timing ou ordem |
| `max_unacked` experimental | Referência | Valor definido somente após teste de protocolo |
| Software decode/render | Adiar | Ferramenta diagnóstica, nunca produção |
| Borealis | Adiar | Somente após Gate M5 |
| Widgets e temas | Adiar | Somente após Gate M5 |
| MapLibre/OSRM | Adiar | Fora do núcleo Android Auto |
| `MediaProjection` | Descartar da V3 | Histórico permanece na V1 |
| Protocolo JSON Companion V1 | Não migrar | Novo plano de controle será definido depois |
| Scripts de build/ADB/FTP | Portar | Nomes e destinos próprios da V3 |
| Configuração DHU | Portar | Remover dados locais e documentar baseline |
| Testes em `core_tests.cpp` | Referência | Dividir por componente; não criar nova suíte monolítica |

## Procedimento para portar um item

1. declarar a tarefa e o owner;
2. criar teste de caracterização ou fixture;
3. extrair apenas a responsabilidade necessária;
4. substituir dependências legadas por contratos V3;
5. executar teste desktop;
6. executar teste de plataforma quando aplicável;
7. registrar diferenças intencionais no handoff.

Se o item não pode seguir esse procedimento, ele permanece como referência.
