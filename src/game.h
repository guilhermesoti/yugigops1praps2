#ifndef YFM_GAME_H
#define YFM_GAME_H
#include <stdint.h>
#include <stddef.h>
#include "fusion.h"

enum { MAX_CARDS=722, OPPONENTS=40, ROSTER_COLS=5, FIELD=5, DECK=40, HAND=5, THUMB=1408, PORTRAIT=2432 };
enum Page { TITLE, MODE_MENU, ROSTER, DECK_VIEW, HAND_PICK, FIELD_PICK, BATTLE, RESULT };

typedef struct {
    uint32_t packed;
    uint8_t level_attr, pad[3];
    char name[48];
} CardInfo;
typedef struct { char name[32]; uint16_t weights[MAX_CARDS]; } Opponent;
typedef struct {
    CardInfo cards[MAX_CARDS];
    Opponent opponents[OPPONENTS];
    uint8_t portraits[OPPONENTS][PORTRAIT];
    uint8_t thumbs[MAX_CARDS][THUMB];
    uint16_t title[320*240];
    int count, opponent_count, from_disc;
} Catalog;
typedef struct { FusionCard card; unsigned star, defending, spent; } Unit;
typedef struct {
    int lp, hand[HAND], deck[DECK], draw_pos;
    Unit field[FIELD];
} Side;
typedef struct {
    Side side[2];
    uint32_t random;
    enum Page page;
    int session_deck[DECK];
    unsigned wins[OPPONENTS], losses[OPPONENTS];
    int opponent, cursor, picked[HAND], picks, phase_turn, selected_attacker;
    int choose_second_star, defending, winner;
    char log[120];
} Game;

enum Action { PREV, NEXT, ABOVE, BELOW, ACCEPT, BACK, MARK, FLIP, STAR, END_TURN };
extern Catalog catalog;
extern uint16_t gDuel_awEquipTable[];
extern uint16_t gDuel_aFusionTable[];
extern const char *const star_name[];
void catalog_demo(void);
int catalog_load_memory(const uint8_t *data, size_t size);
FusionCard lookup_card(int id);
const CardInfo *card_info(int id);
int fusion_of(int a, int b);
int equipment_on(int equip, int target);
int star_bonus(int own, int rival);
int effective_attack(const Unit *self, const Unit *other);
int compare_units(const Unit *attacker, const Unit *defender);
void shuffled(Game *g, Side *s, const uint16_t *pool);
void game_init(Game *game, uint32_t seed);
void game_action(Game *game, enum Action key);
void game_start(Game *game);
int game_selftest(void);
#endif
