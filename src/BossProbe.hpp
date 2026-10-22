#pragma once
// Current GameAssembly only. Coroutine state and elapsed game time are authoritative;
// WarningAOE graphics alone never authorize a defense. No scene transforms are written.
namespace BossProbe {
using namespace ProjectileProbe;
struct Result {bool known=false,hit=false;float first=-1,last=-1;const char*reason="boss_state_unknown";V3 escape={};float escapeLead=-1;V3 center={};float radius=0;};
struct Attack {uintptr_t handle=0;uint64_t id=0;int kind=0,lastState=-1;float created=0;DWORD reported=0;Result result,reservation;};
static Attack attacks[64];static unsigned next=0;static P curveEvaluate,projectileOwner,sleeping,dashDuration,transformForward;
struct AreaAttack {uintptr_t object=0;uint64_t id=0;DWORD seen=0,reported=0;Result result;V3 center={};bool sampled=false;};
static AreaAttack barrels[64];static unsigned nextBarrel=0;
struct RhinoAttack {uintptr_t handle=0,nameHandle=0;uint64_t id=0;float created=0,delay=0,sampledGame=0;DWORD reported=0;int lastState=-1;V3 center={};bool sampled=false,stationary=false,ambiguous=false;Result result,reservation;};
static RhinoAttack rhinos[16];static unsigned nextRhino=0;
static P rhinoClass,invokeMethod,isInvokingMethod,rhinoSlamMethod,rhinoStateMethod,behaviourActiveMethod;
static void(*realInvoke)(P,P,float,P);static void(*realRhinoSlam)(P,P);
static void(*realBarrelUpdate)(P,P);
static bool(*realLeapStep)(P,P);static bool(*realTackleStep)(P,P);
static P leapStep,tackleStep,receiverMethod,inFrontMethod;static uintptr_t receiverTypeHandle;
// GroundSlamRPC uses Component.GetComponentInParent<NetworkHealthObject>(false),
// not a cached collider-ID list. A child/attached collider can be absent from it.
static int receiverOverlap(BombProbeResult&q,V3 center,float radius,int mask,const BombProbe::Ids&fallback){
 return BombProbe::receiverOverlap(q,center,radius,mask,fallback,receiverMethod,receiverTypeHandle);
}
static void clear(Attack&a){if(a.handle)il2cpp_gchandle_free(a.handle);a={};}
static void clearRhino(RhinoAttack&a,const char*reason=nullptr){if(reason&&a.id){Trace::Json j("boss_attack_finished");j.integer("correlation_id",a.id);j.str("kind","rhino_slam");j.str("reason",reason);Trace::enqueue(j);}if(a.handle)il2cpp_gchandle_free(a.handle);if(a.nameHandle)il2cpp_gchandle_free(a.nameHandle);a={};}
static bool slamName(P name){static constexpr char16_t expected[]=u"GroundSlam";return name&&read<int>(name,0x10)==10&&std::memcmp((char*)name+0x14,expected,20)==0;}
static void invokeHook(P self,P name,float delay,P mi){
 // Observe the actual Unity schedule, including the exact managed method-name
 // string. The network RPC's outer send path does not itself schedule damage.
 realInvoke(self,name,delay,mi);
 if(faulted||!alive(self)||!kind(self,rhinoClass)||!slamName(name))return;
 float now=value<float>(mTime);if(!std::isfinite(now)||!std::isfinite(delay)||delay<=0||delay>15)return;
 for(auto&a:rhinos)if(a.handle&&il2cpp_gchandle_get_target(a.handle)==self){a.ambiguous=true;a.result={};a.reservation={};a.result.reason="rhino_multiple_pending_slam_invokes";return;}
 auto&a=rhinos[nextRhino++%16];clearRhino(a,"rhino_schedule_capacity_replaced");a.handle=il2cpp_gchandle_new(self,false);a.nameHandle=il2cpp_gchandle_new(name,false);a.id=nextCombatId();a.created=now;a.delay=delay;
 if(!a.handle||!a.nameHandle){clearRhino(a);return;}
 Trace::Json j("boss_attack_scheduled");j.integer("correlation_id",a.id);j.str("kind","rhino_slam");j.number("invoke_delay_game_seconds",delay);Trace::enqueue(j);
}
static void rhinoSlamHook(P self,P mi){
 // The engine event consumes the schedule before dealing damage. It cannot
 // provide fresh predictive lead, and a retained entry must not fire afterward.
 for(auto&a:rhinos)if(a.handle&&il2cpp_gchandle_get_target(a.handle)==self)clearRhino(a,"rhino_slam_event_consumed");
 realRhinoSlam(self,mi);
}
static void track(P iterator,int kind){
 if(!iterator)return;auto&a=attacks[next++%64];clear(a);a.handle=il2cpp_gchandle_new(iterator,false);a.id=nextCombatId();a.kind=kind;a.created=value<float>(mTime);
 Trace::Json j("boss_attack_scheduled");j.integer("correlation_id",a.id);j.str("kind",kind==1?"leap_slam":"tackle_contact");Trace::enqueue(j);
}
static bool stepHook(P iterator,P mi,int kind,bool(*original)(P,P)){
 // TriggerLeap/TriggerTackle allocate the iterator inline; the named iterator
 // factory methods are bypassed by the shipping IL2CPP code. Observe MoveNext
 // itself, after the original establishes its owner/state, on the game thread.
 bool running=original(iterator,mi);if(faulted||!running||!iterator)return running;
 int state=read<int>(iterator,0x10);if(state!=1&&state!=2)return running;
 for(const auto&a:attacks)if(a.handle&&il2cpp_gchandle_get_target(a.handle)==iterator)return running;
 track(iterator,kind);return running;
}
static bool leapStepHook(P iterator,P mi){return stepHook(iterator,mi,1,realLeapStep);}
static bool tackleStepHook(P iterator,P mi){return stepHook(iterator,mi,2,realTackleStep);}
static P nested(P parent,const char*name){P cursor=nullptr;while(P klass=il2cpp_class_get_nested_types(parent,&cursor)){const char*actual=il2cpp_class_get_name(klass);if(actual&&std::strcmp(actual,name)==0)return klass;}return nullptr;}
static bool clock(float&scale,float&frame){
 scale=value<float>(mTimeScale);frame=value<float>(DodgeBridgeDetails::deltaTimeMethod);
 return std::isfinite(scale)&&scale>0&&std::isfinite(frame)&&frame>0&&frame<=.1f;
}
// Match the game's OverlapSphere, including real 3D collider membership and
// global trigger policy. Reverse the player's current translation, never invent
// a player radius or use distance to the warning graphic.
static Result area(V3 center,float radius,float seconds,int mask,float scale,float frame,bool stopForAttack=false){
 Result r;if(!finite(center)||!std::isfinite(radius)||radius<=0||radius>100||!std::isfinite(seconds)||seconds<0||seconds>15)return r;
 P body=alive(player)?read<P>(player,0x190):nullptr;V3 velocity={},angular={};
 BombProbe::Ids ids;if(!alive(body)||!get(linearVelocity,body,nullptr,velocity)||!get(angularVelocity,body,nullptr,angular)||!finite(velocity)||!finite(angular)||dot(angular,angular)>.00000001f||!BombProbe::actorIds(player,ids))return r;
 if(stopForAttack)velocity={};r.center=center;r.radius=radius;center=sub(center,mul(velocity,seconds));float margin=std::sqrt(dot(velocity,velocity))*frame*2;
 BombProbeResult query;int outer=BossProbe::receiverOverlap(query,center,radius+margin,mask,ids);if(outer<0){r.reason=query.reason;return r;}
 // A real collider intersection at the nominal impact time is still a threat
 // at the edge. Requiring an artificially shrunken blast to hit suppressed
 // genuine edge contacts while moving. The outer tube only proves a clear miss;
 // if it hits but the real radius misses, leave the time-boundary case unknown.
 if(outer){int exact=BossProbe::receiverOverlap(query,center,radius,mask,ids);if(exact<0){r.reason=query.reason;return r;}if(!exact){r.reason="boss_blast_boundary_time_uncertain";return r;}}
 r.known=true;r.hit=outer!=0;r.first=std::max(0.f,seconds-frame)*1000/scale;r.last=(seconds+frame*2)*1000/scale;r.reason=r.hit?"boss_real_sphere_overlaps_future_player":"boss_real_sphere_misses_future_player";return r;
}
static Result leap(Attack&a,P iterator,P owner,float scale,float frame,bool stopForAttack=false){
 Result r;int state=read<int>(iterator,0x10);if(state!=1&&state!=2)return r;
 float duration=read<float>(owner,0x854),warmup=read<float>(owner,0x850),elapsed=read<float>(iterator,0x34);
 if(!std::isfinite(duration)||duration<=0||!std::isfinite(warmup)||warmup<0||!std::isfinite(elapsed)||elapsed<0){r.reason="leap_clock_or_elapsed_invalid";return r;}
 // State1 is the Wait(warmup) phase. It may resume a frame late. Reserve input
 // here, but launch only after actual airborne state2 exposes the live timer.
 float remaining=duration-elapsed;
 if(state==1){float now=value<float>(mTime);if(!std::isfinite(now)||!std::isfinite(a.created)||now<a.created){r.reason="leap_warmup_clock_invalid";return r;}remaining=std::max(0.f,warmup-(now-a.created))+duration;}
 V3 center=read<V3>(iterator,0x28);if(!finite(center)){r.reason="leap_target_invalid";return r;}center.y=.05f;
 r=area(center,read<float>(owner,0x830),std::max(0.f,remaining),-1,scale,frame,stopForAttack);
 if(state==1&&r.known)r.reason=r.hit?"leap_warmup_reservation_only":"leap_warmup_misses";
 return r;
}
static Result tackle(P iterator,P owner,float scale,float frame){
 Result r;int state=read<int>(iterator,0x10);if(state!=2){r.reason="tackle_path_not_locked_or_finished";return r;}
 int tackleEffect=16;P effectArgs[]={&tackleEffect};
 if(!read<bool>(owner,0x39)||read<int>(owner,0x490)!=2||!value<bool>(mEffectActive,owner,effectArgs)||read<P>(owner,0x878)||value<bool>(mIncapacitated,owner)){r.known=true;r.reason="tackle_connected_or_cancelled";return r;}
 P hero=read<P>(player,0x1C8),enemy=read<P>(owner,0x1C8),body=read<P>(player,0x190);Capsule capsule,enemyCapsule;
 bool enabled=false;if(!alive(hero)||!alive(enemy)||!alive(body)||!get(colliderEnabled,enemy,nullptr,enabled)||!enabled||!capsuleSnapshot(hero,capsule)||!capsuleSnapshot(enemy,enemyCapsule)||std::fabs(capsule.axis.y)<.9999f||std::fabs(enemyCapsule.axis.y)<.9999f)return r;
 V3 hv={},av={},start=read<V3>(iterator,0x44),target=read<V3>(iterator,0x50),position={};
 float elapsed=read<float>(iterator,0x3C);P curve=read<P>(owner,0x868);int id=0,layer=-1;P go=call(componentGameObject,enemy);
 if(!alive(curve)||!finite(start)||!finite(target)||!finite(position)||!std::isfinite(elapsed)||elapsed<0||elapsed>=.85f||!get(linearVelocity,body,nullptr,hv)||!get(angularVelocity,body,nullptr,av)||!finite(hv)||!finite(av)||dot(av,av)>.00000001f||!get(instanceId,enemy,nullptr,id)||!get(gameObjectLayer,go,nullptr,layer)||layer<0||layer>31)return r;
 ProjectileProbeResult q;float limit=std::min(.46f,.85f-elapsed);V3 facing={},heroPosition={};
 if(!get(mTransformPosition,call(mGetTransform,owner),nullptr,position)||!get(mModelForward,owner,nullptr,facing)||!get(mTransformPosition,call(mGetTransform,player),nullptr,heroPosition)||!finite(position)||!finite(facing)||!finite(heroPosition)||!unit(facing))return r;
 // The coroutine moves with clamp01(tackleMoveCurve(timer/.85)), not constant
 // speed. Sample that exact curve against the actual physical capsule. The
 // interval, rather than a point estimate, is passed to the iframe scheduler.
 for(int n=0;n<24;n++){
  float t=std::min(n*.02f,limit),u=(elapsed+t)/.85f;P args[]={&u};float fraction=0;
  if(!get(curveEvaluate,curve,args,fraction)||!std::isfinite(fraction))return r;
  V3 predicted=add(start,mul(sub(target,start),std::clamp(fraction,0.f,1.f))),offset=sub(mul(hv,t),sub(predicted,position));
  V3 toHero=sub(add(heroPosition,mul(hv,t)),predicted);toHero.y=0;
  // IsInFront measures from the model origin using the model animator's
  // forward, not the root transform. Reverse the boss's predicted translation
  // and delegate its angle/zero-vector rules to that exact game method.
  bool front=dot(facing,toHero)>0;
  if(inFrontMethod){V3 relativeHero=add(heroPosition,offset);float angle=90;P args[]={&relativeHero,&angle};if(!get(inFrontMethod,owner,args,front)){r.reason="tackle_front_query_failed";return r;}}
  int hit=front?ProjectileProbe::overlap(q,add(capsule.a,offset),add(capsule.b,offset),capsule.radius,1<<layer,id):0;
  if(hit<0)return r;if(hit){r.known=r.hit=true;r.first=std::max(0.f,t-.02f-frame)*1000/scale;r.last=(t+frame)*1000/scale;r.reason="tackle_curve_physical_capsules_intersect_in_front";return r;}
  if(t>=limit)break;
 }
 // Discrete samples can miss a brief crossing between them. A miss is proved
 // only when the entire translated hero capsule is outside the full clamped
 // tackle-path bounds; otherwise absence of a sampled hit stays unknown.
 V3 hmove=mul(hv,limit),from=sub(start,position),to=sub(target,position);
 auto separated=[&](float ha,float hb,float move,float ea,float eb,float x,float z){
  float hmin=std::min(ha,hb)-capsule.radius+std::min(0.f,move),hmax=std::max(ha,hb)+capsule.radius+std::max(0.f,move);
  float emin=std::min(ea,eb)-enemyCapsule.radius+std::min(x,z),emax=std::max(ea,eb)+enemyCapsule.radius+std::max(x,z);
  return hmax<emin||emax<hmin;
 };
 r.known=separated(capsule.a.x,capsule.b.x,hmove.x,enemyCapsule.a.x,enemyCapsule.b.x,from.x,to.x)||separated(capsule.a.y,capsule.b.y,hmove.y,enemyCapsule.a.y,enemyCapsule.b.y,from.y,to.y)||separated(capsule.a.z,capsule.b.z,hmove.z,enemyCapsule.a.z,enemyCapsule.b.z,from.z,to.z);
 r.reason=r.known?"tackle_full_path_bounds_miss_player":"tackle_no_confirmed_sample";return r;
}
static bool escapeRoute(V3 direction,float length){
 Capsule c;P collider=read<P>(player,0x1C8);if(!capsuleSnapshot(collider,c))return false;
 BombProbe::Ids own;if(!BombProbe::actorIds(player,own))return false;
 P array=il2cpp_gchandle_get_target(hitHandle);if(!array)return false;
 // A conservative three-sphere cover of the capsule's swept volume. Ignore
 // triggers; any other collider, including a wall on Ground, declines the route.
 float radius=std::sqrt(c.radius*c.radius+c.half*c.half*.25f);int mask=-1,ignoreTriggers=1;
 for(V3 from:{c.a,c.center,c.b}){int count=0;P args[]={&from,&radius,&direction,array,&length,&mask,&ignoreTriggers};
  if(!get(sphereCast,nullptr,args,count)||count<0||count>=64)return false;
  for(int n=0;n<count;n++){int id=read<int>(array,0x20+n*0x2C+0x28);if(!BombProbe::matches(own,id))return false;}
 }
 return true;
}
static void prepareEscape(Result&r,P iterator){
 if(!r.known||!r.hit)return;V3 line=sub(read<V3>(iterator,0x50),read<V3>(iterator,0x44));line.y=0;if(!unit(line))return;
 V3 perpendicular={-line.z,0,line.x};float length=read<float>(player,0x564),duration=value<float>(dashDuration,player),scale,frame;
 if(!std::isfinite(length)||length<=0||!std::isfinite(duration)||duration<=0||duration>1||!clock(scale,frame))return;
 Capsule hero,enemy;P owner=read<P>(iterator,0x28);if(!capsuleSnapshot(read<P>(player,0x1C8),hero)||!capsuleSnapshot(read<P>(owner,0x1C8),enemy)||length<=hero.radius+enemy.radius)return;
 if(dot(perpendicular,sub(hero.center,enemy.center))<0)perpendicular=mul(perpendicular,-1);
 if(!escapeRoute(perpendicular,length)){perpendicular=mul(perpendicular,-1);if(!escapeRoute(perpendicular,length))return;}
 r.escape=perpendicular;r.escapeLead=(duration+frame*2)*1000/scale;
}
static void refreshRhino(RhinoAttack&a,float scale,float frame,bool reserving){
 a.result={};a.lastState=-1;if(reserving)a.reservation={};P self=il2cpp_gchandle_get_target(a.handle),name=il2cpp_gchandle_get_target(a.nameHandle);
 if(!alive(self)||!name){clearRhino(a,"rhino_owner_or_name_absent");return;}
 bool invoking=false;P args[]={name};if(!get(isInvokingMethod,self,args,invoking)){a.result.reason="rhino_invoke_state_unknown";return;}if(!invoking){clearRhino(a,"rhino_invoke_no_longer_pending");return;}
 float now=value<float>(mTime);if(!std::isfinite(now)||!std::isfinite(a.created)||now<a.created){a.result.reason="rhino_invoke_clock_invalid";return;}
 if(now-a.created>a.delay+1){clearRhino(a,"rhino_schedule_expired");return;}
 if(a.ambiguous){a.result.reason="rhino_multiple_pending_slam_invokes";return;}
 bool active=false,dead=false;int state=-1;bool stateKnown=get(rhinoStateMethod,self,nullptr,state);if(stateKnown)a.lastState=state;
 if(!get(behaviourActiveMethod,self,nullptr,active)||!get(mDead,self,nullptr,dead)||!stateKnown||!active||dead||state!=7){a.sampled=a.stationary=false;a.result.reason="rhino_not_active_slam_state";return;}
 P pfx=read<P>(self,0x288);V3 center={};if(!alive(pfx)||!get(mTransformPosition,call(mGetTransform,pfx),nullptr,center)||!finite(center)){a.sampled=a.stationary=false;a.result.reason="rhino_slam_pfx_center_unknown";return;}center.y=0;
 bool equal=a.sampled&&dot(sub(center,a.center),sub(center,a.center))<.00000001f;
 if(!equal)a.stationary=false;else if(now>a.sampledGame)a.stationary=true;
 a.center=center;a.sampled=true;a.sampledGame=now;
 // Slam state has no predicted charge motion. Still require two observations
 // on different game frames of the actual damage-center transform; movement,
 // animation offsets, disabled state, or a changed center invalidate it.
 if(!a.stationary){a.result.reason="rhino_slam_center_not_observed_stationary";return;}
 float remaining=std::max(0.f,a.delay-(now-a.created));
 a.result=area(center,read<float>(self,0x290),remaining,-1,scale,frame);
 if(a.result.known)a.result.reason=a.result.hit?"rhino_pending_slam_sphere_hits_future_player":"rhino_pending_slam_sphere_misses_future_player";
 if(reserving){a.reservation=a.result;auto stopped=area(center,read<float>(self,0x290),remaining,-1,scale,frame,true);if(stopped.known&&stopped.hit)a.reservation=stopped;}
}
static void refresh(bool reserving=false){
 float scale,frame;if(!alive(player)||!clock(scale,frame)){for(auto&a:attacks)if(a.handle){a.result={};a.reservation={};a.result.reason="boss_clock_or_player_unavailable";}for(auto&a:rhinos)if(a.handle){a.result={};a.reservation={};a.result.reason="boss_clock_or_player_unavailable";}return;}
 for(auto&a:attacks){if(!a.handle)continue;P iterator=il2cpp_gchandle_get_target(a.handle);P owner=iterator?read<P>(iterator,a.kind==1?0x20:0x28):nullptr;int state=iterator?read<int>(iterator,0x10):-1;
  if(!iterator||!alive(owner)||value<bool>(mDead,owner)||noteIgnoredHallucinationActor(owner,"boss_prediction")||value<float>(mTime)-a.created>20||(state!=0&&state!=1&&state!=2)||read<int>(owner,0x494)!=(a.kind==1?3:4)){clear(a);continue;}
  Result result=a.kind==1?leap(a,iterator,owner,scale,frame):tackle(iterator,owner,scale,frame);if(a.kind==2)prepareEscape(result,iterator);
  if(result.reason!=a.result.reason||result.known!=a.result.known||result.hit!=a.result.hit){Trace::Json j("boss_geometry");j.integer("correlation_id",a.id);j.str("kind",a.kind==1?"leap_slam":"tackle_contact");j.str("reason",result.reason);j.boolean("known",result.known);j.boolean("intersects",result.hit);j.boolean("parent_health_receiver_lookup",receiverMethod&&receiverTypeHandle);j.number("eta_earliest_ms",result.first);j.number("eta_latest_ms",result.last);j.integer("coroutine_state",state);j.number("blast_radius",result.radius);vectorJson(j,"blast_center",result.center);vectorJson(j,"hero_position",value<V3>(mPosition,player));actorJson(j,"attacker",owner);Trace::enqueue(j);}
  a.lastState=state;a.result=result;
  if(reserving){a.reservation=result;if(a.kind==1){auto stopped=leap(a,iterator,owner,scale,frame,true);if(stopped.known&&stopped.hit)a.reservation=stopped;}}

 }
 for(auto&a:rhinos)if(a.handle){Result previous=a.result;refreshRhino(a,scale,frame,reserving);if(a.handle&&(previous.reason!=a.result.reason||previous.known!=a.result.known||previous.hit!=a.result.hit)){Trace::Json j("boss_geometry");j.integer("correlation_id",a.id);j.str("kind","rhino_slam");j.str("reason",a.result.reason);j.boolean("known",a.result.known);j.boolean("intersects",a.result.hit);j.integer("actual_state",a.lastState);j.number("eta_earliest_ms",a.result.first);j.number("eta_latest_ms",a.result.last);j.number("blast_radius",a.result.radius);vectorJson(j,"blast_center",a.center);vectorJson(j,"hero_position",value<V3>(mPosition,player));Trace::enqueue(j);}}
}
static void act(unsigned&threats,float&nearest,bool&uncertain){
 refresh();DWORD now=GetTickCount();for(auto&a:attacks){if(!a.handle)continue;auto&r=a.result;if(!r.known){uncertain=true;continue;}if(!r.hit)continue;++threats;nearest=std::min(nearest,r.first);
  // Leap Wait timing is not precise enough to create iframes; actual flight is.
  if(a.kind==1&&a.lastState!=2)continue;
  if(a.kind==2){
   // A tackle pins on physical contact even if its damage is immune. Escape its
   // path with a full, ordinary game dash; never equate iframes with pin immunity.
   if(r.escapeLead>=0&&r.first>r.escapeLead)continue;const char*reason=r.escapeLead<0?"no_clear_perpendicular_escape_route":r.first<=0?"physical_contact_has_no_predictive_escape_lead":"toggle_off";bool started=false;
   if(r.escapeLead>=0&&r.first>0&&(config()&2)&&automationReady()&&chargedInterruptionAllowed()){
    struct FacingScope {FacingScope(V3 direction){requestedEscapeFacing=true;escapeFacing=direction;}~FacingScope(){requestedEscapeFacing=false;}} scope(r.escape);
    started=startDodge(reason);
    if(started){lastDefenseId=a.id;lastDefenseTick=now;lastDefenseAction="dodge";}
   }
   if(!a.reported||(DWORD)(now-a.reported)>=250){a.reported=now;Trace::Json j("tackle_escape_decision");j.integer("correlation_id",a.id);j.boolean("dash_started",started);j.boolean("confirmed_evade",false);j.str("reason",reason);j.number("eta_earliest_ms",r.first);j.number("escape_lead_ms",r.escapeLead);vectorJson(j,"direction",r.escape);Trace::enqueue(j);}continue;
  }
  float lead=defensiveDodgeLeadMilliseconds();if(r.first>lead)continue;bool report=!a.reported||(DWORD)(now-a.reported)>=250;if(report)a.reported=now;
  protect(true,nullptr,a.kind==1?"boss_leap_prediction":"boss_tackle_prediction",a.id,report,r.last,false,false,false,r.first);
 }
 for(auto&a:rhinos){if(!a.handle)continue;auto&r=a.result;if(!r.known){uncertain=true;continue;}if(!r.hit)continue;++threats;nearest=std::min(nearest,r.first);if(r.first>defensiveDodgeLeadMilliseconds())continue;bool report=!a.reported||(DWORD)(now-a.reported)>=250;if(report)a.reported=now;protect(true,nullptr,"rhino_slam_prediction",a.id,report,r.last,false,false,false,r.first);}
 for(auto&a:barrels){if(!a.object||(DWORD)(now-a.seen)>=100)continue;auto r=a.result;r.first=std::max(0.f,r.first-(now-a.seen));r.last=std::max(r.first,r.last-(now-a.seen));if(!r.known){uncertain=true;continue;}if(!r.hit)continue;++threats;nearest=std::min(nearest,r.first);if(r.first>defensiveDodgeLeadMilliseconds())continue;bool report=!a.reported||(DWORD)(now-a.reported)>=250;if(report)a.reported=now;protect(true,nullptr,"boss_barrel_fuse_prediction",a.id,report,r.last,false,false,false,r.first);}
}
static void barrelHook(P self,P mi){
 if(!faulted&&hostActive()&&!(config()&8)&&gameWindow()&&alive(self)&&alive(player)){
  AreaAttack*entry=nullptr;for(auto&a:barrels)if(a.object==(uintptr_t)self){entry=&a;break;}
  if(!entry){entry=&barrels[nextBarrel++%64];*entry={};entry->object=(uintptr_t)self;entry->id=nextCombatId();}
  auto&a=*entry;Result previous=a.result;a.seen=GetTickCount();a.result={};
  if(!read<bool>(self,0x108)||read<bool>(self,0x109)){a.sampled=false;a.result.known=true;a.result.reason="barrel_inactive_or_exploded";}
  else {float scale,frame;V3 center={};bool poseKnown=get(mTransformPosition,call(mGetTransform,self),nullptr,center)&&finite(center);float remaining=read<float>(self,0xAC)-read<float>(self,0x10C);
   // An activated barrel is an arena obstacle. Require two consecutive equal
   // positions; if knocked/moved, invalidate the stationary forecast immediately.
   if(poseKnown&&a.sampled&&dot(sub(center,a.center),sub(center,a.center))<.00000001f&&clock(scale,frame))a.result=area(center,read<float>(self,0xA8),remaining,-1,scale,frame);
   a.center=center;a.sampled=poseKnown;
  }
  if(previous.reason!=a.result.reason||previous.known!=a.result.known||previous.hit!=a.result.hit){Trace::Json j("boss_barrel_prediction");j.integer("correlation_id",a.id);j.str("reason",a.result.reason);j.boolean("known",a.result.known);j.boolean("intersects",a.result.hit);j.number("eta_earliest_ms",a.result.first);j.number("eta_latest_ms",a.result.last);vectorJson(j,"blast_center",a.center);Trace::enqueue(j);}
 }
 realBarrelUpdate(self,mi);
 // Original Update may activate and explode the object in this same frame.
 // Its event cannot create a fresh dodge retroactively.
 if(!faulted)for(auto&a:barrels)if(a.object==(uintptr_t)self&&read<bool>(self,0x109)){a.result={};a.result.known=true;a.sampled=false;break;}
}
// Timed projectiles can remain on the ground after impact. Their fuse is liveTimer,
// not projectile flight time. Only a sleeping/kinematic body is stationary enough
// for this forecast; airborne and externally forced bombs stay explicitly unknown.
static BombProbeResult timer(P self){
 BombProbeResult r;r.supported=true;r.reason="timed_aoe_motion_unknown";
 if(!BombProbe::ready||!alive(player)||!read<bool>(self,0xA4)||!read<bool>(self,0xC8)||read<bool>(self,0x17C)||!read<bool>(self,0x1A0)||projectileIsHallucination(self)||!value<bool>(projectileOwner,self)||!at(method(il2cpp_object_get_class(self),"TriggerImpactGameplayEffects",1),0x7642E0))return r;
 P body=read<P>(self,0x140);bool asleep=false,kinematic=false;V3 velocity={};float scale,frame;
 if(!alive(body)||!clock(scale,frame)||!get(sleeping,body,nullptr,asleep)||!get(isKinematic,body,nullptr,kinematic)||!get(linearVelocity,body,nullptr,velocity)||!finite(velocity)||(!asleep&&!kinematic)||dot(velocity,velocity)>.00000001f)return r;
 float remaining=read<float>(self,0xCC)-read<float>(self,0xD0);if(!std::isfinite(remaining)||remaining<0||remaining>15)return r;
 V3 center={};if(!get(mTransformPosition,call(mGetTransform,self),nullptr,center))return r;auto result=area(center,read<float>(self,0xA8),remaining,BombProbe::AoeMask,scale,frame);
 r.known=result.known;r.intersects=result.hit;r.etaMilliseconds=result.first;r.latestMilliseconds=result.last;r.radius=read<float>(self,0xA8);r.blastCenter=center;r.reason=result.known?(result.hit?"timed_aoe_fuse_hits_player":"timed_aoe_fuse_misses_player"):result.reason;return r;
}
static bool init(){
 P enemy=findClass("","Enemy");leapStep=method(nested(enemy,"<Leap>d__287"),"MoveNext",0);tackleStep=method(nested(enemy,"<Tackle>d__318"),"MoveNext",0);
 rhinoClass=findClass("","Rhino");P behaviour=findClass("UnityEngine","MonoBehaviour");invokeMethod=method(behaviour,"Invoke",2);isInvokingMethod=method(behaviour,"IsInvoking",1);rhinoSlamMethod=method(rhinoClass,"GroundSlam",0);rhinoStateMethod=method(rhinoClass,"get_State",0);behaviourActiveMethod=method(findClass("UnityEngine","Behaviour"),"get_isActiveAndEnabled",0);
 curveEvaluate=method(findClass("UnityEngine","AnimationCurve"),"Evaluate",1);
 projectileOwner=method(findClass("","Projectile"),"get_IsOwnerOfThrower",0);
 sleeping=method(findClass("UnityEngine","Rigidbody"),"IsSleeping",0);
 dashDuration=method(findClass("","Player"),"get_AdjustedDashDuration",0);
 transformForward=method(findClass("UnityEngine","Transform"),"get_forward",0);
 inFrontMethod=method(findClass("","NetworkAgent"),"IsInFront",2);
 receiverMethod=method(findClass("UnityEngine","Component"),"GetComponentInParent",2);
 P health=findClass("","NetworkHealthObject"),type=health&&il2cpp_class_get_type&&il2cpp_type_get_object?il2cpp_type_get_object(il2cpp_class_get_type(health)):nullptr;
 if(type&&!receiverTypeHandle)receiverTypeHandle=il2cpp_gchandle_new(type,false);
 return at(leapStep,0xA8FBB0)&&at(tackleStep,0xA90D50)&&at(invokeMethod,0x3706990)&&at(isInvokingMethod,0x3706C00)&&at(rhinoSlamMethod,0xA3BAA0)&&at(rhinoStateMethod,0xA43490)&&at(behaviourActiveMethod,0x36FC970)&&at(curveEvaluate,0x36739E0)&&at(projectileOwner,0x7652E0)&&at(sleeping,0x37EA3E0)&&at(dashDuration,0x7213D0)&&at(transformForward,0x37334A0)&&at(inFrontMethod,0x6A7D40)&&at(receiverMethod,0x36FD3B0)&&receiverTypeHandle&&BombProbe::ready;
}
}
