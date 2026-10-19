# OSC C: decisão e reconfiguração gráfica

19/10/2026, ticket `13-decisao-osc-c`. Complementa [SERUM2_GAP_ANALYSIS.md](SERUM2_GAP_ANALYSIS.md) §"Terceiro oscilador" e [CPU_PROFILING.md](CPU_PROFILING.md).

## Decisão

**Não implementar um OSC C agora.** Revisar quando os três pré-requisitos abaixo estiverem cumpridos e houver um patch de referência que precise dele.

1. **Um oscilador desligado tem que custar zero.** Hoje ele não custa zero (veja "Medição"). Corrigir isso primeiro barateia todos os patches que já existem, e é condição para que um terceiro slot só custe quando estiver em uso.
2. **A terceira camada flexível já existe.** A fonte SAMPLE (ticket 06) toca uma gravação por nota ao lado de A/B/sub/noise. Um C só de wavetable acrescentaria mais do mesmo, o que o próprio ticket descarta ("não criar um OSC C somente wavetable sem ganho demonstrado").
3. **CPU a 96 kHz.** Com 16 notas e 8 lanes, um C levaria `extreme_no_routes` de 73% para cerca de 98% do orçamento, acima do gate de 70% de `CPU_PROFILING.md`. A 48 kHz caberia (31% → cerca de 41%).

**Critérios para implementar depois:**
- um patch de referência com duas camadas de wavetable mais uma camada wavetable independente (pitch, posição ou modulação próprios) que A/B + sample + sub/noise não reproduza;
- osciladores desligados pulam o render, exceto B quando é o modulador de FM;
- o patch-alvo abaixo de 70% do orçamento em três execuções, a 48 e a 96 kHz;
- a interface na opção recomendada abaixo, sem perder o acesso direto a A e B.

## Exemplos musicais: A/B + sub/noise (+ sample) contra uma terceira fonte

| Patch | Como montar hoje | Precisa de C? |
| --- | --- | --- |
| Supersaw com sub e ar | A saw, 8 vozes de unison; sub −1 oct; noise baixo no Filter 2 | Não |
| Pad em duas camadas com ataque gravado | A e B wavetables em oitavas; SAMPLE one-shot com o ataque (`sampleLoop` off) | Não: o SAMPLE é a terceira camada |
| Baixo FM com camada limpa | B modula A (FM FROM B); sub para o fundamental | Não, mas B fica preso como modulador e deixa de ser camada audível independente |
| Lead com duas wavetables em movimento mais uma terceira em outro pitch e LFO | A e B ocupados; a terceira só cabe como SAMPLE, sem posição de wavetable | **Sim**, é o caso que justificaria um C |

Os três primeiros são patches que já existem na fábrica: "Chorus Silk Pad", os presets FM e os Atelier. O quarto é o critério de entrada acima. **Ainda não renderizei esses exemplos em áudio para comparar de ouvido.** Essa parte do ticket continua aberta para uma pessoa.

## Medição

Release, buffer de 128 amostras, mesma máquina. Três execuções de `test_modulation --benchmark`, porcentagem do orçamento de tempo real offline:

| Perfil | 44.1 kHz | 48 kHz | 96 kHz |
| --- | ---: | ---: | ---: |
| light (1 nota, 1 lane) | 0.81% | 0.88% | 1.83% |
| unison8 (1 nota, 8 lanes) | 1.78% | 1.96% | 4.51% |
| extreme_a_only (16 notas, B off) | 28.3–28.5% | 31.2% | 72.5% |
| extreme_no_routes (16 notas, A+B) | 28.3–29.6% | 31.5–32.1% | 73.1–74.0% |

- **B desligado custa o mesmo que B ligado.** `extreme_a_only` e `extreme_no_routes` empatam. Em `WaveVoice` o laço de lanes multiplica pela flag `enabled`, mas nunca pula o render. Isso é intencional para B, cujo sinal bruto alimenta o FM mesmo mudo, e desnecessário em todos os outros casos.
- **Custo de uma lane:** em `unison8`, A e B ganham 7 lanes cada, porque o perfil aplica o unison aos dois. Isso dá (unison8 − light) / 14 ≈ 0,069 / 0,077 / 0,19 ponto por lane por nota a 44.1 / 48 / 96 kHz. Esse modelo prevê `extreme_no_routes` em 28,5 / 31,4 / 72,2%, contra 28,3–29,6 / 31,5–32,1 / 73,1–74,0% medidos.
- **Estimativa para C com 16 notas e 8 lanes:** +9 / +10 / +25 pontos. Com 1 lane: +1,1 / +1,2 / +3,1.

## O que um C atravessaria

| Área | Custo |
| --- | --- |
| Parâmetros do host | cerca de 18 novos, acrescentados ao fim: os 16 de `OscParams` mais o liga/desliga e a quantidade de warp |
| Estado e presets | slot de tabela do usuário e nome embutidos; sessões antigas carregam C desligado. `kStateVersion` não muda se tudo for acrescentado |
| Modulação | 7 destinos novos (posição, nível, pan, pitch, detune, spread, warp), acrescentados depois de `kFmDestination`. `kDestinations` passa de 24 para 31 e a matriz e o arrasto para modular crescem |
| FM | hoje é fixo B → A. Com C é preciso escolher modulador e portadora, ou deixar fixo e documentado |
| Roteamento | mais um `xRoute` (Filter 1 / Filter 2 / ambos / direto). O mixer de barramentos já é genérico |
| Voz | mais um `TableOscillator` e um `Source` com 8 lanes por voz, cerca de 1 KB × 16 vozes. O laço já é indexado |
| UI | um terceiro painel que não cabe no layout atual (veja abaixo) |

## Reconfiguração gráfica

A área de desenho é de 1240 × 940. A página SYNTH usa a linha de cima para A (446 px), B (446 px) e Filtros (280 px), e a de baixo para Envelopes, LFO e Sub/Noise. Um terceiro painel de 446 px não cabe.

### Opção 1: três colunas estreitas

```
| OSC A (300) | OSC B (300) | OSC C (300) | FILTERS (280) |
| tabela 270x90, 6 knobs por linha, unison numa segunda página do painel     |
```
Mantém tudo à vista e é parecida com o Serum 2. Mas o painel fica com 300 px: a vista 3D da tabela e os 12 knobs atuais não cabem, e metade dos controles de cada oscilador some atrás de uma aba. É a opção que mais mexe nas coordenadas de `resized()`, nos testes de interface e nos screenshots.

### Opção 2: aumentar a tela para cerca de 1700 × 940

Três painéis de 446 px mais os filtros. É a menor mudança de código, porque basta mudar as posições. Porém a proporção fixa muda, e o tamanho mínimo (992 px hoje) teria que subir para cerca de 1360 px ou a escala mínima ficaria ilegível. Em telas de 1366 px de laptop o plugin não caberia inteiro. **Descartada.**

### Opção 3 (recomendada): barra de fontes + editor da fonte selecionada

```
+-- SOURCES -------------------------------------------------------------+
| [A on] lvl pan >F1 | [B on] lvl pan >F1 | [C off] ... | SMP | SUB | NOISE |
+------------------------------------------------------------------------+
| EDITOR: OSC A  (tabela 2D/3D/FFT grande, posição, pitch, fine, warp,   |
|  FM, unison...)  -- mesmo painel para A, B ou C, pela barra acima      |
+-------------------------------------------+----------------------------+
| ENVELOPES / LFO (como hoje)               | FILTERS (como hoje)        |
+-------------------------------------------+----------------------------+
```
- **Barra de fontes (cerca de 1200 × 56):** um chip por fonte com liga/desliga, nível, pan, rota e um mini-gráfico. Mixar e rotear tudo continua a um clique.
- **Editor da fonte:** um único painel largo (cerca de 900 × 330) para a fonte selecionada, com a tabela grande e os knobs numa linha só. O mesmo componente atende A, B e C. SAMPLE e SUB/NOISE podem migrar para cá depois, substituindo a página SAMPLE.
- **Ganhos:** cabe um número arbitrário de fontes. Os componentes de oscilador viram uma classe só, em vez das duas listas paralelas `wt/wtB`, `flat/flatB` etc. que existem hoje. A aba SAMPLE separada sai.
- **Custos:** perde a edição de A e B lado a lado; a barra mostra os dois, mas só um editor fica aberto. Mexe em `selectPage` e `resized`, nos testes de interface e nos screenshots de referência.
- **Pode ser feita antes do C.** Essa refatoração com A/B + sample + sub/noise já se paga com uma página a menos e o código paralelo de A/B unificado, e reduz o C a "mais um chip".

**Sequência sugerida:**
1. Pular o render de osciladores desligados, com medição antes e depois.
2. Barra de fontes e editor único com as fontes atuais.
3. Reavaliar o C com o patch de referência e o gate de CPU.
