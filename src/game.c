#include "game.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { EQUIP=23 };
static const FusionRules rules = {.card=lookup_card,.fusion=fusion_of,.equip=equipment_on};

static void note(Game *g, const char *format, ...)
{
    va_list args;
    va_start(args,format);
    vsnprintf(g->log,sizeof(g->log),format,args);
    va_end(args);
}
static uint32_t next_number(Game *g)
{
    uint32_t x=g->random;
    x^=x<<13; x^=x>>17; x^=x<<5;
    return g->random=x;
}
static int first_free(const Side *s)
{
    for (int i=0;i<FIELD;i++) if (!s->field[i].card.id) return i;
    return -1;
}
static int occupied(const Side *s)
{
    for (int i=0;i<FIELD;i++) if (s->field[i].card.id) return i;
    return -1;
}
static int first_ready(const Side *s)
{
    for (int i=0;i<FIELD;i++)
        if (s->field[i].card.id && !s->field[i].defending && !s->field[i].spent) return i;
    return -1;
}
static void build_deck(Game *g, Side *s, int opponent)
{
    if (catalog.from_disc && opponent) {
        const uint16_t *pool=catalog.opponents[g->opponent].weights;
        /* The retail routine retries when a card already has three copies.
           Keep its actual cutoff and use it only if 40 cards can be dealt. */
        unsigned sum=0, selectable=0;
        for (int i=0; i<MAX_CARDS-2 && sum<2048; i++) {
            if (pool[i]) selectable++;
            sum+=pool[i];
        }
        if (selectable>=14 && sum>=2048) {shuffled(g,s,pool);return;}
    }
    unsigned weights[MAX_CARDS]={0}, total=0;
    unsigned char copies[MAX_CARDS]={0};
    for (int i=0;i<catalog.count;i++) {
        FusionCard c=lookup_card(i+1);
        if (c.type>=20 && (!opponent && c.type!=EQUIP)) continue;
        if (opponent && c.type>=20) continue;
        if (!opponent && c.type==EQUIP && !catalog.from_disc && i!=3) continue;
        if (catalog.from_disc && opponent) weights[i]=catalog.opponents[g->opponent].weights[i];
        else if (catalog.from_disc && c.attack>2100) weights[i]=0;
        else weights[i]=(c.type==EQUIP ? 1 : 6);
        total+=weights[i];
    }
    for (int j=0;j<DECK;j++) {
        if (!total) {
            for (int i=0;i<catalog.count;i++) {
                if (lookup_card(i+1).type<20 && (!catalog.from_disc || copies[i]<3)) {
                    weights[i]=1; total++;
                }
            }
        }
        if (!total) { s->deck[j]=1; continue; }
        uint32_t sample=next_number(g)%total;
        for (int i=0;i<catalog.count;i++) {
            if (sample>=weights[i]) { sample-=weights[i]; continue; }
            s->deck[j]=i+1;
            if (++copies[i]>=3 && catalog.from_disc) { total-=weights[i]; weights[i]=0; }
            break;
        }
    }
    shuffled(g,s,NULL);
}
static int fill_hand(Side *s)
{
    for (int i=0;i<HAND;i++) if (!s->hand[i]) {
        if (s->draw_pos==DECK) return 0;
        s->hand[i]=s->deck[s->draw_pos++];
    }
    return 1;
}
static void finish(Game *g, int winner)
{
    if (winner) g->losses[g->opponent]++;
    else g->wins[g->opponent]++;
    g->winner=winner; g->page=RESULT;
}
static void begin_player(Game *g)
{
    Side *p=&g->side[0];
    if (!fill_hand(p)) {note(g,"Seu baralho acabou.");finish(g,1);return;}
    for (int i=0;i<FIELD;i++) p->field[i].spent=0;
    g->page=HAND_PICK; g->picks=0; g->cursor=0; g->defending=0; g->choose_second_star=0;
    g->selected_attacker=-1;
}
void game_init(Game *g, uint32_t seed)
{
    memset(g,0,sizeof(*g)); g->random=seed?seed:0x14f11;
    g->page=TITLE; g->opponent=1;
    build_deck(g,&g->side[0],0);
    if (!catalog.from_disc) {
        const int opening[HAND]={1,2,4,5,6};
        memcpy(g->side[0].deck,opening,sizeof(opening));
    }
    memcpy(g->session_deck,g->side[0].deck,sizeof(g->session_deck));
}
void game_start(Game *g)
{
    memset(g->side,0,sizeof(g->side));
    g->phase_turn=1; g->winner=-1;
    g->side[0].lp=g->side[1].lp=8000;
    memcpy(g->side[0].deck,g->session_deck,sizeof(g->session_deck));
    if (catalog.from_disc) shuffled(g,&g->side[0],NULL);
    build_deck(g,&g->side[1],1);
    begin_player(g);
    note(g,catalog.from_disc ? "Treino com os dados do seu disco." : "Dica: marque Brasa e Serpente para testar uma fusao.");
}
static int selected_chain(const Game *g, FusionCard *out)
{
    if (!g->picks) return 0;
    *out=lookup_card(g->side[0].hand[g->picked[0]]);
    int combined=0;
    for (int i=1;i<g->picks;i++) {
        FusionCard next;
        combined+=Fusion_Step(&rules,*out,lookup_card(g->side[0].hand[g->picked[i]]),&next);
        *out=next;
    }
    return combined;
}
static void toggle_material(Game *g)
{
    for (int i=0;i<g->picks;i++) if (g->picked[i]==g->cursor) {
        for (int j=i+1;j<g->picks;j++) g->picked[j-1]=g->picked[j];
        g->picks--; return;
    }
    if (g->picks<HAND && g->side[0].hand[g->cursor]) g->picked[g->picks++]=g->cursor;
}
static void place_player(Game *g)
{
    FusionCard out;
    int combined=selected_chain(g,&out);
    Unit *cell=&g->side[0].field[g->cursor];
    if (cell->card.id) {
        FusionCard next;
        combined+=Fusion_Step(&rules,cell->card,out,&next);
        out=next;
    }
    if (!out.id||out.type>=20) {note(g,"Jogue um monstro ou combine o equipamento com ele.");return;}
    for (int i=0;i<g->picks;i++) g->side[0].hand[g->picked[i]]=0;
    *cell=(Unit){.card=out,.star=(unsigned)g->choose_second_star,.defending=(unsigned)g->defending};
    note(g,combined ? "Combinacao: %s." : "Em campo: %s.",card_info(out.id)->name);
    g->page=BATTLE; g->picks=0; g->selected_attacker=-1;
    if (g->phase_turn==1) note(g,combined ? "Combinacao: %s. Primeiro turno: START passa." :
                                "%s em campo. Primeiro turno: START passa.",card_info(out.id)->name);
}
static void lose_lp(Side *side, int damage)
{
    if (damage>side->lp) damage=side->lp;
    if (damage>0) side->lp-=damage;
}
/* The attacker is the only card which receives the signed guardian adjustment.
   This is how the original battle scene compares it to the defender's base stat. */
static void strike(Game *g, int side, int slot, int target)
{
    Side *attacking=&g->side[side], *defending=&g->side[!side];
    Unit *a=&attacking->field[slot];
    int id=a->card.id;
    a->spent=1;
    if (target<0) {
        int damage=compare_units(a,NULL);
        lose_lp(defending,damage); note(g,"%s: ataque direto de %d.",card_info(id)->name,damage);
    } else {
        Unit *b=&defending->field[target];
        int diff=compare_units(a,b); /* -1 is the retail tied-attack sentinel. */
        if (diff>0) {
            if (!b->defending) lose_lp(defending,diff);
            note(g,"%s venceu %s.",card_info(id)->name,card_info(b->card.id)->name);
            *b=(Unit){0};
        } else if (diff < -1) {
            lose_lp(attacking,-diff);
            note(g,"%s perdeu %d LP no combate.",side?"Rival":"Voce",-diff);
            if (!b->defending) *a=(Unit){0};
        } else if (!b->defending) {
            *a=(Unit){0}; *b=(Unit){0}; note(g,"Empate: os dois monstros sairam de campo.");
        } else note(g,"A defesa segurou o ataque.");
    }
    if (!attacking->lp) finish(g,!side);
    else if (!defending->lp) finish(g,side);
}
static void cpu_turn(Game *g)
{
    Side *cpu=&g->side[1], *player=&g->side[0];
    if (!fill_hand(cpu)) {note(g,"O baralho do rival acabou.");finish(g,0);return;}
    int best=-1;
    for (int i=0;i<HAND;i++) {
        FusionCard c=lookup_card(cpu->hand[i]);
        if (c.type<20 && (best<0 || c.attack>lookup_card(cpu->hand[best]).attack)) best=i;
    }
    if (best<0) {
        /* Magic/trap scripts are not active in training. Use that turn as a discard. */
        cpu->hand[0]=0; note(g,"O rival descartou uma carta.");
    } else {
        int dest=first_free(cpu);
        if (dest<0) {
            dest=0;
            for (int i=1;i<FIELD;i++) if (Fusion_Attack(cpu->field[i].card)<Fusion_Attack(cpu->field[dest].card)) dest=i;
        }
        cpu->field[dest]=(Unit){.card=lookup_card(cpu->hand[best])};
        cpu->hand[best]=0;
    }
    for (int i=0;i<FIELD && g->page!=RESULT;i++) {
        Unit *u=&cpu->field[i];
        if (!u->card.id) continue;
        int foe=occupied(player);
        if (foe>=0) {
            int best_diff=-32000;
            for (int j=0;j<FIELD;j++) {
                Unit *v=&player->field[j];
                if (!v->card.id) continue;
                int diff=compare_units(u,v);
                if (diff>best_diff) {best_diff=diff;foe=j;}
            }
            if (best_diff<0) continue;
        }
        strike(g,1,i,foe);
    }
    if (g->page!=RESULT) { g->phase_turn++; begin_player(g); }
}
static int grid_step(int index, int delta, int low, int high)
{
    int next=index+delta;
    return next<low?low:next>high?high:next;
}
void game_action(Game *g, enum Action key)
{
    int delta=(key==NEXT)-(key==PREV);
    if (g->page==TITLE) {
        if (key==ACCEPT || key==END_TURN) g->page=MODE_MENU;
    } else if (g->page==MODE_MENU) {
        if (key==BACK) g->page=TITLE;
        if (key==ACCEPT) {g->page=ROSTER; g->cursor=0;}
    } else if (g->page==ROSTER) {
        int column=g->cursor%ROSTER_COLS;
        int step=delta+ROSTER_COLS*((key==BELOW)-(key==ABOVE));
        if (key==BACK) g->page=MODE_MENU;
        else if (key==ACCEPT && !g->cursor) g->page=DECK_VIEW;
        else if (key==ACCEPT) {g->opponent=g->cursor; game_start(g);}
        else if (column+delta>=0 && column+delta<ROSTER_COLS &&
                 g->cursor+step>=0 && g->cursor+step<catalog.opponent_count)
            g->cursor+=step;
    } else if (g->page==DECK_VIEW) {
        if (key==BACK) {g->page=ROSTER; g->cursor=0;}
        else g->cursor=grid_step(g->cursor,(key==BELOW)-(key==ABOVE)+8*delta,0,DECK-1);
    } else if (g->page==HAND_PICK) {
        if (delta) g->cursor=grid_step(g->cursor,delta,0,HAND-1);
        else if (key==END_TURN) {
            g->side[0].hand[g->cursor]=0;
            note(g,"Uma carta descartada. Turno passado.");
            cpu_turn(g);
        } else if (key==MARK) toggle_material(g);
        else if (key==BACK) g->picks=0;
        else if (key==ACCEPT) {
            if (!g->picks) toggle_material(g);
            if (!g->picks) return;
            g->page=FIELD_PICK; int empty=first_free(&g->side[0]); g->cursor=empty<0?0:empty;
        }
    } else if (g->page==FIELD_PICK) {
        if (delta) g->cursor=grid_step(g->cursor,delta,0,FIELD-1);
        else if (key==FLIP) g->defending=!g->defending;
        else if (key==STAR) g->choose_second_star=!g->choose_second_star;
        else if (key==ACCEPT) place_player(g);
        else if (key==BACK) {g->cursor=g->picked[g->picks-1];g->page=HAND_PICK;}
    } else if (g->page==BATTLE) {
        if (key==END_TURN) cpu_turn(g);
        else if (key==BACK) {if (g->selected_attacker>=0) g->cursor=g->selected_attacker; g->selected_attacker=-1;}
        else if (delta) g->cursor=grid_step(g->cursor,delta,0,FIELD-1);
        else if (key==ACCEPT && g->phase_turn>1) {
            if (g->selected_attacker<0) {
                Unit *u=&g->side[0].field[g->cursor];
                if (!u->card.id || u->defending || u->spent) return;
                int foe=occupied(&g->side[1]);
                if (foe<0) strike(g,0,g->cursor,-1);
                else {g->selected_attacker=g->cursor;g->cursor=foe;}
            } else if (g->side[1].field[g->cursor].card.id) {
                strike(g,0,g->selected_attacker,g->cursor);
                g->cursor=g->selected_attacker;g->selected_attacker=-1;
            }
        }
    } else if (key==ACCEPT || key==BACK || key==END_TURN) {
        g->page=ROSTER;g->cursor=g->opponent;
    }
}

int game_selftest(void)
{
    if (catalog.from_disc) return -1;
    if (fusion_of(1,2)!=3 || fusion_of(2,1)!=3 || fusion_of(1,5)!=0 ||
        equipment_on(4,3)!=3 || equipment_on(3,4)!=0 || star_bonus(1,2)!=500 ||
        star_bonus(2,1)!=-500 || star_bonus(10,7)!=500 || star_bonus(1,7)!=0) return 0;
    Game g;
    game_init(&g,75);
    game_action(&g,END_TURN); game_action(&g,ACCEPT); game_action(&g,ACCEPT);
    if (g.page!=DECK_VIEW) return 0;
    game_action(&g,BACK); game_action(&g,NEXT); game_action(&g,ACCEPT);
    if (g.page!=HAND_PICK || g.opponent!=1) return 0;
    game_action(&g,MARK);game_action(&g,NEXT);game_action(&g,MARK);
    game_action(&g,NEXT);game_action(&g,MARK);game_action(&g,ACCEPT);game_action(&g,ACCEPT);
    Unit *u=&g.side[0].field[0];
    if (g.page!=BATTLE || u->card.id!=3 || effective_attack(u,NULL)!=2500 || g.side[0].hand[0]) return 0;
    Unit challenger = *u;
    if (compare_units(u,&challenger)!=-1) return 0;
    challenger.defending=1;
    if (compare_units(u,&challenger)!=400) return 0;
    game_action(&g,END_TURN);
    if (g.phase_turn!=2 || g.page!=HAND_PICK || !g.side[0].hand[0]) return 0;
    for (int t=0;t<80 && g.page!=RESULT;t++) {
        int best=-1;
        for (int i=0;i<HAND;i++) {
            FusionCard c=lookup_card(g.side[0].hand[i]);
            if (c.type<20 && (best<0 || c.attack>lookup_card(g.side[0].hand[best]).attack)) best=i;
        }
        if (best<0) return 0;
        g.cursor=best; game_action(&g,ACCEPT); game_action(&g,ACCEPT);
        while (g.page==BATTLE && first_ready(&g.side[0])>=0) {
            g.cursor=first_ready(&g.side[0]); game_action(&g,ACCEPT);
            if (g.selected_attacker>=0) game_action(&g,ACCEPT);
        }
        if (g.page==BATTLE) game_action(&g,END_TURN);
    }
    if (g.page!=RESULT || g.wins[1]+g.losses[1]!=1) return 0;
    game_action(&g,ACCEPT);
    return g.page==ROSTER && g.cursor==1;
}
