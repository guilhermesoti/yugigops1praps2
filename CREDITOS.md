# Créditos

Este port é **gratuito** e sem fins lucrativos. Ele só existe por causa do trabalho de muita gente
antes dele: abaixo está quem fez cada parte.

## O port para PS2

| Quem | O que fez |
|---|---|
| **Guilherme Soti** — [Retrogames Patos de Minas](https://github.com/guilhermesoti) | Ideia e direção do projeto, todos os testes num PlayStation 2 de verdade (vídeos, fotos, logs e capturas da VRAM do console), escolhas de visual |
| **Claude** (Anthropic, pelo Claude Code) | Desenvolvimento do port: camada PS2 (memória pela TLB, renderizador no GS, controle, áudio, USB, thread de leitura do disco), correções a partir dos testes no console, documentação |
| **Gemini** (Google) | Revisões técnicas do código que encontraram problemas reais: o mapeamento de TLB que travava o console, o log que engasgava o USB, os registradores de 64 bits no `setjmp`, os gargalos dos vídeos (DMAs inúteis, busca linear do VLC, a espera do MDEC) |
| **ChatGPT** (OpenAI) | Revisões técnicas junto com o Gemini (áudio durante os vídeos) e o conceito visual do menu de salvar/carregar ([docs/relatorio-menu-save-load.md](docs/relatorio-menu-save-load.md)) |

## O jogo decompilado (a base de tudo)

- **[Yu-Gi-Oh! Forbidden Memories Recompiled](https://github.com/Unchiga/Yu-Gi-Oh-Forbidden-Memories-Recompiled)**
  — por **Unchiga** e os contribuidores do Re-Decomp. A decompilação completa do jogo em C e o port
  nativo para PC, de onde vêm `src/game/`, `src/pc/`, `tools/` e quase todo o código deste
  repositório. Licença MIT (veja [LICENSE](LICENSE)). Este port parte do commit `36d499c`.
- **[memories-decomp](https://github.com/krystalgamer/memories-decomp)** — por **krystalgamer** e
  colaboradores: a decompilação original em que o projeto acima se baseia. O código decompilado
  pertence aos seus autores.

## Ferramentas e bibliotecas do PlayStation 2

- **[ps2dev](https://github.com/ps2dev/ps2dev)** — o compilador (`ee-gcc`) e as ferramentas para o
  Emotion Engine e o IOP.
- **[PS2SDK](https://github.com/ps2dev/ps2sdk)** — o kernel, os drivers do IOP (USB, FAT/exFAT,
  controle, `audsrv`, `fileXio`) e as bibliotecas usadas pelo port.
- **[gsKit](https://github.com/ps2dev/gsKit)** — biblioteca gráfica do GS.
- **[ps2sdk-ports](https://github.com/ps2dev/ps2sdk-ports)** — libpng, zlib e FreeType compiladas
  para o PS2.

## Para testar e rodar

- **[PCSX2](https://github.com/PCSX2/pcsx2)** — o emulador de PS2 em que o port foi testado e
  depurado antes de cada versão ir para o console.
- **[Open PS2 Loader (OPL)](https://github.com/ps2homebrew/Open-PS2-Loader)** e
  **[wLaunchELF](https://github.com/ps2homebrew/wLaunchELF)** — para abrir o jogo pelo pendrive no
  console.

## O jogo

*Yu-Gi-Oh! Forbidden Memories* © Konami. *Yu-Gi-Oh!* © 1996 Kazuki Takahashi. Este repositório
**não** contém o jogo nem seus dados: é preciso ter a sua própria cópia do disco. Projeto feito por
fãs, sem ligação com a Konami.
