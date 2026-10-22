#pragma once
// Bounds are a conservative *attack reservation*, not proof for an automatic
// dodge. Precision Physics and the actual contact still choose the defense.
static bool supportedBow(P weapon){return alive(weapon)&&apMethodAt(method(il2cpp_object_get_class(weapon),"AttackEffect",13),0x7C6A70)&&DefensePolicy::projectile(read<int>(weapon,0x1C8),false).arrow;}
static bool rangedWindup(P actor,P weapon){return supportedBow(weapon)||(alive(weapon)&&read<int>(actor,fAttackType)==2);}
static void resolveBowShot(P self,const ProjectileTrace&t){
 if(!t.arrowKnown&&!t.contact)return;
 P thrower=read<P>(self,0x118);
 for(auto&p:pending)if(p.handle&&!p.bowResolved&&il2cpp_gchandle_get_target(p.handle)==thrower&&(int)(t.born-p.createdTick)>=0&&p.directionLocked){
   p.bowResolved=true;
   if(p.reservationLatched){Trace::Json j("ranged_reservation_handoff");j.integer("correlation_id",p.id);j.integer("projectile_id",t.id);j.boolean("flight_bounds_known",t.arrowKnown);j.boolean("potential_flight_hit",t.arrowIntersects);j.boolean("direct_contact",t.contact);j.str("reason","committed_release_transfers_reservation_to_flight");Trace::enqueue(j);}
 }

}
static bool arrowBoundsWindow(ProjectileProbe::Bounds hero,ProjectileProbe::Bounds arrow,V3 heroVelocity,V3 arrowVelocity,float gravity,float&first,float&last){
 using namespace ProjectileProbe;float radius=std::sqrt(dot(arrow.extents,arrow.extents));
 if(!valid(hero)||!valid(arrow)||!finite(heroVelocity)||!finite(arrowVelocity)||!std::isfinite(gravity)||hero.extents.y<=0||radius<=0)return false;
 V3 ext=add(hero.extents,{radius,radius,radius}),offset=sub(hero.center,arrow.center),relative=sub(heroVelocity,arrowVelocity);first=0;last=Horizon;
 if(!slab(offset.x,relative.x,ext.x,first,last)||!slab(offset.z,relative.z,ext.z,first,last))return false;
 Window windows[5];int count=verticalWindows(offset.y,relative.y,-gravity,ext.y,first,last,windows);if(!count)return false;
 first=windows[0].first;last=windows[count-1].last;return true;
}
static void updateArrowPriority(P self,ProjectileTrace&t){
 using namespace ProjectileProbe;if(!ready||t.hallucination||t.contact)return;
  if(agentContactIsNoop(self)){t.arrowKnown=true;t.arrowIntersects=false;t.arrowSeen=GetTickCount();t.arrowDue=t.arrowLatest=0;resolveBowShot(self,t);return;}
 DWORD previousReading=t.arrowSeen;bool previousHit=t.arrowIntersects;t.arrowKnown=false;
 P hc=read<P>(player,0x1D8),pc=read<P>(self,0x148),hr=read<P>(player,0x190),pr=read<P>(self,0x140);Bounds h={},p={};V3 hv={},pv={};
 float scale=value<float>(mTimeScale),gravity=read<float>(self,0x150);bool enabled=false,global=false,kinematic=false;float damping=-1;int frozen=0;
 if(!alive(hc)||!alive(pc)||!alive(hr)||!alive(pr)||!get(colliderEnabled,pc,nullptr,enabled)||!enabled||!get(colliderBounds,hc,nullptr,h)||!get(colliderBounds,pc,nullptr,p)||!valid(h)||!valid(p)||h.extents.y<=0||dot(p.extents,p.extents)<=0||!get(linearVelocity,hr,nullptr,hv)||!get(linearVelocity,pr,nullptr,pv)||!finite(hv)||!finite(pv)||!get(isKinematic,pr,nullptr,kinematic)||kinematic||!get(linearDamping,pr,nullptr,damping)||!std::isfinite(damping)||damping>.000001f||!get(useGravity,pr,nullptr,global)||!std::isfinite(gravity)||!std::isfinite(scale)||scale<=0)return;
 if(global){V3 g={};if(!get(physicsGravity,nullptr,nullptr,g)||!finite(g)||std::fabs(g.x)>.000001f||std::fabs(g.z)>.000001f)return;gravity+=g.y;}
 if(!get(constraints,pr,nullptr,frozen))return;
 if(frozen&2)pv.x=0;if(frozen&4){pv.y=0;gravity=0;}if(frozen&8)pv.z=0;
 // Reserve the swept orientation envelope about COM for all launched weapons,
 // including javelins. This is deliberately not the activation proof.
 V3 mass={};if(!worldCenterOfMass||!get(worldCenterOfMass,pr,nullptr,mass)||!finite(mass))return;
 float envelope=std::sqrt(dot(p.extents,p.extents))+std::sqrt(dot(sub(p.center,mass),sub(p.center,mass)));
 if(!std::isfinite(envelope))return;p.center=mass;
 // For attack reservation a detonating projectile threatens its real blast
 // footprint as well as its body. This envelope alone never starts a dodge.
 float blast=read<bool>(self,0xA4)?read<float>(self,0xA8):0;
 if(!std::isfinite(blast)||blast<0||blast>100)return;
 p.extents={envelope+blast,0,0};
 float first,last,stoppedFirst,stoppedLast;bool moving=arrowBoundsWindow(h,p,hv,pv,gravity,first,last),stopped=arrowBoundsWindow(h,p,{},pv,gravity,stoppedFirst,stoppedLast);
 DWORD now=GetTickCount();t.arrowKnown=true;t.arrowIntersects=moving||stopped;t.arrowSeen=now;
 if(stopped){if(moving){first=std::min(first,stoppedFirst);last=std::max(last,stoppedLast);}else{first=stoppedFirst;last=stoppedLast;}}
 t.arrowDue=t.arrowIntersects?now+(DWORD)std::floor(first/scale*1000):0;t.arrowLatest=t.arrowIntersects?now+(DWORD)std::ceil(last/scale*1000):0;
 resolveBowShot(self,t);
 if(!previousReading||previousHit!=t.arrowIntersects){Trace::Json j("arrow_priority_flight");j.integer("correlation_id",t.id);j.boolean("potential_hit",t.arrowIntersects);j.boolean("hit_if_attack_stops_movement",stopped);j.number("arrival_earliest_ms",t.arrowIntersects?first/scale*1000:-1);j.number("arrival_latest_ms",t.arrowIntersects?last/scale*1000:-1);j.str("purpose","reserve_attack_only_not_proof_for_dodge");Trace::enqueue(j);}
}
static bool arrowReserveWindow(const ProjectileTrace&t,DWORD now,float&ms,float&latest){
 if(t.contact||t.hallucination||!t.arrowIntersects||(DWORD)(now-t.lastSeen)>=100||(!t.arrowKnown&&(DWORD)(now-t.arrowSeen)>200)||(int)(t.arrowLatest+20-now)<0)return false;
 ms=std::max(0.f,(float)(int)(t.arrowDue-now));latest=std::max(ms,(float)(int)(t.arrowLatest-now));return true;
}
