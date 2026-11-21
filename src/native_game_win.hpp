#pragma once
// Native Win32 host ABI for the SELF-AUTHORED PS1 MIPS demo (not PlayStation BIOS).
// Generated native C++ invokes this bridge; no interpreter, emulator, or console ROM.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include "native_runtime_state.hpp"

namespace lv_demo {
namespace fs=std::filesystem;
using lumavun::runtime::Snapshot;
using lumavun::runtime::SaveBytes;
struct Host {
    HWND hwnd=nullptr;
    bool active=true;
    bool waiting=false;
    bool ready=false;
    bool loadPending=false;
    std::uint32_t frames=0;
    int action=0;
    Snapshot view{};
    Snapshot loaded{};
    std::wstring notice=L"A/D: Move | Space: Stay | F5: Save | F9: Load | Esc: Quit";
};
inline Host state{};

inline fs::path userRoot() {
    std::array<wchar_t,32768> root{};
    const auto length=GetEnvironmentVariableW(
        L"LOCALAPPDATA",root.data(),static_cast<DWORD>(root.size()));
    if(length>0&&length<root.size())
        return fs::path(root.data())/L"LUMAVUN"/L"PSNative";
    return fs::current_path();
}
inline fs::path savePath(){return userRoot()/L"homebrew-demo.sav";}
inline fs::path settingsPath(){return userRoot()/L"settings.json";}

inline int readIntSetting(const std::string& json,const char* key,int fallback) {
    const auto pos=json.find(key);
    if(pos==std::string::npos)return fallback;
    const auto colon=json.find(':',pos+std::strlen(key));
    if(colon==std::string::npos)return fallback;
    auto i=colon+1;
    while(i<json.size() && (json[i]==' '||json[i]=='\t'))++i;
    int result=0,count=0;
    while(i<json.size()&&json[i]>='0'&&json[i]<='9'&&count<6) {
        result=result*10+(json[i]-'0');++i;++count;
    }
    return count>0?result:fallback;
}
inline void gameDimensions(int& width,int& height) {
    width=960;height=540;
    std::ifstream file(settingsPath(),std::ios::binary);
    if(!file)return;
    std::string text(8192,'\0');
    file.read(text.data(),static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    const auto w=readIntSetting(text,"\"width\"",width);
    const auto h=readIntSetting(text,"\"height\"",height);
    if(w>=640&&w<=1920)width=w;
    if(h>=360&&h<=1080)height=h;
}
inline bool saveSnapshot(const Snapshot& view) {
    if(!lumavun::runtime::valid(view))return false;
    std::error_code ec;
    fs::create_directories(savePath().parent_path(),ec);
    if(ec)return false;
    const auto data=lumavun::runtime::encode(view);
    const auto tmp=savePath().wstring()+L".tmp";
    std::ofstream file(fs::path(tmp),std::ios::binary|std::ios::trunc);
    if(!file)return false;
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    file.flush();
    if(!file.good())return false;
    file.close();
    return MoveFileExW(tmp.c_str(),savePath().c_str(),
                       MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
inline bool loadSnapshot(Snapshot& view) {
    std::ifstream file(savePath(),std::ios::binary);
    if(!file)return false;
    SaveBytes buffer{};
    file.read(reinterpret_cast<char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    if(file.gcount()!=static_cast<std::streamsize>(buffer.size()))return false;
    if(file.peek()!=std::char_traits<char>::eof())return false;
    return lumavun::runtime::decode(buffer,view);
}
inline void text(HDC dc,int x,int y,const std::wstring& t) {
    TextOutW(dc,x,y,t.c_str(),static_cast<int>(t.size()));
}
inline void draw(HDC dc,const RECT& area) {
    HBRUSH background=CreateSolidBrush(RGB(15,21,39));
    FillRect(dc,&area,background);DeleteObject(background);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(242,243,250));
    text(dc,24,20,L"LUMAVUN NATIVE PORT  |  MIPS-I HOMEBREW");
    text(dc,24,48,L"Original PS-X EXE code statically translated to Win32 x64");
    text(dc,24,83,L"Turn: "+std::to_wstring(state.view.turn)+L"/10    Score: "+
         std::to_wstring(state.view.score));
    const int w=area.right-area.left,h=area.bottom-area.top;
    for(unsigned i=0;i<5;++i) {
        const auto cell=lumavun::runtime::tileRect(w,h,i);
        if(cell.width<=0)continue;
        const bool player=state.view.position==i;
        const COLORREF rgb=player?RGB(111,169,247):
                           i==4?RGB(223,181,90):RGB(45,55,79);
        HBRUSH brush=CreateSolidBrush(rgb);
        RECT rect{cell.left,cell.top,cell.left+cell.width,cell.top+cell.height};
        FillRect(dc,&rect,brush);DeleteObject(brush);
        if(player) {
            SetTextColor(dc,RGB(11,19,37));
            text(dc,cell.left+12,cell.top+22,L"PLAYER");
        } else if(i==4) {
            SetTextColor(dc,RGB(24,30,46));
            text(dc,cell.left+12,cell.top+22,L"STAR");
        }
        SetTextColor(dc,RGB(242,243,250));
    }
    SetTextColor(dc,RGB(177,195,220));
    text(dc,24,h-87,state.notice);
    text(dc,24,h-51,L"Native GDI, event-driven rendering | Not a PS3 renderer");
}
inline LRESULT CALLBACK windowProc(HWND wnd,UINT message,WPARAM key,LPARAM info) {
    switch(message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc=BeginPaint(wnd,&paint);
            RECT rect{};GetClientRect(wnd,&rect);
            draw(dc,rect);
            EndPaint(wnd,&paint);
            return 0;
        }
        case WM_ERASEBKGND:return 1;
        case WM_KEYDOWN: {
            if(key==VK_ESCAPE) {
                DestroyWindow(wnd);
                return 0;
            }
            if(key==VK_F5) {
                state.notice=saveSnapshot(state.view)?
                    L"Saved demo state. F9 restores progress.":
                    L"Save failed. Check folder permissions.";
                InvalidateRect(wnd,nullptr,FALSE);
                return 0;
            }
            if(key==VK_F9) {
                Snapshot restored{};
                if(loadSnapshot(restored)) {
                    state.loaded=restored;
                    state.loadPending=true;
                    state.notice=L"Loaded saved state. Turn resumes on next step.";
                    if(state.waiting) {
                        state.action=0;
                        state.ready=true;
                    }
                } else {
                    state.notice=L"No valid save available (or save was corrupted).";
                }
                InvalidateRect(wnd,nullptr,FALSE);
                return 0;
            }
            if(state.waiting) {
                if(key=='A'||key==VK_LEFT)state.action=-1;
                else if(key=='D'||key==VK_RIGHT)state.action=1;
                else if(key==VK_SPACE)state.action=0;
                else return 0;
                state.ready=true;
            }
            return 0;
        }
        case WM_DESTROY:
            state.active=false;
            PostQuitMessage(0);
            return 0;
        default:return DefWindowProcW(wnd,message,key,info);
    }
}
inline bool initialize(HINSTANCE app,int show,bool selfTest) {
    state=Host{};
    if(selfTest)return true;
    WNDCLASSW klass{};
    klass.lpfnWndProc=windowProc;
    klass.hInstance=app;
    klass.lpszClassName=L"LumavunRecompiledHomebrewWin32";
    klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&klass))return false;
    int width=960,height=540;
    gameDimensions(width,height);
    width=std::min(width,std::max(640,GetSystemMetrics(SM_CXSCREEN)-80));
    height=std::min(height,std::max(360,GetSystemMetrics(SM_CYSCREEN)-80));
    RECT frame{0,0,width,height};
    AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);
    state.hwnd=CreateWindowW(klass.lpszClassName,
        L"LUMAVUN | Native Recompiled PS1 Homebrew",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,frame.right-frame.left,frame.bottom-frame.top,
        nullptr,nullptr,app,nullptr);
    if(!state.hwnd)return false;
    ShowWindow(state.hwnd,show);
    UpdateWindow(state.hwnd);
    return true;
}
} // namespace lv_demo

inline bool demo_syscall(std::uint32_t (&r)[32],bool selfTest) {
    using namespace lv_demo;
    switch(r[2]) {
        case 1: { // graphics and score status emitted by translated MIPS code
            ++state.frames;
            const std::uint32_t prev=state.view.score;
            const Snapshot incoming{r[4],r[5],r[6]};
            if(!lumavun::runtime::valid(incoming))std::exit(9);
            state.view=incoming;
            if(!selfTest && state.hwnd) {
                if(incoming.score>prev)MessageBeep(MB_OK); // minimal system sound cue
                InvalidateRect(state.hwnd,nullptr,FALSE);
                UpdateWindow(state.hwnd);
            }
            return true;
        }
        case 2: { // input from native keyboard (A/D/left/right/Space)
            if(selfTest){r[2]=1;return true;}
            state.waiting=true;state.ready=false;
            MSG message{};
            while(state.active&&!state.ready) {
                const BOOL result=GetMessageW(&message,nullptr,0,0);
                if(result<=0){state.active=false;break;}
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            state.waiting=false;
            if(!state.active)return false;
            if(state.loadPending) {
                r[16]=state.loaded.position;
                r[17]=state.loaded.turn;
                r[18]=state.loaded.score;
                state.loadPending=false;
            }
            r[2]=static_cast<std::uint32_t>(state.action);
            return true;
        }
        case 3: { // completion status
            if(selfTest) {
                if(state.frames!=10||r[4]!=10||r[5]!=2)std::exit(11);
                return false;
            }
            const std::wstring result=
                L"Native homebrew finished!\nTurns: "+std::to_wstring(r[4])+
                L"\nScore: "+std::to_wstring(r[5]);
            MessageBoxW(state.hwnd,result.c_str(),L"LUMAVUN Homebrew",MB_OK|MB_ICONINFORMATION);
            return false;
        }
        default:std::exit(3);
    }
}
