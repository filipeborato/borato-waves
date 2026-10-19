# 11: Adicionar equalizador

**What to build:** EQ com controles na GUI para ajustar a cor final do patch e persistir o estado no host.

**Blocked by:** 01: Medir o orçamento de CPU por patch.

**Status:** ready-for-human

- [x] Validar resposta em frequência, estabilidade em extremos e automação sem cliques.
- [x] Comparar áudio processado e bypass em volume equivalente e medir CPU.
- [x] Restaurar controles e som após salvar/reabrir projeto e preset.

## Progress

EQ na aba EFFECTS (05), depois do reverb:
- **Bandas:** low shelf em 200 Hz, bell (FREQUENCY 40 Hz–16 kHz, GAIN, Q 0,3–8) e high shelf em 5 kHz, cada uma ±18 dB.
- **Gráfico:** mostra a resposta real das configurações atuais.
- **Filtro:** SVF trapezoidal (Simper), estável enquanto se move. Enquanto um ajuste desliza, os coeficientes são recalculados a cada amostra; atualizar a cada 16 amostras gerava zipper de 1,16 com shelves em +18 dB.
- **Testes (`test_effects`):**
  - plano é plano, o bell atinge o ganho e os shelves atingem o deles longe da quina;
  - o ganho medido com senos bate com a resposta em 44.1/48/96 kHz, inclusive nos extremos;
  - saltos e automação aleatória não passam dos degraus do som estático mais impulsionado;
  - quando desligado, o sinal sai bit-idêntico;
  - o mesmo som volta pelo estado do host e por arquivo de preset.
- **Volume equivalente:** num pad, +3/−4/+5 dB muda o nível em +0,3 dB. Depois de igualar o nível, a diferença fica 9,3 dB abaixo do sinal.
- **CPU:** abaixo de 1 ponto a 44.1/48 kHz (`docs/CPU_PROFILING.md`).
- **Correção junto:** depois de `prepare`, efeitos ligados começam ligados, sem fade-in de 20 ms, e os smoothers começam nos valores atuais.
- **Pendente:** ouvir e verificar em host.
