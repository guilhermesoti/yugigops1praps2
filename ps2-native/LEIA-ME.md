# Yu-Gi-Oh! Forbidden Memories — port nativo para PS2

Este é um **port de verdade**, não um emulador: o código do jogo (a decompilação
completa de Forbidden Memories USA, SLUS-01411) é compilado em C para o
processador do PS2 (Emotion Engine, MIPS R5900) e a imagem é desenhada direto
pelo GS do PS2. Os dados (cartas, músicas, modelos 3D, vídeos) são lidos da sua
própria imagem `.bin` do jogo, que **não** vem junto.

## Como jogar (pendrive + OPL ou uLaunchELF)

1. No pendrive (FAT32), crie a pasta `YFM`.
2. Copie para ela:
   - `YFM_PS2.ELF` (pasta `release/`)
   - a imagem do seu disco renomeada para **`YFM.bin`**
     (o arquivo `Yu-Gi-Oh! Forbidden Memories (USA).bin` também funciona com o
     nome original).
3. No PS2, abra `mass:/YFM/YFM_PS2.ELF` pelo uLaunchELF / wLaunchELF, ou pelo
   OPL como app (crie um `title.cfg` apontando para o ELF).
4. Os saves ficam em `YFM/YFM-SAVE/` no pendrive (é o "memory card" do jogo).

No PCSX2 (Windows): *Sistema → Iniciar ELF*, com `YFM.bin` na mesma pasta do ELF.

## Controles

O controle do PS2 funciona exatamente como o do PS1 (porta 1).
Dica do jogo: ao escolher o alvo do ataque, **Quadrado** mostra a batalha 3D dos
monstros; **X** faz o ataque rápido.

## Situação

Testado no PCSX2 e num PS2 de verdade (USB em exFAT, pelo OPL):

- Abertura, título, menus, prólogo, montagem de deck
- Duelos completos: mão, campo 3D, estrelas guardiãs, fusão, cartas de campo,
  turnos da CPU, batalhas 2D e **batalhas 3D com os modelos dos monstros**
- 60 fps nos duelos e batalhas; vídeos a 15 fps (a velocidade original deles)
- Som pelo `audsrv` (mixado a 22 kHz)
- Tela de save/load por cima do jogo, com 5 slots em `YFM-SAVE/saves/`
  (a gravação no pendrive ainda está sendo testada no console)

## Diagnóstico

Esta versão grava `YFM-LOG.txt` ao lado do ELF e mostra as etapas da
inicialização na tela. Se uma etapa travar o console, ela fica anotada em
`YFM-SAVE/passo.txt` e é pulada no próximo boot. Se der problema, mande o
`YFM-LOG.txt` e uma foto da tela.

## Como compilar

Precisa do toolchain/SDK do PS2 (ps2dev, com gsKit, libpng, zlib e freetype dos
ports), `git` e `python3`:

```sh
export PS2DEV=/usr/local/ps2dev
./build.sh            # baixa a decompilação na versão certa, aplica o patch e compila
```

O resultado sai em `YFM_PS2.ELF`.

## Estrutura

- `patches/upstream.patch` — mudanças no projeto base
  (Unchiga/Yu-Gi-Oh-Forbidden-Memories-Recompiled, commit `36d499c`)
- `src-ps2/` — a camada PS2: mapeamento de memória (TLB), renderizador GS,
  entrada, áudio, USB, IOP
- `ps2/` — o script de build e o linkfile do EE
- `release/YFM_PS2.ELF` — o executável pronto

## Licenças

O código de `src-ps2/`, `ps2/` e do patch segue a licença MIT do projeto base.
A decompilação pertence aos seus autores. Yu-Gi-Oh! e Forbidden Memories são da
Konami; você precisa da sua própria cópia do jogo.
