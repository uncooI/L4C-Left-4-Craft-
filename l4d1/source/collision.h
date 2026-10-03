#pragma once
#include "engine/IEngineTrace.h"
#include "mathlib/vector4d.h"
#include "integration.h"
namespace left4craft {
// Export clipped BSP brush faces. Displacements and moving props are not covered yet.
class Collision {
 struct V {float x,y,z;};
 static V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
 static V mul(V a,float s){return {a.x*s,a.y*s,a.z*s};}
 static float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
 static V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
 static V normal(V a){return mul(a,1.f/std::sqrt(dot(a,a)));}
 int brushes_[65536]{};Vector4D planes_[128]{};
 struct Packet {p::ColRegion region;p::ColTri triangles[60000];} packet_{};
 int center_[3]{},regionIndex_{27},brushIndex_{},brushCount_{};bool dirty_{true},pending_{};
 CUtlVector<int> brushesVector_{brushes_,65536};CUtlVector<Vector4D> planesVector_{planes_,128};
 void brushFaces(int count,unsigned flags) {
  for(int face=0;face<count;++face){
   V n{planes_[face].x,planes_[face].y,planes_[face].z};float n2=dot(n,n);if(n2<0.5f)continue;
   V center=mul(n,planes_[face].w/n2),u=normal(cross(n,std::fabs(n.z)<0.9f?V{0,0,1}:V{0,1,0})),v=cross(n,u);
   V polygon[128],next[128];int size=4;
   polygon[0]=add(center,add(mul(u,-65536),mul(v,-65536)));polygon[1]=add(center,add(mul(u,65536),mul(v,-65536)));
   polygon[2]=add(center,add(mul(u,65536),mul(v,65536)));polygon[3]=add(center,add(mul(u,-65536),mul(v,65536)));
   for(int clip=0;clip<count&&size>=3;++clip){if(clip==face)continue;V cn{planes_[clip].x,planes_[clip].y,planes_[clip].z};float cd=planes_[clip].w;int out=0;
    for(int i=0;i<size;++i){auto a=polygon[i],b=polygon[(i+1)%size];float da=dot(cn,a)-cd,db=dot(cn,b)-cd;
     if(da<=0.02f&&out<127)next[out++]=a;if((da<=0)!=(db<=0)&&out<127)next[out++]=add(a,mul(add(b,mul(a,-1)),da/(da-db)));
    }size=out;std::memcpy(polygon,next,size*sizeof(V));
   }
   for(int i=1;i+1<size&&packet_.region.count<60000;++i){auto& t=packet_.triangles[packet_.region.count++];V points[]={polygon[0],polygon[i],polygon[i+1]};for(int k=0;k<3;++k){auto mc=sourceToMc({points[k].x,points[k].y,points[k].z});t.v[k*3]=float(mc.x);t.v[k*3+1]=float(mc.y);t.v[k*3+2]=float(mc.z);}t.flags=flags;}
  }
 }
public:
 bool ready{};
 void reset(){renderClearCollision();dirty_=true;ready=false;pending_=false;regionIndex_=27;}
 void step(IEngineTrace* trace,Link& link,Point position,unsigned epoch){
  int c[]={int(std::floor(position.x/8)),int(std::floor(position.y/8)),int(std::floor(position.z/8))};
  if(regionIndex_>=27&&(dirty_||c[0]!=center_[0]||c[1]!=center_[1]||c[2]!=center_[2])){for(int i=0;i<3;++i)center_[i]=c[i];regionIndex_=0;brushIndex_=0;brushCount_=0;dirty_=false;}
  if(regionIndex_>=27)return;
  if(pending_){auto bytes=sizeof(p::ColRegion)+packet_.region.count*sizeof(p::ColTri);if(!link.collision(p::kColTris,&packet_,unsigned(bytes)))return;
   auto region=packet_.region;region.count=0;if(!link.collision(p::kColRegion,&region,sizeof(region)))return;renderCollisionRegion(packet_.region,packet_.triangles);pending_=false;++regionIndex_;brushCount_=brushIndex_=0;if(regionIndex_>=27)ready=true;return;}
  if(!brushCount_&&!brushIndex_){
   int x=(center_[0]+regionIndex_%3-1)*8,z=(center_[2]+(regionIndex_/3)%3-1)*8,y=(center_[1]+regionIndex_/9-1)*8;
   packet_.region={x,y,z,x+7,y+7,z+7,epoch,0};
   auto a=mcToSource({double(x),double(y),double(z+8)}),b=mcToSource({double(x+8),double(y+8),double(z)});
   brushesVector_.RemoveAll();trace->GetBrushesInAABB(Vector(float(a.x),float(a.y),float(a.z)),Vector(float(b.x),float(b.y),float(b.z)),&brushesVector_,MASK_PLAYERSOLID);brushCount_=brushesVector_.Count();
  }
  for(int work=0;work<12&&brushIndex_<brushCount_;++work,++brushIndex_){
   planesVector_.RemoveAll();int contents=0;if(!trace->GetBrushInfo(brushes_[brushIndex_],&planesVector_,&contents)||(contents&MASK_PLAYERSOLID)==0)continue;
   int count=planesVector_.Count();if(count>110)continue;
   auto& r=packet_.region;auto a=mcToSource({double(r.minX),double(r.minY),double(r.maxZ+1)}),b=mcToSource({double(r.maxX+1),double(r.maxY+1),double(r.minZ)});
   planes_[count++].Init(1,0,0,float(b.x));planes_[count++].Init(-1,0,0,float(-a.x));planes_[count++].Init(0,1,0,float(b.y));planes_[count++].Init(0,-1,0,float(-a.y));planes_[count++].Init(0,0,1,float(b.z));planes_[count++].Init(0,0,-1,float(-a.z));brushFaces(count,(contents&CONTENTS_SOLID)?0:collisionNoVisual);
  }
  if(brushIndex_>=brushCount_)pending_=true;
 }
};
}
