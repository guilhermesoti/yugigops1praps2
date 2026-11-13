#ifndef YFM_OPENING_H
#define YFM_OPENING_H
int opening_active(void);
void opening_start(void);
int opening_tick(int *changed, int refresh_rate);
void opening_stop(void);
void title_music_tick(int enabled,int refresh_rate);
#endif
