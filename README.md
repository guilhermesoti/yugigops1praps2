# Yu-Gi-Oh! Forbidden Memories — port nativo para PlayStation 2

Port **nativo** de *Yu-Gi-Oh! Forbidden Memories* (PS1, versão americana SLUS-01411) para o
PlayStation 2. Não é um emulador: o código do jogo (decompilado para C) é compilado para o
processador do PS2 (Emotion Engine, MIPS R5900) e a imagem é desenhada pelo chip gráfico do PS2
(o GS). Os dados do jogo (cartas, músicas, modelos 3D e vídeos) são lidos da sua própria imagem
`.bin` do disco, que **não** vem neste repositório.

- Como funciona por dentro, mapa das pastas e pontos de atenção para quem for analisar o código:
  **[ARQUITETURA.md](ARQUITETURA.md)**
- Projeto em que este port se baseia: a decompilação + port de PC
  [Unchiga/Yu-Gi-Oh-Forbidden-Memories-Recompiled](https://github.com/Unchiga/Yu-Gi-Oh-Forbidden-Memories-Recompiled)
  (commit `36d499c`). O README original está em
  [docs/projeto-base/README-original.md](docs/projeto-base/README-original.md).

## Como jogar

1. No pendrive (FAT32 ou exFAT), crie uma pasta (por exemplo `APPS/YFM`).
2. Copie para ela:
   - `release/YFM_PS2.ELF`
   - a imagem do seu disco renomeada para **`YFM.bin`** (o nome original
     `Yu-Gi-Oh! Forbidden Memories (USA).bin` também funciona).
3. No PS2 abra o ELF pelo OPL (como app) ou pelo wLaunchELF.
4. Os saves ficam em `YFM-SAVE/saves/` dentro da mesma pasta.

No PCSX2: *Sistema → Iniciar ELF*, com o `YFM.bin` na mesma pasta do ELF.

Controle: igual ao do PS1. Ao escolher o alvo de um ataque, **Quadrado** mostra a batalha 3D dos
monstros e **X** faz o ataque rápido.

## Situação

Testado no PCSX2 e num PS2 de verdade (USB exFAT, pelo OPL):

| Parte | Situação |
|---|---|
| Abertura, título, menus, nome, prólogo, montar deck | funciona |
| Duelos (mão, campo 3D, estrelas, fusão, campos, CPU) | funciona, 60 fps |
| Batalha 3D dos monstros (Quadrado) | funciona, 60 fps |
| Vídeos (STR) | 15 fps no PCSX2; no console ainda em ajuste |
| Som | funciona (mixado a 22 kHz) |
| Save / load | funciona: 5 slots em `YFM-SAVE/saves/` |

O jogo grava `YFM-LOG.txt` ao lado do ELF e mostra as etapas da inicialização na tela. Se algo der
errado, mande o `YFM-LOG.txt` e uma foto da tela.

## Como compilar

Precisa de Linux (ou WSL), `python3` e o toolchain do PS2 (`ps2dev`: ee-gcc, PS2SDK, gsKit e os
ports libpng, zlib e freetype).

```sh
export PS2DEV=/usr/local/ps2dev     # onde o ps2dev está instalado
./build.sh                          # compila e copia para release/YFM_PS2.ELF
```

O build não precisa da imagem do jogo.

## Licença

Código sob licença MIT (veja [LICENSE](LICENSE)). A decompilação pertence aos seus autores.
Yu-Gi-Oh! e Forbidden Memories são marcas da Konami; você precisa da sua própria cópia do jogo.
