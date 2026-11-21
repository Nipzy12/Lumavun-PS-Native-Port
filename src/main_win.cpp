#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <vector>
#include <string>
#include "preset.hpp"
#include "inspector.hpp"
#include "console_elf.hpp"
#include <commdlg.h>
#include <shlobj.h>
namespace fs=std::filesystem;
namespace {
constexpr int BTN_SCAN=101, BTN_SAVE=102, BTN_PLAY=103, BTN_FILE=104, BTN_FOLDER=105, BTN_REPORT=106, BTN_DEMO=107, BTN_FEASIBILITY=108;
HWND root=nullptr,info=nullptr,label=nullptr,status=nullptr,playBtn=nullptr;
HWND resolution=nullptr,scale=nullptr,fps=nullptr,fullscreen=nullptr,importInfo=nullptr;
lumavun::Inspection imported;bool hasImport=false;
lumavun::Specs specs;lumavun::Preset recommended;
std::string utf8(const std::wstring& w){
  if(w.empty())return {};
  int size=WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,nullptr,0,nullptr,nullptr);
  if(size<=0)return {};
  std::string result(static_cast<size_t>(size),'\0');
  WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,result.data(),size,nullptr,nullptr);
  result.resize(static_cast<size_t>(size-1));return result;
}
std::wstring wide(const std::string& s){
  if(s.empty())return {};
  int size=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);
  if(size<=0)return {};
  std::wstring result(static_cast<size_t>(size),L'\0');
  MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,result.data(),size);
  result.resize(static_cast<size_t>(size-1));return result;
}
fs::path exeFolder(){
  std::array<wchar_t,32768> path{};
  DWORD n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
  return fs::path(std::wstring(path.data(),n)).parent_path();
}
fs::path gamePath(){return exeFolder()/L"Game"/L"GameCore.exe";}
fs::path configPath(){
  wchar_t folder[MAX_PATH]{};
  DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",folder,MAX_PATH);
  if(n>0 && n<MAX_PATH)return fs::path(folder)/L"LUMAVUN"/L"PSNative"/L"settings.json";
  return exeFolder()/L"settings.json";
}
bool hasNativeCore(){
  std::ifstream f(gamePath(),std::ios::binary);
  if(!f)return false;
  IMAGE_DOS_HEADER dos{};
  f.read(reinterpret_cast<char*>(&dos),sizeof(dos));
  if(!f || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<static_cast<LONG>(sizeof(dos)) || dos.e_lfanew>1048576)return false;
  f.seekg(dos.e_lfanew);
  DWORD sig=0;IMAGE_FILE_HEADER header{};
  f.read(reinterpret_cast<char*>(&sig),sizeof(sig));
  f.read(reinterpret_cast<char*>(&header),sizeof(header));
  return bool(f) && sig==IMAGE_NT_SIGNATURE && header.Machine==IMAGE_FILE_MACHINE_AMD64;
}
void set(HWND w,const std::wstring& text){SetWindowTextW(w,text.c_str());}
HWND control(const wchar_t* cls,const wchar_t* value,DWORD style,int x,int y,int width,int height,int id=0){
  return CreateWindowExW(0,cls,value,WS_CHILD|WS_VISIBLE|style,x,y,width,height,root,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
}
HWND makeCombo(int y,const std::vector<std::wstring>& options){
  HWND h=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,305,y,205,200);
  for(const auto& option:options)SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(option.c_str()));
  SendMessageW(h,CB_SETCURSEL,0,0);return h;
}
void choose(HWND h,const std::wstring& match){
  int count=static_cast<int>(SendMessageW(h,CB_GETCOUNT,0,0));
  for(int i=0;i<count;++i){
    wchar_t option[100]{};
    SendMessageW(h,CB_GETLBTEXT,static_cast<WPARAM>(i),reinterpret_cast<LPARAM>(option));
    if(match==option){SendMessageW(h,CB_SETCURSEL,i,0);return;}
  }
}
std::wstring selected(HWND h){
  int i=static_cast<int>(SendMessageW(h,CB_GETCURSEL,0,0));
  if(i<0)return {};
  wchar_t option[100]{};
  SendMessageW(h,CB_GETLBTEXT,static_cast<WPARAM>(i),reinterpret_cast<LPARAM>(option));return option;
}
lumavun::Specs detect(){
  lumavun::Specs s{};
  wchar_t cpu[512]{};DWORD bytes=sizeof(cpu);
  if(RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
     L"ProcessorNameString",RRF_RT_REG_SZ,nullptr,cpu,&bytes)==ERROR_SUCCESS)s.cpu=utf8(cpu);
  else s.cpu="Unknown CPU";
  MEMORYSTATUSEX mem{};mem.dwLength=sizeof(mem);
  if(GlobalMemoryStatusEx(&mem))s.ramMB=static_cast<int>(mem.ullTotalPhys/(1024*1024));
  SYSTEM_INFO sys{};GetNativeSystemInfo(&sys);s.cores=static_cast<int>(sys.dwNumberOfProcessors);
  IDXGIFactory1* factory=nullptr;
  if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory)))){
    IDXGIAdapter1* adapter=nullptr;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){
      DXGI_ADAPTER_DESC1 desc{};
      if(SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)){
        s.gpu=utf8(desc.Description);
        s.dedicatedVideoMB=static_cast<int>(desc.DedicatedVideoMemory/(1024*1024));
        adapter->Release();break;
      }
      adapter->Release();adapter=nullptr;
    }
    factory->Release();
  }
  if(s.gpu.empty())s.gpu="Unknown GPU";
  return s;
}
void refresh(){
  specs=detect();recommended=lumavun::recommend(specs);
  set(info,L"CPU: "+wide(specs.cpu)+L" | Threads: "+std::to_wstring(specs.cores)+
    L"\r\nGPU: "+wide(specs.gpu)+L" | Dedicated VRAM: "+std::to_wstring(specs.dedicatedVideoMB)+L" MB"+
    L"\r\nRAM: "+std::to_wstring(specs.ramMB)+L" MB");
  set(label,L"Recommended: "+wide(recommended.name)+L" (FPS is not guaranteed)");
  choose(resolution,std::to_wstring(recommended.width)+L"x"+std::to_wstring(recommended.height));
  choose(scale,std::to_wstring(recommended.renderScale)+L"%");
  choose(fps,std::to_wstring(recommended.fps));
  const bool valid=hasNativeCore();
  EnableWindow(playBtn,valid?TRUE:FALSE);
  set(status,valid?L"Native x64 GameCore detected. Gameplay compatibility NOT verified."
                  :L"Play disabled: genuine native Game/GameCore.exe is missing.");
}
bool save(){
  std::wstring res=selected(resolution);
  auto x=res.find(L'x');
  if(x==std::wstring::npos)return false;
  int w=0,h=0,sc=0,cap=0;
  try{
    w=std::stoi(res.substr(0,x));h=std::stoi(res.substr(x+1));
    sc=std::stoi(selected(scale));cap=std::stoi(selected(fps));
  }catch(...){return false;}
  if(w<640||h<360||sc<50||sc>100||cap<15||cap>240)return false;
  std::error_code ec;fs::create_directories(configPath().parent_path(),ec);
  if(ec)return false;
  std::ofstream out(configPath(),std::ios::binary|std::ios::trunc);
  if(!out)return false;
  bool full=SendMessageW(fullscreen,BM_GETCHECK,0,0)==BST_CHECKED;
  out<<"{\n \"settings\": {\n"
     <<"  \"width\": "<<w<<", \"height\": "<<h<<",\n"
     <<"  \"renderScalePercent\": "<<sc<<",\n"
     <<"  \"fpsLimit\": "<<cap<<",\n"
     <<"  \"fullscreen\": "<<(full?"true":"false")<<",\n"
     <<"  \"textures\": \"low\", \"shadows\": \"off\", \"effects\": \"low\",\n"
     <<"  \"antialiasing\": false, \"vsync\": false\n }\n}\n";
  out.flush();return out.good();
}
void launch(){
  if(!hasNativeCore()){refresh();return;}
  if(!save()){MessageBoxW(root,L"Settings could not be saved.",L"LUMAVUN",MB_OK|MB_ICONERROR);return;}
  const auto game=gamePath();
  std::wstring cmd=L"\""+game.wstring()+L"\" --config \""+configPath().wstring()+L"\"";
  std::vector<wchar_t> writable(cmd.begin(),cmd.end());writable.push_back(L'\0');
  STARTUPINFOW startup{};startup.cb=sizeof(startup);
  PROCESS_INFORMATION process{};
  const std::wstring cwd=game.parent_path().wstring();
  if(!CreateProcessW(game.c_str(),writable.data(),nullptr,nullptr,FALSE,0,nullptr,cwd.c_str(),&startup,&process)){
    MessageBoxW(root,L"GameCore failed to start; check dependencies.",L"LUMAVUN",MB_OK|MB_ICONERROR);
    return;
  }
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
  set(status,L"Native process started. Playability and performance not yet verified.");
}

void importedPath(const fs::path& p){
  imported=lumavun::inspect(p);hasImport=true;
  set(importInfo,L"Detected: "+wide(imported.platform)+L" | "+wide(imported.format)+
    L"\r\nStatus: "+wide(imported.status)+L" (native game port NOT available)");
}
void importFile(){
  std::array<wchar_t,32768> p{};
  OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=root;
  d.lpstrFile=p.data();d.nMaxFile=static_cast<DWORD>(p.size());
  d.lpstrFilter=L"Games\0*.iso;*.bin;*.cue;*.elf;*.pkg;*.img;*.exe;*.self;*.sprx\0All files\0*.*\0";
  d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
  if(GetOpenFileNameW(&d))importedPath(fs::path(p.data()));
}
void importFolder(){
  BROWSEINFOW d{};d.hwndOwner=root;
  d.lpszTitle=L"Select PS3_GAME or extracted game folder";
  d.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
  LPITEMIDLIST item=SHBrowseForFolderW(&d);
  if(!item)return;
  std::array<wchar_t,32768> p{};
  bool good=SHGetPathFromIDListW(item,p.data())!=FALSE;
  CoTaskMemFree(item);
  if(good)importedPath(fs::path(p.data()));
}
void saveImport(){
  if(!hasImport){MessageBoxW(root,L"Import a game first.",L"LUMAVUN",MB_OK);return;}
  fs::path output=configPath().parent_path()/L"import-report.json";
  std::error_code ec;fs::create_directories(output.parent_path(),ec);
  if(ec){MessageBoxW(root,L"Could not create output folder.",L"LUMAVUN",MB_OK);return;}
  std::ofstream f(output,std::ios::binary|std::ios::trunc);
  f<<lumavun::toJson(imported);f.flush();
  MessageBoxW(root,f.good()?L"Saved inspection report. No native port was created.":
    L"Report save failed.",L"LUMAVUN",MB_OK);
}

void analyzeConsoleBinary(){
  if(!hasImport){
    MessageBoxW(root,L"Import a game file or PS3_GAME folder first, then click Analyze.",
      L"LUMAVUN",MB_OK|MB_ICONINFORMATION);
    return;
  }
  const auto result=lumavun::feasibility::probe(imported.path);
  std::wstring message=L"Binary analysis: "+wide(result.status)+L"\r\n"+
    wide(result.platform)+L" | "+wide(result.architecture)+L"\r\n"+
    L"Load segments: "+std::to_wstring(result.segments.size())+
    L"\r\nNative PC game port: NOT AVAILABLE.";
  const fs::path path=configPath().parent_path()/L"feasibility-report.json";
  std::error_code ec;
  fs::create_directories(path.parent_path(),ec);
  if(!ec) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file<<lumavun::feasibility::json(result);
    file.flush();
    if(file.good())message+=L"\r\nReport saved to LocalAppData.";
    else message+=L"\r\nCould not save report.";
  } else message+=L"\r\nCould not create report folder.";
  set(importInfo,L"V0.6 "+wide(result.status)+L" | "+wide(result.platform)+
      L"\r\n"+wide(result.detail));
  MessageBoxW(root,message.c_str(),L"LUMAVUN Binary Feasibility",MB_OK|MB_ICONINFORMATION);
}

void launchPs1Demo(){
  const auto path=exeFolder()/L"LumavunPS1MiniPort.exe";
  std::error_code ec;
  if(!fs::is_regular_file(path,ec) || ec){
    MessageBoxW(root,L"PS1 homebrew demo EXE not found in launcher folder.",
                L"LUMAVUN",MB_OK|MB_ICONWARNING);
    return;
  }
  std::wstring command=L"\""+path.wstring()+L"\"";
  std::vector<wchar_t> args(command.begin(),command.end());
  args.push_back(L'\0');
  STARTUPINFOW si{};si.cb=sizeof(si);
  PROCESS_INFORMATION pi{};
  auto cwd=path.parent_path().wstring();
  if(!CreateProcessW(path.c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_NEW_CONSOLE,
                     nullptr,cwd.c_str(),&si,&pi)){
    MessageBoxW(root,L"Could not start native PS1 homebrew mini-game.",
                L"LUMAVUN",MB_OK|MB_ICONERROR);
    return;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
}

LRESULT CALLBACK wndProc(HWND h,UINT message,WPARAM w,LPARAM l){
  if(message==WM_CREATE){
    root=h;
    control(L"STATIC",L"LUMAVUN PS Native | Binary Feasibility v0.6",0,24,18,620,30);
    info=control(L"STATIC",L"Detecting...",0,24,56,645,78);
    label=control(L"STATIC",L"",0,24,145,630,28);
    control(L"STATIC",L"Resolution",0,24,210,230,22);
    control(L"STATIC",L"Render scale",0,24,255,230,22);
    control(L"STATIC",L"FPS cap",0,24,300,230,22);
    resolution=makeCombo(204,{L"854x480",L"960x540",L"1280x720",L"1600x900",L"1920x1080"});
    scale=makeCombo(249,{L"50%",L"60%",L"70%",L"75%",L"85%",L"100%"});
    fps=makeCombo(294,{L"30",L"45",L"60"});
    fullscreen=control(L"BUTTON",L"Fullscreen",BS_AUTOCHECKBOX,305,345,205,28);
    SendMessageW(fullscreen,BM_SETCHECK,BST_CHECKED,0);
    control(L"BUTTON",L"Scan & Recommend",WS_TABSTOP,24,415,190,42,BTN_SCAN);
    control(L"BUTTON",L"Save Settings",WS_TABSTOP,235,415,190,42,BTN_SAVE);
    playBtn=control(L"BUTTON",L"Play Game",WS_TABSTOP,445,415,190,42,BTN_PLAY);
    status=control(L"STATIC",L"",0,24,482,645,38);
    control(L"STATIC",L"GAME IMPORT / INSPECTION",0,24,526,660,25);
    control(L"BUTTON",L"Import Game File",WS_TABSTOP,24,555,190,38,BTN_FILE);
    control(L"BUTTON",L"Import Game Folder",WS_TABSTOP,235,555,190,38,BTN_FOLDER);
    control(L"BUTTON",L"Save Report",WS_TABSTOP,445,555,190,38,BTN_REPORT);
    importInfo=control(L"STATIC",L"No game imported. Real-game recompilation is not available.",0,24,610,670,55);
    control(L"BUTTON",L"Play Native PS1 Homebrew Demo",WS_TABSTOP,24,678,400,36,BTN_DEMO);
    control(L"BUTTON",L"Analyze PS2/PS3 ELF or SELF",WS_TABSTOP,24,725,400,36,BTN_FEASIBILITY);
    refresh();return 0;
  }
  if(message==WM_COMMAND){
    switch(LOWORD(w)){
      case BTN_SCAN:refresh();return 0;
      case BTN_SAVE:{bool ok=save();
        MessageBoxW(h,ok?L"Saved settings.":L"Could not save settings.",L"LUMAVUN",
          MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));return 0;}
      case BTN_PLAY:launch();return 0;
      case BTN_FILE:importFile();return 0;
      case BTN_FOLDER:importFolder();return 0;
      case BTN_REPORT:saveImport();return 0;
      case BTN_DEMO:launchPs1Demo();return 0;
      case BTN_FEASIBILITY:analyzeConsoleBinary();return 0;
    }
  }
  if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
  return DefWindowProcW(h,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
  WNDCLASSW wc{};wc.lpfnWndProc=wndProc;wc.hInstance=instance;
  wc.lpszClassName=L"LumavunNativeLauncher";
  wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  if(!RegisterClassW(&wc))return 1;
  HWND h=CreateWindowW(wc.lpszClassName,L"LUMAVUN PS Native Port",WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,CW_USEDEFAULT,710,850,nullptr,nullptr,instance,nullptr);
  if(!h)return 2;
  ShowWindow(h,show);UpdateWindow(h);
  MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
  return static_cast<int>(msg.wParam);
}
