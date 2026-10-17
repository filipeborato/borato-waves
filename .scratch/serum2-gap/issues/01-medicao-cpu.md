# 01: Medir o orçamento de CPU por patch

**What to build:** Um procedimento repetível para medir o custo de notas, unison, modulação e efeitos em 44,1/48/96 kHz no Standalone e em um host VST3.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] Registrar máquina, buffer, sample rate, patch, polifonia, tempo de processamento e eventuais falhas de áudio.
- [ ] Comparar casos leves e o cenário extremo existente, distinguindo render offline de reprodução em dispositivo.
- [ ] Registrar margem de CPU que permita avaliar novos motores e um eventual OSC C.
