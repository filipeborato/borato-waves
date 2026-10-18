# 01: Medir o orçamento de CPU por patch

**What to build:** Um procedimento repetível para medir o custo de notas, unison, modulação e efeitos em 44,1/48/96 kHz no Standalone e em um host VST3.

**Blocked by:** None (can start immediately).

**Status:** ready-for-human

- [ ] Registrar máquina, buffer, sample rate, patch, polifonia, tempo de processamento e eventuais falhas de áudio.
- [x] Comparar casos leves e o cenário extremo existente, distinguindo render offline de reprodução em dispositivo.
- [x] Registrar margem de CPU que permita avaliar novos motores e um eventual OSC C.

## Progress

O benchmark reproduzível, resultados em Ryzen 7 5700X e critério provisório de headroom estão em [CPU_PROFILING.md](../../../docs/CPU_PROFILING.md). Falta executar o roteiro com dispositivo ASIO real no Standalone e no REAPER/VST3 e registrar driver, CPU em tempo real e glitches observados. Os números offline não substituem essa verificação.
