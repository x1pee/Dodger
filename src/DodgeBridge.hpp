#pragma once
#include "DodgePolicy.hpp"
#include <cstdint>
#include <limits>

// Include after AutomaticDashRequestScope and the IL2CPP/game-thread helpers.
// startBlock needs a forward declaration if its body follows this header.
namespace DodgeBridgeDetails {
static P networkAnimatorMethod, animatorMethod, stateMethod, transitionMethod;
static P speedMethod, modeMethod, unscaledTimeMethod, deltaTimeMethod;
static P unscaledDeltaMethod, strafingMethod;
static bool ready=false,requesting=false;
struct AnimatorInfo {
    int name=0,path=0,fullPath=0;
    float normalized=-1.f,length=0.f,speed=0.f,multiplier=0.f;
    int tag=0,loop=0;
};
static_assert(sizeof(AnimatorInfo)==0x24, "Installed AnimatorStateInfo size");
struct Observation {
    P owner=nullptr,animator=nullptr;
    uint32_t sequence=0,frame=0;
    DWORD tick=0;
    bool valid=false,actualIFrames=false,dashing=false,transition=true;
    int mode=-1;
    float clock=0.f,scale=0.f,animatorSpeed=0.f,rate=0.f,frameSeconds=0.f;
    AnimatorInfo state;
};
static Observation observation;
static bool sameFloat(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&a==b;}
}
struct DodgeCoverage {
    bool skip=false,known=false,dashing=false,actualIFrames=false;
    float remainingMs=0.f,safetyMs=0.f,normalizedTime=-1.f,observedRate=0.f;
    const char* reason="current_iframe_projection_unknown";
};
static bool initDodgeBridge(P pc,P agent){
    using namespace DodgeBridgeDetails;
    P animator=findClass("UnityEngine","Animator"),time=findClass("UnityEngine","Time");
    networkAnimatorMethod=method(agent,"get_NetworkAnimator",0);
    animatorMethod=method(findClass("Unity.Netcode.Components","NetworkAnimator"),"get_Animator",0);
    stateMethod=method(animator,"GetCurrentAnimatorStateInfo",1);
    transitionMethod=method(animator,"IsInTransition",1);
    speedMethod=method(animator,"get_speed",0);modeMethod=method(animator,"get_updateMode",0);
    unscaledTimeMethod=method(time,"get_unscaledTime",0);
    deltaTimeMethod=method(time,"get_deltaTime",0);
    unscaledDeltaMethod=method(time,"get_unscaledDeltaTime",0);
    strafingMethod=method(agent,"get_IsStrafing",0);
    ready=pc&&agent&&networkAnimatorMethod&&animatorMethod&&stateMethod&&transitionMethod&&
        speedMethod&&modeMethod&&unscaledTimeMethod&&deltaTimeMethod&&unscaledDeltaMethod&&
        strafingMethod&&mTime&&mTimeScale&&mDodgeIFrames&&mStaminaCost&&mHasInjury&&
        mCanDash&&mDash&&mIsDashing;
    observation={};return ready;
}
static void observeDodgeState(){
    using namespace DodgeBridgeDetails;
    Observation current;current.owner=player;current.sequence=actualDashSequence;
    current.frame=frames;current.tick=GetTickCount();
    if(!ready||!alive(player)){observation=current;return;}
    current.actualIFrames=value<bool>(mDodgeIFrames,player);
    current.dashing=value<bool>(mIsDashing,player);
    P net=call(networkAnimatorMethod,player);
    current.animator=net?call(animatorMethod,net):nullptr;
    if(!alive(current.animator)){observation=current;return;}
    int layer=3;P args[]={&layer};
    float unknown=std::numeric_limits<float>::quiet_NaN();
    AnimatorInfo invalid;invalid.normalized=unknown;
    current.state=value<AnimatorInfo>(stateMethod,current.animator,args,invalid);
    current.transition=value<bool>(transitionMethod,current.animator,args,true);
    current.mode=value<int>(modeMethod,current.animator,nullptr,-1);
    current.animatorSpeed=value<float>(speedMethod,current.animator,nullptr,unknown);
    current.scale=current.mode==2?1.f:value<float>(mTimeScale,nullptr,nullptr,unknown);
    current.clock=value<float>(current.mode==2?unscaledTimeMethod:mTime,nullptr,nullptr,unknown);
    current.frameSeconds=value<float>(current.mode==2?unscaledDeltaMethod:deltaTimeMethod,nullptr,nullptr,unknown);
    current.valid=(current.mode>=0&&current.mode<=2)&&std::isfinite(current.state.normalized)&&
        std::isfinite(current.clock)&&std::isfinite(current.scale)&&current.scale>0.f&&
        std::isfinite(current.animatorSpeed)&&current.animatorSpeed>0.f&&
        std::isfinite(current.state.speed)&&current.state.speed>0.f&&
        std::isfinite(current.state.multiplier)&&current.state.multiplier>0.f;
    const Observation& previous=observation;
    bool same=previous.valid&&current.valid&&previous.owner==current.owner&&
        previous.animator==current.animator&&previous.sequence==current.sequence&&
        previous.state.fullPath==current.state.fullPath&&previous.state.name==current.state.name&&
        !previous.transition&&!current.transition&&
        (DWORD)(current.tick-previous.tick)<=100;
    bool stable=same&&previous.mode==current.mode&&sameFloat(previous.scale,current.scale)&&
        sameFloat(previous.animatorSpeed,current.animatorSpeed)&&
        sameFloat(previous.state.speed,current.state.speed)&&
        sameFloat(previous.state.multiplier,current.state.multiplier);
    double elapsed=static_cast<double>(current.clock)-previous.clock;
    if(stable&&elapsed==0.&&current.state.normalized>=previous.state.normalized)
        current.rate=previous.rate; // More than one call in the same game frame.
    else current.rate=DodgePolicy::observedNormalizedRate(previous.state.normalized,
        current.state.normalized,elapsed,same,stable);
    observation=current;
}
static DodgeCoverage currentDodgeCoverage(float etaMs,bool ignoreInvulnerability=false){
    using namespace DodgeBridgeDetails;
    observeDodgeState();const Observation& o=observation;DodgeCoverage result;
    result.dashing=o.dashing;result.actualIFrames=o.actualIFrames;
    result.normalizedTime=o.state.normalized;result.observedRate=o.rate;
    if(ignoreInvulnerability){result.known=true;result.reason="attack_bypasses_dodge_invulnerability";return result;}
    if(!ready||!alive(player)){result.reason="dodge_bridge_unavailable";return result;}
    if(!o.actualIFrames){result.known=true;result.reason="current_dash_has_no_iframes";return result;}
    if(etaMs==0.f){result.known=true;result.skip=true;result.reason="current_dodge_iframes_cover_contact_now";return result;}
    if(!o.valid||o.transition||!std::isfinite(o.frameSeconds)||o.frameSeconds<0.f)
        return result;
    // At least one 16ms game/animator-clock frame, expressed in wall milliseconds.
    result.safetyMs=std::max(0.016f,o.frameSeconds)/o.scale*1000.f;
    auto projection=DodgePolicy::currentIFramesCover(o.actualIFrames,o.state.normalized,
        o.rate,o.scale,etaMs,result.safetyMs);
    result.known=projection.known;result.skip=projection.covers;
    result.remainingMs=projection.remainingMilliseconds;result.reason=projection.reason;
    return result;
}
static bool requestDefensiveDodge(const char*& reason){
    using namespace DodgeBridgeDetails;
    if(!ready){reason="dodge_bridge_unavailable";return false;}
    if(requesting){reason="defensive_dodge_request_in_progress";return false;}
    struct RequestScope {RequestScope(){requesting=true;}~RequestScope(){requesting=false;}} requestScope;
    if(!automationReady()){reason="mod_readiness_gate";return false;}
    if(value<bool>(mIsDashing,player,nullptr,true)){reason="existing_dash_in_progress";return false;}
    if(physicalBlock()){reason="physical_block_held_full_dash_unavailable";return false;}
    if(!value<bool>(mCanDash,player)){reason="game_CanDash_rejected";return false;}
    int tiny=13;P injuryArgs[]={&tiny};
    auto availability=[&](){return DodgePolicy::fullDashAvailable(value<bool>(mCanDash,player),
        value<bool>(strafingMethod,player,nullptr,true),read<float>(player,0x5E4),
        value<float>(mStaminaCost,player,nullptr,std::numeric_limits<float>::quiet_NaN()),
        value<int>(mHasInjury,player,injuryArgs,-1));};
    bool released=autoHolding;DWORD oldUntil=holdUntil;
    // Reject stamina/injury problems before needlessly dropping our shield.
    auto preliminary=DodgePolicy::fullDashAvailable(true,false,read<float>(player,0x5E4),
        value<float>(mStaminaCost,player,nullptr,std::numeric_limits<float>::quiet_NaN()),
        value<int>(mHasInjury,player,injuryArgs,-1));
    if(!preliminary.available){reason=preliminary.reason;return false;}
    if(released)releaseBlock();
    auto restore=[&](){
        if(!released||physicalBlock()||!automationReady()||
           value<bool>(mIsDashing,player,nullptr,true)||
           (int32_t)(oldUntil-GetTickCount())<=0)return;
        const char* restoreReason="not_attempted";
        if(startBlock(nullptr,restoreReason,false,0.f))holdUntil=oldUntil;
    };
    auto candidate=availability();
    if(!candidate.available){reason=candidate.reason;restore();return false;}
    if(physicalBlock()){reason="physical_block_held_full_dash_unavailable";restore();return false;}
    const uint32_t previousSequence=actualDashSequence;
    unsigned char context[64]={};P args[]={context};
    {AutomaticDashRequestScope request;call(mDash,player,args);}
    if(actualDashSequence!=previousSequence&&value<bool>(mIsDashing,player)){
        reason="full_dash_started_not_yet_confirmed_evade";return true;
    }
    reason="game_did_not_start_new_defensive_dash";restore();return false;
}
