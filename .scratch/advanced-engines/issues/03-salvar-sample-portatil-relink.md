# 03: Salvar sample portátil e relincar referências antigas

**What to build:** Preset portátil que incorpora seu áudio em um container versionado com identidade por conteúdo (hash), limites validados antes de alocações e escrita atômica; sessões antigas com caminho continuam abrindo e a UI permite localizar arquivo ausente (relink) e salvar novamente.

**Blocked by:** 02: Importar e cancelar áudio em segundo plano.

**Status:** ready-for-human

- [x] Definir container versionado (`containerVersion`, `channels`, `frames`, `rate`, `pcmBytes`, `hash` FNV-1a 64-bit e `data` Base64) com escrita atômica em `savePreset`.
- [x] Exportar preset portátil, remover/mover arquivo de origem, importar em nova pasta e reproduzir o mesmo PCM com hash e parâmetros preservados.
- [x] Rejeitar payload truncado, tamanho declarado falso, `containerVersion` desconhecida e hash inválido antes/durante a restauração sem perder o estado atual.
- [x] Cobrir relink de arquivo ausente (UI + API `relinkSample`), `undo`/`redo` e estados legados (sem incorporar arquivos externos não selecionados).
