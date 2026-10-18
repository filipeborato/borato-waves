# 07: Editar frames e harmônicos de wavetable

**What to build:** Edição própria de frames e harmônicos na interface, com resultado imediatamente audível e publicação segura da tabela.

**Blocked by:** None (can start immediately).

**Status:** ready-for-human

- [x] Editar, salvar, fechar e reabrir uma tabela sem perder os dados ou interromper notas ativas.
- [x] Validar limites de amplitude, continuidade de ciclo e ausência de trabalho bloqueante no áudio.
- [ ] Incluir teste determinístico do resultado sonoro e verificação interativa da GUI.

## Progress

Aba EDITOR:
- **Edição:** desenhar o ciclo, 32 barras de harmônicos e +/- frame, em A ou B. Cada edição é publicada como tabela de usuário (no máximo 15 vezes por segundo e no mouse-up), com crossfade de 15 ms sob notas tocando.
- **Frame e POSITION:** o frame editado é o que o oscilador toca. Trocar de frame move o POSITION, e mover o POSITION troca o frame do editor. Antes disso, editar um frame diferente do POSITION parecia não ter efeito.
- **Salvar:** a tabela vai no estado e nos presets, e também há EXPORT WAV.
- **Garantias:** frames normalizados (pico 1, sem DC, ciclo fechado). A thread de áudio só troca ponteiros.
- **Testes (`test_table_editor`):** razão de harmônicos ouvida, ausência de alias, edições ao vivo sem queda e liberação de memória, e ida e volta de estado/WAV.
- **GUI:** dirigida por mouse e teclas simulados, com screenshots.
- **Pendente:** uso interativo real no Standalone (por isso o último item continua aberto).
