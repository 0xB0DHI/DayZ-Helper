#pragma once
#include <array>
#include <cstddef>

namespace mapcal {
struct Pair { double worldX=0,worldZ=0,pixelX=0,pixelY=0; };
struct Affine {
    // pixelX=a*worldX+b*worldZ+c; pixelY=d*worldX+e*worldZ+f
    double a=0,b=0,c=0,d=0,e=0,f=0;
};
bool Fit(const std::array<Pair,3>& points,Affine& out);
bool FitAxisAligned(const std::array<Pair,2>& points,Affine& out);
// Source tiles are a square image of imagePixels per side. coverage and
// worldExtent come from the same source map's coordinate metadata.
bool TileAligned(double worldExtent,double coverage,double imagePixels,Affine& out);
bool Forward(const Affine& map,double worldX,double worldZ,double& pixelX,double& pixelY);
bool Inverse(const Affine& map,double pixelX,double pixelY,double& worldX,double& worldZ);
double Residual(const Affine& map,const Pair& point);
}
