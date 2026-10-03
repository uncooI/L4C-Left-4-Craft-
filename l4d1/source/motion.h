#pragma once
#include "skycraft_protocol.h"
#include <cmath>
#include <cstring>
namespace left4craft {
class MotionClock {
    skycraft::proto::McState history_[8]{};
    unsigned count_{};
public:
    void sample(skycraft::proto::McState& state,std::int64_t now,std::int64_t frequency) {
        if(frequency<=0||state.tickQpc<=0||!std::isfinite(state.tickMs)||state.tickMs<=0||state.tickMs>1000)return;
        if(count_&&(state.tickQpc<history_[count_-1].tickQpc||state.teleportAck!=history_[count_-1].teleportAck))count_=0;
        if(!count_||history_[count_-1].tickQpc!=state.tickQpc){
            if(count_==8){std::memmove(history_,history_+1,7*sizeof(*history_));count_=7;}
            history_[count_++]=state;
        }
        // A 25 ms publication allowance absorbs the other process's 60 Hz render phase.
        double renderTime=double(now)-double(frequency)*.025;
        unsigned index=0;for(unsigned i=0;i<count_;++i)if(double(history_[i].tickQpc)<=renderTime)index=i;
        const auto& tick=history_[index];
        double t=(renderTime-double(tick.tickQpc))/(double(frequency)*tick.tickMs/1000.0);
        if(t<0)t=0;if(t>1)t=1;
        state.x=tick.prevX+(tick.curX-tick.prevX)*t;
        state.y=tick.prevY+(tick.curY-tick.prevY)*t;
        state.z=tick.prevZ+(tick.curZ-tick.prevZ)*t;
        if(state.cameraMode==0){state.eyeX=state.x;state.eyeY=state.y+tick.tickEyeO+(tick.tickEye-tick.tickEyeO)*t;state.eyeZ=state.z;}
    }
};
inline float sourceBaseFov(float vertical){return float(2*std::atan(std::tan(vertical*0.00872664626)*(4.0/3.0))*57.295779513);}
}
