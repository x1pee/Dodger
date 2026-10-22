#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Include after DodgeBridge.hpp. Sampling and logging run only on the game's
// main thread. A learned startup is an observed scheduling estimate, never a
// claim that a newly requested dash already has invulnerability.
struct DodgeStartupLead {
 bool available=false,measured=false,reliable=false,windowKnown=false,physicsFrameKnown=false;
 float milliseconds=-1,onsetLowerMilliseconds=0,onsetUpperMilliseconds=0,frameMarginMilliseconds=0;
 float earliestImpactMilliseconds=0,latestImpactMilliseconds=-1,usableWindowMilliseconds=0;
 uint32_t onsetSamples=0,windowSamples=0,sequence=0;
 const char*reason="dodge_startup_observer_inactive";
};
namespace DodgeStartupDetails {
using Observation=DodgeBridgeDetails::Observation;
struct Key {P owner=nullptr,animator=nullptr;int mode=-1;float scale=0,speed=0,stateSpeed=0,multiplier=0,physicsScale=0;};
struct History {
 Key key;bool valid=false,hasOnset=false,hasWindow=false,invalidWindow=false;
 float onsetLower=0,onsetUpper=0,endLower=0,frame=0;
 float dodgeStateSpeed=0,dodgeMultiplier=0;int dodgePath=0;
 uint32_t onsetSamples=0,windowSamples=0;
};
struct Episode {
 bool active=false,seenFalse=false,seenTrue=false,edgeLogged=false;
 uint32_t sequence=0,trueSamples=0;float start=0,lastFalse=0,firstTrue=0,lastTrue=0,frame=0;
};
static bool active=false;
struct StartNote {Key key;uint32_t sequence=0;float clock=0;DWORD tick=0;bool valid=false;};
static StartNote startNote;
static P fixedDeltaMethod=nullptr;
static History history;
static Episode episode;
static Observation previous;
static bool havePrevious=false,physicsFrameKnown=false;
static float currentFrame=0;
static const char*status="dodge_startup_observer_inactive";
static Key keyOf(const Observation&o){float physicsScale=o.mode==2?value<float>(mTimeScale,nullptr,nullptr,std::numeric_limits<float>::quiet_NaN()):o.scale;return {o.owner,o.animator,o.mode,o.scale,o.animatorSpeed,o.state.speed,o.state.multiplier,physicsScale};}
// Idle and dodge are different animator states and may legitimately have
// different state.speed/multiplier. Global clock/Animator changes invalidate
// tracking; the actual dodge playback profile is versioned at its first iframe.
static bool same(const Key&a,const Key&b){return a.owner==b.owner&&a.animator==b.animator&&a.mode==b.mode&&a.scale==b.scale&&a.physicsScale==b.physicsScale&&a.speed==b.speed;}
static bool usable(const Observation&o){return o.valid&&o.owner==player&&o.sequence==actualDashSequence&&alive(o.owner)&&alive(o.animator)&&
 std::isfinite(o.clock)&&std::isfinite(o.scale)&&o.scale>0&&std::isfinite(o.frameSeconds)&&o.frameSeconds>=0&&
 std::isfinite(o.animatorSpeed)&&o.animatorSpeed>0&&std::isfinite(o.state.speed)&&o.state.speed>0&&std::isfinite(o.state.multiplier)&&o.state.multiplier>0&&
 (DWORD)(GetTickCount()-o.tick)<=100;}
static void clear(const char*reason){history={};episode={};previous={};havePrevious=false;currentFrame=0;physicsFrameKnown=false;status=reason;}
static void clearMeasurements(const char*reason){Key key=history.key;bool valid=history.valid;history={};history.key=key;history.valid=valid;status=reason;}
static bool frameQuantum(const Observation&o,float&frame){
 frame=o.frameSeconds;physicsFrameKnown=false;
 // FixedUpdate may be less frequent than animation/render samples. Respect its
 // real wall-clock quantum when this optional existing Unity method is present.
 if(fixedDeltaMethod){
   float unknown=std::numeric_limits<float>::quiet_NaN();
   float fixed=value<float>(fixedDeltaMethod,nullptr,nullptr,unknown);
   float physicsScale=o.mode==2?value<float>(mTimeScale,nullptr,nullptr,unknown):o.scale;
   if(!std::isfinite(fixed)||fixed<=0||!std::isfinite(physicsScale)||physicsScale<=0)return false;
   frame=std::max(frame,fixed/physicsScale*o.scale);physicsFrameKnown=true;
 }
 return std::isfinite(frame)&&frame>0;
}
static void logEdge(const Observation&o,bool accepted,float lower,float upper,const char*reason){
 Trace::Json j("dash_first_iframe_observed");j.integer("dash_sequence",o.sequence);j.pointer("player",o.owner);
 j.boolean("actual_iframes",o.actualIFrames);j.boolean("startup_measurement_accepted",accepted);j.boolean("confirmed_evade",false);
 j.str("proof",accepted?"game_thread_false_to_true_IsDodgeIFrames_observation":"IsDodgeIFrames_already_true_no_new_edge_proven");j.str("reason",reason);j.integer("animator_mode",o.mode);
 j.number("clock_scale",o.scale);j.number("onset_lower_clock_seconds",lower);j.number("onset_upper_clock_seconds",upper);
 j.number("onset_upper_wall_ms",upper/o.scale*1000);j.number("frame_margin_wall_ms",currentFrame/o.scale*1000);
 j.integer("onset_samples",history.onsetSamples);Trace::enqueue(j);
}
}
static bool initDodgeStartup(){
 using namespace DodgeStartupDetails;active=DodgeBridgeDetails::ready;clear(active?"dodge_startup_bootstrap":"dodge_startup_bridge_unavailable");
 startNote={};
 fixedDeltaMethod=active?method(findClass("UnityEngine","Time"),"get_fixedDeltaTime",0):nullptr;
 return active;
}
// Call only from the successful Player.StartDash hook, immediately after its
// sequence increment and a fresh observeDodgeState(). This records Unity's exact
// current animation/game clock; GetTickCount is never converted into start time.
static void noteDodgeStartupStart(){
 using namespace DodgeStartupDetails;if(!active)return;const Observation&o=DodgeBridgeDetails::observation;
 startNote={};if(!usable(o)||!o.sequence||!o.dashing)return;
 startNote.key=keyOf(o);if(!std::isfinite(startNote.key.physicsScale)||startNote.key.physicsScale<=0)return;
 startNote.sequence=o.sequence;startNote.clock=o.clock;startNote.tick=o.tick;startNote.valid=true;
}
static void observeDodgeStartup(){
 using namespace DodgeStartupDetails;if(!active)return;
 const Observation&o=DodgeBridgeDetails::observation;
 if(!usable(o)){clear("dodge_startup_observation_unavailable");return;}
 float frame=0;if(!frameQuantum(o,frame)){clear("dodge_startup_frame_quantum_unknown");return;}bool fixedKnown=physicsFrameKnown;
 Key key=keyOf(o);
 if(!std::isfinite(key.physicsScale)||key.physicsScale<=0){clear("dodge_startup_physics_clock_unavailable");return;}
 bool changed=history.valid&&!same(history.key,key);
 bool gap=havePrevious&&((DWORD)(o.tick-previous.tick)>100||o.clock<previous.clock);
 if(changed||gap)clear(changed?"dodge_startup_context_changed":"dodge_startup_sampling_gap");
 currentFrame=frame;physicsFrameKnown=fixedKnown;
 if(!history.valid){history.valid=true;history.key=key;}
 // Repeated coverage checks in one frame must not manufacture new true samples.
 bool duplicate=havePrevious&&o.sequence==previous.sequence&&o.clock==previous.clock&&o.actualIFrames==previous.actualIFrames;
 bool newSequence=!havePrevious||o.sequence!=previous.sequence;
 bool markedStart=startNote.valid&&startNote.sequence==o.sequence&&same(startNote.key,key)&&
   (DWORD)(o.tick-startNote.tick)<=100&&o.clock>=startNote.clock;
 if(history.invalidWindow&&((!o.dashing&&!o.actualIFrames)||(newSequence&&markedStart)))clearMeasurements("dodge_startup_bootstrap_relearning");
 bool profileChanged=history.hasOnset&&(o.actualIFrames||(!episode.active&&o.state.fullPath==history.dodgePath))&&
   (o.state.speed!=history.dodgeStateSpeed||o.state.multiplier!=history.dodgeMultiplier);
 if(profileChanged){
   bool expectedFirstEdge=episode.active&&!episode.seenTrue;
   clearMeasurements("dodge_startup_dodge_playback_changed");
   if(!expectedFirstEdge)episode.active=false;
 }
 if(newSequence){
   episode={};episode.sequence=o.sequence;
   if(o.sequence&&o.dashing&&markedStart){
     episode.active=true;episode.start=startNote.clock;episode.lastFalse=o.clock;episode.seenFalse=!o.actualIFrames;episode.frame=frame;
   }else if(o.sequence&&o.dashing)status="dodge_startup_start_not_marked_freshly";
 }
 if(episode.active&&!duplicate){
   episode.frame=std::max(episode.frame,frame);
   float elapsed=o.clock-episode.start;
   if(!std::isfinite(elapsed)||elapsed<0||elapsed>2.f){episode.active=false;status="dodge_startup_episode_clock_invalid";}
   else if(!episode.seenTrue){
     if(!o.actualIFrames){episode.seenFalse=true;episode.lastFalse=o.clock;if(!o.dashing){episode.active=false;status="dash_ended_without_observed_iframes";}}
     else if(!episode.edgeLogged){
       episode.edgeLogged=true;
       float lower=std::max(0.f,episode.lastFalse-episode.start),upper=elapsed;
       bool accepted=episode.seenFalse&&upper>=lower;
       if(accepted){
         if(!history.hasOnset){history.onsetLower=lower;history.onsetUpper=upper;}else{history.onsetLower=std::min(history.onsetLower,lower);history.onsetUpper=std::max(history.onsetUpper,upper);}
         history.hasOnset=true;history.dodgeStateSpeed=o.state.speed;history.dodgeMultiplier=o.state.multiplier;history.dodgePath=o.state.fullPath;
         history.frame=std::max(history.frame,episode.frame);++history.onsetSamples;episode.seenTrue=true;episode.firstTrue=episode.lastTrue=o.clock;episode.trueSamples=1;
         status="dodge_startup_measured_onset";
       }else{episode.active=false;status="dodge_startup_iframes_already_active_at_start";}
       logEdge(o,accepted,lower,upper,status);
     }
   }else if(o.actualIFrames){if(o.clock>episode.lastTrue){episode.lastTrue=o.clock;++episode.trueSamples;}}
   else{
     // The last true sample is a lower bound on the window end. The subsequent
     // false sample is not treated as an exact endpoint or extra immunity.
     if(episode.trueSamples>=2&&episode.lastTrue>episode.firstTrue){
       float end=episode.lastTrue-episode.start;
       history.endLower=history.hasWindow?std::min(history.endLower,end):end;history.frame=std::max(history.frame,episode.frame);history.hasWindow=true;++history.windowSamples;
       history.invalidWindow=history.endLower-history.frame<history.onsetUpper+history.frame;
       Trace::Json j("dash_iframe_window_observed");j.integer("dash_sequence",o.sequence);j.number("last_true_clock_seconds_from_start",end);
       j.number("first_false_clock_seconds_from_start",elapsed);j.number("observed_usable_clock_seconds",episode.lastTrue-episode.firstTrue);
       j.integer("window_samples",history.windowSamples);j.boolean("confirmed_evade",false);Trace::enqueue(j);
     }
     episode.active=false;status=history.hasWindow?"dodge_startup_measured_window":"dodge_startup_window_not_bracketed";
   }
 }
 previous=o;havePrevious=true;
}
static float defensiveDodgeLeadMilliseconds(DodgeStartupLead*details=nullptr){
 using namespace DodgeStartupDetails;DodgeStartupLead result;float unknown=std::numeric_limits<float>::quiet_NaN();
 if(active)observeDodgeStartup();
 const Observation&o=DodgeBridgeDetails::observation;
 if(!active||!history.valid||!usable(o)){result.reason=status;if(details)*details=result;return unknown;}
 result.sequence=o.sequence;result.onsetSamples=history.onsetSamples;result.windowSamples=history.windowSamples;
 result.measured=history.hasOnset;result.windowKnown=history.hasWindow;result.physicsFrameKnown=physicsFrameKnown;
 float frame=std::max(currentFrame,history.frame),leadClock=0;
 if(history.hasOnset){leadClock=history.onsetUpper+frame;result.onsetLowerMilliseconds=history.onsetLower/o.scale*1000;result.onsetUpperMilliseconds=history.onsetUpper/o.scale*1000;}
 else{
   // Config slot16 is the startup fallback in a scale1 animation/game clock.
   // Preserve its duration when Normal/Fixed animations run in slow motion.
   float bootstrap=map?slot<float>(16):90.f;
   if(!std::isfinite(bootstrap)||bootstrap<0){result.reason="dodge_startup_bootstrap_invalid";if(details)*details=result;return unknown;}
   leadClock=std::max(bootstrap*.001f,frame);result.onsetUpperMilliseconds=bootstrap/o.scale;
 }
 result.frameMarginMilliseconds=frame/o.scale*1000;result.earliestImpactMilliseconds=leadClock/o.scale*1000;
 if(history.hasWindow){
   float latestClock=history.endLower-frame;result.latestImpactMilliseconds=latestClock/o.scale*1000;
   result.usableWindowMilliseconds=std::max(0.f,history.endLower-history.onsetUpper)/o.scale*1000;
   if(history.invalidWindow||latestClock<leadClock||latestClock<=0){result.reason="dodge_startup_observed_window_too_narrow";if(details)*details=result;return unknown;}
 }
 result.milliseconds=result.earliestImpactMilliseconds;result.available=std::isfinite(result.milliseconds)&&result.milliseconds>=0;
 result.reliable=history.onsetSamples>=2&&history.windowSamples>=2&&history.hasWindow;
 result.reason=history.hasOnset?(result.reliable?"dodge_startup_measured_repeated":"dodge_startup_measured_limited_samples"):"dodge_startup_bootstrap";
 if(details)*details=result;return result.available?result.milliseconds:unknown;
}
