# Serum 2 × Borato Wave: lacunas e prioridades

Levantamento de 17/10/2026. O alvo local analisado é `Serum2.vst3`, SHA-256
`7978c9be5b2107e985c24c174000faee87e11483ec989d81dc61b5829b45ed70`.
As observações do binário vêm de [SERUM2_CONTEXT.md](../SERUM2_CONTEXT.md),
[OSCILLATOR_RESEARCH.md](OSCILLATOR_RESEARCH.md) e [PRESET_RESEARCH.md](PRESET_RESEARCH.md).
Presença de classe, parâmetro ou caminho no decompile confirma uma capacidade ou
estrutura; não revela sua qualidade sonora, seu algoritmo completo nem a frequência
com que músicos a utilizam. As propostas abaixo são decisões de produto para um
instrumento original, não instruções para copiar código, presets ou interface.
O [guia oficial da Xfer](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)
confirma três osciladores principais A/B/C **selecionáveis entre cinco modos**:
wavetable, sample, multisample, granular e spectral. O sub e o noise são dedicados.
O decompile enumera também estados internos noise/sub em `OscState`; isso não
significa que a interface ofereça sete modos principais.

## O que já existe e o que muda o som

| Área | Borato hoje (código verificado) | Serum 2 (evidência local) | Lacuna prática |
|---|---|---|---|
| Fontes | Dois osciladores wavetable A/B com unison até 8, sub senoidal e ruído branco; dois filtros e via direta | Três `OscState` A/B/C no decompile; cinco modos principais na documentação oficial | A terceira **fonte flexível** e motores alternativos importam mais que um terceiro leitor WT isolado. |
| Criação tímbrica | Oito bancos próprios, importação WAV/AIFF/FLAC em ciclos, 2D/3D/FFT, edição de posição | Editor de frames/harmônicos/FFT, importação por detecção de frequência e ressíntese; warp de fase, FM/AM/RM/PD (§5) | Falta transformar e criar tabelas no instrumento e mudar o espectro enquanto a nota toca. |
| Expressão/modulação | ADSR de amplitude + Env 2, dois LFOs editáveis, 16 rotas/21 destinos, quatro macros | Estados para LFO/env, atribuição a pontos, MPE e MIDI learn; Xfer documenta até 10 LFOs, quatro envelopes e oito macros | Mais destinos úteis, controle por nota e gestos de performance. Quantidade de slots por si só não prova benefício. |
| Voz | 16 vozes polifônicas, bend fixo de ±2 semitons; `controllerMoved` vazio | Processamento de Note Expression/MPE, configurações de voz e afinação (§2–4) | Mono/legato/glide e tratamento comprovado de pedal para basses e leads. |
| Filtros/FX | Dois SVF por voz; distorção 2×, delay e reverb em ordem fixa | Várias famílias de filtros; chorus, flanger, phaser, EQ, compressor, convolution e roteadores de FX (§4.3) | Mais cores de filtro e efeitos de dinâmica/espacialidade; cadeia flexível após medir CPU. |
| Conteúdo | 61 presets originais, browser pesquisável, `.bwpreset` com tabelas e curvas incorporadas | Banco SQLite com tags, rating, prévia e metadados; estado com recursos embutidos (contexto de presets) | Prévia, organização e undo tornam a descoberta/edição menos trabalhosa; não melhoram o DSP diretamente. |

O Filter 2 já é destino da matriz (`cutoff2` e `reso2`), e o rack de efeitos já
processa distorção, delay e reverb. Essas capacidades não são lacunas.
O Borato usa mipmaps para limitar harmônicos armazenados, mas seus próprios testes
não demonstram ausência de alias sob varredura rápida de frame, pitch ou futuros warps.

## O que usuários relatam

- Em [discussão direta de usuários](https://www.reddit.com/r/serum/comments/1cuuwnq/serum_2/),
  houve pedidos de terceiro oscilador, filtros, roteamento e undo; outras pessoas
  destacaram a simplicidade do fluxo. É sinal de interesses diferentes, não votação
  representativa de prioridades.
- Após o lançamento, [usuários descreveram](https://www.reddit.com/r/serum/comments/1jg9eo3/serum_2_is_like_a_baby_daw_now/)
  valor na combinação de sample, granular, spectral e clip/arp. O exemplo é
  compatível com priorizar interação e transformação de fontes antes de C isolado.
- Em [tópico do fórum Xfer](https://xferrecords.com/forums/general/serum-2-overloads-the-cpu),
  usuários relataram presets pesados; uma resposta da própria Xfer aponta unison,
  quantidade de grãos, caudas longas, granular, spectral e certos filtros como
  fatores de custo. Isso reforça o gate de desempenho, mas não mede o Borato.

Fontes e limites de cada relato estão detalhados em
[SERUM2_WEB_RESEARCH.md](SERUM2_WEB_RESEARCH.md).

## Terceiro oscilador: decisão para agora

**Adiar um OSC C somente wavetable.** A/B, sub e ruído já cobrem muitos empilhamentos;
um C igual amplia sobretudo camadas simultâneas. No Borato, a mudança atravessaria
parâmetros do host, estado/presets antigos, tabela embutida, 16 vozes com até oito
lanes adicionais por nota, filtros, 6 novos destinos mínimos de modulação e um
painel que hoje ocupa espaço para A e B. Isso é análise da arquitetura atual, não
medição de custo do C.

Há uma restrição mensurável: o [benchmark extremo existente](MODULATION.md) de A/B
com 16 notas, oito lanes em cada, 16 rotas e buffer de 128 amostras usou **42%**
do tempo de um núcleo a 48 kHz, **92%** a 96 kHz e excedeu o tempo disponível a
192 kHz no Ryzen 7 5700X. É um teste offline curto, não previsão linear nem medida
de latência ASIO. Implementar C primeiro, sem perfil por configuração e política
de headroom, arrisca piorar exatamente esse cenário.

Reconsiderar C quando houver um segundo tipo de fonte pronto para ocupar o slot
(por exemplo, sample/granular), patches de referência que A/B+sub não consigam
realizar e benchmark de três slots sem estouro de prazo em 48/96 kHz. Planejar
o C como slot de tipo selecionável, seguindo apenas o *conceito* observado no
Serum; continuar usando DSP e GUI próprios.

## Onde atacar, em ordem

1. **Execução e orçamento de áudio:** medir CPU por nota/unison/sample rate em
   Standalone/DAW; testar pedal, mono/legato/glide, prioridade de notas e transições
   sem clique. Isso entrega articulação a basses/leads e define margem para DSP novo.
2. **Profundidade dos A/B:** implementar um warp de fase musical (PWM ou bend)
   e depois FM/phase distortion entre A e B, com testes de alias, afinação,
   estabilidade e automação. A/B ganham timbres novos sem outro painel de fonte.
3. **Nova matéria-prima:** implementar sample playback simples, com pontos de
   início/fim e loop; distinguir esse motor da importação atual que converte áudio
   em ciclos WT. Validar arquivos ausentes, persistência, memória e custo por voz.
   Granular e spectral ficam para incrementos próprios depois dessa base.
4. **Ferramentas de timbre e acabamento:** edição original de frames/harmônicos,
   importação com detecção de pitch, undo; chorus, EQ e compressor com ganho
   calibrado. Validar o som seco, efeitos e presets sob a mesma loudness.
5. **Expansão de slots:** comparar um C selecionável com patches demonstráveis
   usando A/B e sample. Implementar somente quando houver ganho musical e orçamento
   medido de CPU, UI e estado.

MPE, clip player/arp, mais LFOs e grande matriz são extensões posteriores,
condicionadas a casos musicais concretos e testes. A ordem acima é uma **inferência
de produto** sobre a lacuna do Borato e seu custo atual; não é uma classificação
oficial do Serum nem uma medida de preferência dos usuários.

## Validação para cada incremento

Comparar A/B com e sem a função em notas graves/agudas, acordes, 44,1/48/96 kHz,
automação rápida e render de caudas; medir alias, pico, CPU, latência e retorno de
estado/preset. Ouvir pares com volume equivalente e capturar comportamento em VST3
e Standalone. Para qualquer novo tipo de fonte, testar troca durante notas ativas,
arquivo ausente e abertura de projetos antigos antes de publicar patches de fábrica.
