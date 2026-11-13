#include "view.h"
#include "opening.h"
#include <turbojpeg.h>
#include <audsrv.h>
#include <stdio.h>
#include <loadfile.h>
#include <sbv_patches.h>

extern unsigned char intro_video[], intro_video_end[], intro_audio[], intro_audio_end[];
extern unsigned char audio_driver[], audio_driver_end[];
extern const unsigned short konami[], company[];
extern unsigned char title_track[], title_track_end[];
static unsigned char *music;
static int logo_tick;
static tjhandle decoder;
static unsigned char *picture, *sound;
static int tick, shown, audio_ok;

static int send_samples(const unsigned char *data,int count)
{
    if (audsrv_wait_audio(count)!=0) return 0;
    int sent=audsrv_play_audio((const char *)data,count);
    return sent>0 && sent<=count ? sent : 0;
}

int opening_active(void) { return decoder!=NULL; }

void opening_start(void)
{
    decoder=tjInitDecompress();
    picture=intro_video;
    sound=intro_audio;
    shown=-1;
    int result=-1;
    audsrv_fmt_t format={24000,16,2};
    if (SifLoadModule("rom0:LIBSD",0,NULL)>=0 && sbv_patch_enable_lmb()==0 &&
        SifExecModuleBuffer(audio_driver,audio_driver_end-audio_driver,0,NULL,&result)>=0 &&
        result>=0 && audsrv_init()==0 && audsrv_set_format(&format)==0) {
        audsrv_set_volume(85);
        audio_ok=1;
    }
}

int opening_tick(int *changed,int refresh_rate)
{
    /* The game presents both company screens before starting MOVIE.STR. */
    if (logo_tick < 10*refresh_rate) {
        if (logo_tick==0 || logo_tick==5*refresh_rate) {
            const unsigned short *logo=logo_tick==0?konami:company;
            for (int i=0;i<SCREEN_W*SCREEN_H;i++) {
                unsigned c=logo[i];
                unsigned r=(c>>11)*255/31, g=((c>>5)&63)*255/63, b=(c&31)*255/31;
                pixels[i]=0x80000000u|(b<<16)|(g<<8)|r;
            }
            *changed=1;
        }
        logo_tick++;
        return 1;
    }
    int number=tick*15/refresh_rate;
    if (!decoder) return 0;
    if (shown!=number) {
        unsigned char *end=picture;
        while (intro_video_end-end>=2 && !(end[0]==0xff && end[1]==0xd9)) end++;
        if (intro_video_end-end<2) return 0;
        end+=2;
        int width,height,sub,color;
        if (tjDecompressHeader3(decoder,picture,end-picture,&width,&height,&sub,&color)<0 ||
            width!=SCREEN_W || height!=SCREEN_H ||
            tjDecompress2(decoder,picture,end-picture,(unsigned char *)pixels,width,0,height,
                          TJPF_RGBA,TJFLAG_FASTDCT)<0) return 0;
        picture=end;
        shown=number;
        *changed=1;
    }
    if (audio_ok && sound<intro_audio_end) {
        int bytes=24000*4/refresh_rate;
        if (bytes>intro_audio_end-sound) bytes=intro_audio_end-sound;
        sound+=send_samples(sound,bytes);
    }
    if (tick==0) printf("YFM opening: video started, audio=%d\n",audio_ok);
    tick++;
    return 1;
}
void opening_stop(void)
{
    printf("YFM opening: last picture=%d, audio=%d\n",shown,audio_ok);
    if (audio_ok) audsrv_stop_audio();
    if (decoder) tjDestroy(decoder);
    decoder=NULL;
}

void title_music_tick(int enabled,int refresh_rate)
{
    if (!audio_ok) return;
    if (!enabled) {
        if (music) audsrv_stop_audio();
        music=NULL;
        return;
    }
    if (!music || music==title_track_end) music=title_track;
    int count=24000*4/refresh_rate;
    if (count>title_track_end-music) count=title_track_end-music;
    int sent=send_samples(music,count);
    if (music==title_track && sent) printf("YFM title: streaming music\n");
    music+=sent;
}
