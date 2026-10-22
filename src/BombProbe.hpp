#pragma once
// Read-only forecast for the verified Sergeant Bomb_Enemy(100) sphere prefab.
// No transforms/colliders/rigidbodies are changed. Future velocity/input, scene
// changes and external collision-matrix patches remain unmodeled game events.
struct BombProbeResult {
 bool supported=false,known=false,intersects=false;
 float etaMilliseconds=-1,latestMilliseconds=-1,gameSeconds=-1,radius=0,sphereRadius=0,frameSeconds=0;
 V3 blastCenter={},heroVelocity={},projectileVelocity={};
 int contactId=0,contactLayer=-1;bool contactTrigger=false;
 unsigned queries=0;const char*reason="bomb_probe_not_initialized";
};
namespace BombProbe {
using namespace ProjectileProbe;
static P centerMethod,radiusMethod,centerOfMassMethod,bodyPositionMethod,attachedBodyMethod,triggerMethod,excludeMethod,bodyExcludeMethod,interpolationMethod,fixedDeltaMethod,overlapMethod,colliderFromIdMethod;
static P damageReceiverMethod;static uintptr_t damageReceiverTypeHandle=0;
static uintptr_t hitsHandle=0,overlapsHandle=0;static bool ready=false;
constexpr int Capacity=64,Budget=48;
constexpr float Horizon=1.2f,Segment=.12f;
// Current globalgamemanagers, sha256 ed8aca42cb11d7c6df14b30d1970f915b0942b234bdcd952a4245b71a0b37604.
// GameAssembly's only IgnoreLayerCollision call changes Default/IgnoreRaycast.
constexpr int Ground=3,Receiver=9,Obstacle=10,PlayerFF=16,AoeMask=(1<<6)|(1<<10)|(1<<16);
struct Ids {int values[128]={},count=0;};
static bool actorIds(P actor,Ids&out){
 auto append=[&](P collider){if(!alive(collider))return true;int id=0;if(!get(instanceId,collider,nullptr,id)||!id)return false;for(int i=0;i<out.count;i++)if(out.values[i]==id)return true;if(out.count==128)return false;out.values[out.count++]=id;return true;};
 if(!alive(actor))return false;
 if(!append(read<P>(actor,0x1C8))||!append(read<P>(actor,0x1D0))||!append(read<P>(actor,0x1D8)))return false;
 P cached=read<P>(actor,0x1E0);if(cached){uintptr_t count=read<uintptr_t>(cached,0x18);if(count>128)return false;for(uintptr_t i=0;i<count;i++)if(!append(read<P>(cached,0x20+i*sizeof(P))))return false;}
 if(actor==player&&!append(read<P>(actor,0x540)))return false;
 return out.count>0;
}
static bool matches(const Ids&ids,int id){for(int i=0;i<ids.count;i++)if(ids.values[i]==id)return true;return false;}
static bool query(BombProbeResult&r){if(r.queries>=Budget){r.reason="bomb_physics_query_budget";return false;}++r.queries;return true;}
static int overlap(BombProbeResult&r,V3 center,float radius,int mask,const Ids*find=nullptr){
 if(!query(r))return -1;P array=il2cpp_gchandle_get_target(overlapsHandle);if(!array){r.reason="bomb_overlap_buffer_absent";return -1;}
 int count=-1;P args[]={&center,&radius,array,&mask};
 if(!get(overlapMethod,nullptr,args,count)||count<0||count>Capacity){r.reason="bomb_overlap_query_failed";return -1;}
 if(count==Capacity){r.reason="bomb_overlap_buffer_full";return -1;}
 if(!find)return count;
 for(int i=0;i<count;i++){P c=read<P>(array,0x20+i*sizeof(P));int id=0;if(!alive(c)||!get(instanceId,c,nullptr,id)){r.reason="bomb_overlap_identity_unknown";return -1;}if(matches(*find,id))return 1;}
 return 0;
}
// Both Projectile.TriggerImpactGameplayEffects and Enemy.GroundSlamRPC resolve
// GetComponentInParent<NetworkHealthObject>(false) for each overlapped collider.
// Cached actor collider IDs alone cannot prove a miss for an attached child.
static int receiverOverlap(BombProbeResult&r,V3 center,float radius,int mask,const Ids&fallback,P receiverMethod=nullptr,uintptr_t receiverTypeHandle=0){
 if(!receiverMethod){receiverMethod=damageReceiverMethod;receiverTypeHandle=damageReceiverTypeHandle;}
 if(!receiverMethod||!receiverTypeHandle)return overlap(r,center,radius,mask,&fallback);
 int count=overlap(r,center,radius,mask);if(count<0)return -1;
 P array=il2cpp_gchandle_get_target(overlapsHandle),type=il2cpp_gchandle_get_target(receiverTypeHandle);if(!array||!type){r.reason="aoe_receiver_type_or_buffer_absent";return -1;}
 bool includeInactive=false;P args[]={type,&includeInactive};
 for(int i=0;i<count;i++){
  P collider=read<P>(array,0x20+i*sizeof(P));if(!alive(collider)){r.reason="aoe_receiver_collider_absent";return -1;}
  P exception=nullptr,receiver=il2cpp_runtime_invoke(receiverMethod,collider,args,&exception);
  if(exception){r.reason="aoe_damage_receiver_lookup_failed";return -1;}
  if(receiver==player)return 1;
 }
 return 0;
}
struct Contact {bool found=false,uncertain=false,trigger=false;float distance=0;int id=0,layer=-1;};
// Damage immunity effect17 does not suppress projectile contact/detonation.
// The trigger handler tests its virtual IsInvulnerable getter, and the game's
// dodgedAgents list records history rather than granting future immunity.
static int heroTriggerImmunity(float eta){
 for(int effect:{3,4}){P args[]={&effect};if(value<bool>(mEffectActive,player,args))return eta<=0?1:-1;}
 if(value<bool>(mDead,player))return 1;
 if(!value<bool>(mInvulnerable,player))return 0;
 if(eta<=0)return 1;auto coverage=currentDodgeCoverage(eta);
 if(coverage.actualIFrames&&coverage.known)return coverage.skip?1:0;
 return -1;
}
static bool classify(BombProbeResult&r,P collider,P bombBody,int sourceLayer,const Ids&hero,const Ids&thrower,bool heroOnly,float eta,Contact&c){
 int id=0,layer=-1,exclude=0;bool trigger=false;P go=call(componentGameObject,collider),body=call(attachedBodyMethod,collider);
 if(!alive(collider)||!alive(go)||!get(instanceId,collider,nullptr,id)||!get(gameObjectLayer,go,nullptr,layer)||!get(triggerMethod,collider,nullptr,trigger)||!get(excludeMethod,collider,nullptr,exclude))return false;
 if(body==bombBody)return true;if(exclude){c.uncertain=c.found=true;return true;}
 if(alive(body)){int rbExclude=0;if(!get(bodyExcludeMethod,body,nullptr,rbExclude))return false;if(rbExclude){c.uncertain=c.found=true;return true;}}
 bool isHero=matches(hero,id);if(heroOnly&&!isHero)return true;if(!heroOnly&&isHero)return true;
 if(trigger&&matches(thrower,id))return true; // Agent trigger path skips its thrower.
 if(trigger&&layer==Ground)return true; // OnTriggerEnter never routes Ground to LandedOnGround.
 // The caller handles the projectile's dodge list and current immunity. Keep
 // other receivers/dynamic bodies unknown rather than predict their behavior.
 c.id=id;c.layer=layer;c.trigger=trigger;
 if(!isHero&&(trigger||alive(body))){c.uncertain=true;c.found=true;return true;}
 if(layer!=Ground&&layer!=Receiver&&layer!=Obstacle&&layer!=PlayerFF){c.uncertain=true;c.found=true;return true;}
 c.found=true;return true;
}
static bool sweep(BombProbeResult&r,P bombBody,int sourceLayer,int mask,const Ids&hero,const Ids&thrower,bool heroOnly,V3 origin,float radius,V3 direction,float length,float begin,float duration,float scale,P projectile,Contact&out){
 if(!query(r))return false;P array=il2cpp_gchandle_get_target(hitsHandle);if(!array){r.reason="bomb_sweep_buffer_absent";return false;}
 int collide=2,count=-1;P args[]={&origin,&radius,&direction,array,&length,&mask,&collide};
 if(!get(sphereCast,nullptr,args,count)||count<0||count>Capacity){r.reason="bomb_sweep_query_failed";return false;}
 if(count==Capacity){r.reason="bomb_sweep_buffer_full";return false;}
 out.distance=length;
 for(int i=0;i<count;i++){
  size_t offset=0x20+i*0x2C;float distance=read<float>(array,offset+0x1C);int id=read<int>(array,offset+0x28);
  if(!std::isfinite(distance)||distance<0||distance>length+.001f){r.reason="bomb_sweep_distance_invalid";return false;}
  if(out.found&&distance>=out.distance)continue;P idArgs[]={&id};P collider=call(colliderFromIdMethod,nullptr,idArgs);if(!alive(collider)){r.reason="bomb_sweep_collider_absent";return false;}
  Contact candidate;float eta=(begin+duration*distance/length)*1000/scale;
  if(!classify(r,collider,bombBody,sourceLayer,hero,thrower,heroOnly,eta,candidate)){r.reason="bomb_contact_filter_unknown";return false;}
  if(!candidate.found)continue;
  if(heroOnly&&candidate.trigger){int immunity=heroTriggerImmunity(eta);if(immunity>0)continue;if(immunity<0)candidate.uncertain=true;}
  candidate.distance=distance;out=candidate;
 }
 return true;
}
static int startingOverlap(BombProbeResult&r,P body,int layer,int mask,const Ids&hero,const Ids&thrower,bool heroOnly,V3 center,float radius,float eta){
 int count=overlap(r,center,radius,mask);if(count<0)return -1;P array=il2cpp_gchandle_get_target(overlapsHandle);
 for(int i=0;i<count;i++){Contact contact;P collider=read<P>(array,0x20+i*sizeof(P));if(!classify(r,collider,body,layer,hero,thrower,heroOnly,eta,contact)){r.reason="bomb_starting_overlap_filter_unknown";return -1;}if(!contact.found)continue;if(heroOnly&&contact.trigger&&heroTriggerImmunity(eta)>0)continue;return 1;}
 return 0;
}
}
static bool initBombProbe(){
 using namespace BombProbe;if(BombProbe::ready)return true;if(!ProjectileProbe::ready)return false;
 P sphere=findClass("UnityEngine","SphereCollider"),rb=findClass("UnityEngine","Rigidbody"),collider=findClass("UnityEngine","Collider"),physics=findClass("UnityEngine","Physics");
 struct Api{P*target;P klass;const char*name;int count;uintptr_t rva;};
 Api apis[]={
 {&centerMethod,sphere,"get_center",0,0x37EE020},{&radiusMethod,sphere,"get_radius",0,0x37EE100},{&centerOfMassMethod,rb,"get_worldCenterOfMass",0,0x37EC5E0},{&bodyPositionMethod,rb,"get_position",0,0x37EC060},
 {&attachedBodyMethod,collider,"get_attachedRigidbody",0,0x37DF440},{&triggerMethod,collider,"get_isTrigger",0,0x37DF7B0},{&excludeMethod,collider,"get_excludeLayers",0,0x37DF6E0},{&bodyExcludeMethod,rb,"get_excludeLayers",0,0x37EB6A0},
 {&interpolationMethod,rb,"get_interpolation",0,0x37EBAA0},{&fixedDeltaMethod,findClass("UnityEngine","Time"),"get_fixedDeltaTime",0,0x372B390},{&overlapMethod,physics,"OverlapSphereNonAlloc",4,0x37E5650},{&colliderFromIdMethod,physics,"GetColliderByInstanceID",1,0x37E4AE0}};
 for(auto&a:apis){*a.target=method(a.klass,a.name,a.count);if(!at(*a.target,a.rva))return false;}
 damageReceiverMethod=method(findClass("UnityEngine","Component"),"GetComponentInParent",2);
 P health=findClass("","NetworkHealthObject"),type=health&&il2cpp_class_get_type&&il2cpp_type_get_object?il2cpp_type_get_object(il2cpp_class_get_type(health)):nullptr;
 if(!at(damageReceiverMethod,0x36FD3B0)||!type)return false;
 if(!damageReceiverTypeHandle)damageReceiverTypeHandle=il2cpp_gchandle_new(type,false);
 if(!damageReceiverTypeHandle)return false;
 P hits=il2cpp_array_new(findClass("UnityEngine","RaycastHit"),BombProbe::Capacity),colliders=il2cpp_array_new(collider,BombProbe::Capacity);
 if(hits)hitsHandle=il2cpp_gchandle_new(hits,true);if(colliders)overlapsHandle=il2cpp_gchandle_new(colliders,true);
 BombProbe::ready=hitsHandle&&overlapsHandle;return BombProbe::ready;
}
static BombProbeResult probeBomb(P self){
 using namespace BombProbe;BombProbeResult r;
 if(!alive(self)||read<int>(self,0xF0)!=100||!read<bool>(self,0xA4)){r.reason="bomb_family_not_supported";return r;}r.supported=true;
 if(!BombProbe::ready)return r;
 if(!alive(player)||read<bool>(self,0xC8)||!read<bool>(self,0x58)||read<bool>(self,0x17C)||!read<bool>(self,0x1A0)||read<bool>(self,0x158)||read<bool>(self,0x15A)||read<int>(self,0x108)>=0){r.reason="bomb_contact_lifecycle_or_family_unsupported";return r;}
 P collider=read<P>(self,0x148),body=read<P>(self,0x140),heroBody=read<P>(player,0x190),back=read<P>(self,0x80),transform=call(mGetTransform,self),ct=call(mGetTransform,collider);
 if(!alive(collider)||!alive(body)||!alive(heroBody)||!alive(transform)||!alive(ct)||!kind(collider,sphereClass)||alive(back)){r.reason="bomb_requires_single_sphere_body";return r;}
 bool enabled=false,trigger=true,kinematic=true,globalGravity=false;int interpolation=-1,layer=-1,excluded=0,bodyExcluded=0,frozen=0;
 float radius=0,damping=-1,scale=0;V3 local={},lossy={},center={},bodyPosition={},massCenter={},rootPosition={},heroAngular={};P go=call(componentGameObject,collider);
 if(!get(colliderEnabled,collider,nullptr,enabled)||!get(triggerMethod,collider,nullptr,trigger)||!enabled||trigger){r.reason="bomb_main_sphere_inactive_or_trigger";return r;}
 if(!alive(go)||!get(gameObjectLayer,go,nullptr,layer)||(layer!=8&&layer!=12)||!get(excludeMethod,collider,nullptr,excluded)||!get(bodyExcludeMethod,body,nullptr,bodyExcluded)||excluded||bodyExcluded){r.reason="bomb_layer_profile_unknown";return r;}
 if(!get(centerMethod,collider,nullptr,local)||!get(radiusMethod,collider,nullptr,radius)||!get(lossyScale,ct,nullptr,lossy)||!finite(local)||!finite(lossy)||!std::isfinite(radius)||radius<=0){r.reason="bomb_sphere_geometry_unknown";return r;}
 P centerArgs[]={&local};Bounds bounds={};
 if(!get(transformPoint,ct,centerArgs,center)||!get(colliderBounds,collider,nullptr,bounds)||!valid(bounds)||!get(bodyPositionMethod,body,nullptr,bodyPosition)||!get(centerOfMassMethod,body,nullptr,massCenter)||!get(mTransformPosition,transform,nullptr,rootPosition)){r.reason="bomb_sphere_pose_unknown";return r;}
 r.sphereRadius=radius*std::max({std::fabs(lossy.x),std::fabs(lossy.y),std::fabs(lossy.z)});if(!std::isfinite(r.sphereRadius)||r.sphereRadius<=0){r.reason="bomb_sphere_scale_invalid";return r;}
 auto coincident=[&](V3 a,V3 b){return finite(a)&&finite(b)&&dot(sub(a,b),sub(a,b))<=.000004f;};
 if(!coincident(center,massCenter)||!coincident(center,rootPosition)||!coincident(center,bodyPosition)||!coincident(center,bounds.center)||!nearlyEqual(bounds.extents.x,r.sphereRadius)||!nearlyEqual(bounds.extents.y,r.sphereRadius)||!nearlyEqual(bounds.extents.z,r.sphereRadius)){r.reason="bomb_rotating_origin_or_sphere_bounds_mismatch";return r;}
 if(!get(linearVelocity,body,nullptr,r.projectileVelocity)||!get(linearVelocity,heroBody,nullptr,r.heroVelocity)||!get(angularVelocity,heroBody,nullptr,heroAngular)||!finite(r.projectileVelocity)||!finite(r.heroVelocity)||!finite(heroAngular)||dot(heroAngular,heroAngular)>.00000001f||!get(linearDamping,body,nullptr,damping)||!get(isKinematic,body,nullptr,kinematic)||!get(useGravity,body,nullptr,globalGravity)||!get(interpolationMethod,body,nullptr,interpolation)||!get(constraints,body,nullptr,frozen)||!get(fixedDeltaMethod,nullptr,nullptr,r.frameSeconds)||!get(mTimeScale,nullptr,nullptr,scale)||!std::isfinite(scale)||scale<=0||!std::isfinite(r.frameSeconds)||r.frameSeconds<=0||r.frameSeconds>.1f){r.reason="bomb_motion_or_clock_unknown";return r;}
 if(kinematic||interpolation!=0||!std::isfinite(damping)||damping>.000001f){r.reason="bomb_motion_profile_unsupported";return r;}
 float gravity=read<float>(self,0x150);r.radius=read<float>(self,0xA8);if(!std::isfinite(gravity)||!std::isfinite(r.radius)||r.radius<=0||r.radius>100){r.reason="bomb_gravity_or_blast_radius_invalid";return r;}
 if(globalGravity){V3 g={};if(!get(physicsGravity,nullptr,nullptr,g)||!finite(g)||std::fabs(g.x)>.000001f||std::fabs(g.z)>.000001f){r.reason="bomb_global_gravity_unknown";return r;}gravity+=g.y;}
 if(frozen&2)r.projectileVelocity.x=0;if(frozen&4){r.projectileVelocity.y=0;gravity=0;}if(frozen&8)r.projectileVelocity.z=0;
 Ids hero,thrower;if(!actorIds(player,hero)||!actorIds(read<P>(self,0x118),thrower)){r.reason="bomb_actor_colliders_unknown";return r;}
 int mask=((1<<Ground)|(1<<Receiver)|(1<<PlayerFF)|(layer==8?(1<<Obstacle):0))&~(excluded|bodyExcluded);
 if(!mask){r.known=true;r.reason="bomb_collision_layers_excluded";return r;}
 // Sphere rotation is harmless ONLY because root, sphere center and actual
 // Rigidbody COM coincide. Other collider shapes/families remain unsupported.
 auto pose=[&](float t){return add(center,displacement(r.projectileVelocity,gravity,t));};
 for(bool heroOnly:{false,true}){int initial=startingOverlap(r,body,layer,mask,hero,thrower,heroOnly,center,r.sphereRadius,0);if(initial<0)return r;if(initial){r.reason="bomb_already_overlapping_has_no_predictive_lead";return r;}}
 for(float a=0;a<BombProbe::Horizon-.000001f;){
  float b=std::min(a+Segment,BombProbe::Horizon);Contact best;float bestTime=b,latest=b;
  for(bool heroOnly:{false,true}){
   V3 from=pose(a),to=pose(b);if(heroOnly){from=sub(from,mul(r.heroVelocity,a));to=sub(to,mul(r.heroVelocity,b));}
   V3 travel=sub(to,from);float length=std::sqrt(dot(travel,travel));if(length<.000001f){if(std::fabs(gravity)>.000001f){r.reason="bomb_curved_stationary_chord_unknown";return r;}continue;}V3 direction=mul(travel,1/length);
   // PhysX semi-implicit acceleration can differ from a continuous parabola by
   // half a fixed acceleration step times elapsed time. Add that entire tube,
   // not just the smaller chord/parabola deviation within this cast segment.
   float error=std::fabs(gravity)*((b-a)*(b-a)/8+b*r.frameSeconds*.5f);float inner=r.sphereRadius-error;if(inner<=.00001f){r.reason="bomb_motion_envelope_exceeds_sphere";return r;}
   int initial=startingOverlap(r,body,layer,mask,hero,thrower,heroOnly,from,r.sphereRadius+error,a*1000/scale);if(initial<0)return r;if(initial){r.reason="bomb_outer_motion_envelope_starts_overlapping";return r;}
   Contact outer;if(!sweep(r,body,layer,mask,hero,thrower,heroOnly,from,r.sphereRadius+error,direction,length,a,b-a,scale,self,outer))return r;
   if(!outer.found)continue;float first=a+(b-a)*outer.distance/length;
   if(outer.uncertain){if(first<=bestTime){r.reason="bomb_other_dynamic_or_trigger_contact_unknown";return r;}continue;}
   Contact exact;if(!sweep(r,body,layer,mask,hero,thrower,heroOnly,from,inner,direction,length,a,b-a,scale,self,exact))return r;
   if(!exact.found||exact.uncertain||exact.id!=outer.id){r.reason="bomb_grazing_or_first_contact_ambiguous";return r;}
   float last=a+(b-a)*exact.distance/length;
   if(first<=bestTime){best=outer;bestTime=first;latest=std::max(first,last)+r.frameSeconds;}
  }
  if(best.found){
   // Preserve the engine's 3D OverlapSphere membership and global trigger policy.
   // Reverse only current hero translation; do not guess a hero radius/XZ circle.
   float middle=(bestTime+latest)*.5f,half=(latest-bestTime)*.5f;V3 relativeVelocity=sub(r.projectileVelocity,r.heroVelocity),relativeCenter=sub(pose(middle),mul(r.heroVelocity,middle));
   float margin=(std::sqrt(dot(relativeVelocity,relativeVelocity))+std::fabs(gravity)*latest)*half+std::fabs(gravity)*latest*r.frameSeconds*.5f;
   int outerHit=receiverOverlap(r,relativeCenter,r.radius+margin,AoeMask,hero);if(outerHit<0)return r;bool hit=false;
   if(outerHit){float innerRadius=r.radius-margin;if(innerRadius<=0){r.reason="bomb_blast_boundary_or_contact_time_ambiguous";return r;}int innerHit=receiverOverlap(r,relativeCenter,innerRadius,AoeMask,hero);if(innerHit<0)return r;if(!innerHit){r.reason="bomb_blast_boundary_or_contact_time_ambiguous";return r;}hit=true;}
   r.known=true;r.intersects=hit;r.gameSeconds=bestTime;r.etaMilliseconds=std::max(0.f,bestTime-r.frameSeconds)*1000/scale;r.latestMilliseconds=latest*1000/scale;r.blastCenter=pose(bestTime);r.contactId=best.id;r.contactLayer=best.layer;r.contactTrigger=best.trigger;r.reason=hit?"bomb_first_contact_blast_overlaps_future_player":"bomb_first_contact_blast_misses_future_player";return r;
  }
  a=b;
 }
 r.known=true;r.reason="bomb_no_detonating_contact_in_forecast_horizon";return r;
}
