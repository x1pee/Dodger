#pragma once
// Read-only, game-thread-only current-build projectile prediction. Unknown never
// means a miss and must never authorize an early defensive action. This predicts
// constant current actor velocity plus the projectile's existing acceleration;
// later input, collisions, homing and animation changes remain engine events.
struct ProjectileProbeResult {
 bool known=false,intersects=false;
 float etaMilliseconds=-1,etaLatestMilliseconds=-1,gameSeconds=-1;
 const char*reason="projectile_probe_not_initialized";
 unsigned queries=0;
 V3 heroVelocity={},projectileVelocity={};
};
namespace ProjectileProbe {
struct Bounds {V3 center,extents;};struct Quaternion {float x,y,z,w;};
struct Capsule {V3 center,axis,a,b;float radius=0,half=0;};
struct Window {float first,last;};
static P capsuleClass,sphereClass,boxClass;
static P capsuleCenter,capsuleRadius,capsuleHeight,capsuleDirection;
static P colliderBounds,colliderEnabled,componentGameObject,gameObjectLayer,instanceId;
static P lossyScale,transformPoint,transformDirection,transformRotation;
static P linearVelocity,angularVelocity,linearDamping,useGravity,isKinematic,constraints,physicsGravity,worldCenterOfMass;
static P rigidbodyPosition,rigidbodyRotation,colliderAttachedBody;
static P sphereCast,overlapCapsule,computePenetration;
static uintptr_t hitHandle=0,colliderHandle=0;
static bool ready=false;
constexpr unsigned Capacity=64,QueryBudget=24;
constexpr float Horizon=12.f,MaxSegment=.04f;
static bool at(P m,uintptr_t rva){return m&&*(P*)m==(char*)game+rva;}
// ProjectileSpawnTrap intentionally ignores agent triggers. The shared no-op
// implementation is verified by RVA; a visible flying trap is not a body hit.
static bool agentContactIsNoop(P self){return self&&at(method(il2cpp_object_get_class(self),"HandleTriggerWithAgent",1),0x6005A0);}
static bool kind(P o,P k){return o&&k&&il2cpp_class_is_assignable_from(k,il2cpp_object_get_class(o));}
static V3 add(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static V3 sub(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static V3 mul(V3 a,float n){return {a.x*n,a.y*n,a.z*n};}
static float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static bool finite(V3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
static bool finite(Quaternion q){return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w);}
template<class T>static bool get(P m,P o,P*args,T&v){P box=call(m,o,args);if(!box)return false;v=*(T*)il2cpp_object_unbox(box);return true;}
static bool unit(V3&v){float n=std::sqrt(dot(v,v));if(!finite(v)||!std::isfinite(n)||n<.000001f)return false;v=mul(v,1/n);return true;}
static bool valid(Bounds b){return finite(b.center)&&finite(b.extents)&&b.extents.x>=0&&b.extents.y>=0&&b.extents.z>=0;}
static bool nearlyEqual(float a,float b){return std::fabs(a-b)<=.003f+.003f*std::max(std::fabs(a),std::fabs(b));}
static bool capsuleSnapshot(P collider,Capsule&c){
 if(!kind(collider,capsuleClass))return false;
 P t=call(mGetTransform,collider);if(!alive(t))return false;
 V3 local={},scale={};float radius=0,height=0;int direction=-1;
 if(!get(capsuleCenter,collider,nullptr,local)||!get(capsuleRadius,collider,nullptr,radius)||!get(capsuleHeight,collider,nullptr,height)||
    !get(capsuleDirection,collider,nullptr,direction)||!get(lossyScale,t,nullptr,scale)||!finite(local)||!finite(scale)||
    !std::isfinite(radius)||!std::isfinite(height)||radius<=0||height<=0||direction<0||direction>2)return false;
 float s[3]={std::fabs(scale.x),std::fabs(scale.y),std::fabs(scale.z)};
 if(s[0]<.000001f||s[1]<.000001f||s[2]<.000001f)return false;
 V3 axis=direction==0?V3{1,0,0}:direction==1?V3{0,1,0}:V3{0,0,1};P pa[]={&axis},pc[]={&local};
 if(!get(transformPoint,t,pc,c.center)||!get(transformDirection,t,pa,c.axis)||!finite(c.center)||!unit(c.axis))return false;
 c.radius=radius*std::max(s[(direction+1)%3],s[(direction+2)%3]);c.half=std::max(0.f,height*s[direction]*.5f-c.radius);
 c.a=add(c.center,mul(c.axis,-c.half));c.b=add(c.center,mul(c.axis,c.half));
 Bounds actual={};if(!get(colliderBounds,collider,nullptr,actual)||!valid(actual))return false;
 V3 e={std::fabs(c.axis.x)*c.half+c.radius,std::fabs(c.axis.y)*c.half+c.radius,std::fabs(c.axis.z)*c.half+c.radius};
 // Reject nonuniform/sheared transforms whose actual Physics capsule disagrees.
 if(!nearlyEqual(e.x,actual.extents.x)||!nearlyEqual(e.y,actual.extents.y)||!nearlyEqual(e.z,actual.extents.z))return false;
 // Interpolated render transforms may lag the actual Physics capsule. Its
 // bounds supply the authoritative center once the shape extents agree.
 c.center=actual.center;c.a=add(c.center,mul(c.axis,-c.half));c.b=add(c.center,mul(c.axis,c.half));return true;
}
static bool slab(float offset,float speed,float extent,float&first,float&last){
 if(std::fabs(speed)<.000001f)return std::fabs(offset)<=extent;
 float a=(-extent-offset)/speed,b=(extent-offset)/speed;if(a>b)std::swap(a,b);
 first=std::max(first,a);last=std::min(last,b);return first<=last;
}
static float ordinate(float offset,float velocity,float acceleration,float t){return offset+velocity*t+.5f*acceleration*t*t;}
static int verticalWindows(float offset,float velocity,float acceleration,float extent,float first,float last,Window(&out)[5]){
 float cuts[6]={first,last};int count=2;
 for(float boundary:{-extent,extent}){
   if(std::fabs(acceleration)<.000001f){if(std::fabs(velocity)>.000001f){float t=(boundary-offset)/velocity;if(t>first&&t<last)cuts[count++]=t;}}
   else{float discriminant=velocity*velocity-2*acceleration*(offset-boundary);if(discriminant>=0){float root=std::sqrt(discriminant);float a=(-velocity-root)/acceleration,b=(-velocity+root)/acceleration;if(a>first&&a<last)cuts[count++]=a;if(b>first&&b<last)cuts[count++]=b;}}
 }
 std::sort(cuts,cuts+count);int windows=0;
 for(int i=1;i<count;i++){float a=cuts[i-1],b=cuts[i];if(b-a<.000001f)continue;if(std::fabs(ordinate(offset,velocity,acceleration,(a+b)*.5f))<=extent+.000001f){
   if(windows&&nearlyEqual(out[windows-1].last,a))out[windows-1].last=b;else out[windows++]={a,b};}}
 return windows;
}
static bool axialCapsuleRotation(P body,const Capsule&c,V3 angular){
 if(finite(angular)&&dot(angular,angular)<=.00000001f)return true;
 V3 mass={};if(!finite(angular)||!worldCenterOfMass||!get(worldCenterOfMass,body,nullptr,mass)||!finite(mass))return false;
 V3 tilt=cross(angular,c.axis),orbit=cross(angular,sub(c.center,mass));
 return dot(tilt,tilt)<=.00000001f&&dot(orbit,orbit)<=.00000001f;
}
static bool rotationInvariant(P collider,P body,V3 angular){
 if(!finite(angular))return false;if(dot(angular,angular)<=.00000001f)return true;
 if(kind(collider,capsuleClass)){Capsule c;return capsuleSnapshot(collider,c)&&axialCapsuleRotation(body,c,angular);}
 if(!kind(collider,sphereClass))return false;
 // A sphere may spin freely, but an off-axis collider centre may orbit its
 // Rigidbody COM. Require the actual world centre to remain stationary there.
 Bounds b={};V3 mass={};if(!get(colliderBounds,collider,nullptr,b)||!valid(b)||!nearlyEqual(b.extents.x,b.extents.y)||!nearlyEqual(b.extents.y,b.extents.z)||!worldCenterOfMass||!get(worldCenterOfMass,body,nullptr,mass)||!finite(mass))return false;
 V3 orbit=cross(angular,sub(b.center,mass));return dot(orbit,orbit)<=.00000001f;
}
static bool queryAvailable(ProjectileProbeResult&r){if(r.queries>=QueryBudget){r.reason="projectile_query_budget_exceeded";return false;}++r.queries;return true;}
static int overlap(ProjectileProbeResult&r,V3 a,V3 b,float radius,int layer,int target){
 if(!queryAvailable(r))return -1;P array=il2cpp_gchandle_get_target(colliderHandle);if(!array){r.reason="projectile_overlap_buffer_missing";return -1;}
 int collide=2,count=-1;P args[]={&a,&b,&radius,array,&layer,&collide};
 if(!get(overlapCapsule,nullptr,args,count)||count<0||count>(int)Capacity){r.reason="projectile_overlap_query_failed";return -1;}
 if(count==(int)Capacity){r.reason="projectile_overlap_buffer_full";return -1;}
 for(int i=0;i<count;i++){P c=read<P>(array,0x20+i*sizeof(P));if(!alive(c))continue;int id=0;if(!get(instanceId,c,nullptr,id)){r.reason="projectile_overlap_identity_unknown";return -1;}if(id==target)return 1;}
 return 0;
}
static int sweep(ProjectileProbeResult&r,V3 origin,float radius,V3 direction,float length,int layer,int target,float&distance){
 if(!queryAvailable(r))return -1;P array=il2cpp_gchandle_get_target(hitHandle);if(!array){r.reason="projectile_hit_buffer_missing";return -1;}
 int collide=2,count=-1;P args[]={&origin,&radius,&direction,array,&length,&layer,&collide};
 if(!get(sphereCast,nullptr,args,count)||count<0||count>(int)Capacity){r.reason="projectile_sweep_query_failed";return -1;}
 if(count==(int)Capacity){r.reason="projectile_sweep_buffer_full";return -1;}
 bool found=false;distance=length;
 // Unity 6000.3 RaycastHit: value stride0x2C, m_Distance+1C, EntityId+28.
 for(int i=0;i<count;i++)if(read<int>(array,0x20+i*0x2C+0x28)==target){float d=read<float>(array,0x20+i*0x2C+0x1C);if(!std::isfinite(d)||d<0||d>length+.001f){r.reason="projectile_sweep_distance_invalid";return -1;}distance=std::min(distance,d);found=true;}
 return found?1:0;
}
static V3 displacement(V3 velocity,float accelerationY,float t){V3 d=mul(velocity,t);d.y+=.5f*accelerationY*t*t;return d;}
static Quaternion compose(Quaternion a,Quaternion b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
static V3 rotate(Quaternion q,V3 v){V3 u={q.x,q.y,q.z};return add(v,mul(cross(u,add(cross(u,v),mul(v,q.w))),2));}
static bool unitQuaternion(Quaternion&q){float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;if(!finite(q)||!std::isfinite(n)||std::fabs(n-1.f)>.003f)return false;float s=1/std::sqrt(n);q={q.x*s,q.y*s,q.z*s,q.w*s};return true;}
static bool physicsProjectilePose(P collider,P body,V3&position,Quaternion&rotation){
 // Legacy isolated fixtures do not initialize the optional probe APIs. Shipping
 // initialization requires all three, so its collider pose always uses Physics.
 if(!rigidbodyPosition&&!rigidbodyRotation&&!colliderAttachedBody)return true;
 if(!rigidbodyPosition||!rigidbodyRotation||!colliderAttachedBody||!alive(body)||call(colliderAttachedBody,collider)!=body)return false;
 P transform=call(mGetTransform,body);V3 renderedBody={},physicsBody={};Quaternion renderedRotation={},physicsRotation={};
 if(!alive(transform)||!get(mTransformPosition,transform,nullptr,renderedBody)||!get(transformRotation,transform,nullptr,renderedRotation)||
    !get(rigidbodyPosition,body,nullptr,physicsBody)||!get(rigidbodyRotation,body,nullptr,physicsRotation)||!finite(renderedBody)||!finite(physicsBody)||!finite(position)||
    !unitQuaternion(renderedRotation)||!unitQuaternion(physicsRotation)||!unitQuaternion(rotation))return false;
 Quaternion inverse={-renderedRotation.x,-renderedRotation.y,-renderedRotation.z,renderedRotation.w},correction=compose(physicsRotation,inverse);
 position=add(physicsBody,rotate(correction,sub(position,renderedBody)));rotation=compose(correction,rotation);
 return finite(position)&&unitQuaternion(rotation);
}
static bool angularStep(V3 angular,float t,Quaternion&step){
 float speed=std::sqrt(dot(angular,angular));if(!finite(angular)||!std::isfinite(speed)||!std::isfinite(t)||t<0)return false;
 if(speed<.000001f){step={0,0,0,1};return true;}float half=.5f*speed*t,scale=std::sin(half)/speed;step={angular.x*scale,angular.y*scale,angular.z*scale,std::cos(half)};return finite(step);
}
static int verify(ProjectileProbeResult&r,P heroCollider,P projectileCollider,P heroTransform,P projectileTransform,float accelerationY,float t,V3 angular={},P body=nullptr){
 if(!queryAvailable(r))return -1;V3 heroPosition={},projectilePosition={},normal={};Quaternion heroRotation={},projectileRotation={};float distance=0;
 if(!get(mTransformPosition,heroTransform,nullptr,heroPosition)||!get(mTransformPosition,projectileTransform,nullptr,projectilePosition)||
    !get(transformRotation,heroTransform,nullptr,heroRotation)||!get(transformRotation,projectileTransform,nullptr,projectileRotation)||!finite(heroRotation)||!finite(projectileRotation)){r.reason="projectile_pose_verification_unavailable";return -1;}
 if(!physicsProjectilePose(projectileCollider,body,projectilePosition,projectileRotation)){r.reason="projectile_physics_body_pose_unavailable";return -1;}
 V3 local={},renderCenter={};Bounds actual={};P centerArgs[]={&local};
 if(!get(capsuleCenter,heroCollider,nullptr,local)||!get(transformPoint,heroTransform,centerArgs,renderCenter)||!get(colliderBounds,heroCollider,nullptr,actual)||!valid(actual)){r.reason="projectile_physics_pose_alignment_unknown";return -1;}
 heroPosition=add(heroPosition,sub(actual.center,renderCenter));
 heroPosition=add(heroPosition,mul(r.heroVelocity,t));
 if(dot(angular,angular)>.00000001f){
   V3 mass={};Quaternion step={};if(!body||!worldCenterOfMass||!get(worldCenterOfMass,body,nullptr,mass)||!finite(mass)||!angularStep(angular,t,step)){r.reason="projectile_rotation_pose_unknown";return -1;}
   projectilePosition=add(mass,rotate(step,sub(projectilePosition,mass)));projectileRotation=compose(step,projectileRotation);
 }
 projectilePosition=add(projectilePosition,displacement(r.projectileVelocity,accelerationY,t));
 P args[]={heroCollider,&heroPosition,&heroRotation,projectileCollider,&projectilePosition,&projectileRotation,&normal,&distance};bool penetrates=false;
 if(!get(computePenetration,nullptr,args,penetrates)||!std::isfinite(distance)){r.reason="projectile_pose_verification_failed";return -1;}
 return penetrates?1:0;
}
}
static bool initProjectileProbe(P,P){
 using namespace ProjectileProbe;if(ready)return true;
 P physics=findClass("UnityEngine","Physics"),collider=findClass("UnityEngine","Collider"),transform=findClass("UnityEngine","Transform"),rb=findClass("UnityEngine","Rigidbody");
 capsuleClass=findClass("UnityEngine","CapsuleCollider");sphereClass=findClass("UnityEngine","SphereCollider");boxClass=findClass("UnityEngine","BoxCollider");
 struct Api{P*target;P klass;const char*name;int argc;uintptr_t rva;};
 Api apis[]={
 {&capsuleCenter,capsuleClass,"get_center",0,0x37DE5A0},{&capsuleRadius,capsuleClass,"get_radius",0,0x37DE800},{&capsuleHeight,capsuleClass,"get_height",0,0x37DE740},{&capsuleDirection,capsuleClass,"get_direction",0,0x37DE680},
 {&colliderBounds,collider,"get_bounds",0,0x37DF530},{&colliderEnabled,collider,"get_enabled",0,0x37DF610},
 {&componentGameObject,findClass("UnityEngine","Component"),"get_gameObject",0,0x36FDF30},{&gameObjectLayer,findClass("UnityEngine","GameObject"),"get_layer",0,0x3704B90},{&instanceId,findClass("UnityEngine","Object"),"GetInstanceID",0,0x370B0E0},
 {&lossyScale,transform,"get_lossyScale",0,0x3733CA0},{&transformPoint,transform,"TransformPoint",1,0x3731F60},{&transformDirection,transform,"TransformDirection",1,0x37318C0},{&transformRotation,transform,"get_rotation",0,0x3733F90},
 {&linearVelocity,rb,"get_linearVelocity",0,0x37EBC70},{&angularVelocity,rb,"get_angularVelocity",0,0x37EB080},{&linearDamping,rb,"get_linearDamping",0,0x37EB5D0},{&useGravity,rb,"get_useGravity",0,0x37EC460},{&isKinematic,rb,"get_isKinematic",0,0x37EBB60},{&constraints,rb,"get_constraints",0,0x37EB490},
 {&worldCenterOfMass,rb,"get_worldCenterOfMass",0,0x37EC5E0},{&rigidbodyPosition,rb,"get_position",0,0x37EC060},{&rigidbodyRotation,rb,"get_rotation",0,0x37EC150},{&colliderAttachedBody,collider,"get_attachedRigidbody",0,0x37DF440},
 {&physicsGravity,physics,"get_gravity",0,0x37E84C0},{&sphereCast,physics,"SphereCastNonAlloc",7,0x37E7BC0},{&overlapCapsule,physics,"OverlapCapsuleNonAlloc",6,0x37E5460},{&computePenetration,physics,"ComputePenetration",8,0x37E47B0}};
 P hitClass=findClass("UnityEngine","RaycastHit");
 if(!capsuleClass||!sphereClass||!boxClass||!hitClass||!il2cpp_array_new||!mGetTransform||!mTransformPosition||!mTimeScale)return false;
 for(auto&a:apis){*a.target=method(a.klass,a.name,a.argc);if(!at(*a.target,a.rva))return false;}
 if(!hitHandle){P array=il2cpp_array_new(hitClass,Capacity);if(array)hitHandle=il2cpp_gchandle_new(array,true);}
 if(!colliderHandle){P array=il2cpp_array_new(collider,Capacity);if(array)colliderHandle=il2cpp_gchandle_new(array,true);}
 ready=hitHandle&&colliderHandle;return ready;
}
static ProjectileProbeResult probeProjectile(P self){
 using namespace ProjectileProbe;ProjectileProbeResult r;if(!ready)return r;
 if(!alive(player)||!alive(self)){r.reason="projectile_actor_absent";return r;}
 if(agentContactIsNoop(self)){r.known=true;r.reason="projectile_agent_contact_is_noop";return r;}
 P heroCollider=read<P>(player,0x1D8),projectileCollider=read<P>(self,0x148),heroRb=read<P>(player,0x190),projectileRb=read<P>(self,0x140);
 if(!alive(heroCollider)||!alive(projectileCollider)||!alive(heroRb)||!alive(projectileRb)){r.reason="projectile_physics_objects_absent";return r;}
 if(!kind(projectileCollider,capsuleClass)&&!kind(projectileCollider,sphereClass)&&!kind(projectileCollider,boxClass)){r.reason="projectile_collider_shape_unsupported";return r;}
 bool heroEnabled=false,projectileEnabled=false;
 if(!get(colliderEnabled,heroCollider,nullptr,heroEnabled)||!get(colliderEnabled,projectileCollider,nullptr,projectileEnabled)||!heroEnabled||!projectileEnabled){r.reason="projectile_collider_inactive_or_unknown";return r;}
 Capsule c;if(!capsuleSnapshot(heroCollider,c)){r.reason="projectile_hero_capsule_geometry_unknown";return r;}
 Bounds bounds={};if(!get(colliderBounds,projectileCollider,nullptr,bounds)||!valid(bounds)){r.reason="projectile_bounds_unknown";return r;}
 V3 heroAngular={},projectileAngular={};bool kinematic=false,globalGravity=false;float damping=0;int frozen=0;
 if(!get(linearVelocity,heroRb,nullptr,r.heroVelocity)||!get(linearVelocity,projectileRb,nullptr,r.projectileVelocity)||!finite(r.heroVelocity)||!finite(r.projectileVelocity)||
    !get(angularVelocity,heroRb,nullptr,heroAngular)||!get(angularVelocity,projectileRb,nullptr,projectileAngular)||
    !get(isKinematic,projectileRb,nullptr,kinematic)||!get(linearDamping,projectileRb,nullptr,damping)||!get(useGravity,projectileRb,nullptr,globalGravity)||!get(constraints,projectileRb,nullptr,frozen)){
   r.reason="projectile_motion_state_unknown";return r;}
 if(kinematic||!std::isfinite(damping)||damping>.000001f){r.reason="projectile_motion_model_unsupported";return r;}
 if(!axialCapsuleRotation(heroRb,c,heroAngular)){r.reason="projectile_hero_rotation_not_predictable";return r;}
 bool rotating=!rotationInvariant(projectileCollider,projectileRb,projectileAngular);
 float acceleration=read<float>(self,0x150);if(!std::isfinite(acceleration)){r.reason="projectile_acceleration_invalid";return r;}
 if(globalGravity){V3 g={};if(!get(physicsGravity,nullptr,nullptr,g)||!finite(g)||std::fabs(g.x)>.000001f||std::fabs(g.z)>.000001f){r.reason="projectile_global_gravity_unsupported";return r;}acceleration+=g.y;}
 // FixedUpdate75DFC5..75E009 applies (0,raw gravity,0), ForceMode.Acceleration5.
 // Raw gravity is signed. Reverse the projectile displacement, not its sign twice.
 if(frozen&2)r.projectileVelocity.x=0;if(frozen&4){r.projectileVelocity.y=0;acceleration=0;}if(frozen&8)r.projectileVelocity.z=0;
 // A tumbling spear has no fixed translated shape. Check future real collider
 // poses about its Rigidbody COM, with the observed world angular velocity.
 // The enclosing COM sphere only narrows the query interval; never proves a hit.
 if(rotating){
   V3 mass={};float scale=0;if(!finite(projectileAngular)||!worldCenterOfMass||!get(worldCenterOfMass,projectileRb,nullptr,mass)||!finite(mass)||!get(mTimeScale,nullptr,nullptr,scale)||!std::isfinite(scale)||scale<=0){r.reason="projectile_rotation_pose_unknown";return r;}
   float orbit=std::sqrt(dot(sub(bounds.center,mass),sub(bounds.center,mass)))+std::sqrt(dot(bounds.extents,bounds.extents));
   V3 rel=sub(r.heroVelocity,r.projectileVelocity),off=sub(c.center,mass);float first=0,last=.6f;
   V3 ext={std::fabs(c.axis.x)*c.half+c.radius+orbit,std::fabs(c.axis.y)*c.half+c.radius+orbit,std::fabs(c.axis.z)*c.half+c.radius+orbit};
   if(!std::isfinite(orbit)||!slab(off.x,rel.x,ext.x,first,last)||!slab(off.z,rel.z,ext.z,first,last)){r.reason="rotating_projectile_outside_short_forecast";return r;}
   Window windows[5];int count=verticalWindows(off.y,rel.y,-acceleration,ext.y,first,last,windows);
   P ht=call(mGetTransform,heroCollider),pt=call(mGetTransform,projectileCollider);if(!alive(ht)||!alive(pt))return r;
   for(int w=0;w<count;w++)for(float t=windows[w].first;t<=windows[w].last+.000001f;t+=.01f){
     if(r.queries>=QueryBudget){r.reason="rotating_projectile_query_budget_exceeded";return r;}
     int hit=verify(r,heroCollider,projectileCollider,ht,pt,acceleration,t,projectileAngular,projectileRb);if(hit<0)return r;
     if(hit){r.known=r.intersects=true;r.gameSeconds=std::max(0.f,t-.01f);r.etaMilliseconds=r.gameSeconds*1000/scale;r.etaLatestMilliseconds=t*1000/scale;r.reason="rotating_projectile_real_pose_intersection";return r;}
   }
   // Sampling can miss a very brief crossing: absence of a sample is unknown.
   r.reason="rotating_projectile_no_confirmed_sample";return r;
 }
 V3 relative=sub(r.heroVelocity,r.projectileVelocity),offset=sub(c.center,bounds.center);
 V3 extent={std::fabs(c.axis.x)*c.half+c.radius+bounds.extents.x,std::fabs(c.axis.y)*c.half+c.radius+bounds.extents.y,std::fabs(c.axis.z)*c.half+c.radius+bounds.extents.z};
 float first=0,last=Horizon;
 if(!slab(offset.x,relative.x,extent.x,first,last)||!slab(offset.z,relative.z,extent.z,first,last)){
   r.known=true;r.reason="projectile_actual_bounds_miss";return r;}
 Window windows[5];int windowCount=verticalWindows(offset.y,relative.y,-acceleration,extent.y,first,last,windows);
 if(!windowCount){r.known=true;r.reason="projectile_actual_height_miss";return r;}
 P gameObject=call(componentGameObject,projectileCollider);int layer=-1,targetId=0;float scale=0;
 if(!alive(gameObject)||!get(gameObjectLayer,gameObject,nullptr,layer)||layer<0||layer>31||!get(instanceId,projectileCollider,nullptr,targetId)||!targetId||!get(mTimeScale,nullptr,nullptr,scale)||!std::isfinite(scale)||scale<=0){r.reason="projectile_query_layer_or_time_unknown";return r;}
 int mask=(int)(1u<<layer);P heroTransform=call(mGetTransform,heroCollider),projectileTransform=call(mGetTransform,projectileCollider);
 if(!alive(heroTransform)||!alive(projectileTransform)){r.reason="projectile_collider_transform_absent";return r;}
 auto reverse=[&](float t){V3 d=mul(relative,t);d.y-=.5f*acceleration*t*t;return d;};
 auto confirmedPose=[&](float first,float sample)->bool{
   int check=verify(r,heroCollider,projectileCollider,heroTransform,projectileTransform,acceleration,sample,{},projectileRb);
   if(check!=1){if(check==0)r.reason="projectile_future_pose_not_confirmed";return false;}
   r.known=true;r.intersects=true;r.gameSeconds=first;r.etaMilliseconds=first*1000/scale;r.etaLatestMilliseconds=sample*1000/scale;r.reason="projectile_actual_collider_crossing";return true;
 };
 auto sampledFuture=[&](float first,float last)->bool{
   // An enclosing cast that starts overlapped cannot establish a miss. Look
   // for positive real-shape evidence within the same broad-phase window. A
   // negative sample never excludes the unsampled time between two poses.
   for(float sample=first;;sample=std::min(sample+.01f,last)){
     if(r.queries>=QueryBudget){r.reason="projectile_query_budget_exceeded";return false;}
     if(confirmedPose(first,sample)){r.reason="projectile_sampled_actual_collider_crossing";return true;}
     if(std::strcmp(r.reason,"projectile_future_pose_not_confirmed")!=0)return false;
     if(sample>=last-.000001f)break;
   }
   r.reason="projectile_no_confirmed_future_sample";return false;
 };
 for(int w=0;w<windowCount;w++)for(float a=windows[w].first;a<windows[w].last-.000001f;){
   float b=std::min(a+MaxSegment,windows[w].last);V3 da=reverse(a),db=reverse(b),travel=sub(db,da);float length=std::sqrt(dot(travel,travel));
   int initial=overlap(r,add(c.a,da),add(c.b,da),c.radius,mask,targetId);if(initial<0)return r;if(initial){confirmedPose(a,a);return r;}
   if(length<.000001f){if(std::fabs(acceleration)>.000001f){r.reason="projectile_curved_stationary_chord";return r;}a=b;continue;}
   V3 direction=mul(travel,1/length);float error=std::fabs(acceleration)*(b-a)*(b-a)/8;
   float inner=c.radius-error;if(inner<=.00001f){r.reason="projectile_curvature_exceeds_capsule";return r;}
   V3 centers[3]={c.a,c.center,c.b};float closest=length;bool innerHit=false;
   for(V3 center:centers){float distance=0;int hit=sweep(r,add(center,da),inner,direction,length,mask,targetId,distance);if(hit<0)return r;if(hit){closest=std::min(closest,distance);innerHit=true;}}
   if(innerHit){
     // Three inscribed spheres underfill the capsule between their centres.
     // Their first touch can occur after the actual capsule's first touch, so
     // retain the segment entry as the conservative earliest impact bound.
     float t=a+(b-a)*closest/length,nudge=std::min(.001f,std::max(0.f,(b-t)*.05f));
     if(confirmedPose(a,t+nudge))return r;
     if(std::strcmp(r.reason,"projectile_future_pose_not_confirmed")==0)sampledFuture(a,windows[w].last);
     return r;
   }
   // Three enlarged spheres cover the entire capsule. Expand by the exact
   // parabola/chord deviation; only their complete miss proves a curved miss.
   float outer=std::sqrt(c.radius*c.radius+c.half*c.half*.25f)+error;
   // SphereCast does not report an initial overlap. A starting overlap of any
   // enclosing sphere prevents treating that sphere's empty cast as a miss.
   int outerInitial=overlap(r,add(c.a,da),add(c.b,da),outer,mask,targetId);if(outerInitial<0)return r;
   if(outerInitial){sampledFuture(a,windows[w].last);return r;}
   for(V3 center:centers){float distance=0;int hit=sweep(r,add(center,da),outer,direction,length,mask,targetId,distance);if(hit<0)return r;if(hit){
     float t=a+(b-a)*distance/length,nudge=std::min(.001f,std::max(0.f,(b-t)*.05f));if(confirmedPose(a,t+nudge))return r;if(std::strcmp(r.reason,"projectile_future_pose_not_confirmed")==0)sampledFuture(a,windows[w].last);return r;
   }}
   a=b;
 }
 r.known=true;r.reason="projectile_actual_collider_sweep_miss";return r;
}
