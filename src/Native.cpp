// Chained Guard: version-gated in-process combat bridge. All Unity calls run on the game thread.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <share.h>
#include "MinHook.h"
#include "Trace.hpp"
#include "GameEnums.hpp"
#include "DefensePolicy.hpp"
using P=void*; struct V3 {float x,y,z;};
static HMODULE game,own;static unsigned char* map;static HANDLE mapping;
static P player=nullptr;static uintptr_t playerHandle=0;static DWORD playerSeen,holdUntil,lastDodge,lastThreat;static bool autoHolding=false,inTick=false,faulted=false;
static uint32_t frames,blocks,dodges;static P directionalClass;
static DWORD arrowSeen,lastImpactDefense;static float arrowMs;static uint32_t arrowWarnings;
template<class T>T read(P o,size_t n){return *(T*)((char*)o+n);}template<class T>T&slot(int n){return *(T*)(map+n);}
static void log(const char*s){Trace::message(s);}
static void textStatus(const char*s){if(map)strncpy_s((char*)map+128,256,s,_TRUNCATE);}
#define DECL(ret,name,args) static ret(*name)args
DECL(P,il2cpp_domain_get,());DECL(P,il2cpp_thread_attach,(P));DECL(void,il2cpp_thread_detach,(P));DECL(P*,il2cpp_domain_get_assemblies,(P,size_t*));
DECL(P,il2cpp_assembly_get_image,(P));DECL(const char*,il2cpp_image_get_name,(P));DECL(P,il2cpp_class_from_name,(P,const char*,const char*));
DECL(P,il2cpp_class_get_method_from_name,(P,const char*,int));DECL(P,il2cpp_class_get_field_from_name,(P,const char*));DECL(size_t,il2cpp_field_get_offset,(P));DECL(void,il2cpp_field_static_get_value,(P,P));
DECL(P,il2cpp_runtime_invoke,(P,P,P*,P*));DECL(P,il2cpp_object_unbox,(P));DECL(P,il2cpp_object_get_class,(P));
DECL(const char*,il2cpp_class_get_name,(P));
DECL(P,il2cpp_class_get_nested_types,(P,P*));
// Unity 6000.3 uses pointer-sized opaque GC handles, unlike older IL2CPP releases.
DECL(bool,il2cpp_class_is_assignable_from,(P,P));DECL(uintptr_t,il2cpp_gchandle_new,(P,bool));DECL(P,il2cpp_gchandle_get_target,(uintptr_t));DECL(void,il2cpp_gchandle_free,(uintptr_t));
DECL(P,il2cpp_array_new,(P,size_t));
DECL(P,il2cpp_class_get_type,(P));DECL(P,il2cpp_type_get_object,(P));
struct InitThreadAttachment {
 P thread=nullptr;
 explicit InitThreadAttachment(P domain):thread(il2cpp_thread_attach(domain)){}
 void detach(){if(thread){il2cpp_thread_detach(thread);thread=nullptr;}}
 ~InitThreadAttachment(){detach();}
};
static P findClass(const char* ns,const char*name){size_t n=0;P*a=il2cpp_domain_get_assemblies(il2cpp_domain_get(),&n);for(size_t i=0;i<n;i++){P k=il2cpp_class_from_name(il2cpp_assembly_get_image(a[i]),ns,name);if(k)return k;}return nullptr;}
static P method(P k,const char*n,int argc){return k?il2cpp_class_get_method_from_name(k,n,argc):nullptr;}
static P call(P m,P obj=nullptr,P*args=nullptr){if(!m)return nullptr;P exc=nullptr;P result=il2cpp_runtime_invoke(m,obj,args,&exc);if(exc){Trace::Json j("managed_call_rejected");j.integer("method_rva",(uintptr_t)*(P*)m-(uintptr_t)game);j.pointer("receiver",obj);if(il2cpp_class_get_name)j.str("exception_class",il2cpp_class_get_name(il2cpp_object_get_class(exc)));Trace::enqueue(j);return nullptr;}return result;}
template<class T>T value(P m,P obj=nullptr,P*args=nullptr,T fallback=T{}){P b=call(m,obj,args);return b?*(T*)il2cpp_object_unbox(b):fallback;}
static bool alive(P o){return o && read<P>(o,0x10)!=nullptr;}
static P mDead,mBlocked,mCanDash,mDash,mIsDashing,mBlockStart,mBlockCancel,mBlockDown,mHasShield,mBlocking,mCanCancelBlock,mDodgeIFrames,mCurrentWeapon,mModelForward,mMeleeRange;
static P mPosition,mCamera,mWorldScreen,mScreenWidth,mScreenHeight,mTime,mGetTransform,mSetForward,mWillHitShield,mProjectileVelocity,mTransformPosition,mFinalMovementSpeed;
static size_t fOwner=0x39,fInput=0x838,fTarget=0x200,fState=0x490,fAttackType=0x494;
struct Pending{uintptr_t handle=0;float dueGame=0,expiresGame=0;bool acted=false;uint64_t id=0;DWORD reported=0;V3 direction={};bool directionLocked=false;int lastGeometry=-1;bool bowResolved=false,reservationLatched=false;DWORD createdTick=0;};static Pending pending[128];
static HWND gameWindow(){HWND w=GetForegroundWindow();DWORD id=0;GetWindowThreadProcessId(w,&id);return id==GetCurrentProcessId()?w:nullptr;}
static bool hostActive(){return map&&slot<uint32_t>(0)==0x43424731&&slot<uint32_t>(4)==1&&(DWORD)(GetTickCount()-slot<uint32_t>(28))<1500;}
static unsigned config(){if(!hostActive())return 0;unsigned a=slot<unsigned>(8);if(a&1)return 0;unsigned f=slot<unsigned>(12);MemoryBarrier();return a==slot<unsigned>(8)?f:0;}
static bool defenseInputAllowed();
static bool automationReady(){unsigned f=config();return !faulted&&hostActive()&&!(f&8)&&gameWindow()&&alive(player)&&!value<bool>(mDead,player)&&defenseInputAllowed();}
static float distance(P a,P b){V3 x=value<V3>(mPosition,a),y=value<V3>(mPosition,b);float dx=x.x-y.x,dz=x.z-y.z;return sqrtf(dx*dx+dz*dz);}
static bool local(P p){return alive(p)&&read<bool>(p,fOwner)&&read<P>(p,fInput);}
#include "CombatTrace.hpp"
#include "DefenseGate.hpp"
#include "AttackProbe.hpp"
#include "ProjectileProbe.hpp"
#include "LaunchForecast.hpp"
#include "HudAnchor.hpp"
static bool (*realBlockDown)(P,P);
static bool blockDownHook(P self,P mi){return (self==player&&autoHolding&&automationReady())||realBlockDown(self,mi);}
static bool physicalBlock(){return realBlockDown&&player&&realBlockDown(player,mBlockDown);}
static void releaseBlock(){if(!autoHolding)return;autoHolding=false;if(alive(player)&&!physicalBlock()){unsigned char ctx[64]={};P args[]={ctx};call(mBlockCancel,player,args);} }
static void safeRelease(){__try{releaseBlock();}__except(EXCEPTION_EXECUTE_HANDLER){autoHolding=false;}}
static void faceIncoming(V3 travel){float len=sqrtf(travel.x*travel.x+travel.z*travel.z);if(len<0.0001f)return;V3 facing={-travel.x/len,0,-travel.z/len};P model=read<P>(player,0x160);if(!alive(model))return;P animator=read<P>(model,0x28);if(!alive(animator))return;P t=call(mGetTransform,animator);if(alive(t)){P args[]={&facing};call(mSetForward,t,args);}}
static void(*realDashStart)(P,P);static P mDashStart;static unsigned automaticDashRequestDepth;
static bool requestedEscapeFacing=false;static V3 escapeFacing={};
struct AutomaticDashRequestScope {AutomaticDashRequestScope(){++automaticDashRequestDepth;}~AutomaticDashRequestScope(){--automaticDashRequestDepth;}};
static bool ownChargedAttackActive(){
 if(!alive(player)||read<int>(player,fState)==0)return false;
 P weapon=call(mCurrentWeapon,player);if(!alive(weapon))return false;
 float charge=read<float>(weapon,0xC4);return std::isfinite(charge)&&charge>0;
}
static bool chargedInterruptionAllowed(){return (config()&64)||!ownChargedAttackActive();}
static bool startBlock(const V3*,const char*&,bool,float);
#include "DodgeBridge.hpp"
#include "DodgeStartup.hpp"
static DefensePolicy::BlockCapacity blockCapacity(){
 bool hasShield=value<bool>(mHasShield,player),shieldOnBack=value<bool>(mShieldOnBack,player);int handSkill=enumValue(mHasSkill,player,2002);
 P w=call(mCurrentWeapon,player);bool weaponPreventsBlock=alive(w)&&value<bool>(method(il2cpp_object_get_class(w),"get_IgnoreHolderBlocks",0),w);
 return DefensePolicy::blockCapacity(hasShield,shieldOnBack,handSkill,weaponPreventsBlock);
}
static ShieldTelemetry shieldTelemetry(){
 ShieldState s=shieldState();ShieldReference reference=shieldReference(s);ShieldTelemetry result;result.type=s.type;result.current=s.durability;result.referenceMax=reference.maximum;result.source=reference.source;result.identity=hudShieldIdentity(s);
 bool physical=value<bool>(mHasShield,player),rawValid=s.data&&s.type>=0&&s.hallucination<0;
 if(physical)result.flags|=1;if(rawValid)result.flags|=2;if(rawValid&&reference.maximum>0)result.flags|=4;
 if(blockCapacity().available)result.flags|=8;if(enumValue(mHasSkill,player,2002)>0)result.flags|=16;if(physical&&shieldNoWear(s))result.flags|=32;
 return result;
}
static void writeDefenseTelemetry(const ShieldTelemetry&s){
 slot<unsigned>(120)=actualDashSequence;slot<unsigned>(124)=confirmedProjectileDodges;
 slot<unsigned>(1536)=2;slot<unsigned>(1540)=s.flags;slot<int>(1544)=s.type;slot<int>(1548)=s.current;slot<int>(1552)=s.referenceMax;
 slot<unsigned>(1556)=actualDashTick;slot<unsigned>(1560)=confirmedProjectileDodgeTick;slot<int>(1564)=s.source;slot<unsigned>(1568)=18;
 slot<unsigned>(1580)=confirmedFullBlocks;slot<unsigned>(1584)=shieldBreakSequence;slot<uint64_t>(1592)=s.identity;slot<uint64_t>(1600)=shieldBreakIdentity;
}
static void(*realShieldUnequip)(P,bool,P);
static void shieldUnequipHook(P self,bool playDestroyPFX,P mi){
 // The RPC also has a send-only path. A true flag alone is insufficient: its
 // original handler must actually remove this exact local shield instance.
 bool localEvent=!faulted&&diagnosticReady&&self==player&&alive(self);
 ShieldState before=localEvent?shieldState():ShieldState{};uint64_t identity=localEvent?hudShieldIdentity(before):0;
 realShieldUnequip(self,playDestroyPFX,mi);
 if(!localEvent)return;ShieldState after=shieldState();hudShieldIdentity(after);
 bool same=after.data==before.data&&after.type==before.type&&after.origin==before.origin;
 if(!playDestroyPFX||!identity||!before.data||same||shieldBreakIdentity==identity)return;
 ++shieldBreakSequence;shieldBreakIdentity=identity;
 Trace::Json j("shield_break_confirmed");j.integer("shield_break_sequence",shieldBreakSequence);j.integer("shield_identity",identity);j.str("proof","UnequipShieldRPC_destroy_flag_and_original_removed_local_shield");Trace::enqueue(j);
}
static bool blockImmunityActive(bool ignoreInvulnerability=false,float etaMs=0){
 int damageImmunityEffect=17;P args[]={&damageImmunityEffect};
 if(value<bool>(mEffectActive,player,args)){
   if(etaMs<=0)return true;
   float remaining=value<float>(mEffectDuration,player,args,-1.f),scale=std::max(0.01f,value<float>(mTimeScale));
   if(!std::isfinite(remaining)||remaining<0||remaining>=etaMs*scale/1000)return true;
 }
 if(ignoreInvulnerability||!value<bool>(mInvulnerable,player))return false;
 // A live dodge iframe is not a promise that an upcoming impact is protected.
 if(etaMs>0&&value<bool>(mDodgeIFrames,player))return currentDodgeCoverage(etaMs,false).skip;
 return true;
}
#include "BombProbe.hpp"
struct BombPrediction {uintptr_t object=0;int generation=0;uint64_t id=0;DWORD seen=0,due=0,latestDue=0,lastDecision=0;bool known=false,intersects=false;const char*reason=nullptr;DWORD reserveDue=0,reserveLatest=0,reserveSeen=0;P reservePlayer=nullptr;};
static void updateBombReservation(BombPrediction&t,bool known,bool intersects,DWORD now){
 if(known&&!intersects){t.reservePlayer=nullptr;t.reserveDue=t.reserveLatest=t.reserveSeen=0;return;}
 if(known&&intersects){t.reserveDue=t.due;t.reserveLatest=t.latestDue;t.reserveSeen=now;t.reservePlayer=player;}
}
static bool bombReserveWindow(const BombPrediction&t,DWORD now,float&ms,float&latest){
 if(!t.object||(DWORD)(now-t.seen)>=100)return false;
 DWORD due=t.due,end=t.latestDue;
 if(!t.known){if(t.reservePlayer!=player||!player||(DWORD)(now-t.reserveSeen)>250||(int)(t.reserveLatest+50-now)<0)return false;due=t.reserveDue;end=t.reserveLatest;}
 else if(!t.intersects)return false;
 ms=std::max(0.f,(float)(int)(due-now));latest=std::max(ms,(float)(int)(end-now));return (int)(end+50-now)>=0;
}
static BombPrediction bombPredictions[128];static unsigned nextBombPrediction;
static thread_local unsigned bombImpactDepth,bombContactDepth;static thread_local P explodingBomb;
static void(*realBombImpact)(P,P,P);
struct BombImpactScope {P previous;bool enabled;explicit BombImpactScope(P self):previous(explodingBomb),enabled(alive(self)&&read<int>(self,0xF0)==100&&read<bool>(self,0xA4)&&!read<bool>(self,0xC8)&&read<int>(self,0x108)<0){if(enabled){explodingBomb=self;++bombImpactDepth;}}~BombImpactScope(){if(enabled){--bombImpactDepth;explodingBomb=previous;}}};
struct BombContactScope {bool enabled;explicit BombContactScope(P self):enabled(alive(self)&&read<int>(self,0xF0)==100&&read<bool>(self,0xA4)&&!read<bool>(self,0xC8)&&read<int>(self,0x108)<0){if(enabled)++bombContactDepth;}~BombContactScope(){if(enabled)--bombContactDepth;}};
static void bombImpactHook(P self,P battleEvent,P mi){BombImpactScope scope(self);if(alive(self)&&read<bool>(self,0xA4)){int generation=read<int>(self,0x178);for(auto&t:bombPredictions)if(t.object==(uintptr_t)self&&t.generation==generation){t.known=true;t.intersects=false;t.due=t.latestDue=0;t.seen=GetTickCount();t.reason="bomb_actual_detonation";updateBombReservation(t,true,false,GetTickCount());}}realBombImpact(self,battleEvent,mi);}
static DWORD blockHoldMilliseconds(float etaMs){
 float lead=std::isfinite(etaMs)?std::clamp(etaMs,0.f,500.f):0.f;return (DWORD)std::max(180.f,std::ceil(lead+100.f));
}
static bool startBlock(const V3*travel,const char*&reason,bool ignoreInvulnerability=false,float etaMs=0){if(!automationReady()){reason="mod_readiness_gate";return false;}
  if(!(config()&32)&&blockImmunityActive(ignoreInvulnerability,etaMs)){reason="player_invulnerable";return false;}
  if(!chargedInterruptionAllowed()){reason="charged_attack_interruption_disabled";return false;}
  auto capacity=blockCapacity();if(!capacity.available){reason=capacity.reason;return false;}if(!value<bool>(mCanCancelBlock,player)){reason="game_cannot_cancel_attack_to_block";return false;}
  int strafeEffect=2;P effectArgs[]={&strafeEffect};bool preexistingStrafe=value<bool>(mEffectActive,player,effectArgs),wasHolding=autoHolding;
  DWORD previousHoldUntil=holdUntil,requestedUntil=GetTickCount()+blockHoldMilliseconds(etaMs);
  autoHolding=true;if(!wasHolding||(int)(requestedUntil-holdUntil)>0)holdUntil=requestedUntil;unsigned char ctx[64]={};P args[]={ctx};call(mBlockStart,player,args);
  bool active=value<bool>(mBlocking,player);if(!active){autoHolding=wasHolding;holdUntil=previousHoldUntil;
    if(DefensePolicy::needsBlockRollback(wasHolding,physicalBlock(),preexistingStrafe,value<bool>(mEffectActive,player,effectArgs)))call(mBlockCancel,player,args);
    reason="game_did_not_activate_block";return false;}
  if(travel){faceIncoming(*travel);P a[]={(P)travel};bool hit=value<bool>(mWillHitShield,player,a);reason=hit?"block_active_facing_hit":"shield_direction_or_game_rule_rejected";return hit;}reason="block_active";return true;
}
static void dashStartHook(P self,P mi){
 if(self==player&&automaticDashRequestDepth&&requestedEscapeFacing){V3 travel={-escapeFacing.x,0,-escapeFacing.z};faceIncoming(travel);}
 realDashStart(self,mi);if(faulted||!diagnosticReady||self!=player||!alive(self)||!value<bool>(mIsDashing,self))return;
 actualDashSequence++;actualDashTick=GetTickCount();observeDodgeState();noteDodgeStartupStart();observeDodgeStartup();Trace::Json j("dash_activated");j.integer("dash_sequence",actualDashSequence);j.str("origin",automaticDashRequestDepth?"automatic_request":"game_or_manual_input");j.boolean("actual_iframes_after_start",value<bool>(mDodgeIFrames,player));j.boolean("confirmed_evade",false);j.str("proof","Player.StartDash_completed_and_IsDashing");Trace::enqueue(j);
}
static bool startDodge(const char*&reason){if(!chargedInterruptionAllowed()){reason="charged_attack_interruption_disabled";return false;}if(!requestDefensiveDodge(reason))return false;lastDodge=GetTickCount();dodges++;slot<uint32_t>(72)=lastDodge;return true;}
static const char* readinessReason(){if(faulted)return "mod_fault_stopped";if(!hostActive())return "host_heartbeat_expired";if(config()&8)return "panel_or_focus_pause";if(!gameWindow())return "game_not_foreground";if(!alive(player))return "player_unavailable";if(value<bool>(mDead,player))return "player_dead";if(!defenseInputAllowed())return "mod_input_blocked_gate";return "ready";}
static bool priorityFullDodgeAvailable();
static bool slowArrowPreference(DefensePolicy::Threat threat){
 // Do not treat standing still, an attack animation, or our own block's speed
 // penalty as a fresh movement impairment. Read the game's available move speed.
 if(!threat.arrow||!alive(player)||autoHolding||physicalBlock()||read<int>(player,0x490)!=0||value<bool>(mIsDashing,player)||value<bool>(DodgeBridgeDetails::strafingMethod,player))return false;
 if(!mFinalMovementSpeed||*(P*)mFinalMovementSpeed!=(char*)game+0x721720)return false;
 return DefensePolicy::slowArrow(threat,value<float>(mFinalMovementSpeed,player,nullptr,std::numeric_limits<float>::quiet_NaN()),read<float>(player,0x548));
}
static bool timelyDodge(float eta,DodgeStartupLead&startup){
 float lead=defensiveDodgeLeadMilliseconds(&startup);
 return startup.measured&&std::isfinite(lead)&&std::isfinite(eta)&&eta>=startup.onsetUpperMilliseconds&&eta<=lead&&(!startup.windowKnown||eta<=startup.latestImpactMilliseconds);
}
static float projectileDefenseDeadline(bool slow,const DodgeStartupLead&startup){
 return slow&&startup.available&&std::isfinite(startup.milliseconds)&&startup.milliseconds>=0?startup.milliseconds:std::clamp(slot<float>(16),20.f,220.f);
}
static float predictiveDefenseDeadline(DefensePolicy::Threat policy){
 return DefensePolicy::predictiveDeadline(config(),policy,blockCapacity().available,value<bool>(mCanCancelBlock,player),slot<int>(24)==1,slot<float>(16),(config()&2)?defensiveDodgeLeadMilliseconds():std::numeric_limits<float>::quiet_NaN());
}
static bool protect(bool unblockable,const V3*travel=nullptr,const char*origin="hit",uint64_t id=0,bool report=true,float eta=-1,bool shieldRisk=false,bool ignoreInvulnerability=false,bool slowArrow=false,float earliestImpact=-1,bool burning=false,bool preContactFlight=false){
 unsigned f=config();const char* gate=readinessReason();const char* blockReason=(f&1)?"not_attempted":"toggle_off";const char* dodgeReason=(f&2)?"not_attempted":"toggle_off";const char* action="none";bool success=false;
 if(strcmp(gate,"ready")==0&&!chargedInterruptionAllowed())gate="charged_attack_interruption_disabled";
 DodgeCoverage coverage=currentDodgeCoverage(eta,ignoreInvulnerability);
 float launchEta=earliestImpact>=0?earliestImpact:eta;
 bool lateFlightAttempt=false;
 auto attemptDodge=[&](){if(bombImpactDepth||bombContactDepth){dodgeReason="bomb_contact_already_executing_no_time_for_new_iframes";return false;}if(!std::isfinite(launchEta)||launchEta<0||(!preContactFlight&&launchEta==0)){dodgeReason="contact_has_no_predictive_lead_for_new_iframes";return false;}
 DodgeStartupLead startup;float lead=defensiveDodgeLeadMilliseconds(&startup);
 if(startup.measured&&launchEta<startup.onsetUpperMilliseconds&&!preContactFlight){dodgeReason="impact_before_observed_iframe_start";return false;}
 if(startup.windowKnown&&eta>startup.latestImpactMilliseconds){dodgeReason="impact_after_observed_iframe_end";return false;}
 if(std::isfinite(lead)&&launchEta>lead){dodgeReason="waiting_for_observed_dodge_launch_window";return false;}
 // A forecast overlap is not an executing damage/contact callback. A rotating
 // projectile can already overlap geometrically yet dispatch its first contact
 // on the next physics step. Request the real dash now instead of categorically
 // refusing a remaining opportunity. Only engine dodge events confirm success.
 lateFlightAttempt=preContactFlight&&(launchEta==0||(startup.measured&&launchEta<startup.onsetUpperMilliseconds));
 return startDodge(dodgeReason);};
 bool preCanBlock=report&&alive(player)?value<bool>(mCanCancelBlock,player):false,preCanDash=report&&alive(player)?value<bool>(mCanDash,player):false;
 if(strcmp(gate,"ready")==0&&(f&3)){
   if(!(f&32)&&blockImmunityActive(ignoreInvulnerability,eta)){action="existing_immunity";blockReason=coverage.skip?coverage.reason:"immunity_covers_impact";dodgeReason="new_defense_not_needed";}
   else {
   bool dodgeFirst=slot<int>(24)==1||unblockable||shieldRisk||slowArrow||burning;
   if(dodgeFirst){if((f&2)&&attemptDodge()){action="dodge";success=true;}else if(!unblockable&&(f&1)&&startBlock(travel,blockReason,ignoreInvulnerability,eta)){action="block";success=true;}}
   else {if((f&1)&&startBlock(travel,blockReason,ignoreInvulnerability,eta)){action="block";success=true;}else if((f&2)&&attemptDodge()){action="dodge";success=true;}}
   }
 }else if(!(f&3)&&strcmp(gate,"ready")==0)gate="automatic_actions_off";
 if(success){lastDefenseId=id;lastDefenseTick=GetTickCount();lastDefenseAction=action;}
 if(report){Trace::Json j("defense_decision");j.integer("correlation_id",id);j.str("origin",origin);j.boolean("own_charged_attack",ownChargedAttackActive());j.boolean("charged_interruption_enabled",(f&64)!=0);j.number("eta_ms",eta);j.number("earliest_eta_ms",launchEta);j.boolean("pre_contact_flight",preContactFlight);j.boolean("late_flight_attempt",lateFlightAttempt);j.integer("flags",f);j.str("gate",gate);j.boolean("unblockable",unblockable);j.boolean("raw_input_blocked",alive(player)&&value<bool>(mBlocked,player));j.boolean("character_lock_permitted_by_game",alive(player)&&value<bool>(mBlocked,player)&&defenseInputAllowed());j.boolean("prefer_dodge_for_fire",burning);j.boolean("prefer_dodge_to_preserve_shield",shieldRisk);j.boolean("prefer_dodge_for_slow_movement",slowArrow);if(slowArrow){j.number("available_movement_speed",value<float>(mFinalMovementSpeed,player));j.number("base_movement_speed",read<float>(player,0x548));}j.boolean("ignore_invulnerability",ignoreInvulnerability);j.str("action",action);j.boolean("activated",success);j.str("block_reason",unblockable?"attack_block_forbidden":blockReason);j.str("dodge_reason",dodgeReason);j.boolean("pre_can_cancel_block",preCanBlock);j.boolean("pre_CanDash",preCanDash);j.boolean("current_dash",coverage.dashing);j.boolean("actual_dodge_iframes",coverage.actualIFrames);j.boolean("iframe_projection_known",coverage.known);j.boolean("current_iframes_cover_impact",coverage.skip);j.number("remaining_iframe_ms",coverage.remainingMs);j.number("iframe_safety_ms",coverage.safetyMs);j.number("dodge_normalized_time",coverage.normalizedTime);j.number("observed_normalized_rate",coverage.observedRate);j.str("iframe_reason",coverage.reason);if(travel)vectorJson(j,"travel",*travel);playerJson(j,"player_after_request");Trace::enqueue(j);}
 return success;
}
#include "BossProbe.hpp"
static BombPrediction& bombPrediction(P object,uint64_t id){
 int generation=read<int>(object,0x178);for(auto&t:bombPredictions)if(t.object==(uintptr_t)object&&t.generation==generation)return t;
 auto&t=bombPredictions[nextBombPrediction++%128];t={};t.object=(uintptr_t)object;t.generation=generation;t.id=id;return t;
}
static void senseBomb(P self,uint64_t id){
 if(!read<bool>(self,0xA4)||(read<int>(self,0xF0)!=100&&!read<bool>(self,0xC8)))return;
 auto&t=bombPrediction(self,id);DWORD now=GetTickCount();t.seen=now;
 if(!hostActive()||(config()&8)||!gameWindow()){t.known=true;t.intersects=false;t.due=t.latestDue=0;t.reason="bomb_sensing_paused";updateBombReservation(t,true,false,now);return;}
 LARGE_INTEGER startCounter,endCounter,frequency;QueryPerformanceCounter(&startCounter);auto r=read<bool>(self,0xC8)?BossProbe::timer(self):probeBomb(self);QueryPerformanceCounter(&endCounter);QueryPerformanceFrequency(&frequency);double probeMicroseconds=frequency.QuadPart>0?(endCounter.QuadPart-startCounter.QuadPart)*1000000.0/frequency.QuadPart:-1;
 if(r.known&&r.intersects&&(!std::isfinite(r.etaMilliseconds)||r.etaMilliseconds<0||r.etaMilliseconds>2147483000.f||!std::isfinite(r.latestMilliseconds)||r.latestMilliseconds<r.etaMilliseconds||r.latestMilliseconds>2147483000.f)){r.known=r.intersects=false;r.reason="bomb_eta_outside_supported_wall_clock_range";}bool changed=t.reason!=r.reason||t.known!=r.known||t.intersects!=r.intersects;
 t.known=r.known;t.intersects=r.known&&r.intersects;t.reason=r.reason;t.due=t.intersects?now+(DWORD)r.etaMilliseconds:0;t.latestDue=t.intersects?now+(DWORD)std::ceil(r.latestMilliseconds):0;updateBombReservation(t,r.known,t.intersects,now);
 DodgeStartupLead startup;float lead=defensiveDodgeLeadMilliseconds(&startup);
 if(changed){Trace::Json j("bomb_aoe_prediction");j.integer("correlation_id",id);j.boolean("known",r.known);j.boolean("intersects",r.intersects);j.str("reason",r.reason);j.number("eta_earliest_ms",r.etaMilliseconds);j.number("eta_latest_ms",r.latestMilliseconds);j.number("blast_radius",r.radius);j.number("sphere_radius",r.sphereRadius);j.number("fixed_game_seconds",r.frameSeconds);j.integer("physics_queries",r.queries);j.number("probe_cpu_microseconds",probeMicroseconds);j.integer("contact_collider_id",r.contactId);j.integer("contact_layer",r.contactLayer);j.boolean("contact_trigger",r.contactTrigger);vectorJson(j,"predicted_blast_center",r.blastCenter);vectorJson(j,"hero_velocity_model",r.heroVelocity);vectorJson(j,"projectile_velocity_model",r.projectileVelocity);j.number("dodge_launch_lead_ms",lead);j.str("dodge_startup_reason",startup.reason);j.boolean("startup_reliable",startup.reliable);j.boolean("direct_contact_already_recorded",projectileTrace(self).contact);j.boolean("direct_contact_already_evaded",listContains(read<P>(self,0x198),player));projectileJson(j,self);Trace::enqueue(j);}
 if(!t.intersects||!std::isfinite(lead)||lead<0||r.etaMilliseconds>lead)return;
 // A sampled entry bound differs from the target launch lead. Crossing a
 // 90ms target at84ms still permits a measured32ms startup; no equality gate.
 const char* declined=nullptr;
 if(startup.measured&&r.etaMilliseconds<startup.onsetUpperMilliseconds)declined="predicted_blast_before_observed_iframe_start";
 if(startup.windowKnown&&r.latestMilliseconds>startup.latestImpactMilliseconds)declined="predicted_blast_after_observed_iframe_end";
 bool report=t.lastDecision==0||(DWORD)(now-t.lastDecision)>=250;if(report)t.lastDecision=now;
 if(declined){if(report){Trace::Json j("bomb_aoe_deadline_unavailable");j.integer("correlation_id",id);j.str("reason",declined);j.number("eta_earliest_ms",r.etaMilliseconds);j.number("eta_latest_ms",r.latestMilliseconds);j.number("onset_upper_ms",startup.onsetUpperMilliseconds);j.number("iframe_end_lower_ms",startup.latestImpactMilliseconds);j.boolean("activated",false);Trace::enqueue(j);}return;}
 protect(true,nullptr,"bomb_aoe_prediction",id,report,r.latestMilliseconds,false,false,false,r.etaMilliseconds);
}
static bool attackTargetsPlayer(P attacker){if(!alive(attacker)||!alive(player)||attacker==player||value<bool>(mDead,attacker)||noteIgnoredHallucinationActor(attacker,"target_filter"))return false;
 return read<P>(attacker,fTarget)==player;}
static bool attackActorActive(P a){return alive(a)&&a!=player&&!value<bool>(mDead,a)&&!noteIgnoredHallucinationActor(a,"attack_geometry")&&(read<int>(a,fState)==1||read<int>(a,fState)==2)&&read<int>(a,fAttackType)!=2;}
static bool pendingCharge(P a){P w=call(mCurrentWeapon,a);return alive(w)&&((apKind(w,apChargeClass)&&read<float>(w,0xC4)>0)||apAxeChargeActive(w))&&attackActorActive(a);}
static ReachProbeResult pendingReach(Pending&p,P a,bool reserving=false){
 P w=call(mCurrentWeapon,a);
 if(p.directionLocked&&alive(w)&&!read<bool>(w,0x1B0)&&value<float>(mTime)>=p.dueGame&&!pendingCharge(a)&&listContains(read<P>(w,0x1B8),player)){
   ReachProbeResult consumed;consumed.known=true;consumed.reason="melee_target_already_consumed_by_this_attack";return consumed;
 }
 V3 direction=p.directionLocked?p.direction:value<V3>(mModelForward,a);auto snapshot=meleeProbe(a,w,direction);
 if(!snapshot.known)return snapshot;
 bool charge=pendingCharge(a);if(charge&&snapshot.intersects){snapshot.projected=true;snapshot.etaMilliseconds=0;return snapshot;}
 float seconds=p.dueGame-value<float>(mTime);
 // Ordinary strikes only damage at release. A present overlap is neither a
 // future hit nor a future miss when either participant is moving.
 auto unknownFuture=[&](const char*reason){snapshot.known=false;snapshot.intersects=false;snapshot.reason=reason;return snapshot;};
 if(!charge&&(!std::isfinite(seconds)||seconds>.6f))return unknownFuture("melee_future_impact_outside_forecast");
 if(!charge&&seconds<=0)return snapshot;
 P rb=read<P>(a,0x190),hero=read<P>(player,0x190);V3 av={},hv={};float scale=value<float>(mTimeScale);
 if(!alive(rb)||!alive(hero)||!ProjectileProbe::linearVelocity||!apValue(ProjectileProbe::linearVelocity,rb,nullptr,av)||!apValue(ProjectileProbe::linearVelocity,hero,nullptr,hv)||!apFinite(av)||!apFinite(hv)||!std::isfinite(scale)||scale<=0)return charge?snapshot:unknownFuture("melee_future_motion_unavailable");
 V3 relative={av.x-hv.x,av.y-hv.y,av.z-hv.z};float speed2=relative.x*relative.x+relative.y*relative.y+relative.z*relative.z;
 if(!charge){
   // An ordinary strike damages at release, not when a moving hero first enters
   // its present shape. Preserve that ETA and query the real shape at impact.
   auto future=meleeProbe(a,w,direction,apMul(relative,seconds));
   if(future.known&&future.intersects){future.reason="melee_hits_at_impact_under_current_relative_motion";return future;}
   // Starting our own attack may stop current movement. Reserve this additional
   // actual pose only before accepting input, never turn a broad radius into a hit.
   if(reserving){auto stopped=meleeProbe(a,w,direction,apMul(av,seconds));if(!stopped.known)return stopped;if(stopped.intersects){stopped.reason="melee_hits_if_new_attack_stops_hero";return stopped;}}
   return future;
 }
 snapshot.projected=true;snapshot.etaMilliseconds=0;
 if(speed2<.000001f)return snapshot;
 // Bounded constant-current-motion forecast; no transform writes or invented
 // radius. 20ms samples of the actual charge capsule, at most30 queries.
 for(int n=1;n<=30;n++){float seconds=n*.02f;auto future=meleeProbe(a,w,direction,apMul(relative,seconds));if(!future.known){future.projected=true;return future;}if(future.intersects){future.projected=true;future.etaMilliseconds=(n-1)*20.f/scale;future.reason="charge_capsule_hits_under_current_relative_motion";return future;}}
 return snapshot;
}
#include "ArrowPriority.hpp"
static void logReach(Pending&p,P a,const ReachProbeResult&r,float ms,bool force=false){int state=r.known?(r.intersects?1:0):2;if(!force&&p.lastGeometry==state)return;p.lastGeometry=state;Trace::Json j("melee_geometry");j.integer("correlation_id",p.id);j.boolean("known",r.known);j.boolean("intersects",r.intersects);j.str("reason",r.reason);j.number("eta_ms",ms);j.number("cast_distance",r.range);j.number("radius",r.radius);j.number("max_distance",r.maxDistance);j.number("start_offset",r.startOffset);j.integer("hit_count",r.hitCount);j.boolean("attack_direction_locked",p.directionLocked);vectorJson(j,"hero_position",value<V3>(mPosition,player));j.integer("hero_attack_state",read<int>(player,fState));vectorJson(j,"origin",r.origin);vectorJson(j,"direction",r.direction);actorJson(j,"attacker",a);Trace::enqueue(j);}
static void(*realWeaponAttack)(P,V3*,P);
static float weaponEffectDelay(P weapon){
 if(!alive(weapon))return 0;
 float delay=read<float>(weapon,0x5C),visual=0;
 bool senate=apMethodAt(method(il2cpp_object_get_class(weapon),"AttackEffect",13),0x7EE140);
 if(senate&&read<int>(weapon,0x180)==2)delay+=.15f;
 if(read<float>(weapon,0xC4)>0){
   visual=read<float>(weapon,0xC0);if(!senate)delay+=read<float>(weapon,0x60);
 }
 if(!std::isfinite(delay)||!std::isfinite(visual))return std::numeric_limits<float>::quiet_NaN();
 return std::max(0.f,delay)+std::max(0.f,visual);
}
#include "FinisherProbe.hpp"
static void scheduleAttackEffect(P self,const V3*direction,float delay,const char*source){
 if(!direction||!alive(self)||!apFinite(*direction)||!std::isfinite(delay))return;
 P holder=read<P>(self,0x178);if(holder==player){Finisher::scheduled(self,delay);return;}
 float nowGame=value<float>(mTime);if(!std::isfinite(nowGame))return;
 for(auto&p:pending)if(p.handle&&il2cpp_gchandle_get_target(p.handle)==holder){
   p.direction=*direction;p.directionLocked=true;p.dueGame=nowGame+std::max(0.f,delay);p.expiresGame=p.dueGame+.35f;p.acted=false;
   Trace::Json j("attack_effect_scheduled");j.integer("correlation_id",p.id);j.str("source",source);j.number("actual_delay_seconds",delay);j.number("due_game_time",p.dueGame);vectorJson(j,"direction",p.direction);Trace::enqueue(j);
 }
}
static void weaponAttackHook(P self,V3*direction,P mi){
 if(alive(self)&&!read<bool>(self,0x1B0)){
   float delay=weaponEffectDelay(self);
   scheduleAttackEffect(self,direction,delay,"Weapon.Attack");
 }
 realWeaponAttack(self,direction,mi);
}
static P(*realAttackAfterDelay)(P,V3*,float,float,P);
static P attackAfterDelayHook(P self,V3*direction,float delay,float visuals,P mi){
 if(std::isfinite(delay)&&std::isfinite(visuals))scheduleAttackEffect(self,direction,std::max(0.f,delay)+std::max(0.f,visuals),"Weapon.AttackAfterDelay");
 return realAttackAfterDelay(self,direction,delay,visuals,mi);
}
static void (*realInit)(P,double,float,P,int,bool,P);
static void initHook(P self,double serverStart,float windup,P weapon,int mode,bool down,P mi){
 // A proven normal finisher is a tap, even while the physical input is held.
 // Otherwise RunWindupLoop can turn the planned hit into indefinite charging.
 bool finishTap=self==player&&mode==0&&Finisher::candidate.allowed&&Finisher::candidate.owner==self&&Finisher::candidate.weapon==weapon;
 realInit(self,serverStart,windup,weapon,mode,finishTap?false:down,mi);
 if(faulted||!map||!alive(self))return;if(self==player){Finisher::started(self,weapon,windup,mode);return;}if(noteIgnoredHallucinationActor(self,"attack_begin"))return;noteUnit(self);float gameNow=value<float>(mTime);float latency=(float)std::clamp(read<double>(self,0x508),0.,5.);float delay=weaponEffectDelay(weapon);if(!std::isfinite(gameNow)||!std::isfinite(windup)||!std::isfinite(latency)||!std::isfinite(delay))return;float remaining=std::clamp(windup-latency+delay,0.f,5.f);
 for(auto&p:pending)if(p.handle&&il2cpp_gchandle_get_target(p.handle)==self){il2cpp_gchandle_free(p.handle);p={};}
 for(auto&p:pending)if(!p.handle||gameNow>=p.expiresGame){if(p.handle)il2cpp_gchandle_free(p.handle);p={};p.handle=il2cpp_gchandle_new(self,false);p.createdTick=GetTickCount();p.dueGame=gameNow+remaining;p.expiresGame=p.dueGame+0.35f;p.id=nextCombatId();
   if(attackTargetsPlayer(self)){Trace::Json j("attack_begin");j.integer("correlation_id",p.id);j.number("windup_seconds",windup);j.number("latency_seconds",latency);j.number("effect_delay_seconds",delay);j.number("due_game_time",p.dueGame);actorJson(j,"attacker",self);Trace::enqueue(j);}break;}}
using DamageFn=P(*)(P,float,int,int,P,bool,bool,bool,bool,P);static DamageFn realDamage;
static thread_local unsigned ignoredProjectileDamageDepth=0;
struct IgnoredProjectileDamageScope {IgnoredProjectileDamageScope(){++ignoredProjectileDamageDepth;}~IgnoredProjectileDamageScope(){--ignoredProjectileDamageDepth;}};
static P damageHook(P self,float amount,int smash,int type,P source,bool a,bool b,bool c,bool d,P mi){
 if(!faulted&&diagnosticReady&&self!=player&&Finisher::lease.target&&il2cpp_gchandle_get_target(Finisher::lease.target)==self){P result=realDamage(self,amount,smash,type,source,a,b,c,d,mi);Finisher::hitResult(self,source,result);return result;}
 if(faulted||!diagnosticReady||self!=player||ignoredProjectileDamageDepth)return realDamage(self,amount,smash,type,source,a,b,c,d,mi);
 bool directional=source&&directionalClass&&il2cpp_class_is_assignable_from(directionalClass,il2cpp_object_get_class(source));
 P dealer=source?call(method(il2cpp_object_get_class(source),"get_DamageDealer",0),source):nullptr;
 if(noteIgnoredHallucinationActor(dealer,"damage_prefix"))return realDamage(self,amount,smash,type,source,a,b,c,d,mi);noteUnit(dealer);uint64_t id=nextCombatId();
 ShieldState before=shieldState();refreshInjuries();
 {Trace::Json j("damage_before");j.integer("correlation_id",id);j.str("source_class",className(source));j.boolean("directional",directional);j.number("requested_damage",amount);j.integer("damage_type",type);j.str("damage_name",GameEnums::name(GameEnums::DamageType,type));j.integer("smash",smash);j.boolean("interrupt",a);j.boolean("nonfatal",b);j.boolean("allow_executions",c);j.boolean("ignore_invulnerability",d);actorJson(j,"dealer",dealer);
 if(directional){j.boolean("melee",read<bool>(source,0x10));j.boolean("unblockable",read<bool>(source,0x44));j.boolean("insta_smash_shield",read<bool>(source,0x45));vectorJson(j,"origin",read<V3>(source,0x20));vectorJson(j,"travel",read<V3>(source,0x2C));}
 j.integer("recent_defense_id",(DWORD)(GetTickCount()-lastDefenseTick)<2000?lastDefenseId:0);j.str("recent_action",lastDefenseAction);playerJson(j,"player");Trace::enqueue(j);}
 if(directional&&amount>0){lastThreat=lastImpactDefense=GetTickCount();V3 travel=read<V3>(source,0x2C);
   if(bombImpactDepth&&read<bool>(source,0x44)){Trace::Json j("bomb_aoe_at_damage_boundary");j.integer("correlation_id",id);j.pointer("projectile",explodingBomb);j.boolean("actual_iframes",value<bool>(mDodgeIFrames,player));j.boolean("invulnerable",value<bool>(mInvulnerable,player));j.boolean("new_dash_requested",false);j.str("reason","instant_explosion_already_executing_new_dash_cannot_enter_iframes_in_this_call");Trace::enqueue(j);}
   else protect(read<bool>(source,0x44),&travel,"damage_prefix",id,true,0,read<bool>(source,0x45),d);}
 P result=realDamage(self,amount,smash,type,source,a,b,c,d,mi);
 int blocked=result?read<int>(result,0x1C):0,damage=result?read<int>(result,0x18):0;
 if(result&&blocked>0&&damage==0){++confirmedFullBlocks;Trace::Json j("full_block_confirmed");j.integer("correlation_id",id);j.integer("block_sequence",confirmedFullBlocks);j.integer("blocked",blocked);j.str("proof","engine_damage_result_blocked_positive_and_damage_zero");Trace::enqueue(j);}
 if(map&&result){slot<unsigned>(92)=std::max(damage,0);if(damage>0)slot<unsigned>(96)=GetTickCount();if(blocked>0){blocks++;slot<unsigned>(88)=GetTickCount();slot<unsigned>(72)=GetTickCount();}}
 {Trace::Json j("damage_result");j.integer("correlation_id",id);j.str("source_class",className(source));j.boolean("directional",directional);j.boolean("engine_result_present",result!=nullptr);j.integer("damage",damage);j.integer("blocked",blocked);j.str("outcome",blocked>0?(damage>0?"partial_block":"confirmed_block"):(damage>0?"damage_taken":"zero_damage_not_proof_of_dodge"));playerJson(j,"player");Trace::enqueue(j);}
 logShieldTransition(before,shieldState(),id);
 return result;
}
static void(*realProjectileUpdate)(P,P);
static void(*realProjectileHit)(P,P,P);
static void projectileHitHook(P self,P target,P mi){
 if(faulted||!diagnosticReady||target!=player||!alive(self)||read<P>(self,0x118)==player){realProjectileHit(self,target,mi);return;}
 if(noteIgnoredHallucinationProjectile(self,"projectile_contact")){IgnoredProjectileDamageScope ignored;realProjectileHit(self,target,mi);return;}
 BombContactScope contactScope(self);
 auto&t=projectileTrace(self);bool first=!t.contact;t.contact=true;bool dodgedBefore=listContains(read<P>(self,0x198),player);int generation=read<int>(self,0x178);ShieldState before=shieldState();noteUnit(read<P>(self,0x118));
 if(first){Trace::Json j("projectile_contact_begin");j.integer("correlation_id",t.id);projectileJson(j,self);actorJson(j,"thrower",read<P>(self,0x118));playerJson(j,"player");Trace::enqueue(j);}
 lastThreat=lastImpactDefense=GetTickCount();V3 travel=value<V3>(mProjectileVelocity,self);auto policy=DefensePolicy::projectile(read<int>(self,0xF0),read<bool>(self,0x1A1),read<int>(self,0x9C),read<int>(self,0x98),read<bool>(self,0xA4));protect(policy.unblockable,&travel,"projectile_contact_prefix",t.id,first,0,policy.shieldRisk,false,false,-1,policy.burning);realProjectileHit(self,target,mi);
 bool confirmed=alive(self)&&read<int>(self,0x178)==generation&&!dodgedBefore&&listContains(read<P>(self,0x198),player);
 if(confirmed){confirmedProjectileDodges++;confirmedProjectileDodgeTick=GetTickCount();Trace::Json j("projectile_dodge_confirmed");j.integer("correlation_id",t.id);j.str("proof","game_added_player_to_projectile_dodgedAgents");projectileJson(j,self);playerJson(j,"player");Trace::enqueue(j);}
 if(first){Trace::Json j("projectile_contact_end");j.integer("correlation_id",t.id);j.boolean("confirmed_dodge",confirmed);projectileJson(j,self);playerJson(j,"player");Trace::enqueue(j);}
 resolveBowShot(self,t);logShieldTransition(before,shieldState(),t.id);
}
static void senseProjectile(P self,const char*phase){if(faulted||!map||!alive(player)||!alive(self))return;
 if(noteIgnoredHallucinationProjectile(self,"projectile_update"))return;
 P thrower=read<P>(self,0x118);if(thrower==player)return;auto&t=projectileTrace(self);senseBomb(self,t.id);
 if(!read<bool>(self,0x1A0)||read<bool>(self,0x158)||read<bool>(self,0x15A)||read<bool>(self,0x17C)){t.arrowKnown=true;t.arrowIntersects=false;if(read<bool>(self,0x1A0))resolveBowShot(self,t);return;}
 if(!hostActive()||(config()&8)||!gameWindow()||t.contact||listContains(read<P>(self,0x198),player)){t.intersects=false;t.geometryKnown=true;t.due=0;return;}
 updateArrowPriority(self,t);auto probe=probeProjectile(self);if(!probe.known)probe=probeLaunchForecast(self,probe);DWORD now=GetTickCount();bool intersects=probe.known&&probe.intersects&&std::isfinite(probe.etaMilliseconds)&&probe.etaMilliseconds>=0;
 float latest=std::max(probe.etaMilliseconds,probe.etaLatestMilliseconds);
 bool changed=t.intersects!=intersects||t.lastPredictionReason!=probe.reason;t.intersects=intersects;t.geometryKnown=probe.known;t.lastPredictionReason=probe.reason;t.due=intersects?now+(DWORD)std::min(probe.etaMilliseconds,120000.f):0;t.latestDue=intersects?now+(DWORD)std::ceil(std::min(latest,120000.f)):0;
 if(changed){noteUnit(thrower);Trace::Json j("projectile_threat");j.integer("correlation_id",t.id);j.number("eta_ms",probe.etaMilliseconds);j.number("eta_latest_ms",latest);j.boolean("geometry_known",probe.known);j.boolean("intersects",intersects);j.str("geometry_reason",probe.reason);j.str("sample_phase",phase);j.integer("physics_queries",probe.queries);vectorJson(j,"hero_velocity",probe.heroVelocity);vectorJson(j,"projectile_velocity",probe.projectileVelocity);j.boolean("cannot_block_safely",projectileNoBlock(self));projectileJson(j,self);actorJson(j,"thrower",thrower);Trace::enqueue(j);}
 if(intersects){lastThreat=now;float ms=probe.etaMilliseconds;auto policy=DefensePolicy::projectile(read<int>(self,0xF0),read<bool>(self,0x1A1),read<int>(self,0x9C),read<int>(self,0x98),read<bool>(self,0xA4));bool slow=(config()&2)&&slowArrowPreference(policy)&&priorityFullDodgeAvailable();DodgeStartupLead startup;bool timely=slow&&timelyDodge(ms,startup);float deadline=slow?projectileDefenseDeadline(slow,startup):predictiveDefenseDeadline(policy);if(ms<=deadline){bool report=t.lastDecision==0||(DWORD)(now-t.lastDecision)>=250;if(report)t.lastDecision=now;if(report&&slow){Trace::Json j("slow_arrow_timing");j.integer("correlation_id",t.id);j.number("eta_ms",ms);j.number("launch_lead_ms",startup.milliseconds);j.number("onset_upper_ms",startup.onsetUpperMilliseconds);j.boolean("measured",startup.measured);j.boolean("timely_dodge_preferred",timely);Trace::enqueue(j);}V3 travel=probe.projectileVelocity;protect(policy.unblockable,&travel,"projectile_prediction",t.id,report,latest,policy.shieldRisk,false,timely,ms,policy.burning,true);}}
}
// Observe a completed launch and collider enable before waiting for Update.
static void projectileHook(P self,P mi){realProjectileUpdate(self,mi);senseProjectile(self,"update");}
static void(*realProjectileFixed)(P,P);
static void projectileFixedHook(P self,P mi){realProjectileFixed(self,mi);if(!faulted&&map&&alive(self)&&read<bool>(self,0x1A0)&&!read<bool>(self,0x158)&&!read<bool>(self,0x15A)&&!read<bool>(self,0x17C))senseProjectile(self,"fixed_update");}
using ProjectileLaunchFn=void(*)(P,V3*,float,float,float,bool,P,int,int,int,uint64_t,int,float,bool,bool,bool,P);
static ProjectileLaunchFn realProjectileLaunch;
static void projectileLaunchHook(P self,V3*direction,float force,float angle,float gravity,bool server,P thrower,int type,int durability,int model,uint64_t origin,int hallucination,float damageScale,bool ignoreObstacles,bool undirectedSpin,bool fire,P mi){
 realProjectileLaunch(self,direction,force,angle,gravity,server,thrower,type,durability,model,origin,hallucination,damageScale,ignoreObstacles,undirectedSpin,fire,mi);
 senseProjectile(self,"launch");
}
static void(*realTryAttack)(P,P);static P mTryAttack,mGrappled,mAttackDown;
// Preserve only a currently held game input, never an OS click or a released
// request. TryAttack can be rejected before the game's own queue is populated.
static P heldAttackOwner;
static bool heldAttackIntent;
static unsigned heldAttackAttemptFrame=~0u;
static bool heldAttackEnabled(){return (config()&16)&&(config()&3)&&automationReady();}
static void clearHeldAttack(){heldAttackOwner=nullptr;heldAttackIntent=false;heldAttackAttemptFrame=~0u;}
static DWORD priorityLastLog;static unsigned prioritySuppressed;
struct PriorityTiming {float windup=0,winddown=0,cancelOffset=0,scale=1,fullLock=0,cancelLock=0,dodgeLead=0;bool armShield=false,dodgeAvailable=false;const char*defense="none";};
static PriorityTiming priorityTiming;
static DWORD priorityEpisodeStart;static uint64_t priorityEpisodeId;static unsigned priorityEpisodeSuppressed;
static bool priorityFullDodgeAvailable(){
 if(!DodgeBridgeDetails::ready||physicalBlock()||value<bool>(mIsDashing,player,nullptr,true))return false;
 int tiny=13;P args[]={&tiny};bool strafing=value<bool>(DodgeBridgeDetails::strafingMethod,player,nullptr,true);
 // requestDefensiveDodge can release only our synthetic hold, never manual RMB
 // or a pre-existing strafe. This is the same read-only full-dash preflight.
 return DodgePolicy::fullDashAvailable(value<bool>(mCanDash,player),strafing&&!autoHolding,read<float>(player,0x5E4),value<float>(mStaminaCost,player,nullptr,std::numeric_limits<float>::quiet_NaN()),value<int>(mHasInjury,player,args,-1)).available;
}
static bool reserveDefense(uint64_t&threatId,float&eta,float&window){
 Finisher::candidate={};priorityTiming={};eta=999999;window=0;threatId=0;
 unsigned f=config();if(!(f&16)||!(f&3)||!automationReady()||read<int>(player,0x490)!=0||read<bool>(player,0x798)||read<bool>(player,0x510)||value<bool>(mGrappled,player)||value<bool>(mWaitingGrapple,player))return false;
 auto capacity=blockCapacity();if(!DefensePolicy::canReserve(f,capacity.available,{}))return false;
 P w=call(mCurrentWeapon,player);if(!alive(w))return false;P k=il2cpp_object_get_class(w);float unknown=std::numeric_limits<float>::quiet_NaN();
 auto&t=priorityTiming;t.windup=value<float>(method(k,"get_WindupDuration",0),w,nullptr,unknown);t.winddown=value<float>(method(k,"get_WindDownDuration",0),w,nullptr,unknown);t.cancelOffset=value<float>(method(k,"get_CanCancelAttackTimingOffset",0),w,nullptr,unknown);t.scale=value<float>(mTimeScale,nullptr,nullptr,unknown);
 if(!std::isfinite(t.windup)||!std::isfinite(t.winddown)||!std::isfinite(t.cancelOffset)||!std::isfinite(t.scale)||t.scale<=0)return false;
 t.armShield=value<bool>(mHasShield,player)&&!value<bool>(mShieldOnBack,player);t.dodgeLead=(f&2)?defensiveDodgeLeadMilliseconds():0;t.dodgeAvailable=(f&2)&&priorityFullDodgeAvailable()&&std::isfinite(t.dodgeLead)&&t.dodgeLead>=0;t.fullLock=DefensePolicy::reserveMilliseconds(t.windup,t.winddown,t.cancelOffset,false,t.scale);t.cancelLock=DefensePolicy::reserveMilliseconds(t.windup,t.winddown,t.cancelOffset,true,t.scale);
 auto windowFor=[&](DefensePolicy::Threat threat,float ms,const char*&defense,float immunityEta,bool preferDodge=false){
   if(!(f&32)&&blockImmunityActive(false,immunityEta))return 0.f;
   float candidate=0;defense="none";
   float frame=value<float>(DodgeBridgeDetails::deltaTimeMethod),padding=std::isfinite(frame)&&frame>0&&frame<=.1f?2*frame/t.scale*1000:0;
   if((f&1)&&capacity.available&&!threat.unblockable&&!threat.explosive){candidate=t.armShield?t.cancelLock:t.fullLock;defense=t.armShield?"shield_cancel_window":"hand_block_waits_attack_end";}
   float dodgeWindow=t.cancelLock+t.dodgeLead+((threat.unblockable||threat.explosive)?padding:0);bool preserveDodge=t.dodgeAvailable||((f&2)&&!physicalBlock()&&(threat.unblockable||threat.explosive||threat.arrow));if(preserveDodge&&(preferDodge||threat.shieldRisk||threat.burning||candidate<=0||dodgeWindow<candidate)){candidate=dodgeWindow;defense=preferDodge?"slow_arrow_dodge_cancel_plus_iframe_startup_window":"dodge_cancel_plus_iframe_startup_window";}
   // A release/coroutine/collider-enable handoff can outlast the theoretical
   // first cancel instant. For ranged reservations use the whole own swing plus
   // two actual game updates; this does not hold block or create earlier iframes.
   if(threat.arrow&&candidate>0)candidate=std::max(candidate,t.fullLock+t.dodgeLead+padding);
   return candidate;
 };
 float nowGame=value<float>(mTime);DWORD now=GetTickCount();
 for(auto&p:pending){if(!p.handle)continue;P a=il2cpp_gchandle_get_target(p.handle);float ms=(p.dueGame-nowGame)/t.scale*1000;P bow=alive(a)?call(mCurrentWeapon,a):nullptr;
 if(!p.bowResolved&&rangedWindup(a,bow)&&(attackTargetsPlayer(a)||p.reservationLatched)&&nowGame<p.expiresGame&&(read<int>(a,fState)==1||read<int>(a,fState)==2||ms<=0)){
   DefensePolicy::Threat launch;launch.arrow=true;P data=read<P>(bow,0x170);launch.unblockable=data&&read<int>(data,0x10)==100;
   float candidate=windowFor(launch,ms,t.defense,std::max(0.f,ms));
   if(candidate>0&&(ms<=candidate||p.reservationLatched)&&std::max(0.f,ms)<eta){p.reservationLatched=true;eta=std::max(0.f,ms);window=candidate;threatId=p.id;t.defense=supportedBow(bow)?"bow_windup_reservation_until_flight_or_contact":"throw_windup_reservation_until_flight_or_contact";}
   continue;
 }
 if(attackActorActive(a)){
   auto reach=pendingReach(p,a,true);if(reach.projected)ms=reach.etaMilliseconds;if(!reach.known||!reach.intersects||ms< -50||ms>=eta)continue;
   P attackWeapon=call(mCurrentWeapon,a);DefensePolicy::Threat threat;if(alive(attackWeapon))threat=DefensePolicy::weapon(read<bool>(attackWeapon,0x3C),read<bool>(attackWeapon,0x3D),read<float>(attackWeapon,0xC4),read<bool>(attackWeapon,0x3E),read<bool>(attackWeapon,0xF8),read<int>(attackWeapon,0xEC),read<int>(attackWeapon,0xF4));
   const char*defense;float candidate=windowFor(threat,ms,defense,ms);if(candidate>0&&ms<=candidate){if(Finisher::skipReservation(p,a,t.windup,t.scale))continue;eta=ms;window=candidate;threatId=p.id;t.defense=defense;}}}
 for(const auto&projectile:projectileTraces){float arrowMs,arrowLast;
 if(arrowReserveWindow(projectile,now,arrowMs,arrowLast)&&arrowMs<eta){const char*basis;auto policy=projectile.policy;policy.arrow=true;float candidate=windowFor(policy,arrowMs,basis,arrowLast);if(candidate>0&&arrowMs<=candidate){eta=arrowMs;window=candidate;threatId=projectile.id;t.defense="arrow_flight_including_attack_stopped_hero";}}
 float ms=(float)(int)(projectile.due-now);if(projectile.object&&!projectile.hallucination&&!projectile.contact&&projectile.geometryKnown&&projectile.intersects&&(DWORD)(now-projectile.lastSeen)<100&&ms>=-50&&ms<eta){const char*defense;DodgeStartupLead startup;defensiveDodgeLeadMilliseconds(&startup);bool prefer=startup.measured&&slowArrowPreference(projectile.policy);float candidate=windowFor(projectile.policy,ms,defense,std::max(ms,(float)(int)(projectile.latestDue-now)),prefer);if(candidate>0&&ms<=candidate){eta=ms;window=candidate;threatId=projectile.id;t.defense=defense;}}}
 for(const auto&bomb:bombPredictions){float ms,latest;if(bombReserveWindow(bomb,now,ms,latest)&&ms<eta){const char*defense;float candidate=windowFor({true,false},ms,defense,latest);if(candidate>0&&ms<=candidate){eta=ms;window=candidate;threatId=bomb.id;t.defense=defense;}}}
 BossProbe::refresh(true);
 for(const auto&boss:BossProbe::attacks)if(boss.handle){
   const auto&forecast=boss.kind==1?boss.reservation:boss.result;float contact=forecast.first,escape=forecast.escapeLead;bool targetedWindup=false;
   if(boss.kind==2&&boss.lastState==1&&t.dodgeAvailable){
     P iterator=il2cpp_gchandle_get_target(boss.handle),owner=iterator?read<P>(iterator,0x28):nullptr;
     // Tackle.MoveNext A910CD..A91120 counts down +40 in game seconds
     // while following its target; the trajectory locks only after windup.
     float remaining=iterator?read<float>(iterator,0x40):-1,frame=value<float>(DodgeBridgeDetails::deltaTimeMethod);
     if(iterator&&attackTargetsPlayer(owner)&&std::isfinite(remaining)&&remaining>=0&&std::isfinite(frame)&&frame>0&&frame<=.1f){
       contact=std::max(0.f,remaining-frame)*1000/t.scale;targetedWindup=true;
     }
   }
   if(!targetedWindup&&(!forecast.known||!forecast.hit))continue;
   if(boss.kind==2&&escape<0){float duration=value<float>(BossProbe::dashDuration,player),frame=value<float>(DodgeBridgeDetails::deltaTimeMethod);if(std::isfinite(duration)&&duration>0&&duration<=1&&std::isfinite(frame)&&frame>0&&frame<=.1f)escape=(duration+2*frame)*1000/t.scale;}
   const char*defense;float candidate=boss.kind==2?(t.dodgeAvailable&&escape>=0?t.cancelLock+escape:0):windowFor({true,false},contact,defense,forecast.last);
   if(candidate>0&&contact>=0&&contact<eta&&contact<=candidate){eta=contact;window=candidate;threatId=boss.id;t.defense=targetedWindup?"targeted_tackle_windup_preserves_dash_cancel":"boss_attack_cancel_plus_iframe_startup_window";}
 }
 for(const auto&rhino:BossProbe::rhinos)if(rhino.handle&&rhino.reservation.known&&rhino.reservation.hit){const auto&r=rhino.reservation;const char*defense;float candidate=windowFor({true,false},r.first,defense,r.last);if(candidate>0&&r.first>=0&&r.first<=candidate&&r.first<eta){eta=r.first;window=candidate;threatId=rhino.id;t.defense="rhino_slam_cancel_plus_iframe_startup_window";}}
 for(const auto&barrel:BossProbe::barrels)if(barrel.object&&(DWORD)(now-barrel.seen)<100&&barrel.result.known&&barrel.result.hit){float ms=std::max(0.f,barrel.result.first-(now-barrel.seen)),latest=std::max(ms,barrel.result.last-(now-barrel.seen));const char*defense;float candidate=windowFor({true,false},ms,defense,latest);if(candidate>0&&ms<=candidate&&ms<eta){eta=ms;window=candidate;threatId=barrel.id;t.defense="barrel_fuse_cancel_plus_iframe_startup_window";}}
 return threatId!=0;
}
static void tryAttackHook(P self,P mi){
 if(self==player){
   heldAttackAttemptFrame=frames;
   if(heldAttackEnabled()&&value<bool>(mAttackDown,self)){heldAttackOwner=self;heldAttackIntent=true;}else clearHeldAttack();
   uint64_t id=0;float eta=0,window=0;DWORD now=GetTickCount();bool reserved=reserveDefense(id,eta,window);
   bool defending=heldAttackEnabled()&&autoHolding;
   if(reserved||defending){Finisher::candidate={};bool first=priorityEpisodeStart==0;if(first){priorityEpisodeStart=now;priorityEpisodeId=id;priorityEpisodeSuppressed=0;}priorityEpisodeSuppressed++;prioritySuppressed++;if(first||(DWORD)(now-priorityLastLog)>=250){priorityLastLog=now;Trace::Json j("new_attack_suppressed");j.integer("correlation_id",id);j.integer("suppressed_total",prioritySuppressed);j.integer("episode_suppressed",priorityEpisodeSuppressed);j.integer("episode_elapsed_ms",now-priorityEpisodeStart);j.number("eta_ms",eta);j.number("reserve_window_ms",window);j.number("weapon_windup_seconds",priorityTiming.windup);j.number("weapon_winddown_seconds",priorityTiming.winddown);j.number("weapon_cancel_offset_seconds",priorityTiming.cancelOffset);j.number("time_scale",priorityTiming.scale);j.number("full_attack_lock_ms",priorityTiming.fullLock);j.number("cancellable_attack_lock_ms",priorityTiming.cancelLock);j.str("defense_window_basis",priorityTiming.defense);j.number("dodge_startup_lead_ms",priorityTiming.dodgeLead);j.boolean("full_dodge_available_now",priorityTiming.dodgeAvailable);j.str("timing_source","live_weapon_virtual_duration_and_cancel_getters");j.integer("automatic_block_remaining_ms",autoHolding?std::max(0,(int)(holdUntil-now)):0);j.boolean("held_attack_pending",heldAttackIntent);j.str("reason",defending?"automatic_block_still_active":"optional_defense_priority_avoids_new_attack_lock");playerJson(j,"player");Trace::enqueue(j);}return;}
   if(priorityEpisodeStart){Trace::Json j("attack_reservation_released");j.integer("correlation_id",priorityEpisodeId);j.integer("elapsed_ms",now-priorityEpisodeStart);j.integer("suppressed_attempts",priorityEpisodeSuppressed);j.str("reason","current_threats_no_longer_require_new_attack_reservation");j.boolean("attack_started",false);Trace::enqueue(j);priorityEpisodeStart=0;priorityEpisodeId=0;priorityEpisodeSuppressed=0;}}
 realTryAttack(self,mi);Finisher::candidate={};
}
static void resumeHeldAttack(){
 if(heldAttackOwner!=player||!heldAttackEnabled()||!value<bool>(mAttackDown,player)){clearHeldAttack();return;}
 if(!heldAttackIntent||heldAttackAttemptFrame==frames||autoHolding||physicalBlock()||value<bool>(mBlocking,player)||value<bool>(mIsDashing,player)||read<int>(player,0x490)!=0||read<bool>(player,0x798)||read<bool>(player,0x510)||value<bool>(mGrappled,player)||value<bool>(mWaitingGrapple,player))return;
 tryAttackHook(player,mTryAttack);
 if(read<int>(player,0x490)!=0){Trace::Json j("held_attack_resumed");j.str("proof","game_TryAttack_entered_attack_state");j.str("reason","held_input_revalidated_after_defense");Trace::enqueue(j);}
}
static void tick(P self){if(inTick||!map)return;if(faulted){slot<unsigned>(36)=3;slot<int>(64)=0;slot<unsigned>(44)=0;return;}inTick=true;DWORD now=GetTickCount();if(local(self)){if(player!=self){safeRelease();if(playerHandle)il2cpp_gchandle_free(playerHandle);playerHandle=il2cpp_gchandle_new(self,false);player=self;log("local player acquired");}playerSeen=now;}if(self!=player){inTick=false;return;}
 bool writing=false;__try {
 unsigned f=config();bool sensing=hostActive()&&!(f&8)&&gameWindow();
 if(!heldAttackEnabled()||!value<bool>(mAttackDown,player)||heldAttackOwner!=player)clearHeldAttack();
 // An attack can already be in progress when defense first takes ownership.
 if(heldAttackEnabled()&&value<bool>(mAttackDown,player)&&read<int>(player,0x490)!=0){heldAttackOwner=player;heldAttackIntent=true;}
 if(autoHolding&&(!automationReady()||!(f&1)||(int)(now-holdUntil)>=0))releaseBlock();
 observeDodgeState();observeDodgeStartup();Finisher::maintain();
 unsigned threats=0;bool uncertainIncoming=false;float nearest=99999;float nowGame=value<float>(mTime),scale=std::max(0.01f,value<float>(mTimeScale));for(auto&p:pending){if(!p.handle)continue;P a=il2cpp_gchandle_get_target(p.handle);bool bowGrace=alive(a)&&!p.bowResolved&&rangedWindup(a,call(mCurrentWeapon,a))&&nowGame>=p.dueGame&&nowGame<p.expiresGame;bool charging=alive(a)&&pendingCharge(a);if(charging||((attackActorActive(a)||rangedWindup(a,call(mCurrentWeapon,a)))&&read<int>(a,fState)==1&&!p.directionLocked))p.expiresGame=std::max(p.expiresGame,nowGame+.35f);if(nowGame>=p.expiresGame||!alive(a)||noteIgnoredHallucinationActor(a,"pending_attack")||(read<int>(a,fState)==0&&!bowGrace)){il2cpp_gchandle_free(p.handle);p={};continue;}
 if(sensing&&attackActorActive(a)){float ms=(p.dueGame-nowGame)/scale*1000;if(ms< -50&&!charging)continue;auto reach=pendingReach(p,a);if(reach.projected)ms=reach.etaMilliseconds;logReach(p,a,reach,ms);if(!reach.known){uncertainIncoming=true;continue;}if(!reach.intersects)continue;
   threats++;nearest=std::min(nearest,ms);lastThreat=now;
   P w=call(mCurrentWeapon,a);DefensePolicy::Threat policy;if(alive(w))policy=DefensePolicy::weapon(read<bool>(w,0x3C),read<bool>(w,0x3D),read<float>(w,0xC4),read<bool>(w,0x3E),read<bool>(w,0xF8),read<int>(w,0xEC),read<int>(w,0xF4));
   if((!p.acted||charging)&&ms<=predictiveDefenseDeadline(policy)&&!Finisher::continueBefore(p,a)){
     V3 travel=reach.direction;bool report=p.reported==0||(DWORD)(now-p.reported)>=250;if(report){p.reported=now;logReach(p,a,reach,ms,true);}p.acted=protect(policy.unblockable,&travel,"melee_prediction",p.id,report,ms,policy.shieldRisk,false,false,-1,policy.burning);
   }
 }}
 for(const auto&t:projectileTraces)if(sensing&&t.object&&!t.hallucination&&!t.contact&&(DWORD)(now-t.lastSeen)<100){if(!t.geometryKnown){uncertainIncoming=true;continue;}if(t.intersects){float ms=(float)(int)(t.due-now);if(ms>=-50){threats++;nearest=std::min(nearest,std::max(0.f,ms));}}}
 for(const auto&t:bombPredictions)if(sensing&&t.object&&(DWORD)(now-t.seen)<100){if(!t.known){uncertainIncoming=true;continue;}if(t.intersects){float ms=(float)(int)(t.due-now);if(ms>=0){threats++;nearest=std::min(nearest,ms);}}}
 if(sensing)BossProbe::act(threats,nearest,uncertainIncoming);
 if(autoHolding&&!threats&&!uncertainIncoming&&(DWORD)(now-lastImpactDefense)>50){releaseBlock();Trace::Json j("auto_block_released");j.str("reason","no_remaining_intersecting_threats");Trace::enqueue(j);}
 resumeHeldAttack();
 diagnosticTick();ShieldTelemetry shield=shieldTelemetry();
 // HUD-only, independent of automation toggles. Red means a full dodge cannot
 // currently protect from the nearest confirmed intersection, not an intrinsic
 // claim that this enemy/attack always ignores iframes. Unknown timing is neutral.
 unsigned hudDanger=0;
 if(sensing&&threats&&nearest<=800&&alive(player)){
   hudDanger=1;float eta=std::max(0.f,nearest);DodgeStartupLead lead;
   defensiveDodgeLeadMilliseconds(&lead);
   if(!blockImmunityActive(false,eta)&&(!priorityFullDodgeAvailable()||(lead.measured&&lead.available&&eta<lead.onsetUpperMilliseconds)))hudDanger|=2;
 }

 slot<unsigned>(32)++;writing=true;MemoryBarrier();slot<unsigned>(36)=value<bool>(mDead,player)?1:2;slot<unsigned>(40)=++frames;slot<unsigned>(44)=threats;slot<unsigned>(48)=blocks;slot<unsigned>(52)=dodges;slot<float>(68)=nearest==99999?-1:std::max(0.f,nearest);slot<unsigned>(80)=1;
 slot<unsigned>(104)=confirmedProjectileDodges;slot<unsigned>(108)=(uint32_t)InterlockedCompareExchange(&Trace::dropped,0,0);slot<unsigned>(112)=(uint32_t)InterlockedCompareExchange(&Trace::ready,0,0);slot<unsigned>(116)=6;writeDefenseTelemetry(shield);slot<unsigned>(1572)=hudDanger;
 textStatus(value<bool>(mDead,player)?"Герой неактивен":"События игры подключены");
 if((DWORD)(now-haSeen)>200)slot<int>(64)=0;char info[512];sprintf_s(info,"Мод в игре | щит: %s | защита: %s | кадры: %u",value<bool>(mHasShield,player)?"есть":"нет",automationReady()?"готова":"пауза",frames);strncpy_s((char*)map+512,512,info,_TRUNCATE);MemoryBarrier();slot<unsigned>(32)++;
 } __except(EXCEPTION_EXECUTE_HANDLER){safeRelease();faulted=true;slot<unsigned>(36)=3;textStatus("Защита остановлена: ошибка доступа к объекту");if(writing&&(slot<unsigned>(32)&1)){MemoryBarrier();slot<unsigned>(32)++;}Trace::Json j("bridge_fault");j.str("phase","player_tick");j.boolean("automation_stopped",true);Trace::enqueue(j);}
 inTick=false;
}
static void(*realUpdate)(P,P);static void updateHook(P self,P mi){realUpdate(self,mi);tick(self);}
static bool addHook(P m,uintptr_t expected,P hook,P*out){if(!m||*(char**)m!=(char*)game+expected)return false;return MH_CreateHook(*(P*)m,hook,out)==MH_OK;}
static DWORD WINAPI initialize(P){
 wchar_t path[MAX_PATH];GetModuleFileNameW(own,path,MAX_PATH);wchar_t*slash=wcsrchr(path,L'\\');if(slash)*slash=0;Trace::start(path);log("native loading");
 wchar_t name[100];swprintf_s(name,L"Local\\ChainedGuard_%lu",GetCurrentProcessId());mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);if(!mapping){log("host mapping absent; inactive");return 1;}map=(unsigned char*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,4096);if(!map)return 1;
 slot<unsigned>(36)=1;textStatus("Ожидание загрузки движка");for(int i=0;i<600&&!game;i++){game=GetModuleHandleW(L"GameAssembly.dll");slot<unsigned>(84)=GetTickCount();if(!game)Sleep(100);}if(!game){textStatus("Движок не загрузился: перезапустите игру");slot<unsigned>(36)=3;log("GameAssembly load timeout");return 1;}
#define LOAD(name) name=(decltype(name))GetProcAddress(game,#name);if(!name){textStatus("API IL2CPP не найден");slot<unsigned>(36)=3;return 2;}
 LOAD(il2cpp_domain_get) LOAD(il2cpp_thread_attach) LOAD(il2cpp_thread_detach) LOAD(il2cpp_domain_get_assemblies) LOAD(il2cpp_assembly_get_image) LOAD(il2cpp_image_get_name) LOAD(il2cpp_class_from_name) LOAD(il2cpp_class_get_method_from_name) LOAD(il2cpp_class_get_field_from_name) LOAD(il2cpp_field_get_offset) LOAD(il2cpp_field_static_get_value) LOAD(il2cpp_runtime_invoke) LOAD(il2cpp_object_unbox) LOAD(il2cpp_object_get_class) LOAD(il2cpp_class_get_name) LOAD(il2cpp_class_get_nested_types) LOAD(il2cpp_class_is_assignable_from) LOAD(il2cpp_gchandle_new) LOAD(il2cpp_gchandle_get_target) LOAD(il2cpp_gchandle_free) LOAD(il2cpp_array_new) LOAD(il2cpp_class_get_type) LOAD(il2cpp_type_get_object)
 for(int i=0;i<600&&!il2cpp_domain_get();i++){slot<unsigned>(84)=GetTickCount();Sleep(100);}if(!il2cpp_domain_get()){slot<unsigned>(36)=3;textStatus("Движок ещё не готов: перезапустите игру");log("IL2CPP domain timeout");return 2;}
 InitThreadAttachment initThread(il2cpp_domain_get());if(!initThread.thread){slot<unsigned>(36)=3;textStatus("Не удалось подключить поток к движку");log("IL2CPP thread attach failed");return 2;}P pc=nullptr;for(int i=0;i<600&&!pc;i++){pc=findClass("","Player");slot<unsigned>(84)=GetTickCount();if(!pc)Sleep(100);}P agent=findClass("","NetworkAgent"),health=findClass("","NetworkHealthObject"),cam=findClass("UnityEngine","Camera"),screen=findClass("UnityEngine","Screen"),projectile=findClass("","Projectile");directionalClass=findClass("","DamageEventSourceDirectionalAttack");log("IL2CPP combat classes resolved");
 mDead=method(health,"get_IsDead",0);mBlocked=method(pc,"get_IsInputBlocked",0);mCanDash=method(pc,"CanDash",0);mDash=method(pc,"OnDash",1);mIsDashing=method(pc,"get_IsDashing",0);mBlockStart=method(pc,"OnBlockStart",1);mBlockCancel=method(pc,"OnBlockCanceled",1);mBlockDown=method(pc,"get_BlockDown",0);mHasShield=method(agent,"get_HasShield",0);mBlocking=method(agent,"get_IsBlocking",0);mCanCancelBlock=method(pc,"CanCancelAttackToBlock",0);mDodgeIFrames=method(agent,"get_IsDodgeIFrames",0);mCurrentWeapon=method(agent,"get_CurrentWeapon",0);mModelForward=method(agent,"get_ModelForward",0);mMeleeRange=method(agent,"get_MeleeRange",0);
 mPosition=method(health,"get_PositionOfColliders",0);mCamera=method(cam,"get_main",0);mWorldScreen=method(cam,"WorldToScreenPoint",1);mScreenWidth=method(screen,"get_width",0);mScreenHeight=method(screen,"get_height",0);mTime=method(findClass("UnityEngine","Time"),"get_time",0);
 mGetTransform=method(findClass("UnityEngine","Component"),"get_transform",0);mSetForward=method(findClass("UnityEngine","Transform"),"set_forward",1);mWillHitShield=method(agent,"WillHitShield",1);mTransformPosition=method(findClass("UnityEngine","Transform"),"get_position",0);mProjectileVelocity=method(projectile,"get_Velocity",0);
 agentClass=agent;enemyClass=findClass("","Enemy");mEnemyType=method(enemyClass,"get_MyEnemyType",0);mEnemyHallucination=method(enemyClass,"get_IsHallucination",0);mEffectActive=method(agent,"IsEffectActive",1);mEffectDuration=method(agent,"GetEffectDuration",1);
 mHasSkill=method(pc,"HasSkill",1);mHasBeastSkill=method(pc,"HasBeastSkill",1);mHasInjury=method(pc,"HasInjury",1);mHasPurchase=method(pc,"HasPurchase",1);mHealth=method(health,"get_Health01",0);mStaminaCost=method(pc,"get_StaminaCostDash",0);mStaminaMax=method(pc,"get_StaminaMax",0);
 mInvulnerable=method(pc,"get_IsInvulnerable",0);mIncapacitated=method(agent,"get_IsIncapacitated",0);mMovementBlocked=method(agent,"get_IsMovementBlocked",0);mWaitingGrapple=method(agent,"get_WaitingForGrapple",0);mCancelDodge=method(pc,"CanCancelAttackToDodge",0);mShieldOnBack=method(agent,"get_ShieldOnBack",0);mShieldActive=method(agent,"get_ShieldCurrentlyActive",0);mTimeScale=method(findClass("UnityEngine","Time"),"get_timeScale",0);mTryAttack=method(pc,"TryAttack",0);mAttackDown=method(pc,"get_AttackDown",0);mDashStart=method(pc,"StartDash",0);mGrappled=method(agent,"get_HasGrappledAgent",0);mFinalMovementSpeed=method(pc,"get_FinalMovementSpeed",0);
 bool finisherReady=Finisher::init();{Trace::Json j("finisher_probe_ready");j.boolean("available",finisherReady);j.str("scope","normal_local_melee_minimum_damage_before_enemy_commit");Trace::enqueue(j);}
 P libraryClass=findClass("","GameLibrary"),pickupDefinition=findClass("","PickupDef"),tutorialClass=findClass("","TutorialManager");
 mGameLibraryInstance=method(libraryClass,"get_Instance",0);mPickupDict=method(libraryClass,"get_PickupDict",0);mStartingDurability=method(pickupDefinition,"get_StartingDurability",0);
 mTutorialInstance=method(tutorialClass,"get_Instance",0);mTutorialLevel=method(tutorialClass,"get_IsTutorialLevel",0);mTutorialStage=method(tutorialClass,"get_TutorialStage",0);
 bool inputGateReady=initDefenseGate();{Trace::Json j("defense_input_gate_support");j.boolean("ready",inputGateReady);j.str("policy","game_character_lock_only_no_UI_pause_override");Trace::enqueue(j);}
 bool reachReady=initAttackProbe(),trajectoryReady=initProjectileProbe(pc,projectile),launchForecastReady=initLaunchForecast(),anchorReady=initHudAnchor(),dodgeReady=initDodgeBridge(pc,agent),startupReady=initDodgeStartup(),bombReady=initBombProbe(),bossReady=BossProbe::init();
 diagnosticReady=mAttackDown&&*(P*)mAttackDown==(char*)game+0x721470&&mEnemyType&&mEnemyHallucination&&mEffectActive&&mEffectDuration&&mHasSkill&&mHasBeastSkill&&mHasInjury&&mHasPurchase&&mHealth&&mStaminaCost&&mStaminaMax&&mInvulnerable&&mIncapacitated&&mMovementBlocked&&mWaitingGrapple&&mCancelDodge&&mShieldOnBack&&mShieldActive&&mTimeScale&&mGrappled&&reachReady&&trajectoryReady&&launchForecastReady&&anchorReady&&dodgeReady&&bossReady;
 markerSeen=slot<uint32_t>(100);slot<unsigned>(116)=6;slot<unsigned>(1568)=18;
 {Trace::Json j("native_revision");j.integer("native_build",6);j.integer("revision",18);Trace::enqueue(j);}
 {Trace::Json j("boss_prediction_support");j.boolean("ready",bossReady);j.str("families","leap_slam,rhino_scheduled_slam,tackle_curve,activated_explosive_barrel,stationary_timer_aoe");j.str("limits","current_motion_only; instantaneous_net_crawl_pins_have_no_predictive_lead; rhino_requires_stationary_slam_center");Trace::enqueue(j);}
 {Trace::Json j("predictive_bomb_support");j.boolean("bomb_probe_ready",bombReady);j.boolean("dodge_startup_ready",startupReady);j.integer("supported_projectile_type",100);j.str("physics_asset_sha256","ed8aca42cb11d7c6df14b30d1970f915b0942b234bdcd952a4245b71a0b37604");j.str("motion_model","current_velocity_signed_gravity_fixed_step_envelope_no_future_input_prediction");Trace::enqueue(j);}
 {Trace::Json j("combat_session_start");j.integer("schema",3);j.integer("native_build",6);j.boolean("diagnostic_methods_available",diagnosticReady);j.boolean("melee_probe_ready",reachReady);j.boolean("projectile_probe_ready",trajectoryReady);j.boolean("healthbar_anchor_ready",anchorReady);j.boolean("dodge_bridge_ready",dodgeReady);j.str("supported_gameassembly_sha256","C7437CED53B682904EAF18356AEF16613AFA436141E2C706A7560D3A1A4DACDD");j.str("timing","game_time_for_windup_real_time_for_reaction_window");j.str("dodge_confirmation","projectile_dodgedAgents_membership_transition_only");Trace::enqueue(j);}
 if(!pc||!mDead||!mBlockStart||!mCanDash||!mPosition||!mWorldScreen||!directionalClass||!mDash||!mBlocked||!mBlockCancel||!mBlocking||!mCanCancelBlock||!mCamera||!mScreenWidth||!mScreenHeight||!mGetTransform||!mSetForward||!mWillHitShield||!mProjectileVelocity||!mTransformPosition||!mIsDashing||!mMeleeRange){slot<unsigned>(36)=3;textStatus("Версия игры не поддержана: функции не найдены");log("required combat method absent");return 2;}
 if(!diagnosticReady){slot<unsigned>(36)=3;textStatus("Версия игры не поддержана: диагностика недоступна");log("diagnostic methods unavailable");return 2;}
 if(MH_Initialize()!=MH_OK||!addHook(method(pc,"Update",0),0x71C430,(P)updateHook,(P*)&realUpdate)||!addHook(method(agent,"InitializeAttack",5),0x6A6A60,(P)initHook,(P*)&realInit)||!addHook(method(health,"TakeDamage",8),0x6CF090,(P)damageHook,(P*)&realDamage)||!addHook(mBlockDown,0x721580,(P)blockDownHook,(P*)&realBlockDown)||!addHook(method(projectile,"Update",0),0x764D90,(P)projectileHook,(P*)&realProjectileUpdate)||!addHook(method(projectile,"HandleTriggerWithAgent",1),0x75E330,(P)projectileHitHook,(P*)&realProjectileHit)||!addHook(mTryAttack,0x717A10,(P)tryAttackHook,(P*)&realTryAttack)||!addHook(mDashStart,0x7159D0,(P)dashStartHook,(P*)&realDashStart)||!addHook(haLateUpdate,0x8435F0,(P)healthBarLateHook,(P*)&realHealthBarLate)||!addHook(method(findClass("","Weapon"),"Attack",1),0x7D2640,(P)weaponAttackHook,(P*)&realWeaponAttack)||!addHook(method(findClass("","Weapon"),"AttackAfterDelay",3),0x7CFC90,(P)attackAfterDelayHook,(P*)&realAttackAfterDelay)){slot<unsigned>(36)=3;textStatus("Версия игры не поддержана: адреса изменились");log("version guard failed");return 3;}
 if(!addHook(method(projectile,"Launch",15),0x761790,(P)projectileLaunchHook,(P*)&realProjectileLaunch)||!addHook(method(projectile,"FixedUpdate",0),0x75DF00,(P)projectileFixedHook,(P*)&realProjectileFixed)){slot<unsigned>(36)=3;textStatus("Версия игры не поддержана: ранние события снарядов изменились");log("early projectile hook version guard failed");return 3;}
 {Trace::Json j("early_projectile_support");j.boolean("launch_forecast_ready",launchForecastReady);j.str("sampling","Launch_complete,FixedUpdate_complete,Update_complete");j.str("disabled_collider_model","verified_future_penetration_after_real_enable_timer");j.boolean("contact_prefix_can_create_instant_iframes",false);Trace::enqueue(j);}
 if(!addHook(method(projectile,"TriggerImpactGameplayEffects",1),0x7642E0,(P)bombImpactHook,(P*)&realBombImpact)){slot<unsigned>(36)=3;textStatus("Версия игры не поддержана: событие взрыва изменилось");log("bomb impact version guard failed");return 3;}
 if(!addHook(method(agent,"UnequipShieldRPC",1),0x6B98C0,(P)shieldUnequipHook,(P*)&realShieldUnequip)){slot<unsigned>(36)=3;textStatus("Версия игры не поддержана: событие щита изменилось");log("shield feedback version guard failed");return 3;}
 if(!addHook(BossProbe::leapStep,0xA8FBB0,(P)BossProbe::leapStepHook,(P*)&BossProbe::realLeapStep)||!addHook(BossProbe::tackleStep,0xA90D50,(P)BossProbe::tackleStepHook,(P*)&BossProbe::realTackleStep)||!addHook(BossProbe::invokeMethod,0x3706990,(P)BossProbe::invokeHook,(P*)&BossProbe::realInvoke)||!addHook(BossProbe::rhinoSlamMethod,0xA3BAA0,(P)BossProbe::rhinoSlamHook,(P*)&BossProbe::realRhinoSlam)||!addHook(method(findClass("","ArenaTrapBarrelExplosive"),"Update",0),0xA55AF0,(P)BossProbe::barrelHook,(P*)&BossProbe::realBarrelUpdate)){slot<unsigned>(36)=3;textStatus("Версия игры не поддержана: события боссов изменились");log("boss hook version guard failed");return 3;}
 if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK){textStatus("Не удалось подключить события");slot<unsigned>(36)=3;return 4;}slot<unsigned>(80)=1;textStatus("Подключено. Ожидание героя");log("hooks active; automation requires host flags");
 // Finish all setup-thread IL2CPP work before the Win32-only heartbeat and game shutdown.
 initThread.detach();
 for(;;){slot<unsigned>(84)=GetTickCount();slot<unsigned>(108)=(uint32_t)InterlockedCompareExchange(&Trace::dropped,0,0);slot<unsigned>(112)=(uint32_t)InterlockedCompareExchange(&Trace::ready,0,0);Sleep(500);if(!faulted&&(DWORD)(GetTickCount()-playerSeen)>1500){slot<unsigned>(36)=1;slot<int>(64)=0;slot<unsigned>(44)=0;textStatus("Подключено. Ожидание героя");}}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){own=h;DisableThreadLibraryCalls(h);HANDLE t=CreateThread(nullptr,0,initialize,nullptr,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
