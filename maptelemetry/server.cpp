#include "server_core.h"
#include "../src/game/map_calibration.h"
#include "../src/config/map_telemetry_settings.h"
#include "../third_party/civetweb/civetweb.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
struct MapDef {const char* slug;const char* title;double worldExtent,coverage;};
constexpr std::array<MapDef,3> Maps{{
    {"chernarusplus","Chernarus+",15360.0,0.9644165039},
    {"namalsk","Namalsk",12800.0,1.0},
    {"nasdara","Nasdara",16384.0,1.0}}};
constexpr int MapPixels=16384;
std::atomic<bool> g_stop{false};
BOOL WINAPI OnConsole(DWORD event) {
    if (event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT || event==CTRL_CLOSE_EVENT) {
        g_stop.store(true);return TRUE;
    }
    return FALSE;
}
bool EqualConstantTime(const std::string& a,const std::string& b) {
    if (a.size()!=b.size()) return false;
    unsigned diff=0;
    for (std::size_t i=0;i<a.size();++i)
        diff|=static_cast<unsigned>(static_cast<unsigned char>(a[i])^
            static_cast<unsigned char>(b[i]));
    return diff==0;
}
std::string Hex(const std::uint8_t* data,std::size_t length) {
    constexpr char digits[]="0123456789abcdef";
    std::string result;result.reserve(length*2);
    for (std::size_t i=0;i<length;++i) {
        result+=digits[data[i]>>4];result+=digits[data[i]&15];
    }
    return result;
}
bool ParseDouble(const char* text,double& value) {
    if (!text || !*text) return false;
    char* end=nullptr;
    const double parsed=std::strtod(text,&end);
    if (end==text || *end || !std::isfinite(parsed) || std::abs(parsed)>1.0e9)
        return false;
    value=parsed;return true;
}
std::string Query(const char* source,const char* key) {
    if (!source) return {};
    char buffer[256]{};
    return mg_get_var(source,std::strlen(source),key,buffer,sizeof(buffer))>=0?
        std::string(buffer):std::string{};
}
struct MapCalibration {
    std::vector<mapcal::Pair> points;
    mapcal::Affine affine{};
    bool valid=false;
    bool sourceAligned=false;
    double residual=0;
    double thirdResidual=0;
};
struct ServerState {
    std::filesystem::path root,configPath,webRoot;
    std::string token;
    std::string shareHost;
    int port=maptelemetry::DefaultPort;
    std::mutex mutex,logMutex,calMutex;
    std::ofstream log;
    maptelemetry::Snapshot latest{};
    bool connected=false;
    std::unordered_set<mg_connection*> clients;
    struct Failure {int count=0;std::chrono::steady_clock::time_point reset{};};
    std::unordered_map<std::string,Failure> authFailures;
    std::array<MapCalibration,Maps.size()> calibration{};
    void Log(const std::string& event) {
        std::lock_guard<std::mutex> lock(logMutex);
        if (log) {log<<static_cast<long long>(std::time(nullptr))<<' '<<event<<'\n';log.flush();}
    }
    std::size_t MapIndex(const std::string& slug) const {
        for (std::size_t i=0;i<Maps.size();++i) if (slug==Maps[i].slug) return i;
        return Maps.size();
    }
    void Fit(std::size_t index) {
        auto& c=calibration[index];c.valid=false;c.sourceAligned=false;
        c.residual=0;c.thirdResidual=0;
        if (c.points.size()<2) {
            c.valid=mapcal::TileAligned(Maps[index].worldExtent,Maps[index].coverage,
                MapPixels,c.affine);
            c.sourceAligned=c.valid;
            return;
        }
        std::array<mapcal::Pair,2> firstTwo{{c.points[0],c.points[1]}};
        mapcal::Affine twoPoint{};
        const bool twoValid=mapcal::FitAxisAligned(firstTwo,twoPoint);
        if (c.points.size()==2) {c.valid=twoValid;c.affine=twoPoint;return;}
        if (twoValid) c.thirdResidual=mapcal::Residual(twoPoint,c.points[2]);
        std::array<mapcal::Pair,3> first{{c.points[0],c.points[1],c.points[2]}};
        c.valid=mapcal::Fit(first,c.affine);
        if (c.valid) for (std::size_t i=3;i<c.points.size();++i)
            c.residual=std::max(c.residual,mapcal::Residual(c.affine,c.points[i]));
    }
    void LoadCalibration() {
        std::lock_guard<std::mutex> lock(calMutex);
        const auto path=configPath.string();
        for (std::size_t i=0;i<Maps.size();++i) {
            const auto section=std::string("calibration.")+Maps[i].slug;
            for (int n=0;n<8;++n) {
                char raw[256]{};
                const auto key="point"+std::to_string(n);
                GetPrivateProfileStringA(section.c_str(),key.c_str(),"",raw,
                    static_cast<DWORD>(sizeof(raw)),path.c_str());
                if (!raw[0]) continue;
                mapcal::Pair p{};
                if (::sscanf_s(raw,"%lf,%lf,%lf,%lf",
                    &p.worldX,&p.worldZ,&p.pixelX,&p.pixelY)==4)
                    calibration[i].points.push_back(p);
            }
            Fit(i);
        }
    }
    void SaveCalibration(std::size_t index) {
        const auto section=std::string("calibration.")+Maps[index].slug;
        const auto path=configPath.string();
        for (int n=0;n<8;++n) {
            const auto key="point"+std::to_string(n);
            if (static_cast<std::size_t>(n)<calibration[index].points.size()) {
                const auto& p=calibration[index].points[static_cast<std::size_t>(n)];
                char value[160]{};
                std::snprintf(value,sizeof(value),"%.9f,%.9f,%.9f,%.9f",
                    p.worldX,p.worldZ,p.pixelX,p.pixelY);
                WritePrivateProfileStringA(section.c_str(),key.c_str(),value,path.c_str());
            } else WritePrivateProfileStringA(section.c_str(),key.c_str(),nullptr,path.c_str());
        }
        if (calibration[index].valid) {
            const auto& m=calibration[index].affine;
            char value[192]{};
            std::snprintf(value,sizeof(value),"%.12f,%.12f,%.12f,%.12f,%.12f,%.12f",
                m.a,m.b,m.c,m.d,m.e,m.f);
            WritePrivateProfileStringA(section.c_str(),"affine",value,path.c_str());
        } else WritePrivateProfileStringA(section.c_str(),"affine",nullptr,path.c_str());
    }
    std::string MapsJson() {
        std::lock_guard<std::mutex> lock(calMutex);
        std::string out="{\"maps\":[";
        for (std::size_t i=0;i<Maps.size();++i) {
            if (i) out+=',';
            out+="{\"slug\":\"";out+=Maps[i].slug;
            out+="\",\"title\":\"";out+=Maps[i].title;
            out+="\",\"width\":16384,\"height\":16384,\"samples\":";
            out+=std::to_string(calibration[i].points.size());
            out+=",\"residual\":";out+=std::to_string(calibration[i].residual);
            out+=",\"thirdResidual\":";out+=std::to_string(calibration[i].thirdResidual);
            out+=",\"sourceAligned\":";
            out+=calibration[i].sourceAligned?"true":"false";
            out+=",\"provisional\":";
            out+=(calibration[i].points.size()==2 && calibration[i].valid)?"true":"false";
            out+=",\"affine\":";
            if (calibration[i].valid) {
                const auto& m=calibration[i].affine;
                out+='[';
                for (double value:{m.a,m.b,m.c,m.d,m.e,m.f}) {
                    if (out.back()!='[') out+=',';
                    out+=std::to_string(value);
                }
                out+=']';
            } else out+="null";
            out+='}';
        }
        out+="]}";return out;
    }
    void Broadcast(const std::string& text) {
        for (auto* client:clients)
            mg_websocket_write(client,MG_WEBSOCKET_OPCODE_TEXT,text.data(),text.size());
    }
    void Snapshot(maptelemetry::Snapshot&& next) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::string message=connected?
            mapserver::DeltaJson(latest,next):mapserver::FullJson(next,true);
        latest=std::move(next);connected=true;
        Broadcast(message);
    }
    void Disconnected() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!connected) return;
        connected=false;
        Broadcast("{\"type\":\"status\",\"connected\":false}");
    }
};
void Response(mg_connection* conn,int code,const char* status,
              const std::string& body,const char* type="application/json") {
    mg_printf(conn,"HTTP/1.1 %d %s\r\nContent-Type: %s; charset=utf-8\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        code,status,type,body.size());
    mg_write(conn,body.data(),body.size());
}
std::string CookieToken(const mg_connection* conn) {
    const char* header=mg_get_header(conn,"Cookie");
    if (!header) return {};
    const std::string cookies(header);
    const auto at=cookies.find("mt_session=");
    if (at==std::string::npos) return {};
    const auto start=at+11;
    const auto end=cookies.find(';',start);
    return cookies.substr(start,end==std::string::npos?end:end-start);
}
bool Authorized(const mg_connection* conn,const ServerState& state) {
    const auto* info=mg_get_request_info(conn);
    if (!info) return false;
    return EqualConstantTime(Query(info->query_string,"token"),state.token) ||
        EqualConstantTime(CookieToken(conn),state.token);
}
bool RateLimited(ServerState& state,const std::string& address) {
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto now=std::chrono::steady_clock::now();
    auto& failure=state.authFailures[address];
    if (now>=failure.reset) {failure.count=0;failure.reset=now+std::chrono::minutes(1);}
    return ++failure.count>static_cast<int>(maptelemetry::MaxAuthFailuresPerMinute);
}
int OnRequest(mg_connection* conn,void* context) {
    auto& state=*static_cast<ServerState*>(context);
    const auto* info=mg_get_request_info(conn);
    if (!info || !info->local_uri) return 400;
    if (!Authorized(conn,state)) {
        const bool limited=RateLimited(state,info->remote_addr);
        state.Log(std::string("auth-denied ")+info->remote_addr);
        Response(conn,limited?429:403,limited?"Too Many Requests":"Forbidden",
            limited?"{\"error\":\"rate limited\"}":"{\"error\":\"token required\"}");
        return limited?429:403;
    }
    const std::string uri(info->local_uri);
    if (uri=="/" && !Query(info->query_string,"token").empty()) {
        mg_printf(conn,"HTTP/1.1 303 See Other\r\nLocation: /\r\n"
            "Set-Cookie: mt_session=%s; HttpOnly; SameSite=Lax; Path=/\r\n"
            "Cache-Control: no-store\r\nContent-Length: 0\r\n\r\n",state.token.c_str());
        return 303;
    }
    if (uri=="/api/maps") {
        Response(conn,200,"OK",state.MapsJson());return 200;
    }
    if (uri=="/api/calibrate") {
        if (std::strcmp(info->request_method,"POST")!=0 ||
            info->content_length<0 || info->content_length>512) {
            Response(conn,400,"Bad Request","{\"error\":\"invalid request\"}");return 400;
        }
        char body[513]{};
        int received=0;
        while (received<info->content_length) {
            const int read=mg_read(conn,body+received,
                static_cast<std::size_t>(info->content_length-received));
            if (read<=0) break;
            received+=read;
        }
        if (received!=info->content_length) {
            Response(conn,400,"Bad Request","{\"error\":\"incomplete body\"}");return 400;
        }
        const auto slug=Query(body,"map");
        const auto index=state.MapIndex(slug);
        if (index>=Maps.size()) {
            Response(conn,400,"Bad Request","{\"error\":\"unknown map\"}");return 400;
        }
        std::lock_guard<std::mutex> lock(state.calMutex);
        if (Query(body,"clear")=="1") state.calibration[index].points.clear();
        else {
            mapcal::Pair pair{};
            if (!ParseDouble(Query(body,"wx").c_str(),pair.worldX) ||
                !ParseDouble(Query(body,"wz").c_str(),pair.worldZ) ||
                !ParseDouble(Query(body,"px").c_str(),pair.pixelX) ||
                !ParseDouble(Query(body,"py").c_str(),pair.pixelY) ||
                pair.pixelX<0 || pair.pixelX>MapPixels ||
                pair.pixelY<0 || pair.pixelY>MapPixels ||
                state.calibration[index].points.size()>=8) {
                Response(conn,400,"Bad Request","{\"error\":\"invalid calibration point\"}");
                return 400;
            }
            state.calibration[index].points.push_back(pair);
        }
        state.Fit(index);state.SaveCalibration(index);
        Response(conn,200,"OK","{\"ok\":true}");return 200;
    }
    static const std::unordered_map<std::string,std::string> assets{{
        {"/","index.html"},{"/app.js","app.js"},{"/style.css","style.css"},
        {"/features.css","features.css"},
        {"/vendor/leaflet.js","vendor/leaflet.js"},
        {"/vendor/leaflet.css","vendor/leaflet.css"}}};
    auto found=assets.find(uri);
    if (found!=assets.end()) {
        const auto path=(state.webRoot/found->second).string();
        mg_send_file(conn,path.c_str());return 200;
    }
    static const std::regex tilePattern(
        R"(^/tiles/(chernarusplus|namalsk|nasdara)/([0-5])/([0-9]{1,2})/([0-9]{1,2})\.jpg$)");
    std::smatch match;
    if (std::regex_match(uri,match,tilePattern)) {
        const int zoom=std::stoi(match[2]);
        const int x=std::stoi(match[3]),y=std::stoi(match[4]);
        if (x<(1<<zoom) && y<(1<<zoom)) {
            const auto path=(state.webRoot/"tiles"/match[1].str()/
                match[2].str()/match[3].str()/(match[4].str()+".jpg")).string();
            // The public preview has no bundled terrain imagery. Licensed tiles
            // placed at this path take precedence over the schematic backdrop.
            if (std::filesystem::exists(path)) mg_send_file(conn,path.c_str());
            else {
                const auto demo=(state.webRoot/"demo-map.svg").string();
                mg_send_file(conn,demo.c_str());
            }
            return 200;
        }
    }
    Response(conn,404,"Not Found","{\"error\":\"not found\"}");return 404;
}
int OnWebSocketConnect(const mg_connection* conn,void* context) {
    auto& state=*static_cast<ServerState*>(context);
    if (Authorized(conn,state)) return 0;
    const auto* info=mg_get_request_info(conn);
    if (info) {
        RateLimited(state,info->remote_addr);
        state.Log(std::string("ws-auth-denied ")+info->remote_addr);
    }
    return 1;
}
void OnWebSocketReady(mg_connection* conn,void* context) {
    auto& state=*static_cast<ServerState*>(context);
    const auto* info=mg_get_request_info(conn);
    std::lock_guard<std::mutex> lock(state.mutex);
    state.clients.insert(conn);
    const auto body=mapserver::FullJson(state.latest,state.connected);
    mg_websocket_write(conn,MG_WEBSOCKET_OPCODE_TEXT,body.data(),body.size());
    state.Log(std::string("ws-open ")+(info?info->remote_addr:"unknown"));
}
int OnWebSocketData(mg_connection*,int,char*,size_t,void*) {return 1;}
void OnWebSocketClose(const mg_connection* conn,void* context) {
    auto& state=*static_cast<ServerState*>(context);
    std::lock_guard<std::mutex> lock(state.mutex);
    state.clients.erase(const_cast<mg_connection*>(conn));state.Log("ws-close");
}
bool ReadAll(HANDLE pipe,void* buffer,std::size_t length) {
    auto* bytes=static_cast<std::uint8_t*>(buffer);
    while (length) {
        DWORD got=0;
        if (!ReadFile(pipe,bytes,static_cast<DWORD>(
                std::min<std::size_t>(length,65536)),&got,nullptr) || !got) return false;
        bytes+=got;length-=got;
    }
    return true;
}
void PipeLoop(ServerState& state) {
    while (!g_stop.load()) {
        if (!WaitNamedPipeW(maptelemetry::PipeName,250)) {Sleep(250);continue;}
        HANDLE pipe=CreateFileW(maptelemetry::PipeName,GENERIC_READ,0,nullptr,
            OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (pipe==INVALID_HANDLE_VALUE) {Sleep(250);continue;}
        state.Log("pipe-open");
        while (!g_stop.load()) {
            std::uint32_t length=0;
            if (!ReadAll(pipe,&length,sizeof(length)) ||
                length<sizeof(maptelemetry::Header) ||
                length>maptelemetry::MaxPacketBytes) break;
            std::vector<std::uint8_t> bytes(length);
            if (!ReadAll(pipe,bytes.data(),bytes.size())) break;
            maptelemetry::Snapshot snapshot{};
            if (!maptelemetry::Decode(bytes.data(),bytes.size(),snapshot)) break;
            state.Snapshot(std::move(snapshot));
        }
        CloseHandle(pipe);state.Disconnected();state.Log("pipe-close");
    }
}
bool LoadConfig(ServerState& state) {
    wchar_t module[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr,module,MAX_PATH)) return false;
    state.root=std::filesystem::path(module).parent_path().parent_path();
    state.webRoot=state.root/"maptelemetry"/"web";
    state.configPath=state.root/"maptelemetry.ini";
    const auto path=state.configPath.string();
    char buffer[256]{};
    GetPrivateProfileStringA("server","port","8765",buffer,sizeof(buffer),path.c_str());
    try {state.port=std::stoi(buffer);} catch (...) {return false;}
    if (state.port<1 || state.port>65535) return false;
    GetPrivateProfileStringA("server","token","",buffer,sizeof(buffer),path.c_str());
    state.token=buffer;
    GetPrivateProfileStringA("server","share_host","",buffer,sizeof(buffer),path.c_str());
    state.shareHost=buffer;
    if (state.token.empty()) {
        std::uint8_t random[24]{};
        if (BCryptGenRandom(nullptr,random,sizeof(random),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0) return false;
        state.token=Hex(random,sizeof(random));
        if (!WritePrivateProfileStringA("server","port",
                std::to_string(state.port).c_str(),path.c_str()) ||
            !WritePrivateProfileStringA("server","token",state.token.c_str(),
                path.c_str())) return false;
    }
    state.LoadCalibration();
    state.log.open(state.root/"notes"/"maptelemetry-server.log",std::ios::app);
    return true;
}
}
int main() {
    std::setlocale(LC_NUMERIC,"C");
    ServerState state{};
    if (!LoadConfig(state)) {
        std::fputs("map telemetry configuration failed\n",stderr);return 1;
    }
    SetConsoleCtrlHandler(OnConsole,TRUE);
    constexpr unsigned required=MG_FEATURES_FILES|MG_FEATURES_WEBSOCKET;
    if ((mg_init_library(required)&required)!=required) {
        std::fputs("CivetWeb initialization failed\n",stderr);return 2;
    }
    const auto port=std::to_string(state.port);
    const char* options[]={"listening_ports",port.c_str(),"num_threads","8",
        "enable_keep_alive","yes","enable_websocket_ping_pong","yes",nullptr};
    mg_context* context=mg_start(nullptr,&state,options);
    if (!context) {std::fputs("HTTP listener failed\n",stderr);mg_exit_library();return 3;}
    mg_set_request_handler(context,"/",OnRequest,&state);
    mg_set_websocket_handler(context,"/stream",OnWebSocketConnect,OnWebSocketReady,
        OnWebSocketData,OnWebSocketClose,&state);
    std::thread pipe(PipeLoop,std::ref(state));
    state.Log("server-start port="+port);
    std::printf("Map telemetry server listening on localhost and LAN, port %s\n",port.c_str());
    std::printf("Open: http://127.0.0.1:%s/?token=%s\n",port.c_str(),state.token.c_str());
    if (!state.shareHost.empty())
        std::printf("Share: http://%s:%s/?token=%s\n",state.shareHost.c_str(),
            port.c_str(),state.token.c_str());
    std::printf("Ctrl+C to stop.\n");
    while (!g_stop.load()) Sleep(100);
    CancelSynchronousIo(static_cast<HANDLE>(pipe.native_handle()));
    pipe.join();mg_stop(context);mg_exit_library();
    state.Log("server-stop");
    return 0;
}
