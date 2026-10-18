# 06: Reproduzir samples como fonte sonora

**What to build:** Reprodução de áudio importado com início, fim e loop ajustáveis na GUI, distinta da importação que converte áudio em frames wavetable.

**Blocked by:** 01: Medir o orçamento de CPU por patch.

**Status:** ready-for-human

- [x] Reproduzir e salvar um patch de sample com controles automatizáveis e restauração no host.
- [x] Tratar arquivo ausente, troca durante notas ativas, limites de memória e projetos antigos sem perda de estado.
- [x] Medir custo por voz e verificar loop sem clique em diferentes alturas e sample rates.

## Progress

Fonte SAMPLE (aba SAMPLE), separada do IMPORT WAV de wavetable:
- **Controles:** liga/desliga, nível, pitch, início, fim e loop, todos automatizáveis.
- **Loop:** crossfade de 10 ms na volta. Início e fim mudam a partir da próxima volta, então o cabeçote nunca salta.
- **Estado:** guarda o caminho do arquivo. Arquivo ausente toca silêncio, mostra MISSING e mantém a referência; ao reaparecer no mesmo caminho, o som volta.
- **Limite:** 2^22 frames por arquivo; acima disso o arquivo é recusado e o sample atual fica.
- **Troca durante notas:** a voz faz crossfade de 5 ms e o áudio antigo é liberado quando nenhuma voz o lê mais.
- **Testes (`test_sample`):** cobrem tudo isso; sabotagens confirmaram que o crossfade, o fade de troca e a liberação são verificados.
- **Custo por voz:** cerca de 1,4× uma voz de wavetable de uma lane (`sample16`).
- **Ressalvas:** sem anti-alias ao transpor muito para cima. Implementado a pedido apesar do bloqueio pelo 01.
- **Pendente:** salvar e reabrir num host real.
