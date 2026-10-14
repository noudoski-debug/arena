#include "main.h"
#include "MenuStubs.h"
#include "Oxorany.hpp"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>
#include "Vector2.h"
#include "Vector3.h"
#include "Quaternion.h"
#include "Android_touch/Touch.hpp"
#include "RemotePatch.h"
#include "Esp.h"
#include "Aim.h"
#include "Bind.h"
#include "FreeCam.h"  // ← новый модуль
#include "menu.hpp"

void Layout_tick_UI() {
    menu::render();
}

static void pollCmdFile() {
    static const char* kPath="/data/local/tmp/67/cmd.txt";
    FILE* f=fopen(kPath,"r"); if(!f)return;
    char buf[512]={0}; size_t n=fread(buf,1,sizeof(buf)-1,f); fclose(f); remove(kPath);
    if(n==0)return; buf[n]='\0';
    char* line=strtok(buf,"\r\n");
    while(line){
        while(*line==' '||*line=='\t')line++;
        if(*line){ char cmd[32]={0},arg[64]={0};
            sscanf(line,"%31s %63s",cmd,arg);
            for(char* p=cmd;*p;p++)if(*p>='A'&&*p<='Z')*p+='a'-'A';
            if(!strcmp(cmd,"save")&&*arg)SaveConfig(arg);
            else if(!strcmp(cmd,"load")&&*arg)LoadConfig(arg);
            else if(!strcmp(cmd,"menu")&&*arg)Vars.Menu.openmenu=(atoi(arg)!=0);
            else if(!strcmp(cmd,"aim")&&*arg)Vars.Aimbot.enableAimbot=(atoi(arg)!=0);
            else if(!strcmp(cmd,"silent")&&*arg)Vars.Aimbot.silentAim=(atoi(arg)!=0);
            else if(!strcmp(cmd,"esp")&&*arg)Vars.ESP.enable=(atoi(arg)!=0);
            else if(!strcmp(cmd,"scope")&&*arg)Vars.Aimbot.scopeCheck=(atoi(arg)!=0);
            else if(!strcmp(cmd,"recoil")&&*arg)Vars.Player.norecoil=(atoi(arg)!=0);
            else if(!strcmp(cmd,"spread")&&*arg)Vars.Player.nospread=(atoi(arg)!=0);
            else if(!strcmp(cmd,"chams")&&*arg)Vars.Chams.enable=(atoi(arg)!=0);
            else if(!strcmp(cmd,"freecam")&&*arg)Vars.FreeCam.enable=(atoi(arg)!=0);
            else if(!strcmp(cmd,"fcspeed")&&*arg){
                float v=(float)atof(arg);
                if(v>=1.f&&v<=50.f)Vars.FreeCam.speed=v;
            }
            else{printf("[cmd] unknown: %s\n",line);fflush(stdout);}
        }
        line=strtok(nullptr,"\r\n");
    }
}

int main(int argc, char** argv) {
    screen_config();
    uint32_t sx=0, sy=0;
    if(displayInfo.width>0&&displayInfo.height>0){sx=(uint32_t)displayInfo.width;sy=(uint32_t)displayInfo.height;}
    else if(argc>=3&&atoi(argv[1])>0&&atoi(argv[2])>0){sx=(uint32_t)atoi(argv[1]);sy=(uint32_t)atoi(argv[2]);}
    else{sx=1920;sy=1080;}
    abs_ScreenX=(int)sx; abs_ScreenY=(int)sy;
    native_window_screen_x=(int)sx; native_window_screen_y=(int)sy;
    printf("[main] surface %ux%u\n",sx,sy); fflush(stdout);

    if(!initGUI_draw(sx,sy,true)){printf("initGUI_draw failed\n");return 1;}
    bool t_ok=touch::init((int32_t)sx,(int32_t)sy,(uint8_t)displayInfo.orientation);
    printf("[main] touch=%d\n",(int)t_ok); fflush(stdout);

    RefreshConfigList();
    Vars.Aimbot.aimBind=273;

    int frames=0;
    while(g_Initialized){
        drawBegin();
        bind::poll();
        Layout_tick_UI();
        esp::tick(native_window_screen_x, native_window_screen_y);
        aim::tick(native_window_screen_x, native_window_screen_y);
        freecam::tick(native_window_screen_x, native_window_screen_y);  // ← FreeCam
        drawEnd();
        if(++frames%300==0){
            pollCmdFile();
            static bool seenGame=false; static int goneTimes=0;
            if(rpatch::pidOf("com.catsbit.oxidesurvivalisland")>0){seenGame=true;goneTimes=0;}
            else if(seenGame&&++goneTimes>=2){
                printf("[main] game closed\n");fflush(stdout);
                touch::shutdown(); shutdown(); exit(0);
            }
        }
        usleep(16000);
    }
    touch::shutdown(); shutdown(); return 0;
}
