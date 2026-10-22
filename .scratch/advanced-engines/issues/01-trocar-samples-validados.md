# 01: Trocar samples validados sem perder recursos em uso

**What to build:** Carregar e trocar sample enquanto toca, mantendo o anterior em caso de erro; publicação e undo respeitam o ciclo de vida real das vozes (confirmação por geração sem timeout cego de `65536` amostras) e um orçamento agregado de memória de 256 MiB visível em mensagens de erro.

**Blocked by:** None (can start immediately).

**Status:** ready-for-human

- [x] Recusar leitura incompleta, arquivo vazio, valores `NaN`/`Inf` e excesso de canais (> 2), preservando o sample anterior intacto.
- [x] Remover a liberação cega após `kSafeSamples` (`65536` amostras) em `PublishSlot<T>` e exigir confirmação por geração de todas as vozes antes de liberar recursos aposentados (`retired`).
- [x] Aplicar orçamento agregado de 256 MiB cobrindo assets ativos, recursos aposentados e histórico de `undo`, com política explícita de evicção de passos antigos de `undo` e recusa com mensagem de erro quando recursos em uso excederem o limite.
- [x] Verificar em teste de estresse com 100 substituições e leitor antigo artificialmente retido por 2 s a 96 kHz: nenhum acesso após liberação, coleta após última confirmação e contador de alocação/destruição no callback igual a zero.

## Progress

### Hipóteses e Resultados
1. **Confirmação por geração vs. `kSafeSamples` (`PublishSlot<T>` em `Source/UserWavetable.h`):**
   - *Hipótese:* Remover `kSafeSamples = 65536` e usar contadores monotônicos `generation` / `settledGeneration` evita tanto a destruição prematura de leitores retidos por mais de 65.536 amostras (ex.: 2 s a 96 kHz = 192.000 amostras) quanto o problema ABA ao desfazer (`undo`) para um `shared_ptr` já visto anteriormente.
   - *Resultado:* Confirmado em `testStress100SwapsRetainedReader96kHz` (`tests/test_sample.cpp`) com 8 vozes a 96 kHz, blocos de 64/128/512 e bloco parcial de 37 amostras, 100 substituições e `undo`/`redo` ABA no meio da janela de retenção de 2 s: o sample inicial permaneceu vivo durante toda a retenção e foi coletado imediatamente após a confirmação final (`retiredMemoryBytes() == 0`).
2. **Validação do decoder (`bw::decodeSample` / `bw::loadSample` em `Source/Sample.h`):**
   - *Hipótese:* Verificar tamanho do arquivo vs. bytes PCM esperados em WAV/AIFF, valor de retorno de `reader.read(...)`, `std::isfinite` em todos os canais antes do mixdown e limite `kMaxSampleChannels = 2` impede a publicação de arquivos corrompidos, truncados, vazios, multicanal (> 2) ou com `NaN`/`Inf`.
   - *Resultado:* Confirmado em `testLimits` (`tests/test_sample.cpp`); todas as falhas preservam o ponteiro e o áudio da amostra anterior em reprodução contínua.
3. **Orçamento agregado de 256 MiB (`WaveProcessor` em `Source/PluginProcessor.*`):**
   - *Política documentada:* Ao carregar um novo asset (`loadSample` / `publishCycles`), o processador coleta itens `retired` já assentados e soma os bytes únicos (deduplicados por endereço) de todos os assets ativos, `retired` ainda retidos pelas vozes e o novo candidato. Se esse subconjunto não-evictável exceder `256 MiB`, o carregamento é recusado com mensagem explícita (`"Sample exceeds 256 MB memory budget"`) e o asset anterior continua ativo. Caso caiba, os passos mais antigos de `history` (`undo`) são evictados até que a soma total deduplicada (`ativos + retired + undo`) fique `<= 256 MiB`.
   - *Resultado:* Confirmado em `testMemoryBudgetAndUndoEviction` (`tests/test_sample.cpp`) com instâncias de 16 MiB (`2^22` floats): 20 cargas com vozes assentadas evictam passos antigos mantendo o uso `<= 256 MiB`, e 16 cargas retidas por uma voz bloqueiam a 17ª carga (272 MiB) até a conclusão da retenção.
4. **Zero alocações/destruições no callback de áudio:**
   - *Resultado:* Instrumentação global de `operator new`/`delete` em `test_sample.cpp` confirmou `0` operações de heap dentro de `processBlock` durante todos os cenários de estresse, roubo de vozes (16+ notas), pedal de sustain, mono/legato e all-notes-off.
