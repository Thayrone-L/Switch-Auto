# Switch Auto Simulator

Simulador Windows que executa o mesmo Core, Widget Registry, widgets e renderer
usados pelo NRO. O framebuffer lógico é sempre `1280×720`.

## Build e execução

Na raiz do repositório:

```powershell
.\tools\build-simulator.ps1
.\apps\simulator\build\switch-auto-simulator.exe
```

Controles:

- `↑`/`↓`: altera velocidade;
- `B`: altera bateria;
- `C`: conecta ou desconecta o Companion simulado.

Para gerar um snapshot determinístico sem abrir a janela:

```powershell
.\apps\simulator\build\switch-auto-simulator.exe --snapshot
```

O simulador valida layout e renderização compartilhada. Hardware, desempenho e
integrações libnx continuam sendo validados no Switch real.
