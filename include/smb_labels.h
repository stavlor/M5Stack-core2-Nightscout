#pragma once
#include <stddef.h>
#include <algorithm>
namespace smblabels {
constexpr size_t CAPACITY=128;
struct Group {
  int first=0,last=0,x=0,width=0,lane=0;
  float units=0;
  unsigned count=0;
};
class Layout {
public:
  Group groups[CAPACITY];
  size_t size=0;
  void reset() {size=0;}
  void add(int x,float units) {
    if (size==CAPACITY) return;
    Group &g=groups[size++];g=Group();g.first=g.last=x;g.units=units;g.count=1;
  }
  template<class Measure>
  bool arrange(int left,int right,Measure measure,int gap=3,int maxShift=8) {
    if (left>right) return false;
    std::sort(groups,groups+size,[](const Group&a,const Group&b){return a.first<b.first;});
    for (;;) {
      int nextLeft[2]={right+1+gap,right+1+gap};
      size_t failed=size;
      for (size_t n=size;n>0;--n) {
        size_t i=n-1;Group &g=groups[i];g.width=measure(g);
        if (g.width>right-left+1) {failed=i;break;}
        int desired=std::max(left,std::min((g.first+g.last)/2-g.width/2,right+1-g.width));
        int chosen=-1,bestShift=maxShift+1,bestX=desired;
        for (int row=0;row<2;++row) {
          int x=std::min(desired,nextLeft[row]-gap-g.width);
          int shift=desired-x;
          if (x>=left && shift<=maxShift && shift<bestShift) {chosen=row;bestShift=shift;bestX=x;}
        }
        if (chosen<0) {failed=i;break;}
        g.x=bestX;g.lane=chosen;nextLeft[chosen]=bestX;
      }
      if (failed==size) return true;
      if (size<2) return false;
      // Merge only adjacent SMB label groups, then measure the new sum again.
      size_t a=failed,b=failed+1;
      if (b==size) {a=failed-1;b=failed;}
      groups[a].first=std::min(groups[a].first,groups[b].first);
      groups[a].last=std::max(groups[a].last,groups[b].last);
      groups[a].units+=groups[b].units;groups[a].count+=groups[b].count;
      for (size_t i=b;i+1<size;++i) groups[i]=groups[i+1];
      --size;
    }
  }
  bool overlaps(int lane,int x,int width,int gap=3) const {
    for (size_t i=0;i<size;++i) {
      const Group &g=groups[i];
      if (g.lane==lane && x<g.x+g.width+gap && x+width+gap>g.x) return true;
    }
    return false;
  }
};
}
