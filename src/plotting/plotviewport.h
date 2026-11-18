#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

// Coordinate state and pointer-anchored transforms; independent of QML/rendering.
class PlotViewport {
public:
    struct Range { double left,right; long double low,high; };
    Range range(double latest) const { const double end=m_follow?latest:m_right; return {end-m_span,end,m_low,m_high}; }
    double timeSpan() const { return m_span; }
    bool following() const { return m_follow; }
    bool autoRange() const { return m_autoY; }
    void reset() { m_span=10; m_follow=true; m_autoY=true; m_low=-1; m_high=1; }
    void fitY(long double low,long double high) {
        if(!m_autoY) return;
        const auto padding=high==low?std::max(1.L,std::abs(high)*0.1L):(high-low)*0.08L;
        setY(low-padding,high+padding);
    }
    void zoom(double factor,bool x,bool y,double anchorX,double anchorY,double latest) {
        const auto old=range(latest);
        if(x) {
            const double anchor=old.left+anchorX*m_span;
            m_span=std::clamp(m_span*factor,0.001,60.);
            m_right=anchor+(1-anchorX)*m_span; m_follow=false; clampTime(latest);
        }
        if(y) {
            const long double anchor=old.low+anchorY*(old.high-old.low);
            const auto span=std::clamp((old.high-old.low)*factor,1e-12L,2.L*std::numeric_limits<double>::max());
            setY(anchor-anchorY*span,anchor+(1-anchorY)*span); m_autoY=false;
        }
    }
    void pan(double xFraction,double yFraction,bool x,bool y,double latest) {
        const auto old=range(latest);
        if(x) { m_right=old.right+xFraction*m_span; m_follow=false; clampTime(latest); }
        if(y) { const auto shift=yFraction*(old.high-old.low); setY(old.low+shift,old.high+shift); m_autoY=false; }
    }
private:
    double m_span=10,m_right=0;
    long double m_low=-1,m_high=1;
    bool m_follow=true,m_autoY=true;
    void clampTime(double latest) { m_right=std::clamp(m_right,latest-60+m_span,latest); }
    void setY(long double low,long double high) {
        const long double limit=std::numeric_limits<double>::max();
        m_low=std::clamp(low,-limit,limit); m_high=std::clamp(high,-limit,limit);
        if(m_high<=m_low) { const auto padding=std::max(1e-12L,std::abs(m_low)*1e-12L); m_low=std::max(-limit,m_low-padding); m_high=std::min(limit,m_high+padding); }
    }
};
