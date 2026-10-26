#include "app.h"
#include "scene.h"
#include "../config/config.h"
#include "../data/placeholder_reader.h"
#include "../game/game.h"
#include "../overlay/overlay.h"
#include "../process/process.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <fstream>
#include <string>
#include <utility>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam);

namespace {
bool g_imguiReady=false;
bool g_menuOpen=false;
LRESULT CALLBACK OverlayProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_NCHITTEST) return HTTRANSPARENT;
    if (message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (g_imguiReady && ImGui_ImplWin32_WndProcHandler(window,message,w,l)) return TRUE;
    if (message==WM_DESTROY) {PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
LRESULT CALLBACK InputProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_NCHITTEST) return HTCLIENT;
    if (message==WM_MOUSEACTIVATE) return MA_ACTIVATE;
    if (g_imguiReady && ImGui_ImplWin32_WndProcHandler(window,message,w,l)) return TRUE;
    return DefWindowProcW(window,message,w,l);
}
struct WindowSearch { DWORD pid; HWND window; };
BOOL CALLBACK FindWindowCallback(HWND window,LPARAM parameter) {
    auto* search=reinterpret_cast<WindowSearch*>(parameter);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if (pid==search->pid && IsWindowVisible(window) && GetWindow(window,GW_OWNER)==nullptr) {
        search->window=window;return FALSE;
    }
    return TRUE;
}
HWND GameWindow(DWORD pid) {
    WindowSearch search{pid,nullptr};EnumWindows(FindWindowCallback,reinterpret_cast<LPARAM>(&search));return search.window;
}
bool GameBounds(HWND game,RECT& bounds) {
    RECT client{};
    if (!GetClientRect(game,&client)) return false;
    if (client.right<=client.left || client.bottom<=client.top) return false;
    POINT topLeft{0,0};
    if (!ClientToScreen(game,&topLeft)) return false;
    bounds={topLeft.x,topLeft.y,
            topLeft.x+(client.right-client.left),topLeft.y+(client.bottom-client.top)};
    return bounds.right>bounds.left && bounds.bottom>bounds.top;
}
bool CopyToClipboard(HWND owner,const char* text) {
    if (!text || !text[0]) return false;
    const std::size_t length=std::strlen(text);
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(length+1)*sizeof(wchar_t));
    if (!memory) return false;
    auto* wide=static_cast<wchar_t*>(GlobalLock(memory));
    if (!wide) {GlobalFree(memory);return false;}
    for (std::size_t i=0;i<length;++i)
        wide[i]=static_cast<wchar_t>(static_cast<unsigned char>(text[i]));
    wide[length]=L'\0';
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) {GlobalFree(memory);return false;}
    const bool copied=EmptyClipboard()!=FALSE &&
        SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
    CloseClipboard();
    if (!copied) GlobalFree(memory);
    return copied;
}
void MenuInteraction(HWND input,bool open,HWND game=nullptr) {
    g_menuOpen=open;
    if (open) {
        const HWND foreground=GetForegroundWindow();
        const DWORD foregroundThread=foreground?GetWindowThreadProcessId(foreground,nullptr):0;
        const DWORD thisThread=GetCurrentThreadId();
        const bool attached=foregroundThread && foregroundThread!=thisThread &&
            AttachThreadInput(thisThread,foregroundThread,TRUE)!=FALSE;
        ShowWindow(input,SW_SHOWNOACTIVATE);
        SetWindowPos(input,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
        SetForegroundWindow(input);SetActiveWindow(input);SetFocus(input);
        if (attached) AttachThreadInput(thisThread,foregroundThread,FALSE);
    } else {
        ShowWindow(input,SW_HIDE);
        if (IsWindow(game)) SetForegroundWindow(game);
    }
}
struct MarkerCache {
    std::uint64_t world=0,local=0,entity=0,type=0;
    bool localOkay=false;
    std::uint32_t entries=0,infected=0,positioned=0,front=0,projected=0;
};
float DistanceSquared(game::Vec3 a,game::Vec3 b) {
    const auto d=game::Difference(a,b);return game::Dot(d,d);
}
MarkerCache DiscoverInfected(PlaceholderReader& reader,const Config& config,
                            std::uint64_t module,std::uint64_t world,
                            const game::CameraSample& camera,float width,float height) {
    MarkerCache selected{};
    game::LocalSample local{};game::Vec3 localPosition{};
    if (!game::FetchLocal(reader,config,module,local) || local.game!=world ||
        !game::CandidatePosition(reader,config,local.entity,localPosition)) return selected;
    selected.localOkay=true;
    const float maxDistance=static_cast<float>(config.markerMaxDistance);
    float bestDistance=maxDistance*maxDistance;
    for (const char* key:{"GameListA","GameListB","GameListC","GameListD","GameSlow"}) {
        std::uint64_t header=0,data=0,dataAgain=0,countAddress=0;
        std::uint32_t count=0,countAgain=0;
        const auto countKey=std::strcmp(key,"GameSlow")==0?"TableCountSlow":"TableCountA";
        if (!AddAddress(world,config.Offset(key),header) ||
            !game::At(reader,header,config.Offset("TableData"),data) ||
            !AddAddress(header,config.Offset(countKey),countAddress) ||
            !reader.Read(countAddress,count) || count>config.maxTableCount ||
            (count && !UserRange(data,1))) continue;
        const auto n=std::min(count,config.markerMaxEntities);
        std::array<std::uint64_t,512> entries{};
        if (n && !reader.ReadRaw(data,entries.data(),static_cast<std::size_t>(n)*sizeof(entries[0]))) continue;
        MarkerCache tableSelected{};
        float tableBest=bestDistance;
        for (std::uint32_t i=0;i<n;++i) {
            if (!UserRange(entries[i],1) || entries[i]==local.entity) continue;
            ++selected.entries;
            std::string name;
            if (!game::ReadName(reader,config,entries[i],name) || name!="dayzinfected") continue;
            ++selected.infected;
            game::Vec3 position{};
            if (!game::CandidatePosition(reader,config,entries[i],position)) continue;
            ++selected.positioned;
            const float distance=DistanceSquared(position,localPosition);
            if (!std::isfinite(distance) || distance>=maxDistance*maxDistance ||
                !game::CandidateInfectedAlive(reader,config,entries[i])) continue;
            position.y+=static_cast<float>(config.markerHeightCm)*0.01f;
            if (game::Dot(game::Difference(position,camera.position),camera.forward)>0.1f)
                ++selected.front;
            game::Vec2 screen{};
            if (!game::ProjectWorld(camera,position,width,height,screen)) continue;
            ++selected.projected;
            if (distance>=tableBest) continue;
            std::uint64_t type=0;
            if (!game::PtrAt(reader,entries[i],config.Offset("EntityType"),type)) continue;
            tableBest=distance;
            tableSelected={world,local.entity,entries[i],type};
        }
        if (!game::At(reader,header,config.Offset("TableData"),dataAgain) ||
            !reader.Read(countAddress,countAgain) || dataAgain!=data || countAgain!=count) continue;
        if (tableSelected.entity) {
            selected.world=tableSelected.world;selected.local=tableSelected.local;
            selected.entity=tableSelected.entity;selected.type=tableSelected.type;
            bestDistance=tableBest;
        }
    }
    return selected;
}
}

int RunApp() {
    SetProcessDPIAware();
    Config config{};
    if (!config.Load(L"dayz.ini")) return 2;
    const DWORD pid=FindProcessId(config.process);
    if (!pid) return 3;
    HWND gameWindow=GameWindow(pid);
    if (!gameWindow) return 4;
    RECT bounds{};if (!GameBounds(gameWindow,bounds)) return 5;
    PlaceholderReader reader{};
    reader.SetHardCap(config.maxReads);
    // Public preview: no module discovery or live game-data acquisition.
    constexpr bool dataSourceConnected=false;
    constexpr std::uint64_t moduleBase=0;
    const wchar_t* className=L"DayZDiagnosticOverlayWindow";
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=OverlayProc;
    cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=className;
    if (!RegisterClassExW(&cls)) return 6;
    const DWORD style=WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
    HWND window=CreateWindowExW(style,className,L"DayZ diagnostic overlay",WS_POPUP,
        bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
        nullptr,nullptr,cls.hInstance,nullptr);
    if (!window) {UnregisterClassW(className,cls.hInstance);return 7;}
    SetLayeredWindowAttributes(window,RGB(0,0,0),0,LWA_COLORKEY);
    ShowWindow(window,SW_SHOWNA);
    const wchar_t* inputClass=L"DayZDiagnosticInputWindow";
    WNDCLASSEXW inputCls{};inputCls.cbSize=sizeof(inputCls);inputCls.lpfnWndProc=InputProc;
    inputCls.hInstance=cls.hInstance;inputCls.lpszClassName=inputClass;
    inputCls.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassExW(&inputCls)) {
        DestroyWindow(window);UnregisterClassW(className,cls.hInstance);return 9;
    }
    HWND inputWindow=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
        inputClass,L"DayZ diagnostic settings input",WS_POPUP,
        bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
        window,nullptr,cls.hInstance,nullptr);
    if (!inputWindow) {
        UnregisterClassW(inputClass,cls.hInstance);
        DestroyWindow(window);UnregisterClassW(className,cls.hInstance);return 10;
    }
    SetLayeredWindowAttributes(inputWindow,0,1,LWA_ALPHA);
    Overlay renderer{};
    if (!renderer.Initialize(window)) {
        DestroyWindow(inputWindow);UnregisterClassW(inputClass,cls.hInstance);
        DestroyWindow(window);UnregisterClassW(className,cls.hInstance);return 8;
    }
    g_imguiReady=true;
    bool menuOpen=false,running=true;
    MarkerCache marker{};
    SceneCache scene{},scanScratch{};
    bool scanInProgress=false;
    OverlayFrame frame{};frame.boxes.reserve(config.entityScanLimit);
    frame.items.reserve(config.entityScanLimit);
    frame.itemGroups.reserve(config.entityScanLimit);
    frame.skeletons.reserve(config.entityScanLimit);
    bool compactItems=config.itemCompact;
    std::ofstream telemetry("notes/milestone2-runtime.log",std::ios::app);
    if (telemetry) telemetry<<"SESSION adaptive-scan hard_cap="<<config.maxReads<<'\n';
    std::array<float,5> rangeMax{};
    std::uint32_t secondFrames=0,secondReads=0,secondLogicalReads=0;
    std::uint32_t peakReads=0,peakFailures=0;
    std::uint32_t refreshCompleted=0,peakRefreshReads=0;
    std::deque<std::chrono::steady_clock::time_point> recentRefreshes;
    float minimumFps=100000.0f;
    std::uint8_t coordinateCopyNotice=0;
    auto copyNoticeUntil=std::chrono::steady_clock::time_point::min();
    auto nextTelemetry=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    auto nextSceneScan=std::chrono::steady_clock::time_point::min();
    auto nextMarkerScan=std::chrono::steady_clock::time_point::min();
    auto last=std::chrono::steady_clock::now();
    while (running) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (message.message==WM_QUIT) running=false;
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if (!running || !IsWindow(gameWindow)) break;
        DWORD foregroundPid=0;
        const HWND foreground=GetForegroundWindow();
        if (foreground) GetWindowThreadProcessId(foreground,&foregroundPid);
        if (foregroundPid!=pid && !(menuOpen && foreground==inputWindow)) {
            if (menuOpen) {menuOpen=false;MenuInteraction(inputWindow,false);}
            if (IsWindowVisible(window)) ShowWindow(window,SW_HIDE);
            Sleep(16);continue;
        }
        if (!IsWindowVisible(window)) ShowWindow(window,SW_SHOWNOACTIVATE);
        if (GetAsyncKeyState(config.endKey)&1) break;
        if (GetAsyncKeyState(config.panelKey)&1) {
            menuOpen=!menuOpen;MenuInteraction(inputWindow,menuOpen,menuOpen?nullptr:gameWindow);
        }
        if (!renderer.BindingHotkey() && (GetAsyncKeyState(config.itemToggleKey)&1)) {
            config.showItems=!config.showItems;
            config.Save();
            scanScratch.Clear();scanInProgress=false;
            nextSceneScan=std::chrono::steady_clock::time_point::min();
        }
        const bool copyRequested=!renderer.BindingHotkey() &&
            (GetAsyncKeyState(config.coordinateCopyKey)&1)!=0;
        RECT next=bounds;
        if (GameBounds(gameWindow,next)) {
            bounds=next;
            SetWindowPos(window,HWND_TOPMOST,next.left,next.top,next.right-next.left,next.bottom-next.top,SWP_NOACTIVATE);
            if (menuOpen) SetWindowPos(inputWindow,HWND_TOPMOST,next.left,next.top,
                next.right-next.left,next.bottom-next.top,SWP_NOACTIVATE);
            if (!renderer.Resize(static_cast<UINT>(next.right-next.left),static_cast<UINT>(next.bottom-next.top))) break;
        }
        const auto now=std::chrono::steady_clock::now();
        const auto seconds=std::chrono::duration<float>(now-last).count();last=now;
        frame.boxes.clear();frame.items.clear();frame.itemGroups.clear();
        frame.skeletons.clear();frame.funnel={};
        frame.skeletonBound=frame.skeletonEligible=frame.skeletonSampled=0;
        frame.skeletonEligibleByType.fill(0);frame.skeletonFailureStage.fill(0);
        frame.healthWatchValid=frame.deathLocalValid=frame.deathNearbyValid=false;
        frame.cameraValid=false;frame.markerValid=false;frame.markerStage=0;
        frame.localPositionValid=false;frame.localPosition={};frame.cameraForward={};
        frame.scanLocal=false;frame.scanEntries=frame.scanInfected=0;
        frame.scanPositioned=frame.scanFront=frame.scanProjected=0;
        frame.compactItems=compactItems;
        frame.attached=IsWindow(gameWindow)!=FALSE;
        frame.dataSourceConnected=dataSourceConnected;
        frame.moduleBase=moduleBase;
        frame.fps=seconds>0?1.0f/seconds:0;
        frame.viewportWidth=static_cast<std::uint32_t>(bounds.right-bounds.left);
        frame.viewportHeight=static_cast<std::uint32_t>(bounds.bottom-bounds.top);
        reader.BeginPass();
        reader.SetPassCap(visual::AdaptiveReadCap(scene.ObjectCount(),config.maxReads));
        const auto frameCap=reader.PassCap();
        // First target read of the frame is the main camera chain. Its output is cleared each iteration.
        if (moduleBase) {
            std::uint64_t world=0;
            game::CameraSample cameraSample{};
            if (game::PtrAt(reader,moduleBase,config.Offset("GameWorldPointerRva"),world)) {
                if (scanInProgress && scanScratch.World()!=world) {
                    scanScratch.Clear();scanInProgress=false;
                    nextSceneScan=std::chrono::steady_clock::time_point::min();
                }
                frame.cameraValid=game::FetchCameraSample(reader,config,world,cameraSample);
                const bool scanCameraValid=frame.cameraValid;
                if (scanCameraValid && (scanInProgress || now>=nextSceneScan))
                    reader.SetPassCap(visual::ScanSampleCap(frameCap));
                if (config.infectedMarker && config.showEntities[1])
                    frame.markerStage=frame.cameraValid?2u:1u;
                bool sceneValid=false;
                if (frame.cameraValid && scene.World()==world) {
                    sceneValid=scene.Sample(reader,config,moduleBase,world,cameraSample,
                        static_cast<float>(frame.viewportWidth),
                        static_cast<float>(frame.viewportHeight),compactItems,frame);
                    if (!sceneValid) nextSceneScan=std::chrono::steady_clock::time_point::min();
                }
                if (frame.cameraValid &&
                    (config.watchHealthCandidates || config.watchDeathCandidates)) {
                    game::LocalSample watchLocal{};
                    if (game::FetchLocal(reader,config,moduleBase,watchLocal) &&
                        watchLocal.game==world) {
                        if (config.watchHealthCandidates)
                            frame.healthWatchValid=game::SampleHealthWatch(reader,config,
                                watchLocal.entity,frame.healthWatch);
                        if (config.watchDeathCandidates) {
                            frame.deathLocalValid=game::SampleDeathWatch(reader,config,
                                watchLocal.entity,frame.deathLocal);
                            const auto nearby=sceneValid?scene.NearestPlayer():0;
                            if (nearby && nearby!=watchLocal.entity)
                                frame.deathNearbyValid=game::SampleDeathWatch(reader,config,
                                    nearby,frame.deathNearby);
                        }
                    }
                }
                if (config.infectedMarker && config.showEntities[1] && frame.cameraValid) {
                    if (now>=nextMarkerScan) {
                        marker=DiscoverInfected(reader,config,moduleBase,world,cameraSample,
                            static_cast<float>(frame.viewportWidth),
                            static_cast<float>(frame.viewportHeight));
                        nextMarkerScan=now+std::chrono::milliseconds(config.markerScanMs);
                        // A bounded list scan can outlast a camera pose. Refresh it before projection.
                        frame.cameraValid=game::FetchCameraSample(reader,config,world,cameraSample);
                    }
                    frame.scanLocal=marker.localOkay;
                    frame.scanEntries=marker.entries;
                    frame.scanInfected=marker.infected;
                    frame.scanPositioned=marker.positioned;
                    frame.scanFront=marker.front;
                    frame.scanProjected=marker.projected;
                    if (frame.cameraValid && marker.entity && marker.world==world) {
                        frame.markerStage=3;
                        std::uint64_t type=0;
                        game::Vec3 localPosition{},targetPosition{};
                        if (game::PtrAt(reader,marker.entity,config.Offset("EntityType"),type) &&
                            type==marker.type &&
                            game::CandidateInfectedAlive(reader,config,marker.entity) &&
                            game::CandidatePosition(reader,config,marker.local,localPosition) &&
                            game::CandidatePosition(reader,config,marker.entity,targetPosition) &&
                            DistanceSquared(targetPosition,localPosition)<=
                                static_cast<float>(config.markerMaxDistance)*config.markerMaxDistance) {
                            frame.markerStage=4;
                            targetPosition.y+=static_cast<float>(config.markerHeightCm)*0.01f;
                            game::Vec2 screen{};
                            if (game::ProjectWorld(cameraSample,targetPosition,
                                    static_cast<float>(frame.viewportWidth),
                                    static_cast<float>(frame.viewportHeight),screen)) {
                                frame.markerValid=true;frame.markerX=screen.x;frame.markerY=screen.y;
                                frame.markerStage=5;
                            }
                        }
                    }
                }
                if (frame.cameraValid) frame.cameraForward=cameraSample.forward;
                const auto refreshReadStart=reader.Reads();
                reader.SetPassCap(frameCap);
                if (scanCameraValid) {
                    if (!scanInProgress && now>=nextSceneScan) {
                        scanInProgress=scanScratch.BeginRefresh(reader,config,moduleBase,world);
                        if (!scanInProgress)
                            nextSceneScan=now+std::chrono::milliseconds(250);
                    }
                    if (scanInProgress) {
                        const auto status=scanScratch.StepRefresh(reader,config,moduleBase);
                        if (status==SceneCache::ScanStatus::Complete) {
                            scene.CarryItemWinnerHistory(scanScratch);
                            std::swap(scene,scanScratch);
                            scanInProgress=false;
                            ++refreshCompleted;
                            recentRefreshes.push_back(now);
                            nextSceneScan=now+std::chrono::milliseconds(config.entityScanMs);
                        } else if (status==SceneCache::ScanStatus::Failed) {
                            scanScratch.Clear();scanInProgress=false;
                            nextSceneScan=now+std::chrono::milliseconds(250);
                        }
                    }
                }
                peakRefreshReads=std::max(peakRefreshReads,reader.Reads()-refreshReadStart);
            }
        }
        while (!recentRefreshes.empty() &&
               now-recentRefreshes.front()>=std::chrono::minutes(1))
            recentRefreshes.pop_front();
        frame.reads=reader.Reads();frame.logicalReads=reader.LogicalReads();
        frame.refreshesPerMinute=static_cast<std::uint32_t>(recentRefreshes.size());
        frame.failed=reader.Failures();
        if (copyRequested) {
            char pair[64]{};
            if (!frame.localPositionValid ||
                !visual::FormatIzurviveCoordinates(pair,sizeof(pair),frame.localPosition))
                coordinateCopyNotice=2;
            else coordinateCopyNotice=CopyToClipboard(window,pair)?1:3;
            copyNoticeUntil=now+std::chrono::milliseconds(1500);
        }
        frame.coordinateCopyNotice=now<copyNoticeUntil?coordinateCopyNotice:0;
        ++secondFrames;secondReads+=frame.reads;secondLogicalReads+=frame.logicalReads;
        peakReads=std::max(peakReads,frame.reads);
        peakFailures=std::max(peakFailures,frame.failed);
        minimumFps=std::min(minimumFps,frame.fps);
        for (std::size_t i=0;i<rangeMax.size();++i)
            rangeMax[i]=std::max(rangeMax[i],frame.funnel[i].maxObserved);
        if (now>=nextTelemetry && telemetry) {
            telemetry<<"SAMPLE fps_min="<<minimumFps<<" reads_avg="
                <<(secondFrames?secondReads/secondFrames:0)<<" reads_peak="<<peakReads
                <<" logical_avg="<<(secondFrames?secondLogicalReads/secondFrames:0)
                <<" ioctl_ratio="<<(secondLogicalReads?
                    double(secondReads)/double(secondLogicalReads):1.0)
                <<" failures_peak="<<peakFailures<<" cap="<<reader.PassCap()
                <<" scanning="<<scanInProgress
                <<" refresh_completed="<<refreshCompleted
                <<" refresh_per_min="<<frame.refreshesPerMinute
                <<" refresh_reads_peak="<<peakRefreshReads
                <<" camera="<<frame.cameraValid
                <<" skeleton_bound="<<frame.skeletonBound
                <<" skeleton_eligible="<<frame.skeletonEligible
                <<" skeleton_drawn="<<frame.skeletonSampled
                <<" skeleton_player_candidates="<<frame.skeletonEligibleByType[0]
                <<" skeleton_infected_candidates="<<frame.skeletonEligibleByType[1]
                <<" skeleton_player_stage="<<frame.skeletonFailureStage[0]
                <<" skeleton_infected_stage="<<frame.skeletonFailureStage[1];
            for (std::size_t i=0;i<rangeMax.size();++i)
                telemetry<<" type"<<i<<"_seen="<<frame.funnel[i].seen
                         <<" type"<<i<<"_gate="<<frame.funnel[i].within
                         <<" type"<<i<<"_drawn="<<frame.funnel[i].drawn
                         <<" type"<<i<<"_max_m="<<rangeMax[i];
            if (config.watchHealthCandidates) {
                telemetry<<" health_valid="<<frame.healthWatchValid;
                if (frame.healthWatchValid)
                    for (std::size_t i=0;i<frame.healthWatch.values.size();++i)
                        telemetry<<" C"<<i+1<<'='<<frame.healthWatch.values[i];
            }
            if (config.watchDeathCandidates) {
                telemetry<<" death_local_valid="<<frame.deathLocalValid
                         <<" death_near_valid="<<frame.deathNearbyValid;
                if (frame.deathLocalValid) {
                    for (std::size_t i=0;i<frame.deathLocal.words.size();++i)
                        telemetry<<" LD"<<i+1<<'='<<frame.deathLocal.words[i];
                    telemetry<<" LB1="<<static_cast<unsigned>(frame.deathLocal.byte);
                }
                if (frame.deathNearbyValid) {
                    for (std::size_t i=0;i<frame.deathNearby.words.size();++i)
                        telemetry<<" ND"<<i+1<<'='<<frame.deathNearby.words[i];
                    telemetry<<" NB1="<<static_cast<unsigned>(frame.deathNearby.byte);
                }
            }
            telemetry<<'\n';telemetry.flush();
            secondFrames=secondReads=secondLogicalReads=peakReads=peakFailures=0;
            refreshCompleted=peakRefreshReads=0;minimumFps=100000.0f;
            nextTelemetry=now+std::chrono::seconds(1);
        }
        bool configChanged=false;
        if (!renderer.Draw(frame,menuOpen,config,compactItems,configChanged)) break;
        if (configChanged) {
            reader.SetHardCap(config.maxReads);
            scene.Clear();scanScratch.Clear();scanInProgress=false;
            nextSceneScan=std::chrono::steady_clock::time_point::min();
        }
    }
    g_menuOpen=false;g_imguiReady=false;renderer.Shutdown();
    DestroyWindow(inputWindow);UnregisterClassW(inputClass,cls.hInstance);
    DestroyWindow(window);UnregisterClassW(className,cls.hInstance);
    return 0;
}
