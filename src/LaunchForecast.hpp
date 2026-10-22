#pragma once
// Include after ProjectileProbe.hpp. Read-only game-thread prediction for a
// launched projectile whose main collider is temporarily disabled by Launch.
// No scene query can see that disabled shape; only an explicit future-pose
// ComputePenetration positive can establish a prospective intersection.
namespace LaunchForecast {
static P fixedDeltaMethod=nullptr;
static P sphereCenterMethod=nullptr,sphereRadiusMethod=nullptr,boxCenterMethod=nullptr,boxSizeMethod=nullptr;
static bool ready=false;
constexpr float Horizon=.6f;
constexpr unsigned Samples=24;
static bool envelope(P collider,P body,V3&mass,float&radius){
 using namespace ProjectileProbe;
 P transform=call(mGetTransform,collider);V3 local={},world={},scale={};Quaternion rotation={};float rawRadius=0,height=0;
 if(!alive(transform)||!get(lossyScale,transform,nullptr,scale)||!finite(scale)||!get(transformRotation,transform,nullptr,rotation)||!worldCenterOfMass||!get(worldCenterOfMass,body,nullptr,mass)||!finite(mass))return false;
 float sx=std::fabs(scale.x),sy=std::fabs(scale.y),sz=std::fabs(scale.z),bound=0;
 if(sx<=0||sy<=0||sz<=0)return false;
 if(kind(collider,sphereClass)){
   if(!get(sphereCenterMethod,collider,nullptr,local)||!get(sphereRadiusMethod,collider,nullptr,rawRadius)||!std::isfinite(rawRadius)||rawRadius<=0)return false;
   bound=rawRadius*(sx+sy+sz);
 }else if(kind(collider,capsuleClass)){
   if(!get(capsuleCenter,collider,nullptr,local)||!get(capsuleRadius,collider,nullptr,rawRadius)||!get(capsuleHeight,collider,nullptr,height)||!std::isfinite(rawRadius)||rawRadius<=0||!std::isfinite(height)||height<=0)return false;
   bound=rawRadius*(sx+sy+sz)+height*.5f*std::max(sx,std::max(sy,sz));
 }else if(kind(collider,boxClass)){
   V3 size={};if(!get(boxCenterMethod,collider,nullptr,local)||!get(boxSizeMethod,collider,nullptr,size)||!finite(size)||size.x<=0||size.y<=0||size.z<=0)return false;
   bound=.5f*(size.x*sx+size.y*sy+size.z*sz);
 }else return false;
 P args[]={&local};
 if(!finite(local)||!get(transformPoint,transform,args,world)||!finite(world)||!physicsProjectilePose(collider,body,world,rotation))return false;
 V3 offset=sub(world,mass);radius=bound+std::sqrt(dot(offset,offset));
 return std::isfinite(radius)&&radius>0;
}
}
static bool initLaunchForecast(){
 using namespace LaunchForecast;
 fixedDeltaMethod=method(findClass("UnityEngine","Time"),"get_fixedDeltaTime",0);
 sphereCenterMethod=method(ProjectileProbe::sphereClass,"get_center",0);sphereRadiusMethod=method(ProjectileProbe::sphereClass,"get_radius",0);
 boxCenterMethod=method(ProjectileProbe::boxClass,"get_center",0);boxSizeMethod=method(ProjectileProbe::boxClass,"get_size",0);
 ready=ProjectileProbe::ready&&ProjectileProbe::at(fixedDeltaMethod,0x372B390)&&ProjectileProbe::at(ProjectileProbe::computePenetration,0x37E47B0)&&
   ProjectileProbe::at(sphereCenterMethod,0x37EE020)&&ProjectileProbe::at(sphereRadiusMethod,0x37EE100)&&ProjectileProbe::at(boxCenterMethod,0x37DE200)&&ProjectileProbe::at(boxSizeMethod,0x37DE2F0);
 return ready;
}
static ProjectileProbeResult probeLaunchForecast(P self,const ProjectileProbeResult&previous){
 using namespace ProjectileProbe;
 ProjectileProbeResult r;r.reason="launch_forecast_not_initialized";
 if(previous.known||!LaunchForecast::ready||!ProjectileProbe::ready)return previous;
 if(!alive(player)||!alive(self))return previous;
 if(!read<bool>(self,0x1A0)||!read<bool>(self,0x7A)||read<bool>(self,0x158)||read<bool>(self,0x15A)||read<bool>(self,0x17C)||agentContactIsNoop(self)){
   return previous;
 }
 P heroCollider=read<P>(player,0x1D8),projectileCollider=read<P>(self,0x148),heroRb=read<P>(player,0x190),projectileRb=read<P>(self,0x140);
 if(!alive(heroCollider)||!alive(projectileCollider)||!alive(heroRb)||!alive(projectileRb)){
   return previous;
 }
 bool heroEnabled=false,projectileEnabled=true;
 if(!get(colliderEnabled,heroCollider,nullptr,heroEnabled)||!get(colliderEnabled,projectileCollider,nullptr,projectileEnabled)||!heroEnabled||projectileEnabled){
   return previous;
 }
 if(!kind(projectileCollider,capsuleClass)&&!kind(projectileCollider,sphereClass)&&!kind(projectileCollider,boxClass)){
   r.reason="launch_forecast_shape_unsupported";return r;
 }
 // Shipping pose verification must use the matching authoritative Rigidbody,
 // never the legacy fixture-only render-pose fallback in ProjectileProbe.
 if(!rigidbodyPosition||!rigidbodyRotation||!colliderAttachedBody||call(colliderAttachedBody,projectileCollider)!=projectileRb){
   r.reason="launch_forecast_physics_pose_unavailable";return r;
 }
 Capsule hero;if(!capsuleSnapshot(heroCollider,hero)){r.reason="launch_forecast_hero_capsule_unknown";return r;}
 V3 heroAngular={},angular={};float damping=0,scale=0,fixed=0;bool kinematic=false,globalGravity=false;int frozen=0;
 if(!get(linearVelocity,heroRb,nullptr,r.heroVelocity)||!get(linearVelocity,projectileRb,nullptr,r.projectileVelocity)||!finite(r.heroVelocity)||!finite(r.projectileVelocity)||
    !get(angularVelocity,heroRb,nullptr,heroAngular)||!get(angularVelocity,projectileRb,nullptr,angular)||!finite(heroAngular)||!finite(angular)||
    !get(isKinematic,projectileRb,nullptr,kinematic)||!get(linearDamping,projectileRb,nullptr,damping)||!get(useGravity,projectileRb,nullptr,globalGravity)||!get(constraints,projectileRb,nullptr,frozen)||
    !get(mTimeScale,nullptr,nullptr,scale)||!get(LaunchForecast::fixedDeltaMethod,nullptr,nullptr,fixed)){
   r.reason="launch_forecast_motion_unknown";return r;
 }
 if(kinematic||!std::isfinite(damping)||damping<0||damping>.000001f||!std::isfinite(scale)||scale<=0||!std::isfinite(fixed)||fixed<=0){
   r.reason="launch_forecast_motion_unsupported";return r;
 }
 if(!axialCapsuleRotation(heroRb,hero,heroAngular)){r.reason="launch_forecast_hero_rotation_not_predictable";return r;}
 float acceleration=read<float>(self,0x150),timer=read<float>(self,0x154);
 if(!std::isfinite(acceleration)||!std::isfinite(timer)||timer<=0){r.reason="launch_forecast_enable_timer_unknown";return r;}
 if(globalGravity){V3 gravity={};if(!get(physicsGravity,nullptr,nullptr,gravity)||!finite(gravity)||std::fabs(gravity.x)>.000001f||std::fabs(gravity.z)>.000001f){r.reason="launch_forecast_global_gravity_unsupported";return r;}acceleration+=gravity.y;}
 if(!std::isfinite(acceleration)){r.reason="launch_forecast_acceleration_invalid";return r;}
 if(frozen&2)r.projectileVelocity.x=0;if(frozen&4){r.projectileVelocity.y=0;acceleration=0;}if(frozen&8)r.projectileVelocity.z=0;
 // FixedUpdate75E00E..75E048 decrements this game-clock timer and enables
 // the collider when it crosses zero. The unknown fixed-step phase costs at
 // most one quantum in either direction. Prove a positive only AFTER the
 // conservative upper enable bound, not while collision is still disabled.
 float earliest=std::max(0.f,timer-fixed),first=timer+fixed;
 if(!std::isfinite(first)||first>LaunchForecast::Horizon){r.reason="launch_forecast_enable_outside_horizon";return r;}
 // A loose COM-centred sphere covers the complete primitive throughout any
 // observed angular rotation. L1 scaled extents deliberately over-cover it.
 // This narrows samples and avoids Physics calls for far or receding objects;
 // the envelope itself never establishes a hit or a known miss.
 V3 mass={};float radius=0;
 if(!LaunchForecast::envelope(projectileCollider,projectileRb,mass,radius)){r.reason="launch_forecast_pose_envelope_unavailable";return r;}
 V3 offset=sub(hero.center,mass),relative=sub(r.heroVelocity,r.projectileVelocity);
 V3 extent={std::fabs(hero.axis.x)*hero.half+hero.radius+radius,std::fabs(hero.axis.y)*hero.half+hero.radius+radius,std::fabs(hero.axis.z)*hero.half+hero.radius+radius};
 float broadFirst=0,broadLast=LaunchForecast::Horizon;
 if(!slab(offset.x,relative.x,extent.x,broadFirst,broadLast)||!slab(offset.z,relative.z,extent.z,broadFirst,broadLast)){r.reason="launch_forecast_outside_pose_envelope";return r;}
 Window windows[5];int count=verticalWindows(offset.y,relative.y,-acceleration,extent.y,broadFirst,broadLast,windows);
 if(!count){r.reason="launch_forecast_outside_pose_envelope";return r;}
 // Keep the lower bound conservative even if a short earlier interval occurs
 // during enable-phase uncertainty and only a later interval is sampled.
 earliest=std::max(earliest,windows[0].first);
 P heroTransform=call(mGetTransform,heroCollider),projectileTransform=call(mGetTransform,projectileCollider);
 if(!alive(heroTransform)||!alive(projectileTransform)){r.reason="launch_forecast_transform_absent";return r;}
 for(int w=0;w<count;w++){
   float start=std::max(first,windows[w].first),last=windows[w].last;if(start>last)continue;
   unsigned samples=std::max(1u,(LaunchForecast::Samples-r.queries)/(count-w));if(start==last)samples=1;
   float step=samples>1?(last-start)/(samples-1):0;
   for(unsigned i=0;i<samples;i++){
     float t=i+1==samples?last:start+step*i;
     int hit=verify(r,heroCollider,projectileCollider,heroTransform,projectileTransform,acceleration,t,angular,projectileRb);
     if(hit<0)return r;
     if(hit){
       r.known=r.intersects=true;r.gameSeconds=earliest;r.etaMilliseconds=earliest*1000/scale;r.etaLatestMilliseconds=t*1000/scale;
       r.reason="launch_forecast_enabled_future_pose_intersection";return r;
     }
   }
 }
 // Sampling can miss a fast crossing. Neither negative samples nor a future
 // enable timer outside the bounded horizon ever establish a geometric miss.
 r.reason="launch_forecast_no_confirmed_enabled_sample";return r;
}
