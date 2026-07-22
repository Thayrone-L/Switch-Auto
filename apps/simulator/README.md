# Switch Auto Simulator

Aplicação Windows que executa a mesma interface Borealis, os mesmos XMLs e o
mesmo contrato de serviços usados pelo NRO. Ela permite iterar no layout sem
copiar arquivos para o console a cada alteração.

## Build e execução

Na raiz do repositório:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-simulator.ps1
.\apps\simulator\build\switch-auto-simulator.exe
```

O artefato precisa da pasta `resources` ao seu lado. O script copia ambos para
`apps/simulator/build/`.

O simulador valida layout, navegação e renderização compartilhada. Hardware,
desempenho e integrações libnx continuam sendo validados no Switch real.
