/* The PS1 GPU on the GS (gs_gpu.c). */
#ifndef MEMORIES_PS2_GS_GPU_H
#define MEMORIES_PS2_GS_GPU_H
#include <stddef.h>
#include <stdint.h>
void GsGpu_Init(void);
void GsGpu_IntroFrame(const uint32_t *abgr);
int GsGpu_Active(void);
size_t GsGpu_Gp0(const uint32_t *words, size_t count);
void GsGpu_Load(int x, int y, int w, int h);
void GsGpu_Fill(int x, int y, int w, int h, uint32_t rgb);
void GsGpu_Move(int sx, int sy, int dx, int dy, int w, int h);
int GsGpu_Stale(int x, int y, int w, int h);
void GsGpu_Present(const uint16_t *vram, int x, int y, int w, int h, int rgb24);
unsigned GsGpu_Stats(unsigned *tiles);
void GsGpu_ReadVram(uint16_t *out);
#endif
