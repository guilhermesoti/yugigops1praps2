PS2DEV ?= /usr/local/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
GSKIT ?= $(PS2DEV)/gsKit
export PATH := $(PS2DEV)/ee/bin:$(PATH)
EE_BIN := build/YFM.ELF
ifeq ($(OPENING),1)
EE_BIN := build/YFM_OPENING.ELF
endif
FM := vendor/fm/src
EE_OBJS := build/main.o build/catalog.o build/game.o build/battle_stats.o build/view.o build/pack_area.o build/shuffle.o build/duel_shuffle_deck.o build/rand_get_interval.o \
           build/duel_calc_guardian_star_matchup.o build/duel_card_checks.o build/duel_battle_stats.o \
           build/duel_calc_card_stats.o build/fusion.o
EE_INCS := -Isrc -I$(FM) -I$(FM)/pc/cards -I$(GSKIT)/include
EE_CFLAGS := $(EXTRA_CFLAGS) -DYFM_PS2 -std=gnu11 -ffunction-sections -fdata-sections -fno-strict-aliasing -fwrapv -MMD -MP
EE_DBGINFOFLAGS :=
EE_LDFLAGS := -L$(GSKIT)/lib -Wl,--gc-sections,-Map,build/YFM.map
EE_LIBS := -lgskit -ldmakit -lpad -lm
ifeq ($(OPENING),1)
EE_OBJS := $(filter-out build/main.o,$(EE_OBJS)) build/main_opening.o build/opening.o build/opening_data.o
EE_INCS += -I$(PS2SDK)/ports/include
EE_LDFLAGS += -L$(PS2SDK)/ports/lib
EE_LIBS := -lturbojpeg -ljpeg -laudsrv -lpatches $(EE_LIBS)
build/main_opening.o: src/main.c
	$(EE_C_COMPILE) -DYFM_OPENING -c $< -o $@
build/opening_data.o: src/opening_data.S build/opening.mjpg build/opening.pcm assets/frontend/konami.565 assets/frontend/company.565 assets/frontend/title.pcm
	$(EE_CC) $(EE_ASFLAGS) -Wa,-I$(PS2SDK)/iop/irx -c $< -o $@
endif
.DEFAULT_GOAL := all
all: $(EE_BIN)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
vpath %.c src $(FM)/game $(FM)/pc/cards
vpath %.S src
build/%.o: %.c
	@mkdir -p build
	$(EE_C_COMPILE) -c $< -o $@
build/%.o: %.S
	@mkdir -p build
	$(EE_CC) $(EE_ASFLAGS) -c $< -o $@
clean:
	rm -rf build
.PHONY: all clean
-include $(EE_OBJS:.o=.d)
