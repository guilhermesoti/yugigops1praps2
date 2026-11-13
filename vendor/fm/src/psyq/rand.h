#ifndef YFM_PS2_RAND_ADAPTER_H
#define YFM_PS2_RAND_ADAPTER_H
/* Only the PS1 game sources include this shim. Keep the PS2 C library's own RNG. */
int yfm_retail_rand(void);
#define rand yfm_retail_rand
#endif
