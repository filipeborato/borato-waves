# 04: Modular warp de fase nos osciladores A/B

**What to build:** Um warp de fase original e musical em A e B, acessível pela GUI, automação e matriz de modulação.

**Blocked by:** 01: Medir o orçamento de CPU por patch.

**Status:** ready-for-human

- [x] Permitir ativar, ajustar e salvar o warp por oscilador sem alterar presets antigos.
- [x] Verificar afinação, continuidade, alias e custo em notas graves/agudas, varredura rápida e diferentes sample rates.
- [ ] Comparar patches secos com e sem warp em volume equivalente.

## Progress

O warp por oscilador tem toggle, dial e destinos 21/22 na matriz, e presets antigos ficam com o warp desligado. O `test_voices` verifica afinação, alias, varredura a 40 Hz e nível (±3 dB) em 44,1/48/96 kHz. Custo: cerca de +27% sobre `extreme_no_routes`. Com o warp nos dois osciladores, o patch extremo a 96 kHz passa do critério de 70% (ver [VOICES.md](../../../docs/VOICES.md)). A comparação seco/warp em volume equivalente ainda precisa ser feita de ouvido. Implementado a pedido apesar de bloqueado pelo 01.
