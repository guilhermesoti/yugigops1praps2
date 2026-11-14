<!-- Texto para colar na página da release. O zip é anexado sozinho pela Action (.github/workflows/release.yml). -->

Primeira versão pública (beta) do port **nativo** de Yu-Gi-Oh! Forbidden Memories (PS1, SLUS-01411) para PlayStation 2. Não é emulador: o código do jogo roda direto no Emotion Engine e a imagem é desenhada pelo GS.

## Como jogar
1. Baixe o `YFM_PS2_v0.1.0-beta.zip` e extraia a pasta `YFM` no pendrive (FAT32 ou exFAT).
2. Coloque na pasta a imagem do **seu** disco renomeada para `YFM.bin`. **A imagem do jogo não vem no pacote.**
3. Abra o `YFM_PS2.ELF` pelo OPL (como app) ou pelo wLaunchELF. No PCSX2, use *Sistema → Iniciar ELF*.

## O que funciona
- Abertura, menus, prólogo, montagem de deck
- Duelos completos com campo 3D a 60 fps e batalha 3D dos monstros
- Vídeos, som e save/load (5 slots em `YFM-SAVE/saves/`)

Testado no PCSX2 e num PS2 de verdade (USB exFAT via OPL).

## Problemas conhecidos
- Caixa do FIELD pode aparecer cortada embaixo na visão do tabuleiro 3D
- Vídeos ainda em ajuste fino no console

Achou um bug? Abra uma issue com o `YFM-LOG.txt` (criado ao lado do ELF) e uma foto da tela.

## Apoie
Projeto gratuito e sem fins lucrativos. Se quiser apoiar os próximos ports: Pix e Ko-fi na seção [Apoie o projeto](https://github.com/guilhermesoti/yugiohps1praps2#apoie-o-projeto).

Créditos completos em [CREDITOS.md](https://github.com/guilhermesoti/yugiohps1praps2/blob/main/CREDITOS.md). Yu-Gi-Oh! é marca de Konami / Kazuki Takahashi; projeto de fã sem ligação com a Konami.
