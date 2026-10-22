# 02: Importar e cancelar áudio em segundo plano

**What to build:** Importação de sample em segundo plano com progresso e cancelamento; o instrumento antigo continua tocando até a publicação e somente o pedido mais recente pode publicar. Restauração com asset externo usa o mesmo caminho e indica carregamento.

**Blocked by:** 01: Trocar samples validados sem perder recursos em uso.

**Status:** ready-for-human

- [x] Executar decodificação e validação de sample fora da thread de áudio e sem bloquear a UI, expondo progresso e cancelamento.
- [x] Garantir que apenas o pedido mais recente (`requestId`) publique ao concluir fora de ordem (iniciar A, iniciar B, cancelar A, concluir fora de ordem: somente B fica ativo).
- [x] Garantir segurança ao cancelar/fechar editor e ao destruir o `WaveProcessor` durante um job em andamento, com limite explícito de jobs/dados pendentes.
- [x] Usar o mesmo caminho na restauração de asset externo indicando carregamento, e testar com eventos instrumentados (sem `sleep` frágil).
