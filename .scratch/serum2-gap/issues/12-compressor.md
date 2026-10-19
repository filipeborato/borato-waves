# 12: Adicionar compressor

**What to build:** Compressor com controles na GUI para controlar dinâmica sem esconder picos ou alterar indevidamente o volume percebido.

**Blocked by:** 01: Medir o orçamento de CPU por patch.

**Status:** ready-for-human

- [x] Verificar ataque, release, redução de ganho, bypass e automação com sinais de referência.
- [x] Comparar loudness e picos com e sem processamento e medir CPU em patches representativos.
- [x] Restaurar o mesmo comportamento após salvar/reabrir projeto e preset.

## Progress

Compressor na aba EFFECTS (06), último da cadeia:
- **Controles:** THRESHOLD (−48–0 dB), RATIO (1–20), ATTACK (0,1–100 ms), RELEASE (10–1000 ms) e MAKEUP (0–24 dB).
- **Detecção:** pico estéreo-ligado, knee suave de 6 dB, sem makeup automático. O ganho só desce; só o MAKEUP sobe o nível.
- **Gráfico:** curva de transferência atual, mais o ponto da redução em tempo real e a redução em dB.
- **Testes (`test_effects`):**
  - curva estática com valores conhecidos;
  - ataque e release chegam a 63% em uma constante de tempo;
  - um seno estável assenta a até 1 dB da curva (+0,5 dB de ripple do detector);
  - nenhuma amostra sai mais alta que a entrada;
  - automação sem degraus acima do sinal seco com o makeup;
  - fade de bypass e bit-idêntico quando desligado;
  - estado do host e preset restauram o mesmo som.
- **Loudness e picos (pluck a −24 dB, 4:1):**
  - ataque de 10 ms: pico −2,6 dB, loudness −8,0 dB;
  - ataque de 0,1 ms: pico −14,3 dB.
- **CPU:** abaixo de 1 ponto a 44.1/48 kHz (`docs/CPU_PROFILING.md`).
- **Pendente:** ouvir e verificar em host.
