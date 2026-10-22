#pragma once
#include <algorithm>
#include <cmath>
namespace DefensePolicy {
struct Threat {bool unblockable=false,shieldRisk=false,arrow=false,burning=false,explosive=false;};
struct BlockCapacity {bool available=false;const char* reason="no_shield_or_hand_upgrade";};
inline BlockCapacity blockCapacity(bool hasShield,bool shieldOnBack,int handSkill,bool weaponPreventsBlock){
    if(weaponPreventsBlock)return {false,"weapon_prevents_block"};
    if((hasShield&&!shieldOnBack)||handSkill>0)return {true,"block_capacity_available"};
    return {false,hasShield&&shieldOnBack?"shield_on_back":"no_shield_or_hand_upgrade"};
}
inline bool needsBlockRollback(bool wasHolding,bool physicalHold,bool wasStrafing,bool nowStrafing){return !wasHolding&&!physicalHold&&!wasStrafing&&nowStrafing;}
inline bool canReserve(unsigned flags,bool blockAvailable,Threat threat){return (flags&2)||((flags&1)&&blockAvailable&&!threat.unblockable);}
inline Threat weapon(bool ignore,bool specialOnly,float charge,bool breaks,bool rips,int flame=0,int damage=0){return {ignore||(specialOnly&&charge>0),breaks||rips,false,flame==2||damage==5};}
inline Threat projectile(int type,bool power,int flame=0,int damage=0,bool aoe=false){return {type==400||type==401||type==403||type==405||type==1002,power||aoe||type==100,type==103||type==116||type==117,flame==2||damage==5,aoe||type==100};}
inline bool slowArrow(Threat threat,float availableSpeed,float baseSpeed){return threat.arrow&&std::isfinite(availableSpeed)&&std::isfinite(baseSpeed)&&availableSpeed>=0&&baseSpeed>0&&availableSpeed<=baseSpeed*.65f;}
// The scheduling deadline must follow the enabled/preferred defense. An
// equipped shield does not provide a fallback when automatic blocking is off.
inline float predictiveDeadline(unsigned flags,Threat threat,bool blockAvailable,bool canCancelBlock,bool preferDodge,float normalLeadMilliseconds,float dodgeLeadMilliseconds){
    float normal=std::clamp(normalLeadMilliseconds,0.f,500.f);
    bool dodgeNeeded=!(flags&1)||preferDodge||threat.unblockable||threat.shieldRisk||threat.burning||!blockAvailable||!canCancelBlock;
    if((flags&2)&&dodgeNeeded&&std::isfinite(dodgeLeadMilliseconds)&&dodgeLeadMilliseconds>=0)return dodgeLeadMilliseconds;
    return normal;
}
// The game rejects normal attack cancellation throughout windup. During winddown
// dodge (and block with a shield in hand) opens after 0.25s + the weapon's live
// cancellation offset, or when the attack ends, whichever comes first.
// These are game-clock seconds; no fixed wall-time reservation floor is needed.
inline float reserveMilliseconds(float windup,float winddown,float cancelOffset,bool cancelInWinddown,float timeScale){
    if(!std::isfinite(windup)||!std::isfinite(winddown)||!std::isfinite(cancelOffset)||!std::isfinite(timeScale)||timeScale<=0)return 0;
    float full=std::max(0.f,windup)+std::max(0.f,winddown);
    float lock=cancelInWinddown?std::min(full,std::max(0.f,windup)+std::max(0.f,0.25f+cancelOffset)):full;
    return lock/timeScale*1000;
}
}
