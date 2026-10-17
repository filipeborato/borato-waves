# 13: Decidir se um OSC C agrega valor agora

**What to build:** Uma decisão documentada sobre implementar um terceiro slot de fonte, apoiada em patches comparáveis e medições de execução.

**Blocked by:** 01: Medir o orçamento de CPU por patch; 05: Criar interação FM/phase distortion entre A/B; 06: Reproduzir samples como fonte sonora.

**Status:** ready-for-agent

- [ ] Construir exemplos musicais que comparem A/B+sub/noise com a necessidade de uma terceira fonte flexível.
- [ ] Estimar custo em CPU, estado/presets, modulação, roteamento e espaço de GUI em 48/96 kHz.
- [ ] Registrar decisão e critérios de implementação; não criar um OSC C somente wavetable sem ganho demonstrado.
