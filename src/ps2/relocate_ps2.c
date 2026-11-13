/* Retail pointers in the overlay images the game loads (force.h).
 *
 * A module the game reads off the disc into one of its banks holds KSEG0
 * addresses of its own tables and code (a pointer table such as the name
 * entry's starter deck pools, a switch's jump table). Native code reaches
 * guest RAM at the window instead, so each word of a delivery into a bank
 * that is such an address is rewritten to the window's as it arrives, and
 * remembered: the gates that check a bank holds the retail bytes before its
 * native C may run (retail_image.c, model_modules.c) hash it as delivered. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pc/guest/sha256.h"

typedef struct { uint32_t start, end; } Bank;
static const Bank banks[] = {
    {0x8013A000u, 0x8013C000u}, /* model slot A's control modules */
    {0x80146000u, 0x8015C000u}, /* the duel effect bank */
    {0x80168000u, 0x80180000u}, /* name entry and password, overworld, free duel */
    {0x8017A000u, 0x8017C000u}, /* model slot B's control modules */
    {0x80180000u, 0x80188000u}, /* main menu, credits */
};
#define FIRST 0x8013A000u
#define LAST 0x80188000u
static uint32_t moved[(LAST - FIRST) / 4 / 32];  /* this word was rewritten */
static uint32_t uncached[(LAST - FIRST) / 4 / 32]; /* ...from a KSEG1 address */

static int in_bank(uint32_t retail)
{
    unsigned i;
    for (i = 0; i < sizeof(banks) / sizeof(banks[0]); i++)
        if (retail >= banks[i].start && retail < banks[i].end) return 1;
    return 0;
}

void Ps2_RelocateDelivery(void *destination, size_t length)
{
    uint32_t at = (uint32_t)(uintptr_t)destination, end, retail;
    if (at < 0xC0000000u || at >= 0xC0200000u) return;
    end = at + (uint32_t)length;
    if (end > 0xC0200000u) end = 0xC0200000u;
    at = (at + 3) & ~3u;
    for (; at + 4 <= end; at += 4) {
        uint32_t word, index, bit;
        retail = (at & 0x1FFFFFu) | 0x80000000u;
        if (retail < FIRST || retail >= LAST || !in_bank(retail)) continue;
        index = (retail - FIRST) / 4;
        bit = 1u << (index & 31);
        word = *(uint32_t *)(uintptr_t)at;
        moved[index / 32] &= ~bit;
        uncached[index / 32] &= ~bit;
        if ((word >= 0x80010000u && word < 0x80200000u) || (word >= 0xA0010000u && word < 0xA0200000u)) {
            moved[index / 32] |= bit;
            if (word >= 0xA0000000u) uncached[index / 32] |= bit;
            *(uint32_t *)(uintptr_t)at = (word & 0x1FFFFFu) | 0xC0000000u;
        }
    }
}

/* Data that is not a module, read into a bank's range (a battle model's
 * record over the duel effect bank, slot 2's over the main menu's), gets its
 * delivered words back before the game reads it. */
void Ps2_UnrelocateRange(void *data, size_t size)
{
    uint32_t at = (uint32_t)(uintptr_t)data, end;
    if (at < 0xC0000000u || at >= 0xC0200000u) return;
    end = at + (uint32_t)size;
    if (end > 0xC0200000u) end = 0xC0200000u;
    for (at = (at + 3) & ~3u; at + 4 <= end; at += 4) {
        uint32_t retail = (at & 0x1FFFFFu) | 0x80000000u, index, bit;
        if (retail < FIRST || retail >= LAST) continue;
        index = (retail - FIRST) / 4;
        bit = 1u << (index & 31);
        if (moved[index / 32] & bit) {
            uint32_t *word = (uint32_t *)(uintptr_t)at;
            *word = (*word & 0x1FFFFFu) | ((uncached[index / 32] & bit) ? 0xA0000000u : 0x80000000u);
            moved[index / 32] &= ~bit;
            uncached[index / 32] &= ~bit;
        }
    }
}

/* SHA-256 of guest bytes as the disc delivered them. */
void Ps2_Sha256Retail(const void *data, size_t size, uint8_t digest[32])
{
    uint32_t at = (uint32_t)(uintptr_t)data, retail = (at & 0x1FFFFFu) | 0x80000000u, i;
    uint32_t *copy;
    if (at < 0xC0000000u || retail < FIRST || retail + size > LAST || (size & 3) || (at & 3) ||
        !(copy = malloc(size))) {
        Sha256(data, size, digest);
        return;
    }
    memcpy(copy, data, size);
    for (i = 0; i < size / 4; i++) {
        uint32_t index = (retail - FIRST) / 4 + i, bit = 1u << (index & 31);
        if (moved[index / 32] & bit)
            copy[i] = (copy[i] & 0x1FFFFFu) | ((uncached[index / 32] & bit) ? 0xA0000000u : 0x80000000u);
    }
    Sha256(copy, size, digest);
    free(copy);
}
