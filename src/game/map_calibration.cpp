#include "map_calibration.h"
#include <cmath>
#include <limits>

namespace mapcal {
namespace {
bool Finite(double value) { return std::isfinite(value) && std::abs(value)<1.0e9; }
}
bool Fit(const std::array<Pair,3>& points,Affine& out) {
    for (const auto& p:points)
        if (!Finite(p.worldX) || !Finite(p.worldZ) ||
            !Finite(p.pixelX) || !Finite(p.pixelY)) return false;
    const auto& p=points[0];const auto& q=points[1];const auto& r=points[2];
    const double ux=q.worldX-p.worldX,uz=q.worldZ-p.worldZ;
    const double vx=r.worldX-p.worldX,vz=r.worldZ-p.worldZ;
    const double det=ux*vz-uz*vx;
    if (std::abs(det)<1.0e-6) return false;
    const double px=q.pixelX-p.pixelX,py=r.pixelX-p.pixelX;
    const double qx=q.pixelY-p.pixelY,qy=r.pixelY-p.pixelY;
    Affine next{};
    next.a=(px*vz-uz*py)/det;next.b=(ux*py-px*vx)/det;
    next.d=(qx*vz-uz*qy)/det;next.e=(ux*qy-qx*vx)/det;
    next.c=p.pixelX-next.a*p.worldX-next.b*p.worldZ;
    next.f=p.pixelY-next.d*p.worldX-next.e*p.worldZ;
    if (!Finite(next.a) || !Finite(next.b) || !Finite(next.c) ||
        !Finite(next.d) || !Finite(next.e) || !Finite(next.f) ||
        std::abs(next.a*next.e-next.b*next.d)<1.0e-12) return false;
    out=next;return true;
}
bool FitAxisAligned(const std::array<Pair,2>& points,Affine& out) {
    const auto& p=points[0];const auto& q=points[1];
    if (!Finite(p.worldX) || !Finite(p.worldZ) || !Finite(p.pixelX) ||
        !Finite(p.pixelY) || !Finite(q.worldX) || !Finite(q.worldZ) ||
        !Finite(q.pixelX) || !Finite(q.pixelY) ||
        std::abs(q.worldX-p.worldX)<1.0e-6 ||
        std::abs(q.worldZ-p.worldZ)<1.0e-6) return false;
    Affine next{};
    next.a=(q.pixelX-p.pixelX)/(q.worldX-p.worldX);
    next.e=(q.pixelY-p.pixelY)/(q.worldZ-p.worldZ);
    next.c=p.pixelX-next.a*p.worldX;
    next.f=p.pixelY-next.e*p.worldZ;
    if (!Finite(next.a) || !Finite(next.e) || !Finite(next.c) ||
        !Finite(next.f) || std::abs(next.a*next.e)<1.0e-12) return false;
    out=next;return true;
}
bool TileAligned(double worldExtent,double coverage,double imagePixels,Affine& out) {
    if (!Finite(worldExtent) || !Finite(coverage) || !Finite(imagePixels) ||
        worldExtent<=0 || coverage<=0 || coverage>1 || imagePixels<=0) return false;
    const double scale=imagePixels*coverage/worldExtent;
    if (!Finite(scale) || scale<=0) return false;
    Affine next{};
    next.a=scale;
    next.e=-scale;
    next.f=imagePixels*coverage;
    out=next;return true;
}
bool Forward(const Affine& map,double worldX,double worldZ,double& pixelX,double& pixelY) {
    const double x=map.a*worldX+map.b*worldZ+map.c;
    const double y=map.d*worldX+map.e*worldZ+map.f;
    if (!Finite(x) || !Finite(y)) return false;
    pixelX=x;pixelY=y;return true;
}
bool Inverse(const Affine& map,double pixelX,double pixelY,double& worldX,double& worldZ) {
    const double det=map.a*map.e-map.b*map.d;
    if (!Finite(pixelX) || !Finite(pixelY) || !Finite(det) ||
        std::abs(det)<1.0e-12) return false;
    const double x=((pixelX-map.c)*map.e-map.b*(pixelY-map.f))/det;
    const double z=(map.a*(pixelY-map.f)-(pixelX-map.c)*map.d)/det;
    if (!Finite(x) || !Finite(z)) return false;
    worldX=x;worldZ=z;return true;
}
double Residual(const Affine& map,const Pair& point) {
    double x=0,y=0;
    if (!Forward(map,point.worldX,point.worldZ,x,y))
        return std::numeric_limits<double>::infinity();
    return std::hypot(x-point.pixelX,y-point.pixelY);
}
}
