# Fontes sample, multisample, granular e espectral

**Status:** needs-triage
**Data:** 2026-10-19
**Etapa:** proposta para aprovação da decomposição; nenhum ticket publicado.

## Objetivo e fronteira

Entregar motores originais e instrumentos tocáveis, com importação, controles, automação, restauração de estado e evidência de qualidade. A referência Serum orienta perguntas e comportamentos públicos; equivalência sonora ou de algoritmos não é um critério presumido. Consultar [investigação dos motores](../../docs/ADVANCED_ENGINES_RESEARCH.md). Afirmações ainda não corroboradas permanecem desconhecidas.

Todas as constantes, limites e tolerâncias abaixo são **critérios propostos para o Borato**, sujeitos a aprovação e às medições indicadas. Não são números recuperados do Serum. Alterar um gate exige registrar motivo e resultado; não reduzir silenciosamente a exigência para acomodar uma implementação.

## Base existente e decisões preservadas

- Sample simples já toca arquivo mono por nota, região e loop, root fixo MIDI 60, interpolação cúbica, bend/glide e ADSR. Entra no Filter 1. Estado guarda caminho absoluto; arquivo ausente mantém referência e toca silêncio.
- Sample básico é o [ticket 06 existente](../serum2-gap/issues/06-sample-playback.md), implementado com validação em host ainda pendente. Esta iniciativa o estende e não o recria nem altera seu status.
- Multisample, agendador de grãos e resíntese espectral temporal não existem. FFT de display, mipmaps de wavetable e edição harmônica não constituem um motor espectral de samples.
- A decisão de [adiar OSC C](../../docs/OSC_C_DECISION.md) permanece. Usar a fonte SAMPLE existente como ponto de entrada dos novos modos; adicionar C não bloqueia esta iniciativa. O [ticket 13](../serum2-gap/issues/13-decisao-osc-c.md) permanece intacto.
- Reutilizar [medição de CPU](../../docs/CPU_PROFILING.md) e [ticket 01](../serum2-gap/issues/01-medicao-cpu.md). Benchmark offline não substitui ASIO/DAW. O antigo item de importação pendente no mapa de arquitetura está desatualizado frente ao código e ao roadmap atual.
- IDs de parâmetros, índices da matriz e valores de modos já publicados permanecem estáveis. Acrescentar novos IDs/índices; estados antigos entram no modo Sample simples com o mesmo comportamento anterior.

## Riscos que a implementação deve fechar

1. A publicação atual permite liberar recursos antigos após 65.536 amostras sem confirmação de todas as vozes: isso presume fades curtos. Não reutilizar a mesma regra para grãos, zonas ou caudas longas. A liberação precisa de confirmação por geração ou garantia comprovada de duração máxima.
2. O limite atual de 2²² frames é por arquivo mono. Stereo, bancos e análises exigem orçamento agregado, incluindo recursos aposentados, jobs pendentes e undo. Não basta contar o asset atualmente visível.
3. O decoder atual não verifica o retorno da leitura nem rejeita valores NaN/Inf. Validar antes de publicar. Arquivo recusado conserva o instrumento anterior.
4. Importação e restauração podem realizar leitura síncrona. Os novos caminhos devem fazer I/O/análise fora do callback e impedir que job antigo sobrescreva carregamento recente.
5. Mono/legato muda a nota durante a voz. Definir se a zona troca no retarget, como o glide afeta pitch e quando grãos/análises antigos deixam de ser referenciados.
6. Cubic Hermite não elimina aliasing de transposição. A mesma lacuna se propaga a zonas e grãos se o leitor for reutilizado sem filtragem.
7. Factory presets hoje limpam sample e não representam bancos de assets. Conteúdo dos novos motores requer serialização, identificação e matching coerentes, não apenas novos parâmetros.

## Regras comuns de aceitação

- Cada fatia fecha um caminho tocável/verificável: controles necessários, áudio, estado, undo quando houver edição e testes. Não entregar apenas structs, parser ou painel vazio.
- Testes determinísticos Debug; desempenho em Release. Rates 44,1/48/96 kHz e blocos 64/128/512, incluindo bloco final parcial. Casos 1/8/16 notas; testar mono/legato, pedal, stealing e all-notes-off quando o comportamento é afetado.
- Callback sem novas alocações/liberações, locks, I/O ou análise pesada não limitada; demonstrar por instrumentação e revisão. Todos os samples de saída finitos. Dados, trabalho e vozes/grãos têm limites explícitos.
- Tolerâncias de transição são relativas ao sinal: usar fixture estacionária de amplitude conhecida e medir resíduo contra referência, sem impor limite arbitrário de diferença entre samples de áudio musical.
- Persistência: sessão antiga, salvar/reabrir, arquivo ausente, cancelamento e asset substituído. Resultado indisponível é mostrado na UI; referências não desaparecem silenciosamente.
- Cada ticket registra hipóteses e resultados. Gates da entrega final complementam testes específicos; não adiam segurança básica para o fim.

## Decomposição proposta

### 01 — Trocar samples validados sem perder recursos em uso

**Blocked by:** nenhum.

**Entrega:** carregar/trocar sample enquanto toca, mantendo o anterior em caso de erro; publicação e undo respeitam o ciclo de vida real das vozes e um orçamento de memória visível em mensagens de erro. Prefatoração mínima demonstrada no caminho atual.

**Verificações:** leitura incompleta, arquivo vazio, NaN/Inf e excesso de canais recusados; orçamento agregado proposto de 256 MiB para assets/aposentados/undo, excluindo buffers fixos reportados separadamente. Stress com 100 substituições e leitor antigo artificialmente retido por 2 s a 96 kHz: nenhum acesso após liberação e coleta após última confirmação. Contador de alocação/destruição no callback permanece zero. Limite e política de recusa/evicção documentados.

### 02 — Importar e cancelar áudio em segundo plano

**Blocked by:** 01.

**Entrega:** importação com progresso/cancelamento; instrumento antigo continua tocando; somente o pedido mais recente pode publicar. Restauração com asset externo usa o mesmo caminho e indica carregamento.

**Verificações:** fixture de decoder lento controlado, iniciar A, iniciar B, cancelar A, concluir fora de ordem: somente B é ativo. Cancelar/fechar editor e destruir processor durante job não acessa objetos destruídos. I/O/análise ausentes do áudio. Quantidade de jobs/dados pendentes limitada; não medir responsividade por sleep frágil, usar eventos instrumentados.

### 03 — Salvar sample portátil e relincar referências antigas

**Blocked by:** 02.

**Entrega:** preset portátil leva seu áudio; sessão antiga com caminho continua abrindo; UI permite localizar arquivo ausente e salvar novamente. Definir container versionado, identidade por conteúdo, limites e escrita atômica.

**Verificações:** exportar, remover/mover origem, importar em nova pasta e reproduzir o mesmo PCM; hash de bytes decodificados e parâmetros preservados. Rejeitar payload truncado, tamanho declarado falso e hash inválido sem perder estado atual. Limite decodificado aplicado antes de grandes alocações. Undo/relink e estados legados cobertos; não incorporar arquivos externos não selecionados.

### 04 — Tocar sample stereo com root note e rota próprios

**Blocked by:** 01.

**Entrega:** preservar L/R do arquivo, editar root note, pan e destino Filter 1/2/Direct; sample mono e estados antigos conservam o resultado anterior. Controles e waveform refletem o asset.

**Verificações:** fixture L=440 Hz/R=660 Hz preserva canais; erro de pitch na raiz <1 cent e duração <1 sample de erro no teste sem loop, descontados fades declarados. Raiz MIDI 60 continua compatível. Pan extremos e mono downmix com fixtures separadas; rota Direct ignora filtros, outras respeitam seleção. Stereo cabe no orçamento agregado.

### 05 — Transpor samples com aliasing medido

**Blocked by:** 04.

**Entrega:** leitor filtrado reutilizável tocável no sample simples; pitch/bend/glide continuam suaves. Algoritmo, atraso e custo documentados, sem alegar ausência universal de aliasing.

**Verificações:** seno, multitone e sweep em transposições de -24 a +24 st; referências offline com resampling filtrado. Nos casos estacionários com banda representável, aliases ao menos 60 dB abaixo do tom de referência e erro de ganho na banda útil <=0,5 dB; definir banda de transição e excluir Nyquist da medida de ripple. Material acima do Nyquist de saída é atenuado. Comparar CPU e latência com leitor anterior nos três rates; automação não troca estado de filtro com degrau.

### 06 — Modular a fonte sample sem reiniciar o cabeçote

**Blocked by:** 04.

**Entrega:** destinos de matriz para level, pan e pitch da fonte; UI atribui LFO/envelope/macros. Start/end conservam semântica atual, sem adicionar scrubbing acidental.

**Verificações:** índices antigos preservados; bipolaridade e faixas corretas, zero-depth reproduz baseline com diferença RMS <-100 dBFS no fixture determinístico. Fade de mute continua definido como pausa ou avanço conforme decisão documentada, sem mudança silenciosa em estados legados. Testar automação rápida e restauração das rotas.

### 07 — Mapear duas zonas de notas em um instrumento

**Blocked by:** 03, 04.

**Entrega:** importar duas amostras, desenhar faixas de notas e root de cada zona, tocar e exportar banco portátil; seleção determinística e indicador de zona ativa. Primeira versão: uma zona por nota, sobreposição inválida mostrada ao editar.

**Verificações:** notas nas duas extremidades e fora do mapa; fora toca silêncio. Banco limitado inicialmente a 128 zonas e orçamento agregado de 01. Retarget mono/legato seleciona zona da nova nota com transição definida; vozes anteriores mantêm recursos válidos. Carregar estado de sample único cria zona equivalente sem alteração sonora. Não esperar editor de mapa completo para entregar estas duas zonas.

### 08 — Cruzar camadas de velocity com ganho previsível

**Blocked by:** 07.

**Entrega:** duas camadas por faixa, limites de velocity e largura de crossfade editáveis e persistentes. Escolha entre lei linear/potência constante é explícita; normalização não deve amplificar layers correlacionadas inesperadamente.

**Verificações:** todas as velocities MIDI 1–127, incluindo limites; soma de pesos e seleção conferidas. Fixture duplicada em fase: nenhum boost >0,1 dB frente a uma camada unitária com lei selecionada para esse caso. Para fixture descorrelacionada medir variação RMS e documentar a lei, sem prometer simultaneamente ganho constante para qualquer correlação. Troca de velocity ocorre no note-on, não por automação global.

### 09 — Alternar round robin de forma reproduzível

**Blocked by:** 07.

**Entrega:** grupo de alternância com até quatro variantes por zona; editor, seed/política de reset documentados, export e retrigger funcionando. Não depende de velocity crossfade.

**Verificações:** 100 note-ons seguem a sequência prevista; note-off, sustain e notas de outros grupos não consomem variante errada. All-notes-off e restauração têm política definida. Render offline com seed igual repete seleção; variante ausente é indicada e não desloca silenciosamente o mapeamento restante.

### 10 — Tocar uma nuvem granular com pool limitado

**Blocked by:** 01, 05.

**Entrega:** modo Granular no sample existente, janela Hann, tamanho e posição, com overlap fixo; uma nota sustenta uma textura estática. Pool por voz pré-alocado e comportamento de saturação visível/documentado. Estado salva modo/controles, usando caminho existente até 03 estar disponível.

**Verificações:** proposta inicial de máximo 32 grãos por voz; 16 vozes nunca excedem 512 grãos ativos. Fixture constante com janelas sobrepostas na configuração nominal: ripple de envelope <0,1 dB após estabilização, sem normalização singular. Grãos fora da região não leem fora do asset. Troca de sample durante grãos preserva vida útil; pool cheio termina/recusa de maneira determinística sem alocação.

### 11 — Controlar densidade, duração e dispersão granular

**Blocked by:** 10.

**Entrega:** densidade Hz, duração, posição e dispersões com seed; UI indica quando densidade/duração solicitadas atingem limite. Pitch de grão e avanço da posição são controles separados.

**Verificações:** ranges iniciais propostos duração 5–500 ms e densidade 1–100 grãos/s; intervalo médio medido em 10 s com erro <=1% quando não saturado. Documentar ocupação esperada densidade×duração e teto de 32. Seed igual e sequência MIDI igual reproduzem áudio; blocos diferentes não alteram instantes de disparo além de 1 sample. Extremos não geram NaN nem salto de ganho ao esgotar pool.

### 12 — Modular e salvar um instrumento granular portátil

**Blocked by:** 03, 11.

**Entrega:** matriz controla posição, duração, densidade e pitch; visualizar grãos ativos sem bloquear áudio; salvar/reabrir asset e configuração completa. Novo conjunto de destinos é acrescentado ao fim.

**Verificações:** distinguir parâmetros amostrados ao nascer do grão e parâmetros suavizados durante sua vida. Abrir/fechar UI não muda render; monitor usa snapshots limitados. Macro endpoints e modulação rápida em 16 notas mantêm pool e memória limitados. Save/load com seed e MIDI fixos reproduz resultado dentro de erro RMS <-90 dBFS no mesmo build.

### 13 — Reproduzir análise STFT com reconstrução de identidade

**Blocked by:** 02, 04.

**Entrega:** modo Spectral analisa sample fora do áudio e toca sua reconstrução sem transformações; indicador de análise, cancelamento e estado dos controles. STFT é uma escolha original proposta do Borato, não algoritmo atribuído ao Serum. Primeiro incremento limita duração para caber no orçamento e mostra recusa claramente.

**Verificações:** tamanho FFT inicial 2048, hop 512, janela e normalização overlap-add derivadas e registradas; impulse, seno, ruído e stereo reconstruídos com erro RMS <-90 dBFS após alinhar atraso e ganho, incluindo início/fim com padding. Latência fixa medida por impulso e coerente com o valor exposto ao host quando houver atraso de saída. Rejeitar análise que ultrapasse orçamento antes da alocação; publicar somente análise completa.

### 14 — Percorrer e congelar um som espectral

**Blocked by:** 13.

**Entrega:** posição e Freeze produzem sustain a partir de um frame com fase contínua; scrub contínuo interpola dados válidos, não reinicia fases arbitrariamente. Controles/state/UI tocáveis.

**Verificações:** fixture seno 440 Hz congelado por 10 s: erro de afinação <1 cent, deriva de amplitude <0,2 dB e saída finita. Entrada/saída de Freeze comparada à referência estacionária não introduz resíduo de transição acima de -50 dBFS para seno -12 dBFS. Avaliar separadamente ruído/transientes, sem aplicar tolerância do seno a conteúdo não estacionário. Stereo preserva relação de fase definida.

### 15 — Separar tempo e pitch na reprodução espectral

**Blocked by:** 14.

**Entrega:** velocidade 0,5–2× e pitch -12 a +12 st independentes, com política de fase e preservação de transientes documentada. Registrar limitações de smear/formantes; não transformar análise em mera reprodução de sample reamostrado.

**Verificações:** seno e chirp: pitch <2 cents de erro onde mensurável; duração do trecho dentro de um hop da razão pedida após descontar latência/padding. Fixtures de impulse train e fala/música original para audição nivelada; medir energia fora da banda e transientes, reportar resultados mesmo sem um único escore universal. Rampas de velocidade/pitch não geram fase descontínua, NaN ou trabalho não limitado.

### 16 — Modular e transportar instrumentos espectrais

**Blocked by:** 03, 15.

**Entrega:** posição, velocidade e transformação tonal expostas à matriz com smoothing apropriado; preset portátil guarda áudio original e versão do algoritmo, regenerando cache quando necessário. UI diferencia pronto/analisando/ausente e preserva instrumento anterior até publicação.

**Verificações:** novos índices ao fim; cache incompatível nunca interpretado com layout antigo. Round trip de assets e controles; resultado espectral reanalisado reproduz baseline do mesmo build com erro RMS <-80 dBFS no fixture determinístico. Automação de extremos respeita latência declarada e orçamento; mudanças estruturais de FFT não são parâmetro automatizável neste incremento.

### 17 — Separar região, loop e direção de playback

**Blocked by:** 05.

**Entrega:** controles/state para região do arquivo e loop interno independentes, forward/reverse/ping-pong e largura de crossfade; modo tailed sai do loop no note-off e percorre a região restante, sob política explícita de interação com ADSR. Estados antigos mantêm loop/região equivalentes.

**Verificações:** fixture com marcadores numéricos identifica sequência de frames em cada direção. Limites cruzados, região mínima, sample-rate e incrementos grandes nunca leem fora do asset. Fixture seno -12 dBFS nos loops compatíveis: resíduo de wrap contra referência contínua <-50 dBFS; material incompatível usa o crossfade declarado, sem prometer continuidade de derivada. Note-off em ambas as direções e pedal verificam exatamente onde começa a cauda; tailed não fica preso em loop. Nenhuma nova leitura de disco por nota.

### 18 — Tocar slices manuais como zonas

**Blocked by:** 07, 17.

**Entrega:** adicionar/mover/remover marcadores na waveform, atribuir slices a notas a partir de uma root de slicing editável, selecionar single-slice ou play-to-end e salvar banco portátil. Um slice é uma região do asset compartilhado; não duplicar PCM por marcador. Auto-slice por threshold é capacidade observada, mas não incluída silenciosamente: proposta opcional separada descrita na rastreabilidade abaixo.

**Verificações:** fixture de quatro ataques distintos, quatro teclas reproduzem as regiões corretas; marcador em 0/final e intervalos mínimos tratados. Undo/reopen conserva mapeamento. Orçamento contabiliza um asset compartilhado, não quatro cópias. Mudança de slice durante notas conserva referências e transição limitada.

### 19 — Importar um subconjunto SFZ com diagnóstico

**Blocked by:** 08, 17.

**Entrega:** importar mapeamento SFZ para instrumento tocável usando sample, root, key/velocity ranges e loop; apresentar lista explícita dos opcodes aceitos e não suportados. Export portátil usa formato Borato; não prometer export SFZ nem suporte universal.

**Verificações:** fixtures pequenos com grupos/regiões, caminhos relativos com espaços, boundaries e samples ausentes; mapa e áudio equivalem ao criado manualmente. Definir precedência de opcodes e limites de texto/regiões. Construções não suportadas geram diagnóstico, nunca conversão silenciosa como se fossem completas. Leitura somente dos assets referenciados pelo arquivo selecionado, limitada pelo orçamento; nenhum I/O no callback.

### 20 — Sincronizar grãos e aplicar key tracking

**Blocked by:** 11.

**Entrega:** densidade em grãos/s, divisões de BPM e target overlap/grains; duração livre, BPM e percentual com denominador explícito, incluindo dotted/triplet de ambos. Distribuição de disparos Together/Even/Exponential/Random, jump-start limitado, key tracking de duração e loop por grão são controles/state próprios. UI mostra unidades e limites efetivos. Esses nomes observados não determinam suas leis: usar os contratos Borato propostos a seguir ou aprovar revisão antes de implementar.

**Verificações:** proposta de percent length como fração do intervalo entre disparos, sujeita a aprovação; não assumir semântica Serum. Eventos em BPM 60/120/180, mudanças, stop/play e seek: erro <=1 sample na programação determinística. Key tracking testado em oitavas com fator esperado documentado. Grão que cruza região respeita loop/direção sem acessar fora. Saturação de pool continua previsível ao baixar BPM ou aumentar duração.

**Contratos propostos:** target overlap N usa taxa N/duração média, limitado ao pool; dotted multiplica duração por1,5 e triplet por2/3, convertendo frequência pelo inverso. Together inicia lote no evento; Even divide uniformemente o intervalo do lote; Exponential usa espaçamento geométrico finito explicitamente normalizado; Random distribui offsets uniformes com seed no intervalo do lote. Não inferir que Exponential significa distribuição exponencial aleatória no Serum. Jump-start inicia até N grãos com idades distribuídas para preencher a janela, sem exceder32. Testar contagem/offsets de cada contrato, zero densidade, N=1/32, target overlap sem saturação (ocupação média dentro de1grão do pedido em10s), jumps/retrigger e persistência. Se confirmar a semântica Serum for necessário, executar os experimentos de scheduling/density do ledger antes de alterar contrato.

### 21 — Modelar janelas, skew e dispersões granulares

**Blocked by:** 12, 20.

**Entrega:** as dez famílias nomeadas — Hann, Welch, Gaussian, Blackman-Harris, Sinc, Tukey, Triangle, Trapezoid, ExpDec e ExpDecRev — com fórmulas originais explícitas, amount/skew e dispersões de posição, direção, pitch, duração, pan, ganho, window amount/skew e warp. Dois slots de warp de grão com ordem definida, inicialmente identidade e bend de leitura limitado; nenhuma equivalência de modo proprietário presumida. Controles/state/modulação limitados. Este ticket é candidato explícito a divisão após aprovação; não entregar só três janelas e declarar as dez prontas.

**Verificações:** cada janela tem suporte, endpoints e energia documentados; compensação não divide por energia nula. Seed fixo reproduz 10.000 decisões; amostras aleatórias dentro das faixas, distribuição esperada verificada por tolerância estatística previamente definida. Stereo pan e direção distinguíveis em fixtures; warp zero reproduz baseline <-90 dBFS de erro RMS e warp máximo recebe teste de alias contra referência. Automação não aumenta máximo de grãos. Não combinar 10.000 renders longos: testar decisões e um conjunto reduzido de áudio.

**Referências matemáticas propostas:** t em[0,1], zero fora; Hann=(1-cos(2πt))/2; Welch=1-(2t-1)²; Gaussian=exp(-0,5((t-0,5)/0,18)²) com fades de borda explicitados; Blackman-Harris=0,35875-0,48829cos(2πt)+0,14128cos(4πt)-0,01168cos(6πt); Sinc=sinc²(4(2t-1)), sinc(x)=sin(πx)/(πx) e sinc(0)=1; Tukey com alpha=0,5 e ramps cossenoidais; Triangle=1-|2t-1|; Trapezoid=min(1,4t,4(1-t)); ExpDec=exp(-6t) multiplicado por fades cossenoidais nos primeiros/últimos2% da duração; ExpDecRev=ExpDec(1-t). Esses são contratos Borato, inclusive taper de Gaussian/Blackman-Harris para endpoint realmente zero; não fórmulas recuperadas.

**Teste das dez:** comparar grade de4097pontos com avaliador double independente, erro absoluto<=1e-5; testar t=0/1, duração mínima e skew perto dos extremos sem divisões inválidas. Skew é remapeamento monotônico por duas partes, ponto de quebra limitado a[0,01;0,99]. Medir energia integrada para compensação declarada, e sequência reversa de ExpDecRev dentro de1e-6. Amount tem lei documentada e endpoint neutro testado. Sem referência runtime, não afirmar que skew/amount ou distribuição probabilística imita Serum.

### 22 — Preservar fase e transientes na resíntese

**Blocked by:** 15.

**Entrega:** modo de phase locking e tratamento de transientes selecionáveis, com algoritmo original explicitamente documentado; UI permite comparar com baseline sem esses modos. Transientes detectados aparecem em visualização limitada.

**Verificações:** comparar impulse train, duas senoides próximas e percussão original com baseline 13–15, em velocidades .5/1/2. Identidade a 1× conserva gate de 13. Nos fixtures de impulso, proposta de reduzir em >=25% a energia de pré-eco na janela de 20 ms anterior ao ataque versus baseline transformado; rejeitar fixture cuja energia baseline seja desprezível em vez de dividir por zero. Reportar atraso, estabilidade stereo e custo. A melhoria exigida é do fixture Borato; não atribuir desempenho ou algoritmo ao Serum.

### 23 — Aplicar dois estágios de transformação espectral

**Blocked by:** 16, 22.

**Entrega:** dois estágios ordenados e controláveis, inicialmente deslocamento de bins e inclinação espectral, com quantidade, bypass e modulação; usuário ouve diferença de ordem onde as operações não comutam. São transformações propostas, não reconstrução de modos desconhecidos.

**Verificações:** estágio neutro/bypass mantém identidade <-90 dBFS no fixture; deslocamento inteiro move seno por número previsto de bins, valores fora da banda descartados sem wrap involuntário. Inclinação medida em multitone dentro de 0,5 dB da curva especificada na banda útil. Validar simetria conjugada/real output, fase, energia limitada e ordem persistida. Troca/bypass crossfade evita resíduo impulsivo no teste estacionário.

### 24 — Filtrar e misturar o domínio espectral

**Blocked by:** 23.

**Entrega:** limites low/high, deslocamento do filtro e wet/dry controláveis e moduláveis; curva visível corresponde ao ganho realmente aplicado. Definir ordem relativa aos dois estágios e compensação de latência entre dry/wet.

**Verificações:** multitone demonstra ganho de passband <=0,5dB de erro e atenuação proposta >=60 dB fora da banda de transição declarada; low>=high tem política determinística e sem NaN. Wet 0 corresponde ao dry alinhado, wet 1 ao processado. Fixture idêntica nos dois ramos não apresenta comb filtering ao variar mix; diferença RMS < -80 dBFS contra referência alinhada. Filtro deslocado não extrapola bins.

### 25 — Entregar presets portáteis sample/multisample

**Blocked by:** 05, 06, 09, 18, 19.

**Entrega:** três instrumentos originais mostrando camadas de velocity, round robin e slices/loops; quatro macros úteis por instrumento, indicação de faixa e assets portáteis. Extensão mínima compartilhável de factory assets/matching, preservando índices anteriores.

**Verificações:** procedência/licença das gravações; export abre com fontes movidas. Todas as zonas e velocities de fronteira tocadas; ausência de macro morta; pico < -1 dBFS no protocolo de conteúdo declarado. Caso musical mostra que cada técnica muda comportamento. Sessões anteriores e 65 presets existentes continuam restaurando valores. Infraestrutura compartilhada deve permanecer compatível com os tickets 26/27, sem bloqueá-los conceitualmente.

### 26 — Entregar presets granulares originais

**Blocked by:** 21.

**Entrega:** três presets granulares portáteis com macros de posição/densidade/timbre/espaço, incluindo nuvem estática e sincronizada. Implementar/reutilizar somente a integração de factory assets necessária, coordenando o modelo comum com 25/27 sem depender de suas engines.

**Verificações:** procedência dos assets e export com fontes ausentes; seed determinístico, MIDI 1/8/16 notas dentro do limite anunciado, velocities .25/.65/1 e extremos de macros: finite, pico < -1 dBFS e ocupação limitada. Audição nivelada, relatório de CPU e resposta audível de cada macro; ausência de aumento de memória após100trocas.

### 27 — Entregar presets espectrais originais

**Blocked by:** 24.

**Entrega:** três presets portáteis demonstrando freeze, tempo/pitch e transformações/filtro, com quatro macros e latência/limites apresentados ao usuário quando relevantes. Compartilhar contrato de factory assets sem esperar presets sample/granular.

**Verificações:** análise regenerada ao abrir, assets movidos não quebram preset, cache incompatível descartado. Velocities e macros em fixtures musicais, finite e pico < -1 dBFS no protocolo de conteúdo; phase/freeze sem deriva não intencional. Audição nivelada e renders de referência; documentar artifacts conhecidos de transientes e material denso sem alegar equivalência Serum.

### 28 — Validar e liberar os motores em host real

**Blocked by:** 25, 26, 27.

**Entrega:** build Standalone/VST3 e relatório conjunto reproduzível. Executar os checks humanos relevantes dos tickets antigos sem reescrever resultados prévios.

**Verificações:** manter gate existente de **tempo médio total <=70% do período em cada uma de três execuções** no perfil-alvo e hardware documentados, em 48/96 kHz/bloco 128; acrescentar p99<70% e nenhum callback acima de100% do período. Esses limites adicionais são propostos. Reportar separadamente extremos que excedam gate; otimizar ou reduzir limites públicos antes de liberar esse perfil. Sessão ASIO/DAW de 30 min com automação/importação/reopen: zero dropouts atribuíveis ao plugin, zero NaN e sem crescimento contínuo de memória após aquecimento. Latência medida/declarada difere no máximo1sample nos modos determinísticos. Seed fixo permite comparação offline/realtime. Renders e audição comparativa não substituem medição e não provam paridade exata com Serum.

## Dependências e paralelismo

01 desbloqueia 02 e 04. Sample e multisample avançam por 03–09 e17–19; granular por 05/10–12/20–21; espectral por 02+04/13–16/22–24. Cada família tem seu próprio ticket de conteúdo25/26/27, sem bloqueio pelas outras. Somente a liberação conjunta 28 reúne as três. OSC C não é pré-requisito. Não há contrato novo de factory assets imposto em 25 como bloqueio oculto para 26/27: o primeiro a implementar estabelece a extensão mínima e os demais a reutilizam.

## Rastreabilidade de capacidades observadas

Cobertura abaixo é **proposta**, não implementação nem aprovação. Probes usam fixtures e experimentos do ledger; nomes não estabelecem algoritmos. Nenhuma família ausente é considerada concluída por equivalência presumida.

| Capacidade/família | Ticket / decisão explícita |
|---|---|
| Paths relativo/absoluto, embedded audio, channels/frames/rate | 01–04: container Borato, validação e portabilidade. |
| Base note/detune, trim head/tail, edge fade/reverse | 04/17: incluir fine tune e fades editáveis; testar cents, PCM e limites. |
| None/forward/reverse/ping-pong/tailed; loop bounds/crossfade/link/release | 17: link ancora loop à região por regra documentada; fixture distingue saída no release. |
| Slice markers/root, single-slice/play-to-end | 18: mapeamento e ordem de frames verificáveis. |
| Auto-slice threshold | **Opcional separado após18, pendente de aprovação:** detector com threshold, preview/undo; fixture de8ataques, todos detectados dentro de5ms e nenhum falso corte em seno sustentado. Não incluído silenciosamente no slicing manual. |
| Key/velocity min/max | 07/08: seleção explícita; velocity MIDI0 tratado como note-off. |
| Key-zone fold/warp | **Probe + proposta opcional após07:** mapa com tons distinguíveis para determinar wrap/fold/transposição; não incluso na seleção básica. |
| Manual/wrap position, scan range/rate/BPM/base tempo/tempo lock | **Probe + extensão opcional após17:** definir scrubbing e avanço versus pitch;06 não cobre scan e nome scan rate não prova time stretch. |
| SFZ, layering, loop por zona | 07–09/19: subset documentado; round robin é escolha Borato. Release-trigger zones são extensão opcional após09, com semântica note-off/pedal/steal a aprovar. |
| Density Free/BPM/Grains e Length Free/BPM/Percent | 11/20: target overlap e percentual com contratos originais, não leis Serum. |
| Dotted/triplet, Together/Even/Exponential/Random, jump-start | 20: contagem/onsets e semânticas explicitadas; probes distinguem interpretações. |
| Max grains, loop grains, length keytracking/reverse | 10/20/21: pool32 é decisão Borato; não máximo Serum afirmado. |
| Dez janelas, amount/skew | 21: dez fórmulas/referências e testes numéricos, não apenas três. |
| Random offset/direction/pitch/length/pan/gain/window amount/skew/warp | 21: cada dimensão limitada e RNG determinístico; distribuições Serum desconhecidas. |
| Dois slots warp granular | 21: identidade/bend originais e ordem; catálogo restante requer probe e fatias futuras. |
| Complex spectrum/transform/overlap-add | 13: STFT2048/512 proposto;4096/1024 observado em um caminho não é universal. |
| Phase lock/transients/smoothing, freeze, tempo/pitch | 14–16/22: identidade/latência/fase verificadas, sem equivalência runtime presumida. |
| Dois warp slots espectrais | 23: arquitetura e primeiros modos originais. |
| Detune/smear/spread, harmonic/subharmonic, gate/robotize, shift/mirror, peaks, Shepard/comb, pitch/phase/formant, mask/vocode | Shift original em23 e pitch em15. **Demais famílias: probes e propostas separadas após23**, com contrato ganho/fase/CPU antes de implementar. Não declaradas cobertas só por existirem dois slots. |
| Filter-table path/relative/embedded e metadata | 24 usando03: incluir curva/asset versionado importável e persistido, validar payload; formato Serum não presumido. |
| Filter shift/wet, low/high e pre/post low/high | 24: seleção pre/post com teste da ordem; curva e dry alinhado. |
| Conteúdo, qualidade e tempo real | 25–28: gates Borato; aprovação numérica não demonstra timbres idênticos. |

Itens opcionais/probes permanecem decisões visíveis para a aprovação; não recebem publicação ou promessa de execução nesta rodada. Se o objetivo aprovado exigir o catálogo inteiro, criar novas fatias após esclarecer as semânticas, sem esconder esse trabalho nos28tickets.

## Aprovação antes de publicar

Confirmar granularidade, dependências e fusões/divisões, incluindo se 21 deve ser separado por grupo de controles. Aprovar como escolhas Borato: STFT inicial 2048/hop 512, limites de memória/grãos, semântica percentual de 20, transformações originais de 23 e modo selecionado na fonte SAMPLE. Round robin é decisão de produto e não inferência automática sobre cada modo Serum.

Após aprovação, criar um arquivo por ticket em `issues/`, com `Status: ready-for-agent`, blockers explícitos e critérios aprovados. Nenhum arquivo de issue foi criado; iniciativas anteriores permanecem intactas. Números científicos da proposta são metas verificáveis, sujeitos à aprovação, não resultados já obtidos.

