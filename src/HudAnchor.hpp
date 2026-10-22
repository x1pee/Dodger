#pragma once
// Match the real overhead HUD after its own LateUpdate, including Canvas mode/zoom.
struct HudPoint {float x,y;};
struct HudRect {float x,y,width,height;};
static P haRect,haTransformPoint,haGraphicCanvas,haCanvasRoot,haCanvasMode,haCanvasCamera,haWorldScreen,haGameObject,haActive,haAlpha,haLateUpdate,haBattleInstance,haBattle;
static bool haReady=false,haFaulted=false;static DWORD haSeen;
static void(*realHealthBarLate)(P,P);
static bool haAt(P m,uintptr_t rva){return m&&*(P*)m==(char*)game+rva;}
template<class T>static bool haValue(P m,P object,P*args,T&result){P boxed=call(m,object,args);if(!boxed)return false;result=*(T*)il2cpp_object_unbox(boxed);return true;}
static bool initHudAnchor(){
 P rect=findClass("UnityEngine","RectTransform"),transform=findClass("UnityEngine","Transform"),graphic=findClass("UnityEngine.UI","Graphic"),canvas=findClass("UnityEngine","Canvas"),component=findClass("UnityEngine","Component");
 haRect=method(rect,"get_rect",0);haTransformPoint=method(transform,"TransformPoint",1);haGraphicCanvas=method(graphic,"get_canvas",0);
 haCanvasRoot=method(canvas,"get_rootCanvas",0);haCanvasMode=method(canvas,"get_renderMode",0);haCanvasCamera=method(canvas,"get_worldCamera",0);
 haWorldScreen=method(findClass("UnityEngine","RectTransformUtility"),"WorldToScreenPoint",2);haGameObject=method(component,"get_gameObject",0);
 haActive=method(findClass("UnityEngine","GameObject"),"get_activeInHierarchy",0);haAlpha=method(findClass("UnityEngine","CanvasGroup"),"get_alpha",0);
 haLateUpdate=method(findClass("","UIAgentHealthBar"),"LateUpdate",0);
 P enemies=findClass("","EnemyManager");haBattleInstance=method(enemies,"get_Instance",0);haBattle=method(enemies,"get_BattleActive",0);
 haReady=haAt(haRect,0x37259B0)&&haAt(haTransformPoint,0x3731F60)&&haAt(haGraphicCanvas,0x38790A0)&&haAt(haCanvasRoot,0x3AC9F80)&&
  haAt(haCanvasMode,0x3AC9D20)&&haAt(haCanvasCamera,0x3ACA700)&&haAt(haWorldScreen,0x3ACD690)&&haAt(haGameObject,0x36FDF30)&&haAt(haActive,0x37047D0)&&haAlpha&&haAt(haLateUpdate,0x8435F0)&&haBattleInstance&&haAt(haBattle,0xAA1790);
 return haReady;
}
static bool haGameplayVisible(){
 // The health bar can stay active behind upgrade/score menus. Its visibility
 // alone does not mean the player is in an interactive battle.
 if(!alive(player)||value<bool>(mDead,player,nullptr,true)||!defenseInputAllowed())return false;
 float scale=value<float>(mTimeScale,nullptr,nullptr,0.f);if(!std::isfinite(scale)||scale<=0)return false;
 if(!haBattleInstance||!haAt(haBattle,0xAA1790))return false;
 P enemies=call(haBattleInstance);return alive(enemies)&&value<bool>(haBattle,enemies);
}
static bool haObjectActive(P component){P go=alive(component)?call(haGameObject,component):nullptr;return go&&value<bool>(haActive,go);}
static bool haBounds(P rect,P camera,float&maxX,float&minY){
 if(!haObjectActive(rect))return false;HudRect r={};if(!haValue(haRect,rect,nullptr,r))return false;
 if(!std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||r.width<=0||r.height<=0)return false;
 float right=-1000000,bottom=1000000;
 for(int i=0;i<4;i++){
  V3 local={r.x+((i&1)?r.width:0),r.y+((i&2)?r.height:0),0},world={};P pointArgs[]={&local};if(!haValue(haTransformPoint,rect,pointArgs,world))return false;
  P screenArgs[]={camera,&world};HudPoint p={};if(!haValue(haWorldScreen,nullptr,screenArgs,p)||!std::isfinite(p.x)||!std::isfinite(p.y))return false;right=std::max(right,p.x);bottom=std::min(bottom,p.y);
 }
 maxX=std::max(maxX,right);minY=std::min(minY,bottom);
 return true;
}
static HudPoint haPhysicalCenter(float right,float bottom,int rw,int rh,RECT client,POINT origin,float dpiScale){
 float width=(float)(client.right-client.left),height=(float)(client.bottom-client.top),inset=34*dpiScale;
 // Put the ring to the right of, and below, the actual bars: 8px gap + 26px radius.
 float x=right/rw*width+inset,y=(1-bottom/rh)*height+inset;
 x=std::clamp(x,inset,std::max(inset,width-inset));y=std::clamp(y,inset,std::max(inset,height-inset));
 return {origin.x+x,origin.y+y};
}
static void updateHudAnchor(P bar){
 slot<int>(64)=0;if(!haReady||haFaulted||!haGameplayVisible()||!alive(bar)||read<P>(bar,0x128)!=player||!haObjectActive(bar))return;
 P group=read<P>(bar,0x20);if(group&&value<float>(haAlpha,group)<=0.01f)return;
 P image=read<P>(bar,0x30),canvas=alive(image)?call(haGraphicCanvas,image):nullptr;if(!alive(canvas))return;
 P root=call(haCanvasRoot,canvas);if(!alive(root))return;canvas=root;int mode=-1;if(!haValue(haCanvasMode,canvas,nullptr,mode)||mode<0||mode>2)return;P camera=mode==0?nullptr:call(haCanvasCamera,canvas);if(mode!=0&&!alive(camera))return;
 float right=-1000000,bottom=1000000;bool found=haBounds(read<P>(bar,0x28),camera,right,bottom);
 found=haBounds(read<P>(bar,0x60),camera,right,bottom)||found;if(!found)return;
 int rw=value<int>(mScreenWidth),rh=value<int>(mScreenHeight);HWND w=gameWindow();if(!w||rw<=0||rh<=0)return;
 RECT client={};POINT origin={};if(!GetClientRect(w,&client)||!ClientToScreen(w,&origin)||client.right<=client.left||client.bottom<=client.top)return;
 if(right<0||right>rw||bottom<0||bottom>rh)return;
 float scale=GetDpiForWindow(w)/96.f;if(scale<0.5f||scale>4)scale=1;
 HudPoint center=haPhysicalCenter(right,bottom,rw,rh,client,origin,scale);slot<float>(56)=center.x;slot<float>(60)=center.y;slot<int>(64)=1;haSeen=GetTickCount();
}
static void healthBarLateHook(P self,P mi){
 realHealthBarLate(self,mi);if(!map||faulted||haFaulted||!alive(player)||!alive(self)||read<P>(self,0x128)!=player||(slot<unsigned>(32)&1))return;
 bool writing=false;__try{slot<unsigned>(32)++;writing=true;MemoryBarrier();updateHudAnchor(self);MemoryBarrier();slot<unsigned>(32)++;}
 __except(EXCEPTION_EXECUTE_HANDLER){haFaulted=true;slot<int>(64)=0;if(writing&&(slot<unsigned>(32)&1)){MemoryBarrier();slot<unsigned>(32)++;}Trace::Json j("hud_anchor_fault");j.str("phase","healthbar_late_update");Trace::enqueue(j);}
}
