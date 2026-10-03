#include "motion.h"
#include <cassert>
#include <iostream>
int main(){
    using namespace left4craft;using skycraft::proto::McState;
    MotionClock clock;McState a{};a.tickQpc=1000;a.tickMs=50;a.curX=1;a.tickEyeO=a.tickEye=1.62;
    auto state=a;clock.sample(state,1050,1000);assert(std::abs(state.x-.5)<1e-9);assert(std::abs(state.eyeY-1.62)<1e-6);
    McState b=a;b.tickQpc=1050;b.prevX=1;b.curX=2;
    state=b;clock.sample(state,1060,1000);assert(std::abs(state.x-.7)<1e-9); // new tick arrives before our delayed frame reaches it
    state=b;clock.sample(state,1080,1000);assert(std::abs(state.x-1.1)<1e-9);
    state=b;clock.sample(state,1300,1000);assert(state.x==2); // no extrapolation through a wall when MC stalls
    b.teleportAck=1;b.prevX=b.curX=100;state=b;clock.sample(state,1060,1000);assert(state.x==100);
    b.tickQpc=500;b.prevX=b.curX=-10;state=b;clock.sample(state,550,1000);assert(state.x==-10); // restarted clock
    b.cameraMode=1;b.eyeX=123;state=b;clock.sample(state,550,1000);assert(state.eyeX==123);
    for(double aspect:{4.0/3,16.0/9,21.0/9}){
        double vertical=70,base=sourceBaseFov(float(vertical));
        double actualHorizontal=2*std::atan(std::tan(base*3.141592653589793/360)*aspect/(4.0/3));
        double recoveredVertical=2*std::atan(std::tan(actualHorizontal/2)/aspect)*180/3.141592653589793;
        assert(std::abs(recoveredVertical-vertical)<.0001);
    }
    std::cout<<"Motion interpolation, teleport reset and widescreen FOV checks passed.\n";
}
