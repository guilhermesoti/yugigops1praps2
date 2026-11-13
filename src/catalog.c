#include "game.h"
#include <stdio.h>
#include <string.h>
#include "game/duel_card_checks.h"
#include "game/duel_calc_guardian_star_matchup.h"

Catalog catalog;
uint16_t gDuel_aFusionTable[65536 / 2 + 3];
uint16_t gDuel_awEquipTable[10240 / 2 + 2];
int32_t gDuel_adwCardStats[MAX_CARDS];
const char *const star_name[] = {"--","Marte","Jupiter","Saturno","Urano","Plutao","Netuno","Mercurio","Sol","Lua","Venus"};
_Static_assert(sizeof(CardInfo) == 56 && sizeof(Opponent) == 1476, "pack layout");

static uint32_t packed(int attack, int defense, unsigned type, unsigned star, unsigned alt)
{
    return (unsigned)(attack/10) | ((unsigned)(defense/10)<<9) | (alt<<18) | (star<<22) | (type<<26);
}

void catalog_demo(void)
{
    static const struct { const char *name; int attack, defense, type, star, alt; } cards[] = {
        {"Brasa",900,700,17,1,7}, {"Serpente",800,1100,11,6,9}, {"Quimera de Fogo",2000,1600,0,1,8},
        {"Lamina de Bronze",0,0,23,0,0}, {"Lobo de Pedra",1300,1500,5,3,10}, {"Guardiao Solar",1400,1000,3,8,2},
        {"Mariposa",1000,900,9,4,9}, {"Fera da Aurora",2300,1700,5,8,1}, {"Vigia",1100,1400,2,9,6},
        {"Golem de Barro",1400,1600,18,5,10}, {"Draco do Vale",1700,1300,0,2,7}, {"Falcao da Serra",1200,1000,6,4,8}
    };
    memset(&catalog, 0, sizeof(catalog));
    memset(gDuel_aFusionTable, 0, sizeof(gDuel_aFusionTable));
    memset(gDuel_awEquipTable, 0, sizeof(gDuel_awEquipTable));
    memset(gDuel_adwCardStats, 0, sizeof(gDuel_adwCardStats));
    catalog.count = sizeof(cards)/sizeof(cards[0]);
    for (int i=0; i<catalog.count; i++) {
        snprintf(catalog.cards[i].name, sizeof(catalog.cards[i].name), "%s", cards[i].name);
        catalog.cards[i].packed = packed(cards[i].attack, cards[i].defense, cards[i].type, cards[i].star, cards[i].alt);
        gDuel_adwCardStats[i] = (int32_t)catalog.cards[i].packed;
    }
    uint8_t *bytes = (uint8_t *)gDuel_aFusionTable;
    gDuel_aFusionTable[1]=1500; bytes[1500]=1; bytes[1502]=2; bytes[1503]=3;
    gDuel_aFusionTable[5]=1510; bytes[1510]=1; bytes[1512]=6; bytes[1513]=8;
    const uint16_t equip[] = {4, 5, 1, 3, 5, 8, 11, 0};
    memcpy(gDuel_awEquipTable, equip, sizeof(equip));
    catalog.opponent_count=3;
    strcpy(catalog.opponents[1].name, "Aprendiz");
    strcpy(catalog.opponents[2].name, "Sentinela");
}

/* Validate lengths before the unmodified retail lookups walk the packed tables.
   A retail odd fusion count can inspect the next two bytes, as on the PS1. */
static int valid_tables(void)
{
    const uint8_t *b = (const uint8_t *)gDuel_aFusionTable;
    for (int id=1; id<=MAX_CARDS; id++) {
        size_t off=gDuel_aFusionTable[id];
        if (!off) continue;
        unsigned n=b[off++];
        if (!n) n=511u-b[off++];
        if (off + ((n+1)/2)*5 > 65536u + 2u) return 0;
    }
    for (size_t i=0; i<5120;) {
        unsigned key=gDuel_awEquipTable[i++];
        if (!key) return 1;
        if (key>MAX_CARDS || i>=5120) return 0;
        unsigned n=gDuel_awEquipTable[i++];
        if (!n || n>5120-i) return 0;
        i+=n;
    }
    return 0;
}

static uint32_t crc_add(uint32_t crc, const uint8_t *data, size_t size)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (unsigned i=0; i<256; i++) {
            uint32_t x=i;
            for (int bit=0; bit<8; bit++) x=(x>>1)^((x&1)?0xedb88320u:0u);
            table[i]=x;
        }
        ready=1;
    }
    while (size--) crc=(crc>>8)^table[(crc^*data++)&255u];
    return crc;
}

/* Section sizes are checked before data are committed. Remaining slot bytes are padding. */
int catalog_load_memory(const uint8_t *bytes, size_t capacity)
{
    uint32_t sizes[8];
    const size_t lengths[] = {sizeof(catalog.cards), 10240, 65536, sizeof(catalog.opponents),
                              sizeof(catalog.portraits), sizeof(catalog.thumbs), sizeof(catalog.title)};
    void *out[] = {catalog.cards,gDuel_awEquipTable,gDuel_aFusionTable,catalog.opponents,
                   catalog.portraits,catalog.thumbs,catalog.title};
    if (capacity<40 || memcmp(bytes,"YFMPS2A1",8)) return 0;
    memcpy(sizes,bytes+8,sizeof(sizes));
    size_t end=40;
    for (unsigned i=0;i<7;i++) {
        if (sizes[i]!=lengths[i] || lengths[i]>capacity-end) return 0;
        end+=lengths[i];
    }
    if ((~crc_add(0xffffffffu,bytes+40,end-40))!=sizes[7]) return 0;
    size_t at=40;
    for (unsigned i=0;i<7;i++) {memcpy(out[i],bytes+at,lengths[i]); at+=lengths[i];}
    if (!valid_tables()) {catalog_demo();return 0;}
    catalog.count=MAX_CARDS;catalog.opponent_count=OPPONENTS;catalog.from_disc=1;
    for (int i=0;i<MAX_CARDS;i++) {
        catalog.cards[i].name[sizeof(catalog.cards[i].name)-1]=0;
        gDuel_adwCardStats[i]=(int32_t)catalog.cards[i].packed;
    }
    for (int i=0;i<OPPONENTS;i++) catalog.opponents[i].name[31]=0;
    return 1;
}

const CardInfo *card_info(int id)
{
    return id>0 && id<=catalog.count ? &catalog.cards[id-1] : NULL;
}
FusionCard lookup_card(int id)
{
    const CardInfo *c=card_info(id);
    if (!c) return (FusionCard){0};
    uint32_t v=c->packed;
    return (FusionCard){.id=id,.type=(int)((v>>26)&31),.attack=(int)((v&511)*10),.defense=(int)(((v>>9)&511)*10)};
}
int fusion_of(int a, int b)
{
    if (!card_info(a)||!card_info(b)) return 0;
    int id=Duel_CheckFusion(a,b);
    return card_info(id)?id:0;
}
int equipment_on(int a, int b)
{
    return card_info(a) && card_info(b) ? Duel_CheckEquip(a,b) : 0;
}
int star_bonus(int a, int b)
{
    return a>=1 && a<=10 && b>=1 && b<=10 ? Duel_CalcGuardianStarMatchup(a,b) : 0;
}
