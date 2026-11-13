#include "game.h"
#include "game/duel_card.h"

static DuelCardRecord retail_record(const Unit *unit)
{
    DuelCardRecord record = {0};
    record.card_id = (short)unit->card.id;
    record.attack = (short)unit->card.attack;
    record.defense = (short)unit->card.defense;
    record.stat_modifier = (short)unit->card.modifier;
    record.terrain_modifier = (short)unit->card.terrain;
    if (unit->star) record.flags |= DUEL_CARD_FLAG_USE_GUARDIAN_STAR_2;
    if (unit->defending) record.flags |= DUEL_CARD_FLAG_DEFENSE_POSITION;
    return record;
}

int effective_attack(const Unit *self, const Unit *other)
{
    DuelCardRecord a=retail_record(self), b;
    if (other) b=retail_record(other);
    return Duel_CalcBattleAttack(&a, other ? &b : 0);
}

#include "game/display_object.h"
#include "game/duel_battle_stats.h"
/* The original comparison addresses cards by the display object's 0x6A slot. */
DuelCardRecord D_801A7AD8[2];
int compare_units(const Unit *attacker, const Unit *defender)
{
    static DisplayObject objects[2];
    objects[0].field_6A=0;
    objects[1].field_6A=1;
    D_801A7AD8[0]=retail_record(attacker);
    if (defender) D_801A7AD8[1]=retail_record(defender);
    return func_8001EFD4(&objects[0], defender ? &objects[1] : 0);
}
