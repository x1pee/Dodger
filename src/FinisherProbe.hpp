#pragma once
// Sufficient, deliberately conservative conditions for a normal local melee
// finisher. No damage RPC, random roll, execution getter or attack mutation is
// invoked to obtain a preview. Unsupported cases retain ordinary defense.
namespace Finisher {
static bool ready;
static P playerClass,levelDamage,enemyInvulnerable,enemyVelocity,backstabBase,backstabTaken;
static P critField,rageField,weakField;
struct Proof {
 bool allowed=false;P owner=nullptr,target=nullptr,weapon=nullptr;
 uint64_t id=0;int hp=0,minimumDamage=0;float hitGame=0,enemyCommitGame=0,safetyGame=0;
 V3 facing={};const char*reason="not_evaluated";
};
static Proof candidate;
static struct Lease {Proof proof;uintptr_t target=0,weapon=0;float start=0;} lease;
static float length(V3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
static bool effect(P actor,int id,bool&active){P args[]={&id};return apValue(mEffectActive,actor,args,active);}
static bool staticFloat(P field,float&v){if(!field||!il2cpp_field_static_get_value)return false;il2cpp_field_static_get_value(field,&v);return std::isfinite(v)&&v>0&&v<100;}
static bool init(){
 P constants=findClass("","GladiatorConsts"),enemy=findClass("","Enemy");
 playerClass=findClass("","Player");
 if(!constants||!enemy||!il2cpp_field_static_get_value)return false;
 levelDamage=method(findClass("","PlayerData"),"get_PlayerLevelDamageModifier",0);
 enemyInvulnerable=method(enemy,"get_IsInvulnerable",0);enemyVelocity=method(enemy,"get_CurrentVelocity",0);
 backstabBase=method(constants,"get_BackstabMultiplierBase",0);backstabTaken=method(enemy,"get_BackstabDamageTakenMultiplier",0);
 critField=il2cpp_class_get_field_from_name(constants,"CritMultiplierBase");rageField=il2cpp_class_get_field_from_name(constants,"RageMultiplierBase");weakField=il2cpp_class_get_field_from_name(constants,"WeakenedDamageMultiplierBase");
 ready=apMethodAt(method(playerClass,"ModifyAttackDamage",7),0x707410)&&apMethodAt(levelDamage,0x738230)&&apMethodAt(enemyInvulnerable,0xA7A830)&&apMethodAt(enemyVelocity,0xA7A130)&&apMethodAt(backstabBase,0x8AA150)&&apMethodAt(backstabTaken,0xA79D90)&&critField&&rageField&&weakField;
 return ready;
}
static void event(const char*name,const Proof&p,const char*reason){
 Trace::Json j(name);j.integer("correlation_id",p.id);j.pointer("target",p.target);j.integer("target_hp",p.hp);j.integer("minimum_damage",p.minimumDamage);
 j.number("own_hit_game_time",p.hitGame);j.number("enemy_commit_game_time",p.enemyCommitGame);j.number("timing_safety_game_seconds",p.safetyGame);j.str("reason",reason);j.boolean("confirmed_kill",false);Trace::enqueue(j);
}
static void clear(const char*reason){if(lease.target){event("finisher_revoked",lease.proof,reason);il2cpp_gchandle_free(lease.target);il2cpp_gchandle_free(lease.weapon);}lease={};}
static bool normalWeapon(P w){
 if(!alive(w))return false;P k=il2cpp_object_get_class(w);
 P pickup=read<P>(w,0x170);if(!pickup||read<int>(pickup,0x48)>=0)return false;
 return read<float>(w,0xC4)==0&&read<P>(w,0x178)==player&&
 apMethodAt(method(k,"Attack",1),0x7D2640)&&
 apMethodAt(method(k,"AttackEffect",13),0x7CFD40)&&apMethodAt(method(k,"GetSphereCastOrigin",2),0x7D3970)&&
 apMethodAt(method(k,"WeaponModifiedDamage",2),0x7D5760)&&apMethodAt(method(k,"get_ThrowOnAttack",0),0x7D6150)&&
 apMethodAt(method(k,"get_ForceAttackDown",0),0x604430)&&
 !value<bool>(method(k,"get_ThrowOnAttack",0),w,nullptr,true)&&read<bool>(w,0xAE);
}
static bool health(P target,int&hp){
 P variable=read<P>(target,0xA8);if(!variable)return false;
 if(!apValue(method(il2cpp_object_get_class(variable),"get_Value",0),variable,nullptr,hp)||hp<=0||hp>1000000)return false;
 // Both the network value and local damage accounting must agree. Never use
 // Health01*MaxHealth (it loses integer HP and can lag local accounting).
 return hp==read<int>(target,0xB8);
}
static int damageFloor(float raw){
 if(!std::isfinite(raw)||raw<=0||raw>1000000)return 0;
 // Player RNG >=0.8; WIPDamageEvent adds [-0.5,0.5], clamps positive
 // damage to >=1 and rounds. Floor minus numerical slack is a lower bound.
 return std::max(1,(int)std::floor(raw*.999f-.5001f));
}
static bool minimumDamage(P target,P weapon,int&result){
 float crit,rage,weak,back,received,level;P data=read<P>(player,0x5C0);
 if(!data||!staticFloat(critField,crit)||!staticFloat(rageField,rage)||!staticFloat(weakField,weak)||
 !apValue(backstabBase,nullptr,nullptr,back)||!apValue(backstabTaken,target,nullptr,received)||
 !apValue(levelDamage,data,nullptr,level)||!std::isfinite(back)||back<1||!std::isfinite(received)||received<0||!std::isfinite(level)||level<=0||level>100)return false;
 int injury=508;P args[]={&injury};int weaker=-1;if(!apValue(mHasInjury,player,args,weaker)||weaker<0||weaker>6)return false;
 float reduction=weaker?1.05f-.15f*weaker:1.f;int base=read<int>(weapon,0x78);if(base<=0||base>100000)return false;
 // All other audited normal-melee Player modifiers are nonnegative bonuses.
 // Count none of them, even if currently active. Include possible rage/weak
 // reductions even when absent, so an expiring buff cannot invalidate the floor.
 float raw=(float)base*.8f*level*std::min(1.f,crit)*std::min(1.f,rage)*std::min(1.f,weak)*reduction;
 // Enemy's only additional non-friendly multiplier is its war-machine branch:
 // formations use .65 or .4. Always budget the smaller value while mounted.
 if(alive(read<P>(target,0x630)))raw*=.4f;
 result=damageFloor(raw);return result>0;
}
static bool geometry(P target,P weapon,V3 facing,float horizon,float frame,const char*&reason){
 // Require a stationary, movement-locked enemy through its windup. Account for
 // our lunge, residual velocities and two update steps by eroding the real cast.
 P enemyWeapon=call(mCurrentWeapon,target),rb=read<P>(player,0x190),enemyBody=read<P>(target,0x190);
 V3 av={},hv={},ev={};bool locked=false;
 if(!alive(enemyWeapon)||!read<bool>(enemyWeapon,0xAE)||!apValue(mMovementBlocked,target,nullptr,locked)||!locked||
 !apValue(enemyVelocity,target,nullptr,av)||!apFinite(av)||length(av)>.01f||!alive(rb)||!alive(enemyBody)||
 !apValue(ProjectileProbe::linearVelocity,rb,nullptr,hv)||!apValue(ProjectileProbe::linearVelocity,enemyBody,nullptr,ev)||!apFinite(hv)||!apFinite(ev)) {reason="movement_not_bounded";return false;}
 int forced=100;P args[]={&forced};int forcedLevel=-1;if(!apValue(mHasInjury,player,args,forcedLevel)||forcedLevel<0)return false;
 float speed=length(hv)+length(ev),lunge=read<float>(weapon,0xB0);
 if(forcedLevel>0){float available;if(!apValue(mFinalMovementSpeed,player,nullptr,available)||!std::isfinite(available)||available<0)return false;speed=std::max(speed,available+length(ev));}
 float duration=read<float>(weapon,0xB8),radius=read<float>(weapon,0x70),range=read<float>(weapon,0x74),margin=speed*(horizon+2*frame)+.02f;
 if(!std::isfinite(radius)||!std::isfinite(range)||!std::isfinite(margin)||!std::isfinite(lunge)||!std::isfinite(duration)||radius<=margin||range<=2*radius){reason="hit_not_inside_motion_margin";return false;}
 // AttackLunge 0x7D2250 uses ModelForward, skips nonpositive distance/duration,
 // and can scale a normal flurry lunge by1.5. Cover the entire [0,1.5*distance]
 // interval instead of incorrectly treating forward travel as lateral error.
 float lungeMax=lunge>0&&duration>0?lunge*1.5f:0;int mainId=0;
 if(lungeMax>0){P collider=read<P>(target,0x1C8);if(!apKind(collider,ProjectileProbe::capsuleClass)||!apValue(apInstanceId,collider,nullptr,mainId)||!mainId){reason="lunge_target_convex_collider_unknown";return false;}}
 int ids[128],count=0,mask=0;if(!apActorColliderIds(target,ids,count)||!apValue(apLayerMask,weapon,nullptr,mask))return false;
 // Query the full, unshrunk game cast first. Any other hit, including scenery,
 // makes ordering/occlusion uncertain. No assumption about Physics hit ordering.
 V3 origin;P originArgs[]={&facing,&radius};if(!apValue(apBaseOrigin,weapon,originArgs,origin)||!apFinite(origin))return false;
 float distance=std::max(.1f,range-2*radius);P array=il2cpp_gchandle_get_target(apHitHandle);if(!array)return false;int collide=2;
 // SphereCast omits starting overlaps. An extra overlap along possible lunge
 // origins rejects foreign blockers that a full cast could otherwise omit.
 int ownIds[128],ownCount=0;if(!apActorColliderIds(player,ownIds,ownCount))return false;
 P overlap=il2cpp_gchandle_get_target(apColliderHandle);if(!overlap)return false;
 V3 end=apAdd(origin,apMul(facing,lungeMax));int overlaps=-1;P overlapArgs[]={&origin,&end,&radius,overlap,&mask,&collide};
 if(!apValue(apOverlapCapsule,nullptr,overlapArgs,overlaps)||overlaps<0||overlaps>=64){reason="lunge_origin_overlap_unknown";return false;}
 for(int n=0;n<overlaps;n++){P collider=read<P>(overlap,0x20+n*sizeof(P));int id=0;if(!alive(collider)||!apValue(apInstanceId,collider,nullptr,id)||(!apIdMatches(id,ids,count)&&!apIdMatches(id,ownIds,ownCount))){reason="foreign_starting_overlap";return false;}}
 auto cast=[&](V3 start,float r,float reach,int required,bool exclusive){int hits=-1;P query[]={&start,&r,&facing,array,&reach,&mask,&collide};if(!apValue(apSphereCast,nullptr,query,hits)||hits<=0||hits>=64)return false;bool hit=false;
 for(int n=0;n<hits;n++){int id=read<int>(array,0x20+n*0x2C+0x28);bool matches=apIdMatches(id,ids,count);if(exclusive&&!matches)return false;hit|=required?id==required:matches;}return hit;};
 // The extended full cast contains every intermediate forward-lunge cast.
 if(!cast(origin,radius,distance+lungeMax,0,true)){reason="target_missed_or_other_collision";return false;}
 if(!cast(origin,radius-margin,distance,mainId,false)){reason="edge_contact_not_guaranteed";return false;}
 // A single convex target capsule intersecting both eroded endpoint casts also
 // intersects every intermediate translation; other limb IDs cannot fill a gap.
 if(lungeMax>0&&!cast(apAdd(origin,apMul(facing,lungeMax)),radius-margin,distance,mainId,false)){reason="lunge_can_pass_target";return false;}
 return true;
}
static Proof prove(Pending&p,P target,float windup,float scale,float remaining=-1,V3*lockedFacing=nullptr){
 Proof r;r.owner=player;r.target=target;r.weapon=alive(player)?call(mCurrentWeapon,player):nullptr;r.id=p.id;
 auto fail=[&](const char*why){r.reason=why;return r;};
 if(!ready||!alive(player)||!alive(target)||read<bool>(player,0x4C1)||!normalWeapon(r.weapon))return fail("normal_local_melee_only");
 if(il2cpp_object_get_class(player)!=playerClass)return fail("player_damage_override_unknown");
 P k=il2cpp_object_get_class(target);
 // Exact virtual implementations: no subclass damage/avoid/death rules.
 if(k!=enemyClass||!read<bool>(target,0x39)||!read<bool>(target,0x41)||read<int>(target,0x114)!=0||
 !apMethodAt(method(k,"get_IsPlayerTeam",0),0x604430)||!apMethodAt(method(il2cpp_object_get_class(player),"get_IsPlayerTeam",0),0x6045C0))return fail("target_authority_or_team_unknown");
 int type=0;if(!apValue(mEnemyType,target,nullptr,type)||type<=0||type>=1000)return fail("boss_or_unknown_unit");
 bool ignored=true;if(!apValue(mEnemyHallucination,target,nullptr,ignored)||ignored)return fail("hallucination");
 float downed=read<float>(target,0x6B4);if(!std::isfinite(downed)||downed>0)return fail("possible_execution_instead_of_kill");
 if(read<int>(target,fState)!=1||read<int>(target,fAttackType)!=1||p.directionLocked||read<P>(target,fTarget)!=player)return fail("enemy_attack_already_committed");
 bool vulnerable=false;if(!apValue(enemyInvulnerable,target,nullptr,vulnerable)||vulnerable)return fail("enemy_invulnerable");
 for(int id:{17,21,22}){bool active=true;if(!effect(target,id,active)||active)return fail("enemy_immunity_or_evasion");}
 bool shield=true,blocking=true;if(!apValue(mHasShield,target,nullptr,shield)||shield||!apValue(mBlocking,target,nullptr,blocking)||blocking)return fail("enemy_can_block");
 if(!health(target,r.hp)||!minimumDamage(target,r.weapon,r.minimumDamage)||r.minimumDamage<r.hp)return fail("minimum_damage_not_lethal");
 // These injuries can interrupt, redirect or disarm a damage action. Do not
 // roll them or assume that the player's next action will avoid their chance.
 for(int id:{3,16,101,502,503,504,1000,1001,1301,1500}){P args[]={&id};int level=-1;if(!apValue(mHasInjury,player,args,level)||level!=0)return fail("unpredictable_action_injury");}
 float frame=value<float>(DodgeBridgeDetails::deltaTimeMethod,nullptr,nullptr,-1.f),now=value<float>(mTime,nullptr,nullptr,-1.f),delay=weaponEffectDelay(r.weapon);
 if(!std::isfinite(frame)||frame<=0||frame>.1f||!std::isfinite(now)||now<0||!std::isfinite(scale)||scale<=0||!std::isfinite(windup)||windup<0||!std::isfinite(delay))return fail("timing_unknown");
 float horizon=remaining>=0?remaining:windup+delay;if(!std::isfinite(horizon)||horizon<0)return fail("own_hit_not_pending");
 P incoming=call(mCurrentWeapon,target);float incomingDelay=weaponEffectDelay(incoming);if(!alive(incoming)||!std::isfinite(incomingDelay))return fail("enemy_effect_delay_unknown");
 // Latest own hit, not just the nominal duration: windup and delayed effect
 // can each resume on the next update. Ongoing leases already contain this bound.
 r.hitGame=now+horizon+(remaining<0?2*frame:0);r.enemyCommitGame=p.dueGame-incomingDelay;
 r.safetyGame=frame+std::max(.0001f,4*std::numeric_limits<float>::epsilon()*std::max(1.f,std::fabs(now)));
 if(r.hitGame+r.safetyGame>=r.enemyCommitGame)return fail("kill_not_before_enemy_release_with_frame_margin");
 // Idle TryAttack can update facing for queued/dash-transition attacks. Those
 // are not a proof of the direction used by the new melee attack.
 if(remaining<0&&(read<bool>(r.weapon,0x1B0)||read<bool>(player,0x618)||value<bool>(mIsDashing,player)||read<float>(player,0x5C8)<value<float>(BossProbe::dashDuration,player,nullptr,99999.f)))return fail("queued_or_dash_facing_unknown");
 if(!apValue(mModelForward,player,nullptr,r.facing)||!apFinite(r.facing)||std::fabs(length(r.facing)-1)>.001f)return fail("attack_facing_unknown");
 if(lockedFacing&&length(apAdd(r.facing,apMul(*lockedFacing,-1)))>.001f)return fail("attack_direction_changed");
 const char*why="geometry_unknown";if(!geometry(target,r.weapon,r.facing,horizon,frame,why))return fail(why);
 r.allowed=true;r.reason="minimum_damage_and_interior_hit_before_enemy_commit";return r;
}
static bool skipReservation(Pending&p,P target,float windup,float scale){
 if(candidate.target&&candidate.target!=target)return false;
 Proof r=prove(p,target,windup,scale);if(!r.allowed){
  struct Rejected{uint64_t id=0;DWORD tick=0;const char*reason=nullptr;};static Rejected rejected[128];
  auto&last=rejected[p.id%128];DWORD now=GetTickCount();
  if(ready&&(last.id!=p.id||last.reason!=r.reason)&&(DWORD)(now-last.tick)>=250){event("finisher_rejected",r,r.reason);last={p.id,now,r.reason};}
  return false;
 }candidate=r;return true;
}
static void started(P self,P weapon,float windup,int mode){
 if(self!=player||!candidate.allowed||candidate.owner!=self||candidate.weapon!=weapon)return;
 clear("new_attack");if(mode!=0||read<int>(self,fState)!=1){candidate={};return;}
 float scale=value<float>(mTimeScale);Pending*incoming=nullptr;
 for(auto&p:pending)if(p.handle&&p.id==candidate.id){incoming=&p;break;}
 if(!incoming){candidate={};return;}Proof actual=prove(*incoming,candidate.target,windup,scale,-1,&candidate.facing);
 if(actual.allowed){lease.proof=actual;lease.target=il2cpp_gchandle_new(actual.target,false);lease.weapon=il2cpp_gchandle_new(weapon,false);lease.start=read<float>(player,0x498);if(!lease.target||!lease.weapon){if(lease.target)il2cpp_gchandle_free(lease.target);if(lease.weapon)il2cpp_gchandle_free(lease.weapon);lease={};}else event("finisher_allowed",actual,actual.reason);}
 candidate={};
}
static void scheduled(P weapon,float delay){
 if(lease.weapon&&il2cpp_gchandle_get_target(lease.weapon)==weapon){
  float frame=value<float>(DodgeBridgeDetails::deltaTimeMethod,nullptr,nullptr,-1.f);
  if(!std::isfinite(delay)||!std::isfinite(frame)||frame<=0||frame>.1f){clear("scheduled_effect_timing_unknown");return;}
  lease.proof.hitGame=value<float>(mTime)+std::max(0.f,delay)+frame;
 }
}
static void maintain(){
 if(!lease.target)return;P target=il2cpp_gchandle_get_target(lease.target),weapon=il2cpp_gchandle_get_target(lease.weapon);
 if(lease.proof.owner!=player||!(config()&16)||!(config()&3)||!automationReady()||!alive(target)||!alive(weapon)||call(mCurrentWeapon,player)!=weapon||read<float>(player,0x498)!=lease.start){clear("owner_input_or_attack_changed");return;}
 if(value<bool>(mDead,target)){clear("target_dead_without_confirmed_player_hit");return;}
 if(read<int>(player,fState)==0||value<float>(mTime)>lease.proof.hitGame+lease.proof.safetyGame){clear("attack_ended_or_expected_kill_missing");}
}
static bool continueBefore(Pending&p,P target){
 if(!lease.target||p.id!=lease.proof.id||il2cpp_gchandle_get_target(lease.target)!=target)return false;
 P weapon=il2cpp_gchandle_get_target(lease.weapon);
 int state=alive(player)?read<int>(player,fState):0;
 if(lease.proof.owner!=player||!automationReady()||autoHolding||physicalBlock()||value<bool>(mIsDashing,player)||
 !alive(weapon)||call(mCurrentWeapon,player)!=weapon||
 (state!=1&&state!=2)||read<int>(player,fAttackType)!=1||read<float>(player,0x498)!=lease.start||
 (state==2&&!read<bool>(weapon,0x1B0))){clear("own_attack_cancelled_or_defense_started");return false;}
 float remaining=lease.proof.hitGame-value<float>(mTime);if(remaining<0){clear("expected_hit_time_passed");return false;}
 Proof current=prove(p,target,0,value<float>(mTimeScale),remaining,&lease.proof.facing);
 if(!current.allowed){clear(current.reason);return false;}return true;
}
static void hitResult(P target,P source,P result){
 if(!lease.target||il2cpp_gchandle_get_target(lease.target)!=target||!source||!result)return;
 P dealer=call(method(il2cpp_object_get_class(source),"get_DamageDealer",0),source);
 if(dealer!=player)return;
 Trace::Json j("finisher_hit_result");j.integer("correlation_id",lease.proof.id);j.integer("damage",read<int>(result,0x18));j.integer("blocked",read<int>(result,0x1C));j.boolean("confirmed_kill",value<bool>(mDead,target));Trace::enqueue(j);
 // After the actual hit there is no forecast lease. A surviving target must
 // immediately regain full defense priority, including execution/nonfatal cases.
 clear(value<bool>(mDead,target)?"kill_observed":"target_survived_actual_hit");
}
}
