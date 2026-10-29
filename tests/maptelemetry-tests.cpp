#include "../src/game/map_telemetry.h"
#include "../src/game/map_calibration.h"
#include "../maptelemetry/server_core.h"
#include "../src/app/map_publisher.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

namespace {
int checks=0,failures=0;
void Check(bool okay,const char* reason) {
    ++checks;if (!okay) {++failures;std::printf("FAIL %s\n",reason);}
}
bool Close(double a,double b) {return std::abs(a-b)<0.001;}
maptelemetry::Snapshot Fixture() {
    maptelemetry::Snapshot frame{};
    frame.header.sequence=7;frame.header.utcMilliseconds=123456;
    frame.header.flags=3;frame.header.localX=4480;frame.header.localY=200;
    frame.header.localZ=6420;frame.header.headingZ=1;
    std::memcpy(frame.header.terrain,"nasdara",8);
    std::snprintf(frame.header.localHeld,sizeof(frame.header.localHeld),"Mosin 91/30");
    maptelemetry::Point player{};
    player.id=maptelemetry::EphemeralId(0x12345678,99);
    player.kind=0;player.life=1;player.x=4482;player.y=200;
    player.z=6424;player.distance=4.5f;
    player.headingX=0.6f;player.headingZ=0.8f;
    std::snprintf(player.name,sizeof(player.name),"Ava");
    std::snprintf(player.held,sizeof(player.held),"Splitting Axe");
    maptelemetry::Point item{};
    item.id=maptelemetry::EphemeralId(0x555555,99);
    item.kind=4;item.category=3;item.x=4484;item.y=200;
    item.z=6425;item.distance=6.5f;item.quality=2;
    std::snprintf(item.name,sizeof(item.name),"Apple");
    frame.points={player,item};frame.header.count=2;
    frame.header.counts[0]=1;frame.header.counts[4]=1;
    return frame;
}
maptelemetry::Snapshot AllLayersFixture() {
    auto frame=Fixture();
    for (std::uint8_t kind=1;kind<=3;++kind) {
        maptelemetry::Point point{};
        point.id=maptelemetry::EphemeralId(0x777000+kind,99);
        point.kind=kind;point.x=4480.0f+kind;point.z=6420.0f+kind;
        point.distance=static_cast<float>(kind)*10.0f;
        frame.points.push_back(point);
        ++frame.header.counts[kind];
    }
    frame.header.count=static_cast<std::uint32_t>(frame.points.size());
    return frame;
}
}
int main(int argc,char** argv) {
    if (argc==2 && std::strcmp(argv[1],"--layers-fixture")==0) {
        MapPublisher publisher{};
        if (!publisher.Start()) return 2;
        for (int i=0;i<40 && !publisher.Connected();++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!publisher.Connected()) return 3;
        auto layers=AllLayersFixture();publisher.Submit(layers);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        publisher.Stop();
        return 0;
    }
    if (argc==2 && std::strcmp(argv[1],"--pipe-fixture")==0) {
        MapPublisher publisher{};
        if (!publisher.Start()) return 2;
        for (int i=0;i<40 && !publisher.Connected();++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!publisher.Connected()) return 3;
        auto first=Fixture();publisher.Submit(first);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        auto second=Fixture();second.header.sequence=8;
        second.points.erase(second.points.begin());
        second.header.count=1;second.header.counts[0]=0;
        second.points[0].x=4490;
        publisher.Submit(second);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        publisher.Stop();
        return 0;
    }
    auto frame=Fixture();
    std::vector<std::uint8_t> wire;
    Check(maptelemetry::Encode(frame,wire),"encode complete fake frame");
    Check(wire.size()==sizeof(maptelemetry::Header)+2*sizeof(maptelemetry::Point),
        "bounded packet size");
    maptelemetry::Snapshot decoded{};
    Check(maptelemetry::Decode(wire.data(),wire.size(),decoded) &&
        decoded.header.sequence==7 && decoded.points.size()==2 &&
        std::strcmp(decoded.points[1].name,"Apple")==0 &&
        std::strcmp(decoded.points[0].held,"Splitting Axe")==0 &&
        std::strcmp(decoded.header.localHeld,"Mosin 91/30")==0 &&
        std::strcmp(decoded.header.terrain,"nasdara")==0,
        "round trip fake frame with current held items");
    const auto preserved=decoded.header.sequence;
    Check(!maptelemetry::Decode(wire.data(),wire.size()-1,decoded) &&
        decoded.header.sequence==preserved,"truncated packet no partial publication");
    wire[0]=0;
    Check(!maptelemetry::Decode(wire.data(),wire.size(),decoded) &&
        decoded.points.size()==2,"invalid magic no partial publication");
    frame.header.counts[4]=2;
    Check(!maptelemetry::Encode(frame,wire),"count mismatch rejected");
    frame=Fixture();frame.points[1].x=std::nanf("");
    Check(!maptelemetry::Encode(frame,wire),"invalid coordinate rejected");
    frame=Fixture();std::memset(frame.points[0].held,'X',sizeof(frame.points[0].held));
    Check(!maptelemetry::Encode(frame,wire),"unterminated held item rejected");
    frame=Fixture();
    const auto full=mapserver::FullJson(frame,true);
    Check(full.find("\"Ava\"")!=std::string::npos &&
        full.find("\"terrain\":\"nasdara\"")!=std::string::npos &&
        full.find("\"connected\":true")!=std::string::npos,
        "full browser frame contains names and status");
    Check(full.find("\"hx\":0.600,\"hz\":0.800")!=std::string::npos,
          "survivor heading reaches browser frame");
    Check(full.find("\"held\":\"Splitting Axe\"")!=std::string::npos &&
          full.find("\"held\":\"Mosin 91/30\"")!=std::string::npos,
          "remote and local held items reach browser frame");
    auto heldSwap=frame;heldSwap.header.sequence=8;
    std::snprintf(heldSwap.points[0].held,sizeof(heldSwap.points[0].held),"BK-18");
    const auto heldDelta=mapserver::DeltaJson(frame,heldSwap);
    Check(heldDelta.find("\"held\":\"BK-18\"")!=std::string::npos &&
          heldDelta.find("\"remove\":[]")!=std::string::npos,
          "held-item swap publishes a player update");
    auto after=frame;after.header.sequence=8;
    after.points.erase(after.points.begin());
    after.points[0].x=4490;after.header.count=1;after.header.counts[0]=0;
    const auto delta=mapserver::DeltaJson(frame,after);
    Check(delta.find("\"remove\":[")!=std::string::npos &&
        delta.find("\"upsert\":[")!=std::string::npos &&
        delta.find("4490.000")!=std::string::npos,
        "delta publishes update and removal");
    const std::array<mapcal::Pair,3> references{{
        {0,0,100,200},{100,0,300,175},{0,100,150,500}}};
    mapcal::Affine affine{};
    Check(mapcal::Fit(references,affine),"three point affine fit");
    double px=0,py=0,wx=0,wz=0;
    Check(mapcal::Forward(affine,25,40,px,py) && Close(px,170) &&
        Close(py,313.75),"affine forward");
    Check(mapcal::Inverse(affine,px,py,wx,wz) && Close(wx,25) &&
        Close(wz,40),"affine inverse");
    Check(Close(mapcal::Residual(affine,{25,40,170,313.75}),0),
        "validation residual");
    const std::array<mapcal::Pair,2> provisional{{
        {100,100,500,600},{200,300,700,200}}};
    Check(mapcal::FitAxisAligned(provisional,affine) &&
        mapcal::Forward(affine,150,200,px,py) && Close(px,600) && Close(py,400),
        "two point north-up provisional fit");
    Check(!mapcal::FitAxisAligned({{{100,100,500,600},{100,300,700,200}}},affine),
        "two point degenerate fit rejected");
    Check(!mapcal::Fit({{{0,0,0,0},{1,1,1,1},{2,2,2,2}}},affine),
        "collinear affine points rejected");
    Check(mapcal::TileAligned(16384,1.0,16384,affine) &&
        mapcal::Forward(affine,7799,11669,px,py) &&
        Close(px,7799) && Close(py,4715),
        "Nasdara source-tile alignment");
    Check(mapcal::Inverse(affine,px,py,wx,wz) &&
        Close(wx,7799) && Close(wz,11669),
        "source-tile alignment inverse");
    Check(mapcal::TileAligned(15360,0.9644165039,16384,affine) &&
        mapcal::Forward(affine,15360,0,px,py) &&
        Close(px,16384*0.9644165039) && Close(py,16384*0.9644165039),
        "Chernarus source coverage");
    Check(!mapcal::TileAligned(0,1.0,16384,affine) &&
        !mapcal::TileAligned(16384,1.1,16384,affine),
        "invalid source metadata rejected");
    auto layers=AllLayersFixture();
    Check(maptelemetry::Encode(layers,wire) &&
        maptelemetry::Decode(wire.data(),wire.size(),decoded) &&
        decoded.header.count==5 && decoded.header.counts[0]==1 &&
        decoded.header.counts[1]==1 && decoded.header.counts[2]==1 &&
        decoded.header.counts[3]==1 && decoded.header.counts[4]==1,
        "all five map-layer categories survive the wire packet");
    decoded.points[2].kind=7;
    Check(!maptelemetry::Encode(decoded,wire),
        "out-of-range map-layer category rejected");
    std::printf("checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
