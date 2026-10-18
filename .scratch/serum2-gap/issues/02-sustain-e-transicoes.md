# 02: Validar sustain e transições de voz

**What to build:** Notas sustentadas por pedal e troca de vozes previsíveis, sem notas presas nem cliques audíveis.

**Blocked by:** None (can start immediately).

**Status:** ready-for-human

- [x] Cobrir pedal pressionado/solto, note-off, all-notes-off e roubo de voz com sequências MIDI determinísticas.
- [ ] Confirmar ausência de notas presas e de descontinuidade audível nas transições em Standalone e VST3.

## Progress

O `tests/test_voices.cpp` cobre pedal pressionado/solto, note-off, all-notes-off e roubo de voz em 24 fases, mais um fuzz MIDI determinístico em cada modo de voz, que precisa terminar em silêncio exato. O roubo agora passa a última amostra da voz para um fade de 5 ms (ver [VOICES.md](../../../docs/VOICES.md)). Falta ouvir no Standalone e no VST3.
