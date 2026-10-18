# 05: Criar interação FM/phase distortion entre A/B

**What to build:** Timbres por modulação entre os osciladores A/B, com profundidade controlável e comportamento estável sob automação.

**Blocked by:** 04: Modular warp de fase nos osciladores A/B.

**Status:** ready-for-human

- [x] Expor o controle na GUI, estado e matriz, mantendo compatibilidade com presets antigos.
- [x] Verificar afinação, alias, picos e custo com unison, acordes e 44,1/48/96 kHz.
- [x] Entregar patches que demonstrem timbres não obtidos pelos A/B sem interação.

## Progress

FM de B em A:
- **Controle:** dial `FM FROM B` (`fmDepth`, 0–1), salvo no estado e destino 23 (`FM Depth`) na matriz. Presets antigos abrem com profundidade 0, idênticos.
- **Modulador:** B modula a fase de A com o próprio sinal, antes do level e do on/off. Assim B pode servir só de modulador, mudo.
- **Alias:** o índice é limitado perto de Nyquist e a banda de A é estreitada pela regra de Carson.
- **Testes (`test_voices`):** afinação (1:1 e 2:1), alias até MIDI 103, picos com acordes e unison de 8 lanes, varredura por LFO a 40 Hz e matriz, em 44,1/48/96 kHz.
- **Custo:** `extreme_fm` gasta cerca de 30% a mais que `extreme_no_routes` (ver [CPU_PROFILING.md](../../../docs/CPU_PROFILING.md)).
- **Patches:** FM Glass Keys, FM Iron Bass e FM Bell Tower. O teste mostra que dois senos sem interação não geram harmônicos acima da fundamental, e com FM geram.
- **Limitações:** só há a direção B→A. Um modulador brilhante (saw) ainda gera algum alias.
- **Pendente:** escutar os patches no Standalone ou num host.
