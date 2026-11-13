#include "game.h"
#include "game/duel_shuffle_deck.h"
#include <string.h>

uint8_t gDuel_awOpponentDeckPool[MAX_CARDS * 2] __attribute__((aligned(16)));
static uint32_t rng_state;
int yfm_retail_rand(void)
{
    rng_state=rng_state*1103515245u+12345u;
    return (int)((rng_state>>16)&32767u);
}
void shuffled(Game *g, Side *s, const uint16_t *pool)
{
    uint16_t input[DECK], out[DECK];
    uint8_t order[DECK];
    for (int i=0;i<DECK;i++) input[i]=(uint16_t)s->deck[i];
    rng_state=g->random;
    if (pool) memcpy(gDuel_awOpponentDeckPool,pool,sizeof(gDuel_awOpponentDeckPool));
    Duel_ShuffleDeck(pool ? 0 : (int)(uintptr_t)input,(uint8_t *)out,order);
    for (int i=0;i<DECK;i++) s->deck[i]=out[i];
    g->random=rng_state;
}
