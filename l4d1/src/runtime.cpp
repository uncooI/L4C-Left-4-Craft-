// Only needed by the Linux cross build (MSVC ABI + MinGW CRT).
// Visual Studio builds use Microsoft's own runtime.
#if defined(LEFT4CRAFT_CROSS_RUNTIME)
#include <stdint.h>
extern "C" uint64_t left4craft_remainder(uint32_t lo,uint32_t hi,uint32_t divisorLo,uint32_t divisorHi){
 uint32_t rlo=0,rhi=0;
 for(int bit=63;bit>=0;--bit){uint32_t carry=rhi>>31;rhi=(rhi<<1)|(rlo>>31);rlo=(rlo<<1)|((bit>=32?hi>>(bit-32):lo>>bit)&1);
 if(carry||rhi>divisorHi||(rhi==divisorHi&&rlo>=divisorLo)){uint32_t old=rlo;rlo-=divisorLo;rhi-=divisorHi+(old<divisorLo);}}
 return (uint64_t(rhi)<<32)|rlo;
}
extern "C" __declspec(naked) void _aullrem(){__asm{
 push dword ptr [esp+16]
 push dword ptr [esp+16]
 push dword ptr [esp+16]
 push dword ptr [esp+16]
 call left4craft_remainder
 add esp,16
 ret 16
}}
extern "C" int _fltused=0;
#endif
