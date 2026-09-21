// ClassicMac's private parent/child control pipe. GPL-3.0-or-later.
// Only enabled by the launcher; no TCP listener or shell command execution.
#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <sstream>
#include <string>
#include <SDL.h>
#include <devices/serial/chario_copland.h>

static bool classicmac_paused = false;

template <typename Key, typename Mouse>
static void classicmac_poll_control(Key key, Mouse mouse) {
    static bool enabled = std::getenv("CLASSICMAC_CONTROL") != nullptr;
    static bool initialized = false;
    static std::string input;
    if (!enabled) return;
    if (!initialized) {
        fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO,F_GETFL) | O_NONBLOCK);
        initialized = true;
    }
    char bytes[512];
    for (int batch=0; batch<8; ++batch) {
        ssize_t count=read(STDIN_FILENO,bytes,sizeof(bytes));
        if (count<0) break;
        if (count==0) { classicmac_paused=false; power_off(po_quit); break; }
        input.append(bytes,count);
        if(input.size()>4096) { input.clear(); break; }
        size_t end;
        while((end=input.find('\n'))!=std::string::npos) {
            std::string line=input.substr(0,end);input.erase(0,end+1);
            if(line=="pause") classicmac_paused=true;
            else if(line=="resume") classicmac_paused=false;
            else if(line=="quit") {classicmac_paused=false;power_off(po_quit);}
            else if(line=="restart") {classicmac_paused=false;power_off(po_restart);}
            else if(line=="continue") CharIoCopland::continue_guest();
            else {
                std::istringstream command(line);
                std::string verb;int a,b,c;
                command>>verb;
                if(verb=="key" && command>>a>>b && a>=0 && a<=127 && (b==0 || b==1)) key(a,b);
                else if(verb=="mouse" && command>>a>>b>>c && a>=-1024 && a<=1024 && b>=-1024 && b<=1024 && (c==0 || c==1)) mouse(a,b,c);
            }
        }
    }
}
