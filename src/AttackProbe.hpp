#pragma once
// Current-build read-only Physics queries. Include after CombatTrace.hpp.
// Only call meleeProbe on Unity's game thread. Results describe this snapshot,
// not the future position of either actor or the game's later damage rules.
struct ReachProbeResult {
 bool known=false,intersects=false,radial=false;
 float range=0,radius=0,maxDistance=0,startOffset=0;
 float etaMilliseconds=-1;bool projected=false;
 V3 origin={},direction={};
 int hitCount=0;
 const char* reason="probe_not_initialized";
};
static P apSphereCast,apOverlapCapsule,apLayerMask,apInstanceId,apBaseOrigin,apReceiverMethod,apColliderFromId;
static uintptr_t apReceiverTypeHandle=0;
static P apWeaponClass,apChargeClass,apSenateClass,apSwordClass,apMaceClass,apGoliathClass;
static uintptr_t apHitHandle=0,apColliderHandle=0;
static bool apReady=false;
static bool apMethodAt(P m,uintptr_t rva){return m&&*(P*)m==(char*)game+rva;}
static bool apKind(P object,P klass){return object&&klass&&il2cpp_class_is_assignable_from(klass,il2cpp_object_get_class(object));}
static bool apFinite(V3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
static V3 apAdd(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static V3 apMul(V3 a,float b){return {a.x*b,a.y*b,a.z*b};}
template<class T>static bool apValue(P m,P object,P*args,T&result){P box=call(m,object,args);if(!box)return false;result=*(T*)il2cpp_object_unbox(box);return true;}
static bool initAttackProbe(){
 if(apReady)return true;
 P physics=findClass("UnityEngine","Physics"),unityObject=findClass("UnityEngine","Object");
 apWeaponClass=findClass("","Weapon");apChargeClass=findClass("","WeaponCharge");apSenateClass=findClass("","WeaponSenateSpear");
 apSwordClass=findClass("","WeaponSword");apMaceClass=findClass("","WeaponMace");apGoliathClass=findClass("","WeaponGoliathClub");
 apSphereCast=method(physics,"SphereCastNonAlloc",7);apOverlapCapsule=method(physics,"OverlapCapsuleNonAlloc",6);
 apLayerMask=method(apWeaponClass,"GetTargetLayerMask",0);apInstanceId=method(unityObject,"GetInstanceID",0);apBaseOrigin=method(apWeaponClass,"GetSphereCastOrigin",2);
 apReceiverMethod=method(findClass("UnityEngine","Component"),"GetComponentInParent",2);apColliderFromId=method(physics,"GetColliderByInstanceID",1);
 P health=findClass("","NetworkHealthObject"),receiverType=health&&il2cpp_class_get_type&&il2cpp_type_get_object?il2cpp_type_get_object(il2cpp_class_get_type(health)):nullptr;
 P hitClass=findClass("UnityEngine","RaycastHit"),colliderClass=findClass("UnityEngine","Collider");
 if(!apWeaponClass||!apChargeClass||!apSenateClass||!apSwordClass||!apMaceClass||!apGoliathClass||!hitClass||!colliderClass||
    !apMethodAt(apSphereCast,0x37E7BC0)||!apMethodAt(apOverlapCapsule,0x37E5460)||!apMethodAt(apLayerMask,0x7D3C40)||!apMethodAt(apInstanceId,0x370B0E0)||!apMethodAt(apBaseOrigin,0x7D3970)||!apMethodAt(apReceiverMethod,0x36FD3B0)||!apMethodAt(apColliderFromId,0x37E4AE0)||!receiverType||!il2cpp_array_new)return false;
 if(!apReceiverTypeHandle)apReceiverTypeHandle=il2cpp_gchandle_new(receiverType,false);if(!apReceiverTypeHandle)return false;
 if(!apHitHandle){P hits=il2cpp_array_new(hitClass,64);if(hits)apHitHandle=il2cpp_gchandle_new(hits,true);}
 if(!apColliderHandle){P colliders=il2cpp_array_new(colliderClass,64);if(colliders)apColliderHandle=il2cpp_gchandle_new(colliders,true);}
 apReady=apHitHandle&&apColliderHandle;return apReady;
}
static bool apActorColliderIds(P actor,int (&ids)[128],int&count){
 count=0;bool failed=false;
 auto append=[&](P collider){if(!alive(collider))return;int id=0;if(!apValue(apInstanceId,collider,nullptr,id)||!id){failed=true;return;}for(int i=0;i<count;i++)if(ids[i]==id)return;if(count==128){failed=true;return;}ids[count++]=id;};
 append(read<P>(actor,0x1C8));append(read<P>(actor,0x1D0));append(read<P>(actor,0x1D8));
 P cached=read<P>(actor,0x1E0);if(cached){uintptr_t n=read<uintptr_t>(cached,0x18);if(n>128)return false;for(uintptr_t i=0;i<n;i++)append(read<P>(cached,0x20+i*sizeof(P)));}
 return !failed&&count>0;
}
static bool apPlayerColliderIds(int (&ids)[128],int&count){return apActorColliderIds(player,ids,count);}
static bool apIdMatches(int id,const int*ids,int count){for(int i=0;i<count;i++)if(ids[i]==id)return true;return false;}
static int apPlayerReceiver(P collider,const int*ids,int count){
 if(!alive(collider))return -1;
 // Actual Weapon.AttackEffect D61A10 resolves parent NetworkHealthObject,
 // including child colliders absent from the actor's cached collider array.
 if(apReceiverMethod&&apReceiverTypeHandle){
   P type=il2cpp_gchandle_get_target(apReceiverTypeHandle);if(!type)return -1;
   bool inactive=false;P args[]={type,&inactive},exception=nullptr;
   P receiver=il2cpp_runtime_invoke(apReceiverMethod,collider,args,&exception);if(exception)return -1;
   return receiver==player?1:0;
 }
 int id=0;return apValue(apInstanceId,collider,nullptr,id)?(apIdMatches(id,ids,count)?1:0):-1;
}
static int apPlayerHitId(int id,const int*ids,int count){
 if(apReceiverMethod&&apReceiverTypeHandle){if(!apColliderFromId)return -1;P args[]={&id};return apPlayerReceiver(call(apColliderFromId,nullptr,args),ids,count);}
 return apIdMatches(id,ids,count)?1:0;
}
static bool apAxeChargeActive(P weapon){return alive(weapon)&&apMethodAt(method(il2cpp_object_get_class(weapon),"AttackEffect",13),0x7C3750)&&read<bool>(weapon,0x268);}
static ReachProbeResult meleeProbe(P attacker,P weapon,V3 direction,V3 motionOffset={}){
 ReachProbeResult result;result.direction=direction;
 if(!apReady)return result;
 if(!alive(player)||!alive(attacker)||!alive(weapon)){result.reason="probe_actor_or_weapon_absent";return result;}
 if(!apKind(weapon,apWeaponClass)||read<P>(weapon,0x178)!=attacker){result.reason="probe_weapon_owner_mismatch";return result;}
 // WeaponFlail bypasses inherited AttackEffect entirely: BallAttack and spin
 // update cast from the animated flail head with their own distance/phase.
 // Its inherited base effect RVA does not establish the actual attack shape.
 if(apMethodAt(method(il2cpp_object_get_class(weapon),"Attack",1),0x7CB6D0)){result.reason="probe_flail_ball_path_not_modeled";return result;}
 float length2=direction.x*direction.x+direction.y*direction.y+direction.z*direction.z;
 if(!apFinite(direction)||!std::isfinite(length2)||length2<0.000001f){result.reason="probe_attack_direction_invalid";return result;}
 int ids[128],idCount=0;if(!apPlayerColliderIds(ids,idCount)&&!(apReceiverMethod&&apReceiverTypeHandle)){result.reason="probe_player_colliders_unknown";return result;}
 int layerMask=0;if(!apValue(apLayerMask,weapon,nullptr,layerMask)){result.reason="probe_target_layer_mask_unavailable";return result;}
 P transform=call(mGetTransform,weapon);V3 weaponPosition={};
 if(!alive(transform)||!apValue(mTransformPosition,transform,nullptr,weaponPosition)||!apFinite(weaponPosition)){result.reason="probe_weapon_transform_unavailable";return result;}
 if(!apFinite(motionOffset)){result.reason="probe_motion_offset_invalid";return result;}weaponPosition=apAdd(weaponPosition,motionOffset);
 float charge=read<float>(weapon,0xC4);if(!std::isfinite(charge)){result.reason="probe_charge_state_invalid";return result;}
 int collide=2; // QueryTriggerInteraction.Collide, identical to Weapon's queries.
 if(apAxeChargeActive(weapon)){
   // WeaponAxeCharge.PerformCollision: OverlapSphere at weapon position plus
   // ModelForward*0.75, radius0.75. A zero-length overlap capsule is that sphere.
   V3 forward={};if(!apValue(mModelForward,attacker,nullptr,forward)||!apFinite(forward)){result.reason="probe_axe_charge_facing_unknown";return result;}
   float norm=forward.x*forward.x+forward.y*forward.y+forward.z*forward.z;
   if(!std::isfinite(norm)||std::fabs(norm-1.f)>.001f){result.reason="probe_axe_charge_facing_invalid";return result;}
   result.radius=.75f;result.origin=apAdd(weaponPosition,apMul(forward,.75f));result.direction=forward;
   P array=il2cpp_gchandle_get_target(apColliderHandle);if(!array){result.reason="probe_collider_buffer_missing";return result;}
   V3 center=result.origin;P args[]={&center,&center,&result.radius,array,&layerMask,&collide};int hits=-1;
   if(!apValue(apOverlapCapsule,nullptr,args,hits)||hits<0||hits>=64){result.reason="probe_axe_charge_query_unknown";return result;}
   result.hitCount=hits;
   for(int i=0;i<hits;i++){int hit=apPlayerReceiver(read<P>(array,0x20+i*sizeof(P)),ids,idCount);if(hit<0){result.reason="probe_axe_charge_receiver_unknown";return result;}if(hit)result.intersects=true;}
   result.known=true;result.reason=result.intersects?"axe_charge_sphere_hits_player_snapshot":"axe_charge_sphere_misses_player_snapshot";return result;
 }
 if(charge>0&&apKind(weapon,apChargeClass)){
   V3 forward={};if(!apValue(mModelForward,attacker,nullptr,forward)||!apFinite(forward)){result.reason="probe_charge_facing_unknown";return result;}
   float norm=std::sqrt(forward.x*forward.x+forward.y*forward.y+forward.z*forward.z);if(!std::isfinite(norm)||norm<0.000001f){result.reason="probe_charge_facing_invalid";return result;}
   V3 a={},b={};result.direction=forward;
   if(apKind(weapon,apSenateClass)){
     if(!apMethodAt(method(il2cpp_object_get_class(weapon),"GetCollisionHits",0),0x7ED570)){result.reason="probe_senate_override_unsupported";return result;}
     V3 raised=apAdd(weaponPosition,{0,1,0});a=apAdd(raised,apMul(forward,3.25f));b=apAdd(raised,apMul(forward,4.5f));result.radius=0.5f;result.range=1.25f;result.maxDistance=4.5f;
   }else{
     // Other WeaponCharge subclasses may override their collision query.
     if(!apMethodAt(method(il2cpp_object_get_class(weapon),"GetCollisionHits",0),0x7C9A90)){result.reason="probe_charge_override_unsupported";return result;}
     result.radius=read<float>(weapon,0x1F8);float halfWidth=read<float>(weapon,0x1FC);
     if(!std::isfinite(result.radius)||result.radius<=0||!std::isfinite(halfWidth)||halfWidth<0){result.reason="probe_charge_geometry_invalid";return result;}
     // Engine: normalize(cross(ModelForward, up)), centered forward by radius.
     V3 side={-forward.z,0,forward.x};float sideLength=std::sqrt(side.x*side.x+side.z*side.z);if(sideLength<0.000001f){result.reason="probe_charge_lateral_invalid";return result;}
     side=apMul(side,halfWidth/sideLength);V3 center=apAdd(weaponPosition,apMul(forward,result.radius));a=apAdd(center,side);b=apAdd(center,apMul(side,-1));result.range=halfWidth*2;result.maxDistance=result.range;
   }
   result.origin=a;P array=il2cpp_gchandle_get_target(apColliderHandle);if(!array){result.reason="probe_collider_buffer_missing";return result;}
   P args[]={&a,&b,&result.radius,array,&layerMask,&collide};int hits=-1;
   if(!apValue(apOverlapCapsule,nullptr,args,hits)||hits<0||hits>64){result.reason="probe_overlap_query_failed";return result;}
   result.hitCount=hits;if(hits==64){result.reason="probe_overlap_buffer_full";return result;}
   for(int i=0;i<hits;i++){int hit=apPlayerReceiver(read<P>(array,0x20+i*sizeof(P)),ids,idCount);if(hit<0){result.reason="probe_overlap_receiver_unknown";return result;}if(hit)result.intersects=true;}
   result.known=true;result.reason=result.intersects?"charge_capsule_hits_player_snapshot":"charge_capsule_misses_player_snapshot";return result;
 }
 P actualClass=il2cpp_object_get_class(weapon),effect=method(actualClass,"AttackEffect",13),originMethod=method(actualClass,"GetSphereCastOrigin",2);
 bool baseEffect=apMethodAt(effect,0x7CFD40),swordEffect=apMethodAt(effect,0x7EF160),maceEffect=apMethodAt(effect,0x7EA600),goliathEffect=apMethodAt(effect,0x7E9770),senateSwordEffect=apMethodAt(effect,0x7EE140);
 bool axeEffect=apMethodAt(effect,0x7C63B0),axeChargeEffect=apMethodAt(effect,0x7C3750),punchEffect=apMethodAt(effect,0x7EC220);
 bool buffTool=apMethodAt(effect,0x7CB4D0)||apMethodAt(effect,0x7E9E90),wireTool=apMethodAt(effect,0x7EF8D0);
 if(apMethodAt(effect,0x7C6A70)){result.known=true;result.reason="bow_release_uses_projectile_prediction";return result;}
 if(buffTool&&charge>0){result.known=true;result.reason="charged_tool_applies_buff_not_melee_hit";return result;}
 bool supportedOrigin=apMethodAt(originMethod,0x7D3970)||(axeEffect&&apMethodAt(originMethod,0x7C67F0))||(axeChargeEffect&&apMethodAt(originMethod,0x7C5610));
 if(!(baseEffect||swordEffect||maceEffect||goliathEffect||senateSwordEffect||axeEffect||axeChargeEffect||punchEffect||buffTool||wireTool)||!supportedOrigin){result.reason="probe_melee_override_unsupported";return result;}
 result.radius=read<float>(weapon,0x70);result.maxDistance=read<float>(weapon,0x74);
 if(senateSwordEffect){
   // Verified override delegates these three shapes to Weapon.AttackEffect.
   // radialAttack changes hit direction, not its SphereCast geometry.
   if(read<bool>(weapon,0x230)){result.radius=read<float>(weapon,0x1D0);result.maxDistance=read<float>(weapon,0x1D4);}
   else if(charge>0){result.radius=read<float>(weapon,0x1EC);result.maxDistance=read<float>(weapon,0x1E8);result.startOffset=read<float>(weapon,0x1F0);}
   else if(read<int>(weapon,0x180)==0){result.radius=read<float>(weapon,0x220);result.maxDistance=read<float>(weapon,0x21C);result.startOffset=-.5f*result.radius;result.radial=true;}
 }else if(charge>0){
   if(axeEffect||axeChargeEffect){result.radius=read<float>(weapon,0x1D0);result.maxDistance=read<float>(weapon,0x1CC);result.radial=true;}
   else if(swordEffect&&apKind(weapon,apSwordClass)){result.maxDistance=read<float>(weapon,0x1CC);result.radius=read<float>(weapon,0x1D0);}
   else if(maceEffect&&apKind(weapon,apMaceClass)){result.maxDistance=read<float>(weapon,0x1D8);result.radius=read<float>(weapon,0x1D4);}
   else if(goliathEffect&&apKind(weapon,apGoliathClass)){result.maxDistance=read<float>(weapon,0x1D4);result.radius=read<float>(weapon,0x1D8);result.startOffset=2;}
   else if(!baseEffect&&!punchEffect&&!wireTool){result.reason="probe_special_geometry_unsupported";return result;}
 }
 if(!std::isfinite(result.radius)||result.radius<=0||!std::isfinite(result.maxDistance)||result.maxDistance<=0||!std::isfinite(result.startOffset)){result.reason="probe_melee_geometry_invalid";return result;}
 // GetSphereCastOrigin uses the weapon's BASE radius at +0x70 even for specials.
 P originArgs[]={&direction,&result.radius};if(!apValue(originMethod,weapon,originArgs,result.origin)||!apFinite(result.origin)){result.reason="probe_cast_origin_unavailable";return result;}
 result.origin=apAdd(apAdd(result.origin,motionOffset),apMul(direction,result.startOffset));result.range=std::max(0.1f,result.maxDistance-2*result.radius);
 P array=il2cpp_gchandle_get_target(apHitHandle);if(!array){result.reason="probe_hit_buffer_missing";return result;}
 P args[]={&result.origin,&result.radius,&direction,array,&result.range,&layerMask,&collide};int hits=-1;
 if(!apValue(apSphereCast,nullptr,args,hits)||hits<0||hits>64){result.reason="probe_sphere_query_failed";return result;}
 result.hitCount=hits;if(hits==64){result.reason="probe_sphere_buffer_full";return result;}
 // Current Unity 6000.3 RaycastHit is 0x2C bytes; EntityId(int) at +0x28.
 for(int i=0;i<hits;i++){int hit=apPlayerHitId(read<int>(array,0x20+i*0x2C+0x28),ids,idCount);if(hit<0){result.reason="probe_melee_receiver_unknown";return result;}if(hit)result.intersects=true;}
 // Do not add starting-overlap hits here: the game's own SphereCast does not.
 result.known=true;result.reason=result.intersects?"melee_sphere_hits_player_snapshot":"melee_sphere_misses_player_snapshot";return result;
}
