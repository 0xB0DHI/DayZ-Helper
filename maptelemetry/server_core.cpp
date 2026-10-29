#include "server_core.h"
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace mapserver {
namespace {
void Number(std::string& out,double value) {
    char buffer[64]{};
    std::snprintf(buffer,sizeof(buffer),"%.3f",value);
    out+=buffer;
}
void Integer(std::string& out,std::uint64_t value) {out+=std::to_string(value);}
void Quoted(std::string& out,const char* value) {
    out+='"';
    for (const auto* c=reinterpret_cast<const unsigned char*>(value);*c;++c) {
        if (*c=='"' || *c=='\\') {out+='\\';out+=static_cast<char>(*c);}
        else if (*c<32) {char s[8]{};std::snprintf(s,sizeof(s),"\\u%04x",*c);out+=s;}
        else out+=static_cast<char>(*c);
    }
    out+='"';
}
void Id(std::string& out,std::uint64_t id) {
    char buffer[24]{};
    std::snprintf(buffer,sizeof(buffer),"%016llx",static_cast<unsigned long long>(id));
    Quoted(out,buffer);
}
void Shared(std::string& out,const maptelemetry::Header& h,bool connected) {
    out+="\"seq\":";Integer(out,h.sequence);
    out+=",\"t\":";Integer(out,h.utcMilliseconds);
    out+=",\"connected\":";out+=connected?"true":"false";
    out+=",\"local\":{\"valid\":";out+=(h.flags&1)?"true":"false";
    out+=",\"x\":";Number(out,h.localX);
    out+=",\"y\":";Number(out,h.localY);
    out+=",\"z\":";Number(out,h.localZ);
    out+=",\"hx\":";Number(out,h.headingX);
    out+=",\"hz\":";Number(out,h.headingZ);
    out+=",\"held\":";Quoted(out,h.localHeld);
    out+='}';
    out+=",\"terrain\":";Quoted(out,h.terrain);
    out+=",\"counts\":[";
    for (std::size_t i=0;i<h.counts.size();++i) {
        if (i) out+=',';
        Integer(out,h.counts[i]);
    }
    out+=']';
}
void Point(std::string& out,const maptelemetry::Point& p) {
    out+="{\"id\":";Id(out,p.id);
    out+=",\"k\":";Integer(out,p.kind);
    out+=",\"c\":";Integer(out,p.category);
    out+=",\"l\":";Integer(out,p.life);
    out+=",\"q\":";Integer(out,p.quality);
    out+=",\"x\":";Number(out,p.x);
    out+=",\"y\":";Number(out,p.y);
    out+=",\"z\":";Number(out,p.z);
    out+=",\"d\":";Number(out,p.distance);
    out+=",\"hx\":";Number(out,p.headingX);
    out+=",\"hz\":";Number(out,p.headingZ);
    out+=",\"n\":";Quoted(out,p.name);
    out+=",\"held\":";Quoted(out,p.held);
    out+='}';
}
}
std::string FullJson(const maptelemetry::Snapshot& snapshot,bool connected) {
    std::string out="{\"type\":\"full\",";
    Shared(out,snapshot.header,connected);
    out+=",\"points\":[";
    for (std::size_t i=0;i<snapshot.points.size();++i) {
        if (i) out+=',';
        Point(out,snapshot.points[i]);
    }
    out+="]}";
    return out;
}
std::string DeltaJson(const maptelemetry::Snapshot& before,
                      const maptelemetry::Snapshot& after) {
    std::unordered_map<std::uint64_t,const maptelemetry::Point*> prior;
    prior.reserve(before.points.size());
    for (const auto& point:before.points) prior[point.id]=&point;
    std::unordered_set<std::uint64_t> current;
    current.reserve(after.points.size());
    std::string out="{\"type\":\"delta\",";
    Shared(out,after.header,true);
    out+=",\"upsert\":[";
    bool comma=false;
    for (const auto& point:after.points) {
        current.insert(point.id);
        const auto old=prior.find(point.id);
        if (old!=prior.end() &&
            std::memcmp(old->second,&point,sizeof(point))==0) continue;
        if (comma) out+=',';
        Point(out,point);comma=true;
    }
    out+="],";
    out+="\"remove\":[";comma=false;
    for (const auto& point:before.points) {
        if (current.count(point.id)) continue;
        if (comma) out+=',';
        Id(out,point.id);comma=true;
    }
    out+="]}";
    return out;
}
}
