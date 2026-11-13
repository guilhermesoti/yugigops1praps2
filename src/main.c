#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>
#include "view.h"
#ifdef YFM_OPENING
#include "opening.h"
#endif

static GSGLOBAL *gs;
static GSTEXTURE texture;
static unsigned char pad_buffer[256] __attribute__((aligned(64)));
static Game game;
static void video_init(void)
{
    gs=gsKit_init_global();
    gs->ZBuffering=GS_SETTING_OFF;
    gs->PSM=GS_PSM_CT16;
    gs->DoubleBuffering=GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF,D_CTRL_MFD_OFF,D_CTRL_STS_UNSPEC,D_CTRL_STD_OFF,D_CTRL_RCYC_8,1<<DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs); gsKit_mode_switch(gs,GS_ONESHOT);
    texture.Width=SCREEN_W; texture.Height=SCREEN_H;
    texture.PSM=GS_PSM_CT32; texture.Filter=GS_FILTER_NEAREST;
    texture.Mem=(u32 *)pixels;
    texture.Vram=gsKit_vram_alloc(gs,gsKit_texture_size(SCREEN_W,SCREEN_H,texture.PSM),GSKIT_ALLOC_USERBUFFER);
}
static void frame(int changed)
{
    if(changed) gsKit_texture_upload(gs,&texture);
    gsKit_clear(gs,0);
    gsKit_prim_sprite_striped_texture(gs,&texture,0,0,0,0,(float)gs->Width,(float)gs->Height,
                                     SCREEN_W,SCREEN_H,1,GS_SETREG_RGBAQ(128,128,128,128,0));
    gsKit_queue_exec(gs); gsKit_finish(); gsKit_sync_flip(gs);
}
enum PadStage { RESET_IOP, WAIT_IOP, LOAD_SIO, LOAD_PAD, INIT_PAD, OPEN_PAD, READ_PAD };
static enum PadStage pad_stage;
static unsigned retry_frame, opened_frame;
static int extended_driver, received_input;

static void retry_controller(unsigned frame)
{
    pad_stage=RESET_IOP;
    extended_driver=!extended_driver;
    retry_frame=frame+120;
}

static unsigned poll_controller(unsigned frame, const char **message)
{
    static const char *const stages[]={
        "Iniciando IOP", "Aguardando IOP", "Carregando SIO2MAN",
        "Carregando PADMAN", "Inicializando libpad", "Abrindo porta 1"
    };
    if (pad_stage==RESET_IOP && (int)(frame-retry_frame)<0) return 0;
    if (pad_stage!=READ_PAD) *message=stages[pad_stage];
    switch (pad_stage) {
    case RESET_IOP:
        SifInitRpc(0);
        if (SifIopReset("",0)) pad_stage=WAIT_IOP;
        break;
    case WAIT_IOP:
        if (SifIopSync()) {SifInitRpc(0); pad_stage=LOAD_SIO;}
        break;
    case LOAD_SIO:
        if (SifLoadModule(extended_driver?"rom0:XSIO2MAN":"rom0:SIO2MAN",0,NULL)>=0)
            pad_stage=LOAD_PAD;
        else {
            *message="Falha SIO2MAN - tentando novamente";
            retry_controller(frame);
        }
        break;
    case LOAD_PAD:
        if (SifLoadModule(extended_driver?"rom0:XPADMAN":"rom0:PADMAN",0,NULL)>=0)
            pad_stage=INIT_PAD;
        else {
            *message="Falha PADMAN - tentando novamente";
            retry_controller(frame);
        }
        break;
    case INIT_PAD:
        if (padInit(0)>=0) pad_stage=OPEN_PAD;
        else {
            *message="Falha libpad - tentando novamente";
            retry_controller(frame);
        }
        break;
    case OPEN_PAD:
        if (padPortOpen(0,0,pad_buffer)>0) {
            opened_frame=frame;
            pad_stage=READ_PAD;
        } else {
            *message="Falha ao abrir porta 1 - tentando novamente";
            retry_controller(frame);
        }
        break;
    case READ_PAD: {
        struct padButtonStatus pad;
        int state=padGetState(0,0);
        if ((state==PAD_STATE_STABLE || state==PAD_STATE_FINDCTP1) && padRead(0,0,&pad)>0) {
            received_input=1;
            *message=NULL;
            return (unsigned)(~pad.btns)&65535u;
        }
        *message=state==PAD_STATE_DISCONN?"Porta 1: aguardando controle":
                 state==PAD_STATE_ERROR?"Porta 1: erro de comunicacao":"Porta 1: sincronizando controle";
        if (!received_input && frame-opened_frame>=360
#ifdef YFM_OPENING
            && !opening_active()
#endif
        ) retry_controller(frame);
        break;
    }
    }
    return 0;
}
static void dispatch(unsigned pressed)
{
    static const struct {unsigned mask; enum Action key;} bindings[] = {
        {PAD_LEFT,PREV},{PAD_RIGHT,NEXT},{PAD_UP,ABOVE},{PAD_DOWN,BELOW},
        {PAD_SQUARE,MARK},{PAD_CROSS,ACCEPT},{PAD_CIRCLE,BACK},
        {PAD_L1,FLIP},{PAD_R1,STAR},{PAD_START,END_TURN}
    };
    for(unsigned i=0;i<sizeof(bindings)/sizeof(bindings[0]);i++) if (pressed&bindings[i].mask) {
        game_action(&game,bindings[i].key);break;
    }
}
extern const unsigned char pack_area[];
extern const unsigned int pack_capacity;
int main(void)
{
    video_init(); catalog_demo(); int selftest=game_selftest();
    int data=-1;
    if (!memcmp(pack_area,"YFMPS2A1",8)) data=catalog_load_memory(pack_area,pack_capacity);
    game_init(&game,0x595047);
    const char *message="Iniciando controle", *previous=NULL;
    unsigned last=0,frames=0;
    for (;;) {
        unsigned buttons=poll_controller(frames,&message), pressed=buttons & ~last;
#ifdef YFM_OPENING
        static int intro_done, intro_started;
        if (!intro_done && pad_stage==READ_PAD) {
            if (!intro_started) { opening_start(); intro_started=1; }
            int changed=0;
            if (!(pressed&PAD_START) && opening_tick(&changed,gs->Mode==GS_MODE_PAL?50:60)) {
                frame(changed);
                last=buttons;
                frames++;
                continue;
            }
            opening_stop();
            intro_done=1;
            previous="opening";
            pressed=0;
        }
#endif
        if(game.page==TITLE && pressed) game.random^=frames|1u;
        if(pressed) dispatch(pressed);
        int redraw=pressed || previous!=message || frames<3;
#ifdef YFM_OPENING
        if (!intro_done && redraw) memset(pixels,0,sizeof(pixels));
        else
#endif
        if(redraw) render(&game,selftest,message,data);
#ifdef YFM_OPENING
        if (intro_done) title_music_tick(game.page==TITLE || game.page==MODE_MENU,gs->Mode==GS_MODE_PAL?50:60);
#endif
        frame(redraw);
        last=buttons;previous=message;frames++;
    }
}
