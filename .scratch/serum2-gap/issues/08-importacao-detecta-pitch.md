# 08: Importar áudio com detecção de pitch

**What to build:** Uma opção de importação wavetable que detecta o período fundamental e cria ciclos afinados a partir do áudio escolhido.

**Blocked by:** None (can start immediately).

**Status:** ready-for-human

- [x] Mostrar o pitch detectado e permitir corrigir resultado ambíguo antes de aceitar a importação.
- [x] Verificar afinação, limites de duração, silêncio, estéreo e arquivos inválidos com casos determinísticos.
- [ ] Comparar auditivamente a nova importação à conversão por frames existente.

## Progress

IMPORT WAV abre um diálogo depois de escolher o arquivo:
- **Detecção:** YIN sobre a janela mais forte, de 25 Hz a 2 kHz. O período é refinado num múltiplo dele, com erro abaixo de 1 cent a 44.1/48/96 kHz.
- **Correção:** o diálogo mostra nota, cents, Hz, confiança e quantos ciclos. Marca AMBÍGUO quando não há período claro ou quando a oitava abaixo encaixa muito melhor (fundamental fraca). OCTAVE -/+ corrigem o resultado.
- **Escolha:** IMPORT TUNED (um ciclo por período, até 256 espalhados pelo som) ou IMPORT FRAMES (a conversão antiga).
- **Limites:** lê até 20 s. Recusa áudio mudo, curto demais e arquivos ilegíveis, com o motivo.
- **Testes (`test_tuned_import`):** detecção, ambiguidade, ciclos de um período, estéreo, arquivo longo, mudo, curto, inválido e o diálogo. Tocado em C4, o parcial mais forte é a fundamental (260.7 Hz) no tuned e 1570 Hz no frames.
- **Pendente:** a comparação auditiva tuned × frames, que é humana.
