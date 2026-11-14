/* Monta a ISO de PS2 do port do Yu-Gi-Oh! Forbidden Memories.
 *
 * Uso (Windows): ponha montar_iso.exe na pasta com o YFM_PS2.ELF, a pasta
 * languages (com o pt-br.txt) e a imagem .bin do SEU disco de PS1, e dê dois
 * cliques. Ou na linha de comando:
 *
 *     montar_iso YFM_PS2.ELF "Yu-Gi-Oh! Forbidden Memories (USA).bin" [pt-br.txt] [saida.iso]
 *
 * A ISO (ISO-9660 simples) leva:
 *     SYSTEM.CNF        BOOT2 = cdrom0:\RGPM_001.01;1
 *     RGPM_001.01       o ELF (o ID que o OPL usa para as capas e a config)
 *     YFM.BIN           a imagem do disco de PS1
 *     LANG/PTBR.TXT     o português
 * Compila em qualquer C99: cc -O2 -o montar_iso montar_iso.c */
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#undef fseeko
#undef ftello
#define fseeko _fseeki64
#define ftello _ftelli64
#else
#include <dirent.h>
#endif

#define SECTOR 2048
#define GAME_ID "RGPM_001.01"

typedef struct {
    const char *name; /* ISO name, sem ";1" */
    const char *path; /* no disco; NULL para os dados em memória */
    const unsigned char *data;
    uint32_t size, lba;
} Entry;

static unsigned char sector[SECTOR];

static void le32(unsigned char *p, uint32_t v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; }
static void be32(unsigned char *p, uint32_t v) { p[3] = v, p[2] = v >> 8, p[1] = v >> 16, p[0] = v >> 24; }
static void both32(unsigned char *p, uint32_t v) { le32(p, v), be32(p + 4, v); }
static void both16(unsigned char *p, uint16_t v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 8, p[3] = v; }

static uint64_t file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint64_t size;
    if (!f) return (uint64_t)-1;
    fseeko(f, 0, SEEK_END);
    size = (uint64_t)ftello(f);
    fclose(f);
    return size;
}

static void record_date(unsigned char *p)
{
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    p[0] = (unsigned char)(t->tm_year), p[1] = (unsigned char)(t->tm_mon + 1), p[2] = (unsigned char)t->tm_mday;
    p[3] = (unsigned char)t->tm_hour, p[4] = (unsigned char)t->tm_min, p[5] = (unsigned char)t->tm_sec, p[6] = 0;
}

/* Um registro de diretório em `p`; devolve o tamanho. */
static int dir_record(unsigned char *p, const char *id, int id_len, uint32_t lba, uint32_t size, int is_dir)
{
    int len = 33 + id_len + ((33 + id_len) & 1);
    memset(p, 0, (size_t)len);
    p[0] = (unsigned char)len;
    both32(p + 2, lba);
    both32(p + 10, size);
    record_date(p + 18);
    p[25] = is_dir ? 2 : 0;
    both16(p + 28, 1);
    p[32] = (unsigned char)id_len;
    memcpy(p + 33, id, (size_t)id_len);
    return len;
}

static int file_record(unsigned char *p, const Entry *e)
{
    char id[40];
    snprintf(id, sizeof(id), "%s;1", e->name);
    return dir_record(p, id, (int)strlen(id), e->lba, e->size, 0);
}

static void pad_text(unsigned char *p, const char *text, int width)
{
    int n = (int)strlen(text);
    memset(p, ' ', (size_t)width);
    memcpy(p, text, (size_t)(n < width ? n : width));
}

static int write_sector(FILE *out) { return fwrite(sector, 1, SECTOR, out) == SECTOR ? 0 : -1; }

static int copy_file(FILE *out, const Entry *e)
{
    static unsigned char buffer[1 << 20];
    uint64_t left = e->size;
    size_t got;
    FILE *in;
    if (e->data) {
        if (fwrite(e->data, 1, e->size, out) != e->size) return -1;
    } else {
        if (!(in = fopen(e->path, "rb"))) return -1;
        while (left && (got = fread(buffer, 1, left < sizeof(buffer) ? (size_t)left : sizeof(buffer), in)) > 0) {
            if (fwrite(buffer, 1, got, out) != got) {
                fclose(in);
                return -1;
            }
            left -= got;
        }
        fclose(in);
        if (left) return -1;
    }
    /* Completa o setor. */
    if (e->size % SECTOR) {
        memset(sector, 0, SECTOR);
        if (fwrite(sector, 1, SECTOR - e->size % SECTOR, out) != SECTOR - e->size % SECTOR) return -1;
    }
    return 0;
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail), i;
    if (a < b) return 0;
    for (i = 0; i < b; i++) {
        char x = s[a - b + i], y = tail[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

/* Sem argumentos: procura o .bin na pasta atual. */
static const char *find_bin(void)
{
    static char name[1024];
#ifdef _WIN32
    struct _finddata_t found;
    intptr_t h = _findfirst("*.bin", &found);
    if (h == -1) return NULL;
    do {
        if (found.size > 100L * 1024 * 1024 || found.size < 0) {
            snprintf(name, sizeof(name), "%s", found.name);
            _findclose(h);
            return name;
        }
    } while (_findnext(h, &found) == 0);
    _findclose(h);
#else
    DIR *d = opendir(".");
    struct dirent *e;
    if (!d) return NULL;
    while ((e = readdir(d))) {
        if (ends_with(e->d_name, ".bin") && file_size(e->d_name) > 100ull * 1024 * 1024) {
            snprintf(name, sizeof(name), "%s", e->d_name);
            closedir(d);
            return name;
        }
    }
    closedir(d);
#endif
    return NULL;
}

static void finish(int code)
{
#ifdef _WIN32
    printf("\nPressione Enter para sair...");
    getchar();
#endif
    exit(code);
}

int main(int argc, char **argv)
{
    const char *elf = "YFM_PS2.ELF", *bin = NULL, *lang = NULL, *out_path = GAME_ID ".Yu-Gi-Oh FM PS2.iso";
    static unsigned char cnf[128];
    Entry files[4], lang_file;
    int nfiles = 0, have_lang, i;
    uint32_t lba;
    uint64_t size;
    FILE *out;
    unsigned char root[SECTOR], langdir[SECTOR], ptl[SECTOR], ptm[SECTOR];
    int root_len = 0, lang_len = 0;

    printf("Montador de ISO - Yu-Gi-Oh! Forbidden Memories PS2 (Retrogames)\n\n");
    for (i = 1; i < argc; i++) {
        if (ends_with(argv[i], ".elf")) elf = argv[i];
        else if (ends_with(argv[i], ".bin")) bin = argv[i];
        else if (ends_with(argv[i], ".txt")) lang = argv[i];
        else if (ends_with(argv[i], ".iso")) out_path = argv[i];
    }
    if (!bin) bin = find_bin();
    if (!lang && file_size("languages/pt-br.txt") != (uint64_t)-1) lang = "languages/pt-br.txt";
    if (!lang && file_size("pt-br.txt") != (uint64_t)-1) lang = "pt-br.txt";
    if (file_size(elf) == (uint64_t)-1) {
        printf("Nao achei o %s. Coloque este programa na pasta do port.\n", elf);
        finish(1);
    }
    if (!bin || (size = file_size(bin)) == (uint64_t)-1) {
        printf("Nao achei a imagem .bin do Yu-Gi-Oh! Forbidden Memories (USA) nesta pasta.\n");
        finish(1);
    }
    if (size >= 0xFFFFFFFFull) {
        printf("A imagem %s e grande demais.\n", bin);
        finish(1);
    }
    have_lang = lang != NULL;
    printf("ELF:    %s\nJogo:   %s\nIdioma: %s\nSaida:  %s\n\n", elf, bin, have_lang ? lang : "(nenhum: ingles)", out_path);

    snprintf((char *)cnf, sizeof(cnf), "BOOT2 = cdrom0:\\%s;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n", GAME_ID);
    /* Na ordem do diretório (ISO-9660 pede nomes em ordem). */
    files[nfiles++] = (Entry){GAME_ID, elf, NULL, (uint32_t)file_size(elf), 0};
    files[nfiles++] = (Entry){"SYSTEM.CNF", NULL, cnf, (uint32_t)strlen((char *)cnf), 0};
    files[nfiles++] = (Entry){"YFM.BIN", bin, NULL, (uint32_t)size, 0};
    if (have_lang) lang_file = (Entry){"PTBR.TXT", lang, NULL, (uint32_t)file_size(lang), 0};

    /* Setores: 0-15 sistema, 16 PVD, 17 fim, 18 tabela L, 19 tabela M,
     * 20 raiz, 21 LANG, depois os arquivos. */
    lba = 22;
    for (i = 0; i < nfiles; i++) files[i].lba = lba, lba += (files[i].size + SECTOR - 1) / SECTOR;
    if (have_lang) lang_file.lba = lba, lba += (lang_file.size + SECTOR - 1) / SECTOR;

    memset(root, 0, sizeof(root));
    root_len += dir_record(root + root_len, "\0", 1, 20, SECTOR, 1);
    root_len += dir_record(root + root_len, "\1", 1, 20, SECTOR, 1);
    if (have_lang) root_len += dir_record(root + root_len, "LANG", 4, 21, SECTOR, 1);
    for (i = 0; i < nfiles; i++) root_len += file_record(root + root_len, &files[i]);
    memset(langdir, 0, sizeof(langdir));
    lang_len += dir_record(langdir + lang_len, "\0", 1, 21, SECTOR, 1);
    lang_len += dir_record(langdir + lang_len, "\1", 1, 20, SECTOR, 1);
    if (have_lang) lang_len += file_record(langdir + lang_len, &lang_file);

    /* Tabelas de caminhos: raiz e LANG. */
    memset(ptl, 0, sizeof(ptl));
    memset(ptm, 0, sizeof(ptm));
    ptl[0] = ptm[0] = 1;
    le32(ptl + 2, 20), be32(ptm + 2, 20);
    ptl[6] = 1, ptm[7] = 1;
    if (have_lang) {
        ptl[10] = ptm[10] = 4;
        le32(ptl + 12, 21), be32(ptm + 12, 21);
        ptl[16] = 1, ptm[17] = 1;
        memcpy(ptl + 18, "LANG", 4), memcpy(ptm + 18, "LANG", 4);
    }

    if (!(out = fopen(out_path, "wb"))) {
        printf("Nao consegui criar %s\n", out_path);
        finish(1);
    }
    memset(sector, 0, SECTOR);
    for (i = 0; i < 16; i++) write_sector(out);
    /* PVD */
    memset(sector, 0, SECTOR);
    sector[0] = 1;
    memcpy(sector + 1, "CD001", 5);
    sector[6] = 1;
    pad_text(sector + 8, "PLAYSTATION", 32);
    pad_text(sector + 40, "YFM_PS2_PORT", 32);
    both32(sector + 80, lba);
    both16(sector + 120, 1);
    both16(sector + 124, 1);
    both16(sector + 128, SECTOR);
    both32(sector + 132, have_lang ? 22 : 10);
    le32(sector + 140, 18);
    be32(sector + 148, 19);
    dir_record(sector + 156, "\0", 1, 20, SECTOR, 1);
    pad_text(sector + 190, "", 128);
    pad_text(sector + 318, "RETROGAMES PATOS DE MINAS", 128);
    pad_text(sector + 446, "", 128);
    pad_text(sector + 574, "MONTAR_ISO", 128);
    pad_text(sector + 702, "", 37), pad_text(sector + 739, "", 37), pad_text(sector + 776, "", 37);
    for (i = 0; i < 4; i++) {
        memset(sector + 813 + 17 * i, '0', 16);
        sector[813 + 17 * i + 16] = 0;
    }
    sector[881] = 1;
    write_sector(out);
    memset(sector, 0, SECTOR);
    sector[0] = 255;
    memcpy(sector + 1, "CD001", 5);
    sector[6] = 1;
    write_sector(out);
    memcpy(sector, ptl, SECTOR), write_sector(out);
    memcpy(sector, ptm, SECTOR), write_sector(out);
    memcpy(sector, root, SECTOR), write_sector(out);
    memcpy(sector, langdir, SECTOR), write_sector(out);
    for (i = 0; i < nfiles; i++) {
        printf("Gravando %s...\n", files[i].name);
        if (copy_file(out, &files[i])) {
            printf("Erro gravando %s\n", files[i].name);
            fclose(out);
            finish(1);
        }
    }
    if (have_lang && copy_file(out, &lang_file)) {
        printf("Erro gravando o idioma\n");
        fclose(out);
        finish(1);
    }
    if (fclose(out)) {
        printf("Erro ao fechar %s (disco cheio?)\n", out_path);
        finish(1);
    }
    printf("\nPronto: %s\nColoque na pasta CD do OPL (ou DVD se a sua versao pedir).\n", out_path);
    finish(0);
    return 0;
}
