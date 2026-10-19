# 13: Decidir se um OSC C agrega valor agora

**What to build:** Uma decisão documentada sobre implementar um terceiro slot de fonte, apoiada em patches comparáveis e medições de execução.

**Blocked by:** 01: Medir o orçamento de CPU por patch; 05: Criar interação FM/phase distortion entre A/B; 06: Reproduzir samples como fonte sonora.

**Status:** ready-for-human

- [ ] Construir exemplos musicais que comparem A/B+sub/noise com a necessidade de uma terceira fonte flexível.
- [x] Estimar custo em CPU, estado/presets, modulação, roteamento e espaço de GUI em 48/96 kHz.
- [x] Registrar decisão e critérios de implementação; não criar um OSC C somente wavetable sem ganho demonstrado.

## Progress

A decisão está em `docs/OSC_C_DECISION.md`: **não fazer o OSC C agora**.
- **Exemplos musicais:** são quatro patches em tabela, indicando quais já saem de A/B + sample + sub/noise. Falta renderizar e comparar de ouvido, por isso o primeiro item segue aberto.
- **CPU:** cada lane custa cerca de 0,07 / 0,08 / 0,19 ponto por nota a 44.1 / 48 / 96 kHz. Um C com 16 notas e 8 lanes somaria +9 / +10 / +25 pontos, e a 96 kHz o perfil extremo iria para cerca de 98%, acima do gate de 70%.
- **Problema encontrado:** um oscilador desligado ainda renderiza todas as lanes. `extreme_a_only` custa o mesmo que `extreme_no_routes`. Corrigir isso vem antes do C.
- **Interface:** três opções avaliadas. A recomendada é uma barra de fontes mais um editor da fonte selecionada, e pode ser feita antes do C.
- **Pendente:** decisão humana sobre a sequência sugerida (pular osciladores desligados → barra de fontes → reavaliar o C).
