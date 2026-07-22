# Ferramentas

Os builds nativos usam CMake e o Borealis versionado em `extern/borealis`. Ao
clonar o projeto, inicialize os submódulos com:

```powershell
git submodule update --init --recursive
```

Scripts auxiliares de build, formatação, validação do protocolo e empacotamento
serão mantidos aqui quando as stacks forem definidas.
