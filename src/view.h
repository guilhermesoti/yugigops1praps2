#ifndef YFM_VIEW_H
#define YFM_VIEW_H
#include "game.h"
enum { SCREEN_W=320, SCREEN_H=240 };
extern uint32_t pixels[SCREEN_W * SCREEN_H];
void render(const Game *game, int test_ok, const char *input_message, int pack_status);
#endif
