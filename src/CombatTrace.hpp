#pragma once
// Included after the version-gated IL2CPP helpers. Called only on the game thread.
static P enemyClass,agentClass,mEnemyType,mEnemyHallucination,mEffectActive,mEffectDuration,mHasSkill,mHasInjury,mHasPurchase;
static P mHealth,mStaminaCost,mStaminaMax,mInvulnerable,mIncapacitated,mMovementBlocked,mWaitingGrapple,mCancelDodge,mShieldOnBack,mShieldActive,mTimeScale;
static DWORD diagnosticPoll,injuryPoll;static P diagnosticPlayer;
static uint32_t markerSeen,confirmedProjectileDodges,actualDashSequence;static DWORD actualDashTick,confirmedProjectileDodgeTick;
static P mGameLibraryInstance,mPickupDict,mStartingDurability,mTutorialInstance,mTutorialLevel,mTutorialStage,mHasBeastSkill;
static int effectiveInjuries[sizeof(GameEnums::InjuryType)/sizeof(GameEnums::Entry)]={};
static bool diagnosticReady=false;
static uint64_t combatId=0,lastDefenseId=0;static DWORD lastDefenseTick;static const char* lastDefenseAction="none";
static uint64_t nextCombatId(){return ++combatId;}
static const char* className(P object){return object?il2cpp_class_get_name(il2cpp_object_get_class(object)):"null";}
static void vectorJson(Trace::Json&j,const char*key,V3 v){j.object(key);j.number("x",v.x);j.number("y",v.y);j.number("z",v.z);j.endObject();}
static int enumValue(P m,P object,int kind){P a[]={&kind};return value<int>(m,object,a);}
static bool purchase(int kind){P a[]={&kind};return value<bool>(mHasPurchase,player,a);}
struct ShieldState {P data=nullptr;int type=-1,durability=0,special=0,model=0,hallucination=-1;uint64_t origin=0;};
static uint32_t confirmedFullBlocks=0,shieldBreakSequence=0;
static uint64_t shieldBreakIdentity=0,hudShieldGeneration=0,hudShieldCurrentIdentity=0;
static P hudShieldOwner=nullptr,hudShieldData=nullptr;static int hudShieldType=-1;static uint64_t hudShieldOrigin=0;
// A generation, rather than an exposed pointer or item type, separates identical
// shields and a respawn/re-equip from durability changes of the current shield.
static uint64_t hudShieldIdentity(ShieldState s){
    if(player!=hudShieldOwner){hudShieldOwner=player;hudShieldData=nullptr;hudShieldCurrentIdentity=0;hudShieldType=-1;hudShieldOrigin=0;}
    if(!s.data||s.type<0||s.hallucination>=0){hudShieldData=nullptr;hudShieldCurrentIdentity=0;hudShieldType=-1;hudShieldOrigin=0;return 0;}
    if(s.data!=hudShieldData||s.type!=hudShieldType||s.origin!=hudShieldOrigin){
        hudShieldData=s.data;hudShieldType=s.type;hudShieldOrigin=s.origin;
        if(++hudShieldGeneration==0)++hudShieldGeneration;hudShieldCurrentIdentity=hudShieldGeneration;
    }
    return hudShieldCurrentIdentity;
}
static ShieldState shieldState(){ShieldState s;if(alive(player)){s.data=read<P>(player,0x1F8);if(s.data){s.type=read<int>(s.data,0x10);s.durability=read<int>(s.data,0x14);s.special=read<int>(s.data,0x18);s.model=read<int>(s.data,0x1C);s.hallucination=read<int>(s.data,0x48);s.origin=read<uint64_t>(s.data,0x40);}}return s;}
struct ShieldReference {int maximum=0,source=0,definition=0,observedPeak=0,characterFactor=1,effectiveBase=0;};
static P observedShieldData;static int observedShieldType=-1,observedShieldPeak=0,referenceType=-1,referenceDefinition=0;static uint64_t observedShieldOrigin;static DWORD referenceProbe;
static ShieldReference shieldReference(ShieldState s){
    if(!s.data){observedShieldData=nullptr;observedShieldType=-1;observedShieldOrigin=0;observedShieldPeak=0;return {};}
    if(s.data!=observedShieldData||s.type!=observedShieldType||s.origin!=observedShieldOrigin){observedShieldData=s.data;observedShieldType=s.type;observedShieldOrigin=s.origin;observedShieldPeak=std::max(0,s.durability);}
    else observedShieldPeak=std::max(observedShieldPeak,s.durability);
    DWORD now=GetTickCount();if(referenceType!=s.type){referenceType=s.type;referenceDefinition=0;referenceProbe=0;}
    if(referenceDefinition<=0&&(!referenceProbe||(DWORD)(now-referenceProbe)>=1000)){
        referenceProbe=now;P library=call(mGameLibraryInstance);P dict=library?call(mPickupDict,library):nullptr;
        if(dict){int type=s.type;P args[]={&type};P definition=call(method(il2cpp_object_get_class(dict),"get_Item",1),dict,args);if(definition)referenceDefinition=value<int>(mStartingDurability,definition);}
    }
    ShieldReference result;result.definition=referenceDefinition;result.observedPeak=observedShieldPeak;
    // EquipShield doubles the stored current durability for an active Rhino beast skill.
    // Only the definition reference needs that factor here; current durability is already adjusted by the game.
    int rhino=5;P beastArgs[]={&rhino};result.characterFactor=value<bool>(mHasBeastSkill,player,beastArgs)?2:1;
    if(referenceDefinition>0){result.effectiveBase=referenceDefinition*result.characterFactor;result.maximum=std::max(result.effectiveBase,observedShieldPeak);
        result.source=observedShieldPeak>result.effectiveBase?(result.characterFactor==2?4:2):(result.characterFactor==2?3:1);}return result;
}
static const char* shieldReferenceSource(int source){switch(source){case 1:return "definition";case 2:return "observed_above_definition";case 3:return "rhino_adjusted_definition";case 4:return "observed_above_rhino_definition";default:return "unknown";}}
static bool shieldNoWear(ShieldState s){
    if(s.type==309)return true;P tutorial=call(mTutorialInstance);return tutorial&&value<bool>(mTutorialLevel,tutorial)&&value<int>(mTutorialStage,tutorial)<=14;
}
struct ShieldTelemetry {unsigned flags=0;int type=-1,current=0,referenceMax=0,source=0;uint64_t identity=0;};
static void shieldJson(Trace::Json&j,const char*key,ShieldState s){
    j.object(key);j.boolean("data_present",s.data!=nullptr);j.integer("type",s.type);j.str("name",GameEnums::name(GameEnums::PickupType,s.type));
    j.integer("raw_durability",s.durability);j.integer("special_durability",s.special);j.integer("model_index",s.model);j.integer("hallucination_index",s.hallucination);
    ShieldReference reference=shieldReference(s);j.integer("display_reference_max",reference.maximum);j.integer("definition_starting_durability",reference.definition);j.integer("character_durability_factor",reference.characterFactor);j.integer("character_adjusted_definition",reference.effectiveBase);j.integer("observed_peak_durability",reference.observedPeak);j.str("max_source",shieldReferenceSource(reference.source));j.integer("max_source_kind",reference.source);j.boolean("no_wear",s.data&&shieldNoWear(s));
    j.str("durability_unit","blocked_hits_before_preservation_modifiers");j.endObject();
}
static ShieldState lastShield;
static void refreshInjuries(bool force=false){DWORD now=GetTickCount();if(!force&&(DWORD)(now-injuryPoll)<1000)return;injuryPoll=now;
    for(unsigned i=0;i<sizeof(effectiveInjuries)/sizeof(int);i++)effectiveInjuries[i]=enumValue(mHasInjury,player,GameEnums::InjuryType[i].id);
}
static void playerJson(Trace::Json&j,const char*key){
    j.object(key);j.pointer("id",player);if(!alive(player)){j.boolean("available",false);j.endObject();return;}
    j.boolean("available",true);j.number("game_time",value<float>(mTime));j.number("time_scale",value<float>(mTimeScale));
    j.number("health01",value<float>(mHealth,player));vectorJson(j,"position",value<V3>(mPosition,player));vectorJson(j,"forward",value<V3>(mModelForward,player));
    j.boolean("dead",value<bool>(mDead,player));j.boolean("input_blocked",value<bool>(mBlocked,player));j.integer("input_block_count",read<int>(player,0x6A0));
    j.boolean("incapacitated",value<bool>(mIncapacitated,player));j.boolean("movement_blocked",value<bool>(mMovementBlocked,player));j.boolean("waiting_grapple",value<bool>(mWaitingGrapple,player));
    j.boolean("can_cancel_to_block",value<bool>(mCanCancelBlock,player));j.boolean("can_cancel_to_dodge",value<bool>(mCancelDodge,player));j.boolean("can_dash",value<bool>(mCanDash,player));
    j.boolean("is_dashing",value<bool>(mIsDashing,player));j.boolean("dodge_iframes",value<bool>(mDodgeIFrames,player));bool invulnerable=value<bool>(mInvulnerable,player);int damageImmunityEffect=17;P immunityArgs[]={&damageImmunityEffect};bool damageImmunity=value<bool>(mEffectActive,player,immunityArgs);
    j.boolean("invulnerable",invulnerable);j.boolean("damage_immunity_effect17",damageImmunity);j.boolean("effective_damage_immunity",invulnerable||damageImmunity);
    j.number("stamina",read<float>(player,0x5E4));j.number("stamina_max",value<float>(mStaminaMax,player));j.number("dash_stamina_cost",value<float>(mStaminaCost,player));
    j.number("dash_timer",read<float>(player,0x5C8));j.number("dash_cooldown_end",read<float>(player,0x5D8));j.number("short_dash_cooldown_end",read<float>(player,0x5DC));
    j.boolean("executing",read<bool>(player,0x798));j.integer("attack_state",read<int>(player,0x490));j.integer("attack_type",read<int>(player,0x494));
    j.boolean("has_shield",value<bool>(mHasShield,player));j.boolean("shield_on_back",value<bool>(mShieldOnBack,player));j.boolean("shield_active",value<bool>(mShieldActive,player));j.boolean("is_blocking",value<bool>(mBlocking,player));
    j.integer("hand_block_skill",enumValue(mHasSkill,player,2002));j.integer("shield_preservation_skill",enumValue(mHasSkill,player,260));j.boolean("double_durability_purchase",purchase(2501));
    P w=call(mCurrentWeapon,player);if(alive(w)){P ignore=method(il2cpp_object_get_class(w),"get_IgnoreHolderBlocks",0);j.boolean("weapon_forbids_holder_block",value<bool>(ignore,w));P data=read<P>(w,0x170);int weaponType=data?read<int>(data,0x10):-1;j.integer("own_weapon_type",weaponType);j.str("own_weapon_name",GameEnums::name(GameEnums::PickupType,weaponType));j.number("own_weapon_charge",read<float>(w,0xC4));j.number("own_attack_started_game_time",read<float>(player,0x498));}
    shieldJson(j,"shield",shieldState());
    j.array("effects");for(const auto&e:GameEnums::EffectType){int k=e.id;P a[]={&k};if(value<bool>(mEffectActive,player,a)){j.object(nullptr);j.integer("id",k);j.str("name",e.name);j.number("remaining_seconds",value<float>(mEffectDuration,player,a));j.endObject();}}j.endArray();
    j.integer("injuries_cache_age_ms",(DWORD)(GetTickCount()-injuryPoll));j.array("injuries_effective");
    for(unsigned i=0;i<sizeof(effectiveInjuries)/sizeof(int);i++)if(effectiveInjuries[i]){j.object(nullptr);j.integer("id",GameEnums::InjuryType[i].id);j.str("name",GameEnums::InjuryType[i].name);j.integer("level",effectiveInjuries[i]);j.endObject();}
    j.endArray();j.endObject();
}
static bool enemy(P a){return alive(a)&&enemyClass&&il2cpp_class_is_assignable_from(enemyClass,il2cpp_object_get_class(a));}
// Match Enemy.IsHallucination, not the visual ShouldLookLikeHallucination or the player's injury list.
static bool actorIsHallucination(P a){return enemy(a)&&value<bool>(mEnemyHallucination,a);}
static bool projectileIsHallucination(P p){return alive(p)&&(read<int>(p,0x108)>=0||actorIsHallucination(read<P>(p,0x118)));}
static void weaponJson(Trace::Json&j,P w){
    j.object("weapon");j.pointer("id",w);if(alive(w)){P data=read<P>(w,0x170);int type=data?read<int>(data,0x10):-1;j.integer("type",type);j.str("name",GameEnums::name(GameEnums::PickupType,type));
        j.boolean("ignore_block",read<bool>(w,0x3C));j.boolean("ignore_block_special_only",read<bool>(w,0x3D));j.boolean("insta_break_shield",read<bool>(w,0x3E));j.boolean("rips_shield_off",read<bool>(w,0xF8));j.number("charge_timer",read<float>(w,0xC4));
    }j.endObject();
}
static void actorJson(Trace::Json&j,const char*key,P a){
    j.object(key);j.pointer("id",a);if(alive(a)){j.str("class",className(a));if(!agentClass||!il2cpp_class_is_assignable_from(agentClass,il2cpp_object_get_class(a))){j.endObject();return;}j.integer("network_id",read<uint64_t>(a,0x50));int type=enemy(a)?value<int>(mEnemyType,a):-1;
        j.integer("unit_type",type);j.str("unit_name",GameEnums::name(GameEnums::EnemyType,type));j.boolean("targets_local",read<P>(a,0x200)==player);j.integer("attack_state",read<int>(a,0x490));j.integer("attack_type",read<int>(a,0x494));vectorJson(j,"position",value<V3>(mPosition,a));weaponJson(j,call(mCurrentWeapon,a));
    }j.endObject();
}
struct SeenUnit{uintptr_t object=0;uint64_t network=0;};static SeenUnit seenUnits[512];static unsigned seenUnitNext;
static SeenUnit ignoredHallucinationActors[512];static unsigned ignoredHallucinationActorNext;
static bool noteIgnoredHallucinationActor(P a,const char*origin){
    if(!actorIsHallucination(a))return false;SeenUnit key={(uintptr_t)a,read<uint64_t>(a,0x50)};
    for(const auto&s:ignoredHallucinationActors)if(s.object==key.object&&s.network==key.network)return true;
    ignoredHallucinationActors[ignoredHallucinationActorNext++%512]=key;
    Trace::Json j("hallucination_actor_ignored");j.str("origin",origin);j.str("reason","game_IsHallucination");actorJson(j,"actor",a);Trace::enqueue(j);return true;
}
static void noteUnit(P a){if(!enemy(a)||noteIgnoredHallucinationActor(a,"unit_seen"))return;SeenUnit key={(uintptr_t)a,read<uint64_t>(a,0x50)};for(const auto&s:seenUnits)if(s.object==key.object&&s.network==key.network)return;
    seenUnits[seenUnitNext++%512]=key;Trace::Json j("unit_seen");actorJson(j,"unit",a);Trace::enqueue(j);
}
static void projectileJson(Trace::Json&j,P p){
    j.object("projectile");j.pointer("id",p);if(alive(p)){int type=read<int>(p,0xF0);j.integer("type",type);j.str("name",GameEnums::name(GameEnums::PickupType,type));j.integer("generation",read<int>(p,0x178));j.integer("hallucination_index",read<int>(p,0x108));j.boolean("hallucination",projectileIsHallucination(p));j.integer("origin_network_id",read<uint64_t>(p,0x100));
        j.boolean("power_throw",read<bool>(p,0x1A1));j.boolean("aoe",read<bool>(p,0xA4));j.number("aoe_radius",read<float>(p,0xA8));j.integer("aoe_damage",read<int>(p,0xAC));j.integer("base_damage",read<int>(p,0x28));j.integer("smash",read<int>(p,0x2C));
        j.integer("flammable_state",read<int>(p,0x9C));j.number("burn_duration",read<float>(p,0xA0));j.integer("damage_type",read<int>(p,0x98));j.number("stun",read<float>(p,0x30));j.number("trip",read<float>(p,0x34));j.number("weaken",read<float>(p,0x3C));j.number("gravity",read<float>(p,0x150));
        j.boolean("collided",read<bool>(p,0x158));j.boolean("bounced",read<bool>(p,0x159));j.boolean("embedded",read<bool>(p,0x15A));vectorJson(j,"travel",read<V3>(p,0x124));vectorJson(j,"velocity",value<V3>(mProjectileVelocity,p));
    }j.endObject();
}
static bool projectileNoBlock(P p){int type=read<int>(p,0xF0);return read<bool>(p,0xA4)||type==100||read<bool>(p,0x1A1)||type==400||type==401||type==403||type==405||type==1002;}
struct ProjectileTrace{uintptr_t object=0;int generation=0;uint64_t id=0;DWORD lastSeen=0,lastDecision=0;bool contact=false,intersects=false;DWORD due=0,latestDue=0;DefensePolicy::Threat policy;bool hallucination=false,ignoredReported=false;const char*lastPredictionReason=nullptr;bool geometryKnown=false;DWORD born=0,arrowDue=0,arrowLatest=0,arrowSeen=0;bool arrowKnown=false,arrowIntersects=false;};static ProjectileTrace projectileTraces[512];static unsigned projectileTraceNext;
static ProjectileTrace& projectileTrace(P p){uintptr_t object=(uintptr_t)p;int generation=read<int>(p,0x178);for(auto&t:projectileTraces)if(t.object==object&&t.generation==generation){t.lastSeen=GetTickCount();t.policy=DefensePolicy::projectile(read<int>(p,0xF0),read<bool>(p,0x1A1),read<int>(p,0x9C),read<int>(p,0x98),read<bool>(p,0xA4));t.hallucination=projectileIsHallucination(p);return t;}
    auto&t=projectileTraces[projectileTraceNext++%512];t={object,generation,nextCombatId(),GetTickCount(),0,false};t.born=GetTickCount();t.policy=DefensePolicy::projectile(read<int>(p,0xF0),read<bool>(p,0x1A1),read<int>(p,0x9C),read<int>(p,0x98),read<bool>(p,0xA4));t.hallucination=projectileIsHallucination(p);return t;
}
static bool noteIgnoredHallucinationProjectile(P p,const char*origin){
    if(!projectileIsHallucination(p))return false;auto&t=projectileTrace(p);t.intersects=false;t.due=0;
    if(!t.ignoredReported){t.ignoredReported=true;Trace::Json j("hallucination_projectile_ignored");j.integer("correlation_id",t.id);j.str("origin",origin);j.str("reason","hallucination_index_or_thrower");projectileJson(j,p);actorJson(j,"thrower",read<P>(p,0x118));Trace::enqueue(j);}return true;
}
static bool listContains(P list,P object){if(!list)return false;P items=read<P>(list,0x10);int n=read<int>(list,0x18);if(!items||n<0||n>4096)return false;for(int i=0;i<n;i++)if(read<P>(items,0x20+i*sizeof(P))==object)return true;return false;}
static void logShieldTransition(ShieldState before,ShieldState after,uint64_t id){
    lastShield=after;
    if(before.data==after.data&&before.type==after.type&&before.durability==after.durability&&before.special==after.special)return;
    Trace::Json j("shield_changed");j.integer("correlation_id",id);shieldJson(j,"before",before);shieldJson(j,"after",after);j.boolean("lost",before.data&&!after.data);Trace::enqueue(j);
}
static void diagnosticTick(){
    DWORD now=GetTickCount();if(!diagnosticReady)return;
    if(diagnosticPlayer!=player){diagnosticPlayer=player;refreshInjuries(true);lastShield=shieldState();markerSeen=slot<uint32_t>(100);Trace::Json j("player_acquired");playerJson(j,"player");Trace::enqueue(j);}
    uint32_t marker=slot<uint32_t>(100);if(marker!=markerSeen){markerSeen=marker;refreshInjuries();Trace::Json j("manual_marker");j.integer("marker_id",marker);j.integer("flags",config());playerJson(j,"player");Trace::enqueue(j);}
    ShieldState shield=shieldState();logShieldTransition(lastShield,shield,0);lastShield=shield;
    if((DWORD)(now-diagnosticPoll)>=1000){diagnosticPoll=now;refreshInjuries();Trace::Json j("player_state");j.integer("flags",config());j.boolean("game_foreground",gameWindow()!=nullptr);j.integer("dropped_records",InterlockedCompareExchange(&Trace::dropped,0,0));playerJson(j,"player");Trace::enqueue(j);}
}
