# Tradução PT-BR

`languages/pt-br.txt` é gerado por `montar_pt_br.py` a partir de `languages/es.txt`
(o texto espanhol do disco PAL: mesmos códigos `{...}`, ids e tamanhos de caixa):

- `names_pt*.tsv`: `ID<TAB>nome` (cartas, tipos, estrelas, duelistas); os lugares estão no script.
- `lines_d*.tsv`: `linha<TAB>texto` das falas e menus (número da linha no `es.txt`, base 0);
  os códigos `{...}` têm de ser os mesmos da linha espanhola (o script confere).
- `desc_pt*.tsv`: `ID<TAB>descrição`; o script quebra em 20 colunas, até 8 linhas.

Para corrigir um texto: edite o `.tsv` e rode `python3 tools/ps2/pt_br/montar_pt_br.py`.
O script avisa de códigos diferentes, falas com mais de 34 letras e descrições grandes demais.
