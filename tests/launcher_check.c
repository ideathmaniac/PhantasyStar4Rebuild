/* Checks the launcher data paths with the real emulator API: the title picture
 * (taken a second after the logo appears) and a save state's money and party.
 * Build: see the gcc line in the commit that added it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include "emu.h"
#include "md.h"
#include "../win/ui_soft.h"
int main(){
    setlocale(LC_ALL,"C.UTF-8");
    static uint8_t rom[0x400000]; FILE*f=fopen("rom/ps4_rus.bin","rb"); size_t n=fread(rom,1,sizeof rom,f); fclose(f);
    printf("kind %d\n", emu_load_rom(rom,n)); emu_reset();
    int W,H,found=-1; int16_t a[8192];
    for(int fr=0;fr<6000;fr++){ emu_set_pad(0,0); emu_run_frame(); while(emu_audio(a,4096)>0){}
        if(found<0&&fr>=1500&&fr%20==0){ const uint32_t*fb=emu_framebuffer(&W,&H); int lw,lh; uint8_t*lg=ui_logo(fb,W,H,MD_W_MAX,&lw,&lh); if(lg){found=fr;free(lg);} } if(found>=0&&fr==found+60){ const uint32_t*fb=emu_framebuffer(&W,&H); FILE*o=fopen("out/title_cap.ppm","wb"); fprintf(o,"P6\n%d %d\n255\n",W,H); for(int y=0;y<H;y++)for(int x=0;x<W;x++){uint32_t c=fb[y*MD_W_MAX+x]; fputc(c>>16,o);fputc((c>>8)&255,o);fputc(c&255,o);} fclose(o); break;} }
    printf("logo found at frame %d (W %d)\n", found, W);
    f=fopen("out/bt/battle.sav","rb"); static uint8_t st[400000]; n=fread(st,1,sizeof st,f); fclose(f);
    const uint8_t*ram=emu_state_ram(st,n); printf("state ram %s\n", ram?"ok":"NULL");
    if(ram){ printf("meseta %u\n", (ram[0xF438]<<24)|(ram[0xF439]<<16)|(ram[0xF43A]<<8)|ram[0xF43B]);
      for(int k=0;k<5;k++){int c=ram[0xF40A+k]; if(c>10)break; wchar_t nm[12]; ui_ps4_name(ram+0xF500+c*0x80,nm,12); printf("%ls lv %d\n",nm,(ram[0xF500+c*0x80+8]<<8)|ram[0xF500+c*0x80+9]);} }
}
