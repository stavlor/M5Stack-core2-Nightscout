#pragma once
#include <algorithm>
#include <cmath>
namespace graphscale {
struct Bounds {float low,high;};
class Range {
  float minimum=0,maximum=0;
  bool any=false;
public:
  void add(float value) {
    if (!std::isfinite(value) || value<20 || value>600) return;
    if (!any) {minimum=maximum=value;any=true;}
    else {minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
  }
  Bounds bounds() const {
    if (!any) return {70,180};
    float padding=std::max(10.0f,(maximum-minimum)*0.1f);
    float lo=minimum-padding,hi=maximum+padding;
    if (hi-lo<60) {float mid=(hi+lo)*0.5f;lo=mid-30;hi=mid+30;}
    return {std::max(0.0f,std::floor(lo/10)*10),std::ceil(hi/10)*10};
  }
};
// Return only the visible intersection of the target band and current scale.
inline bool targetBand(Bounds scale,float &lo,float &hi) {
  lo=std::max(scale.low,70.0f);hi=std::min(scale.high,180.0f);
  return hi>lo;
}
}
