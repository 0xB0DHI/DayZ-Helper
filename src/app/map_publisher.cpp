#include "map_publisher.h"
#include <Windows.h>
#include <vector>

namespace {
bool WriteAll(HANDLE pipe,const void* data,std::size_t length) {
    const auto* bytes=static_cast<const std::uint8_t*>(data);
    while (length) {
        const DWORD chunk=static_cast<DWORD>(std::min<std::size_t>(length,65536));
        DWORD written=0;
        if (!WriteFile(pipe,bytes,chunk,&written,nullptr) || written==0) return false;
        bytes+=written;length-=written;
    }
    return true;
}
}
bool MapPublisher::Start() {
    if (worker_.joinable()) return true;
    stop_.store(false,std::memory_order_release);
    try { worker_=std::thread(&MapPublisher::Run,this); }
    catch (...) { return false; }
    return true;
}
void MapPublisher::Stop() {
    if (!worker_.joinable()) return;
    stop_.store(true,std::memory_order_release);wake_.notify_all();
    HANDLE unblock=CreateFileW(maptelemetry::PipeName,GENERIC_READ,0,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (unblock!=INVALID_HANDLE_VALUE) CloseHandle(unblock);
    CancelSynchronousIo(static_cast<HANDLE>(worker_.native_handle()));
    worker_.join();connected_.store(false,std::memory_order_release);
}
void MapPublisher::Submit(maptelemetry::Snapshot& snapshot) {
    if (!Connected()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.header=snapshot.header;
    pending_.points.swap(snapshot.points);
    hasPending_=true;wake_.notify_one();
}
void MapPublisher::Run() {
    while (!stop_.load(std::memory_order_acquire)) {
        HANDLE pipe=CreateNamedPipeW(maptelemetry::PipeName,PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
            1,262144,262144,0,nullptr);
        if (pipe==INVALID_HANDLE_VALUE) { Sleep(250);continue; }
        const bool attached=ConnectNamedPipe(pipe,nullptr)!=FALSE ||
            GetLastError()==ERROR_PIPE_CONNECTED;
        if (attached && !stop_.load(std::memory_order_acquire)) {
            connected_.store(true,std::memory_order_release);
            while (!stop_.load(std::memory_order_acquire)) {
                maptelemetry::Snapshot next{};
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait(lock,[this]{return hasPending_ || stop_.load();});
                    if (stop_.load()) break;
                    next.header=pending_.header;
                    next.points.swap(pending_.points);
                    hasPending_=false;
                }
                std::vector<std::uint8_t> bytes;
                if (!maptelemetry::Encode(next,bytes)) continue;
                const auto length=static_cast<std::uint32_t>(bytes.size());
                if (!WriteAll(pipe,&length,sizeof(length)) ||
                    !WriteAll(pipe,bytes.data(),bytes.size())) break;
            }
            connected_.store(false,std::memory_order_release);
        }
        DisconnectNamedPipe(pipe);CloseHandle(pipe);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hasPending_=false;pending_.points.clear();
        }
    }
}
