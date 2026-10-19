# 09: Desfazer e refazer edições de timbre

**What to build:** Undo/redo de alterações feitas na interface para que o músico possa experimentar e voltar ao estado anterior.

**Blocked by:** None (can start immediately).

**Status:** ready-for-human

- [x] Cobrir parâmetros, curvas e tabelas editadas, agrupando um gesto contínuo em uma ação compreensível.
- [ ] Manter GUI e áudio sincronizados após desfazer/refazer, sem corromper presets ou automação do host.
- [x] Verificar a sequência editar → desfazer → refazer → salvar → reabrir.

## Progress

UNDO/REDO no topo, mais Ctrl+Z e Ctrl+Shift+Z (ou Ctrl+Y):
- **Passos:** cada gesto da interface é um passo: arrastar um knob, um traço no editor de tabela, um arrasto na curva do LFO. Cliques compostos também são um passo cada: rota por arraste, troca de tabela, importação, carga de preset.
- **Agrupamento:** giros da roda e setas no mesmo controle, com menos de 0,5 s entre si, viram um passo só.
- **O que cobre:** parâmetros, curvas, nomes de macro, tabelas de usuário e sample.
- **O que não grava:** automação do host não é gravada, e o undo só volta parâmetros que o passo tocou. Um setState do host começa um histórico novo.
- **Limites:** 100 passos e 256 MB de tabelas e samples guardados.
- **Testes (`test_history`):** editar → desfazer → refazer, com o áudio igual em cada etapa, depois salvar e reabrir como estado e como preset.
- **Pendente:** verificar em host com automação gravando.
