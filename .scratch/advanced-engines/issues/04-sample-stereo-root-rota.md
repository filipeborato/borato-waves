# 04: Tocar sample stereo com root note e rota próprios

**What to build:** Preservar canais L/R de samples stereo, editar `root note`, `pan` e destino (`Filter 1`, `Filter 2`, `Direct`), mantendo o resultado anterior para samples mono e estados antigos. Controles e waveform refletem o asset.

**Blocked by:** 01: Trocar samples validados sem perder recursos em uso.

**Status:** ready-for-human

- [x] Preservar canais L/R no `bw::Sample` e `bw::SamplePlayer`, mantendo compatibilidade de ganho e comportamento para arquivos mono e sessões legadas (`root = 60`).
- [x] Adicionar parâmetros automatizáveis `sampleRoot` (MIDI 0–127, default 60), `samplePan` (-1..1, default 0) e `sampleRoute` (`Filter 1`, `Filter 2`, `Direct`) e atualizar `SamplePanel` (knobs, seletor de rota e waveform stereo L/R).
- [x] Verificar fixture stereo `L = 440 Hz / R = 660 Hz`, erro de pitch na raiz `< 1 cent`, erro de duração sem loop `< 1 sample` (descontados fades), extremos de pan, downmix mono, rota `Direct` vs filtros e contabilização stereo no orçamento agregado de 256 MiB.
