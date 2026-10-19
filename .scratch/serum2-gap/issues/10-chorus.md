# 10: Adicionar chorus

**What to build:** Chorus original com controles visuais, automação e persistência para ampliar pads e leads.

**Blocked by:** 01: Medir o orçamento de CPU por patch.

**Status:** ready-for-human

- [x] Verificar bypass, transições sem clique, estéreo, cauda e estabilidade sob automação.
- [x] Medir CPU e nível de saída com e sem chorus em patches representativos.
- [x] Demonstrar uso em um patch sem sobrescrever conteúdo de fábrica existente.

## Progress

Chorus entre a distorção e o delay, na aba EFFECTS:
- **Controles:** RATE, DEPTH e MIX, mais um gráfico das varreduras L/R.
- **Som:** duas derivações por lado varrem um atraso de 10 ± 4 ms. O lado direito fica um quarto de ciclo atrás.
- **Bypass:** crossfade de 20 ms, e bit-idêntico quando desligado.
- **Testes (`test_effects`):** sem degraus ao ligar, desligar e sob automação aleatória; L≠R com entrada mono; cauda zerada em menos de 16 ms.
- **Nível com mix 0,5:** −2,2 dB numa serra e −0,3 dB num pad.
- **CPU:** cerca de +0,4 a 1 ponto a 44.1/48 kHz (`docs/CPU_PROFILING.md`).
- **Preset:** "Chorus Silk Pad", adicionado ao fim da lista.
- **Pendente:** ouvir e verificar em host.
