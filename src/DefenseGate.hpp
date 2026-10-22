#pragma once
// A character input lock is not a UI/pause permission. Never alter the lock or
// bypass CanDash/OnBlockStart; only remove the mod's redundant broad veto.
static P dgSessionInstance,dgPaused,dgLevel,dgLevelState,dgInBattle,dgFocus;
static bool dgReady=false;
static bool initDefenseGate(){
 P session=findClass("","NetworkSession");
 dgSessionInstance=method(session,"get_Instance",0);dgPaused=method(session,"get_IsPaused",0);dgLevel=method(session,"get_Level",0);
 dgLevelState=method(findClass("","GameLevel"),"get_ActiveGameState",0);dgInBattle=method(findClass("","Player"),"get_InBattle",0);
 dgFocus=method(findClass("","UIClientNavigation"),"IsAnyInstanceInFocus",1);
 auto at=[](P m,uintptr_t rva){return m&&*(P*)m==(char*)game+rva;};
 dgReady=dgSessionInstance&&at(dgPaused,0x6EE9A0)&&at(dgLevel,0x6EE9F0)&&at(dgLevelState,0xAAD7B0)&&at(dgInBattle,0x722260)&&at(dgFocus,0x90F220);return dgReady;
}
static bool defenseInputAllowed(){
 if(!alive(player))return false;
 if(!value<bool>(mBlocked,player,nullptr,true))return true;
 if(!dgReady||read<int>(player,0x6A0)<=0)return false;
 float scale=value<float>(mTimeScale,nullptr,nullptr,0.f);if(!std::isfinite(scale)||scale<=0)return false;
 P session=call(dgSessionInstance);if(!alive(session)||value<bool>(dgPaused,session,nullptr,true))return false;
 P level=call(dgLevel,session);if(!alive(level)||value<int>(dgLevelState,level,nullptr,-1)!=0||read<bool>(level,0x1F0)||!value<bool>(dgInBattle,player))return false;
 P user=read<P>(player,fInput);if(!user)return false;P args[]={user};
 if(value<bool>(dgFocus,nullptr,args,true))return false;
 return value<bool>(mCanCancelBlock,player)||value<bool>(mCanDash,player);
}
