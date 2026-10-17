# Serum 2: evidência pública para priorização

Pesquisa em 17/10/2026. **Capacidade oficial** indica documentação da Xfer; **relato** indica opinião ou problema descrito por uma pessoa, sem representar a base de usuários. A prioridade proposta é uma inferência para o Borato Wave, não uma afirmação da Xfer.

## Capacidades confirmadas pela Xfer

- O Serum 2 tem **três osciladores principais A/B/C**, além de sub e noise. Cada principal pode usar wavetable, sample, multisample, granular ou spectral. A novidade é, portanto, a combinação de **mais uma camada com motores diferentes**, além da terceira voz em si. [What's New in Serum 2, pp. 3–6](https://static.xferrecords.com/Serum%202%20What's%20New.pdf).
- A mesma fonte documenta interpolação suave entre frames, **dois warps simultâneos**, FM verdadeira, phase distortion, ajustes de fase/unison e roteamento por oscilador; até 10 LFOs, oito macros, quatro envelopes, filtros duplos e 13 efeitos em vários buses. [What's New in Serum 2, pp. 3, 7](https://static.xferrecords.com/Serum%202%20What's%20New.pdf). A [página do produto](https://xferrecords.com/products/serum-2) detalha importação SFZ, sample slicing/loops, granular e ressíntese espectral.
- A Xfer descreve o objetivo do instrumento como **som de alta qualidade aliado a edição visual e fluxo criativo**; três osciladores são uma parte dessa proposta, não seu critério único. [Produto oficial](https://xferrecords.com/products/serum-2).

## Sinais de uso e limites dos fóruns

- Um usuário elogiou cortar áudio nos motores sample/granular/spectral, modular warps e usar clip/arp no mesmo fluxo; outro destacou poder usar as três posições como camadas de sample. Isto favorece **combinações de motores e manipulação imediata do material**. [r/serum, 21/03/2025](https://www.reddit.com/r/serum/comments/1jg9eo3/serum_2_is_like_a_baby_daw_now/).
- Antes do lançamento, pedidos incluíam terceiro oscilador, filtros melhores, roteamento de efeitos e undo/redo; outros participantes preferiam simplicidade. Assim, o terceiro oscilador tem demanda real, mas não há evidência de que seja a primeira melhoria de maior valor. [r/serum, discussão iniciada em 18/05/2024](https://www.reddit.com/r/serum/comments/1cuuwnq/serum_2/).
- Um relato de criação com granular usa uma vogal curta, LFO e unison para formar atmosfera. Outro mostra interesse em interação espectral entre osciladores. Esses exemplos apontam para **movimento e interação entre fontes**, não apenas contagem de fontes. [r/serum, 10/05/2025](https://www.reddit.com/r/serum/comments/1kizq8z/granulator_in_serum_2/); [r/serum, 15/04/2025](https://www.reddit.com/r/serum/comments/1jzzujh/just_found_out_something_insane/).
- Há relatos de presets com custo de CPU alto. A própria Xfer explica que unison/grains, liberação longa, spectral e certos filtros aumentam o custo, sobretudo combinados. Isto torna **medição de CPU por voz e patch** um critério obrigatório antes de acrescentar um terceiro motor ou granular. [Fórum Xfer, 21/09–24/10/2025](https://xferrecords.com/forums/general/serum-2-overloads-the-cpu).

## Decisão sugerida para o Borato Wave

**Não priorizar OSC C agora.** O Borato já tem A/B, sub e noise, mas ainda carece da interação e das opções de timbre que fazem três posições úteis no Serum 2. Primeiro, acrescentar uma operação de warp/phase distortion modulável de boa qualidade, com controle de aliasing e comparação auditiva em presets; depois, avaliar um fluxo simples de sample playback (distinto da conversão atual de arquivo para wavetable). O terceiro oscilador passa a fazer sentido quando um patch demonstrável precisar de **duas camadas de wavetable mais uma camada independente**, ou de sample mais duas camadas, sem alternativa prática em A/B/sub/noise. Antes de incluí-lo, medir custo com acordes, unison e FX; definir roteamento, modulação, serialização e espaço na GUI.

Uma hipótese de produto a validar com músicos é que presets expressivos, warp e tratamento de amostras rendam mais variedade percebida que OSC C isolado. Os fóruns acima são exemplos qualitativos, não pesquisa representativa nem prova de preferência majoritária.
