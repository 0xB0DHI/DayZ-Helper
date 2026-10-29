#pragma once
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>

namespace aim {
// Radius is measured in pixels from the center of the current render rectangle.
struct Candidate {
    std::uint64_t entity=0;
    game::Vec2 screen{};
    float distance=0;
    bool mainView=false;
};
struct Selection {
    std::uint64_t entity=0;
    game::Vec2 screen{};
    float centerPixels=0;
    bool mainView=false;
    bool retained=false;
};
inline float TieTolerancePixels(float stickinessPercent) {
    return std::clamp(stickinessPercent,0.0f,400.0f)*0.0025f;
}
inline float CenterDistance(game::Vec2 point,float width,float height) {
    const float x=point.x-width*0.5f,y=point.y-height*0.5f;
    return std::sqrt(x*x+y*y);
}
inline bool InCone(game::Vec2 point,float width,float height,float radius) {
    return std::isfinite(point.x) && std::isfinite(point.y) &&
           point.x>=0 && point.y>=0 && point.x<=width && point.y<=height &&
           std::isfinite(radius) && radius>0 &&
           CenterDistance(point,width,height)<=radius;
}
inline Selection Select(const std::vector<Candidate>& candidates,
                        std::uint64_t previous,float width,float height,
                        float radius,float stickinessPercent) {
    Selection best{};
    const float tieTolerance=TieTolerancePixels(stickinessPercent);
    for (const auto& candidate:candidates) {
        if (!candidate.entity ||
            !InCone(candidate.screen,width,height,radius)) continue;
        const float distance=CenterDistance(candidate.screen,width,height);
        if (!best.entity ||
            (candidate.entity==previous && best.entity!=previous &&
             distance<=best.centerPixels+tieTolerance) ||
            (distance<best.centerPixels &&
             !(best.entity==previous &&
               best.centerPixels-distance<=tieTolerance)) ||
            (distance==best.centerPixels && candidate.entity!=previous &&
             best.entity!=previous && candidate.entity<best.entity))
            best={candidate.entity,candidate.screen,distance,candidate.mainView,
                  candidate.entity==previous};
    }
    return best;
}
struct Motion {
    float velocityX=0,velocityY=0;
    float remainderX=0,remainderY=0;
    bool settled=false;
    void Reset() { *this={}; }
};
struct Delta { int x=0,y=0; };
struct VelocityHistory {
    struct Point { game::Vec3 position{}; std::uint64_t milliseconds=0; };
    std::array<Point,16> points{};
    std::uint64_t entity=0,lastFrame=0,lastMilliseconds=0;
    std::uint8_t next=0,count=0;
    void Reset() { *this={}; }
    bool Update(std::uint64_t key,game::Vec3 position,std::uint64_t milliseconds,
                std::uint64_t frame,std::uint32_t windowMs,float maxSpeed,
                game::Vec3& result) {
        if (!key || !game::Plausible(position) || !milliseconds || !frame ||
            windowMs<100 || windowMs>300 || !std::isfinite(maxSpeed) || maxSpeed<=0) {
            Reset();return false;
        }
        if (entity!=key || (lastFrame && frame!=lastFrame+1) ||
            (lastMilliseconds && (milliseconds<lastMilliseconds ||
             milliseconds-lastMilliseconds>100))) Reset();
        entity=key;lastFrame=frame;lastMilliseconds=milliseconds;
        const auto previous=static_cast<std::uint8_t>((next+points.size()-1)%points.size());
        if (!count || milliseconds-points[previous].milliseconds>=20) {
            points[next]={position,milliseconds};
            next=static_cast<std::uint8_t>((next+1)%points.size());
            if (count<points.size()) ++count;
        }
        for (std::uint8_t i=0;i<count;++i) {
            const auto index=static_cast<std::size_t>((next+points.size()-1-i)%points.size());
            const auto& old=points[index];
            if (milliseconds<=old.milliseconds) continue;
            const auto age=milliseconds-old.milliseconds;
            if (age<windowMs) continue;
            if (age>windowMs+60) break;
            const float inverse=1000.0f/static_cast<float>(age);
            const game::Vec3 pending{(position.x-old.position.x)*inverse,
                (position.y-old.position.y)*inverse,(position.z-old.position.z)*inverse};
            const float speed2=game::Dot(pending,pending);
            if (!game::Plausible(pending) || !std::isfinite(speed2) ||
                speed2>maxSpeed*maxSpeed) {Reset();return false;}
            result=pending;return true;
        }
        return false;
    }
};
inline constexpr float SettleRadiusPixels=3.0f;
inline constexpr float ResumeRadiusPixels=6.0f;
inline Delta Step(Motion& motion,game::Vec2 target,float width,float height,
                  float elapsed,float maxRate,float acceleration,
                  float snapStrength=5.0f) {
    if (!InCone(target,width,height,std::max(width,height)) ||
        !std::isfinite(elapsed) || elapsed<=0 || !std::isfinite(maxRate) ||
        maxRate<=0 || !std::isfinite(acceleration) || acceleration<=0 ||
        !std::isfinite(snapStrength)) {
        motion.Reset();return {};
    }
    const float dt=std::min(elapsed,0.05f);
    const float errorX=target.x-width*0.5f,errorY=target.y-height*0.5f;
    const float distance=std::sqrt(errorX*errorX+errorY*errorY);
    if (motion.settled) {
        if (distance<=ResumeRadiusPixels) return {};
        motion.Reset();
    }
    if (distance<=SettleRadiusPixels) {
        motion.Reset();motion.settled=true;return {};
    }
    // A delayed camera sample can cross center while velocity still points at
    // the old error. Stop that axis before issuing an opposite correction.
    if (errorX*motion.velocityX<0) {motion.velocityX=0;motion.remainderX=0;}
    if (errorY*motion.velocityY<0) {motion.velocityY=0;motion.remainderY=0;}
    const float strength=std::clamp(snapStrength,1.0f,20.0f);
    // Preserve the existing 1-10 response. Levels above 10 offer a
    // deliberately stronger range while retaining the same settle guard.
    const float strengthScale=strength<=10.0f?
        0.5f+strength*0.1f:1.5f+(strength-10.0f)*0.25f;
    const float tunedAcceleration=acceleration*strengthScale;
    const float speed=std::min({maxRate*strengthScale,
                                std::sqrt(2.0f*tunedAcceleration*distance),
                                distance*(2.0f+strength*0.8f)});
    const float desiredX=errorX/distance*speed,desiredY=errorY/distance*speed;
    const float changeX=desiredX-motion.velocityX,changeY=desiredY-motion.velocityY;
    const float change=std::sqrt(changeX*changeX+changeY*changeY);
    const float limit=tunedAcceleration*dt;
    const float scale=change>limit?limit/change:1.0f;
    motion.velocityX+=changeX*scale;motion.velocityY+=changeY*scale;
    float moveX=motion.velocityX*dt,moveY=motion.velocityY*dt;
    const float move=std::sqrt(moveX*moveX+moveY*moveY);
    const float maxMove=distance*0.5f;
    if (move>maxMove) {moveX*=maxMove/move;moveY*=maxMove/move;}
    const float rawX=moveX+motion.remainderX,rawY=moveY+motion.remainderY;
    const int x=static_cast<int>(std::trunc(rawX));
    const int y=static_cast<int>(std::trunc(rawY));
    motion.remainderX=rawX-static_cast<float>(x);
    motion.remainderY=rawY-static_cast<float>(y);
    return {x,y};
}
inline bool Lead(game::Vec3 position,game::Vec3 velocity,float distance,
                 float projectileSpeed,game::Vec3& result) {
    if (!game::Plausible(position) || !game::Plausible(velocity) ||
        !std::isfinite(distance) || distance<0 ||
        !std::isfinite(projectileSpeed) || projectileSpeed<1.0f) return false;
    const float time=distance/projectileSpeed;
    if (!std::isfinite(time) || time>5.0f) return false;
    const game::Vec3 predicted{position.x+velocity.x*time,
        position.y+velocity.y*time,position.z+velocity.z*time};
    if (!game::Plausible(predicted)) return false;
    result=predicted;return true;
}
inline bool Intercept(game::Vec3 camera,game::Vec3 position,game::Vec3 velocity,
                      float projectileSpeed,game::Vec3& result) {
    if (!game::Plausible(camera) || !game::Plausible(position) ||
        !game::Plausible(velocity) || !std::isfinite(projectileSpeed) ||
        projectileSpeed<1.0f) return false;
    float time=0;
    for (int i=0;i<3;++i) {
        const game::Vec3 predicted{position.x+velocity.x*time,
            position.y+velocity.y*time,position.z+velocity.z*time};
        if (!game::Plausible(predicted)) return false;
        const auto delta=game::Difference(predicted,camera);
        const float distance=std::sqrt(game::Dot(delta,delta));
        time=distance/projectileSpeed;
        if (!std::isfinite(time) || time>5.0f) return false;
    }
    const game::Vec3 pending{position.x+velocity.x*time,
        position.y+velocity.y*time,position.z+velocity.z*time};
    if (!game::Plausible(pending)) return false;
    result=pending;return true;
}
}
