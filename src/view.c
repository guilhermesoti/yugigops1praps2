#include "view.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "font.inc"
#define RGB(r,g,b) (0x80000000u|((uint32_t)(b)<<16)|((uint32_t)(g)<<8)|(r))
#define NAVY RGB(13,22,35)
#define PANEL RGB(29,43,57)
#define PANEL2 RGB(42,62,75)
#define PAPER RGB(238,235,217)
#define MUTE RGB(171,189,196)
#define GOLD RGB(251,192,83)
#define GREEN RGB(138,218,176)
uint32_t pixels[SCREEN_W*SCREEN_H] __attribute__((aligned(128)));

static void box(int x,int y,int w,int h,uint32_t color)
{
    for (int j=y<0?0:y;j<y+h && j<SCREEN_H;j++)
        for (int i=x<0?0:x;i<x+w && i<SCREEN_W;i++) pixels[j*SCREEN_W+i]=color;
}
static void line_text(int x,int y,uint32_t color,int scale,const char *str,int limit)
{
    for (int i=0;str[i] && i<limit;i++) {
        unsigned c=(unsigned char)str[i];
        if (c<32 || c>126) c='?';
        for (int row=0;row<9;row++)
            for (int col=0;col<6;col++)
                if (glyph[c-32][row]&(1<<col)) box(x+(i*6+col)*scale,y+row*scale,scale,scale,color);
    }
}
static void label(int x,int y,uint32_t color,const char *format,...)
{
    char buf[160];va_list args;va_start(args,format);vsnprintf(buf,sizeof(buf),format,args);va_end(args);
    line_text(x,y,color,1,buf,(SCREEN_W-x)/6);
}
static void banner(const char *left,const char *right)
{
    box(0,0,320,22,PANEL2);box(0,21,320,1,GOLD);
    label(7,6,PAPER,"%s",left);label(314-strlen(right)*6,6,GOLD,"%s",right);
}
static uint32_t from_bgr555(unsigned c)
{
    unsigned r=c&31,g=(c>>5)&31,b=(c>>10)&31;
    return RGB((r<<3)|(r>>2),(g<<3)|(g>>2),(b<<3)|(b>>2));
}
static void indexed(const uint8_t *art,int w,int h,int x,int y,int colors)
{
    const uint8_t *palette=art+w*h;
    for(int j=0;j<h;j++) for(int i=0;i<w;i++) {
        int index=art[j*w+i];
        if(index>=colors) continue;
        unsigned c=palette[index*2]|((unsigned)palette[index*2+1]<<8);
        if(c) pixels[(y+j)*SCREEN_W+x+i]=from_bgr555(c);
    }
}
static const uint32_t tokens[]={RGB(199,102,66),RGB(95,156,124),RGB(84,144,174),RGB(169,119,172),RGB(177,147,82)};
static void card(int x,int y,int id,int active,int order)
{
    uint32_t rim=active?GOLD:order?GREEN:PANEL2;
    box(x,y,55,39,rim);box(x+1,y+1,53,37,id?PANEL:NAVY);
    if(id && catalog.from_disc) indexed(catalog.thumbs[id-1],40,32,x+7,y+3,64);
    else if(id) {
        uint32_t ink=tokens[(id-1)%5];
        box(x+5,y+5,45,22,ink);
        line_text(x+20,y+8,PAPER,2,"*",1);
        label(x+7,y+28,PAPER,"#%02d",id);
    }
    if(order) {box(x+1,y+1,8,10,GREEN);label(x+2,y+1,NAVY,"%d",order);}
}
static void title(int test_ok,int status)
{
    if(catalog.from_disc) {
        for(int i=0;i<SCREEN_W*SCREEN_H;i++) pixels[i]=from_bgr555(catalog.title[i]);
        box(0,218,320,22,NAVY); label(7,225,GOLD,"START ou X para entrar");
    } else {
        box(0,0,320,240,NAVY);box(10,37,300,126,PANEL);box(10,37,3,126,GOLD);
        label(20,46,GOLD,"YU-GI-OH!  /  PS2");
        line_text(20,66,PAPER,2,"Forbidden",24);line_text(20,87,PAPER,2,"Memories",24);
        label(20,117,MUTE,"Duelo Livre  /  PS2");
        label(20,133,MUTE,"Cartas de exemplo");
        label(14,172,test_ok?GREEN:GOLD,test_ok?"Regras do jogo: teste OK":"Falha no teste das regras");
        if(status==0) label(14,188,GOLD,"Dados invalidos. Gere o ELF novamente.");
        label(14,213,GOLD,"START ou X para entrar");
    }
}
static void roster(const Game *g)
{
    banner("DUELO LIVRE","ESCOLHA DO RIVAL");
    int row=g->cursor/ROSTER_COLS;
    int first=row>2?row-2:0;
    for (int i=first*ROSTER_COLS;i<catalog.opponent_count && i<(first+3)*ROSTER_COLS;i++) {
        int x=16+(i%ROSTER_COLS)*59, y=27+(i/ROSTER_COLS-first)*51;
        box(x-1,y-1,50,50,i==g->cursor?GOLD:PANEL2);
        box(x,y,48,48,PANEL);
        if (!i) label(x+3,y+19,PAPER,"BARALHO");
        else if (catalog.from_disc) indexed(catalog.portraits[i],48,48,x,y,64);
        else label(x+2,y+19,PAPER,"%.7s",catalog.opponents[i].name);
    }
    if (g->cursor) {
        label(16,184,PAPER,"%s",catalog.opponents[g->cursor].name);
        label(16,199,MUTE,"Vitorias %u   Derrotas %u",g->wins[g->cursor],g->losses[g->cursor]);
    } else label(16,184,PAPER,"Consultar seu baralho");
    label(16,219,GOLD,"Direcional: escolher   X: confirmar   O: voltar");
}
static const char *phase_name(const Game *g)
{
    if(g->page==HAND_PICK) return "MAO";
    if(g->page==FIELD_PICK) return "COLOCAR";
    return g->selected_attacker<0?"ATACAR":"ALVO";
}
static void display_board(const Game *g)
{
    char stats[42];
    snprintf(stats,sizeof(stats),"LP %d | %d",g->side[0].lp,g->side[1].lp);
    banner(phase_name(g),stats);
    for(int row=0;row<3;row++) {
        int y=36+row*51;
        const char *zone=row==0?"RIVAL":row==1?"SEU CAMPO":"SUA MAO";
        label(8,y-11,MUTE,"%s",zone);
        for(int i=0;i<FIELD;i++) {
            int id=row==2?g->side[0].hand[i]:g->side[!row].field[i].card.id;
            int order=0;
            if(row==2) for(int k=0;k<g->picks;k++) if(g->picked[k]==i) order=k+1;
            int active=i==g->cursor && ((row==2 && g->page==HAND_PICK) ||
                (row==1 && (g->page==FIELD_PICK || (g->page==BATTLE && g->selected_attacker<0))) ||
                (row==0 && g->page==BATTLE && g->selected_attacker>=0));
            int x=9+i*61;
            card(x,y,id,active,order);
            if(row<2 && id && g->side[!row].field[i].defending) {box(x+45,y+28,8,10,PANEL);label(x+46,y+28,PAPER,"D");}
        }
    }
    int id=0;
    FusionCard shown={0};
    if(g->page==HAND_PICK) id=g->side[0].hand[g->cursor];
    else if(g->page==FIELD_PICK) {
        if(g->picks) {
            static const FusionRules preview={.card=lookup_card,.fusion=fusion_of,.equip=equipment_on};
            shown=lookup_card(g->side[0].hand[g->picked[0]]);
            for(int i=1;i<g->picks;i++) {
                FusionCard next; Fusion_Step(&preview,shown,lookup_card(g->side[0].hand[g->picked[i]]),&next); shown=next;
            }
        }
        id=shown.id;
    } else {shown=g->side[g->selected_attacker>=0].field[g->cursor].card;id=shown.id;}
    if(id) {
        if(!shown.id) shown=lookup_card(id);
        label(9,178,PAPER,"%.24s  %d/%d",card_info(id)->name,Fusion_Attack(shown),Fusion_Defense(shown));
    }
    if(g->page==HAND_PICK) {
        label(9,193,GOLD,"Quadrado: marcar  X: jogar  START: descartar");
    } else if(g->page==FIELD_PICK) {
        unsigned data=id?card_info(id)->packed:0;
        unsigned star=(data>>(g->choose_second_star?18:22))&15;
        label(9,192,GOLD,"L1: %s  R1: %s  X: jogar",g->defending?"DEF":"ATK",star_name[star<=10?star:0]);
    } else label(9,193,GOLD,g->selected_attacker>=0?"X: atacar alvo  O: cancelar  START: passar":"X: atacar  START: passar turno");
    box(0,209,320,31,PANEL);
    line_text(8,212,MUTE,1,g->log,50);
    if(strlen(g->log)>50) line_text(8,223,MUTE,1,g->log+50,50);
}
void render(const Game *g,int test_ok,const char *input_message,int pack_status)
{
    box(0,0,320,240,NAVY);
    if(g->page==TITLE) title(test_ok,pack_status);
    else if(g->page==MODE_MENU) {
        static const char *const entries[]={"Campanha","Duelo Livre","Montar baralho","Biblioteca","Senha","Salvar"};
        banner("MENU DE MODOS","PS2");
        for (int i=0;i<6;i++) {
            int y=33+i*25;
            box(38,y,244,21,i==1?PANEL2:PANEL);
            label(49,y+6,i==1?GOLD:MUTE,"%s",entries[i]);
        }
        label(38,211,GOLD,"X: Duelo Livre   O: titulo");
    } else if(g->page==DECK_VIEW) {
        char page[24];
        snprintf(page,sizeof(page),"%d / 5",g->cursor/8+1);
        banner("SEU BARALHO",page);
        for (int i=g->cursor/8*8;i<g->cursor/8*8+8;i++)
            label(14,35+(i%8)*19,i==g->cursor?GOLD:PAPER,"%02d  %.38s",i+1,card_info(g->session_deck[i])->name);
        label(14,212,MUTE,"Cima/baixo: carta  Esq/dir: pagina  O: voltar");
    }
    else if(g->page==ROSTER) roster(g);
    else if(g->page==RESULT) {
        banner("RESULTADO DO DUELO",catalog.from_disc?"DISCO USA":"EXEMPLO");
        line_text(16,64,GOLD,2,g->winner?"Derrota":"Vitoria",20);
        label(16,104,PAPER,"Seu LP: %d   Rival: %d",g->side[0].lp,g->side[1].lp);
        line_text(16,130,MUTE,1,g->log,48);
        label(16,191,GOLD,"X para escolher outro rival");
    } else display_board(g);
    if (input_message) {box(0,224,320,16,NAVY);label(7,227,GOLD,"%s",input_message);}
}
