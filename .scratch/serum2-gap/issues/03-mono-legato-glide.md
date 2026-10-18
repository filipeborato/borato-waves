# 03: Tocar em mono, legato e glide

**What to build:** Um modo de performance para basses e leads com mono/legato/glide controláveis na interface e salvos em presets.

**Blocked by:** 02: Validar sustain e transições de voz.

**Status:** ready-for-human

- [x] Definir e testar prioridade de notas, retrigger e interação entre pedal, legato e note-off.
- [x] Automatizar os controles sem cliques e preservar o comportamento de projetos antigos.
- [ ] Demonstrar frases de bass e lead em Standalone e VST3.

## Progress

Os parâmetros `voiceMode` (Poly/Mono/Legato) e `glide` ficam no topo da interface. Projetos antigos carregam em Poly sem glide. As regras de prioridade, retrigger e pedal estão em [VOICES.md](../../../docs/VOICES.md), e o `test_voices` as cobre. Uma troca de modo solta as notas com release (sem clique) e mantém o estado do pedal. Falta tocar frases de bass e lead no Standalone e no VST3.
