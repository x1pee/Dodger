#pragma once
// Bounded, nonblocking producer queue. Only the writer thread touches the disk.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <charconv>
#include <share.h>

namespace Trace {
constexpr size_t Capacity=8192, QueueSize=512;
static volatile LONG64 sequence=0;
static volatile LONG dropped=0, ready=0, stopping=0;
static SRWLOCK queueLock=SRWLOCK_INIT;
struct Slot { unsigned length; char line[Capacity]; };
static Slot queue[QueueSize];
static unsigned head=0,tail=0,count=0;
static HANDLE worker=nullptr;
static wchar_t directory[MAX_PATH],stem[100];

struct Json {
    char data[Capacity]; size_t used=0; bool valid=true;
    unsigned depth=0; bool first[16]={};
    void raw(const char* s) {
        size_t n=strlen(s); if(!valid||used+n>=Capacity){valid=false;return;}
        memcpy(data+used,s,n);used+=n;data[used]=0;
    }
    void format(const char* fmt,...) {
        if(!valid)return; va_list a;va_start(a,fmt);
        int n=vsnprintf_s(data+used,Capacity-used,_TRUNCATE,fmt,a);va_end(a);
        if(n<0){valid=false;return;}used+=n;
    }
    void quoted(const char* s) {
        raw("\"");for(const unsigned char* p=(const unsigned char*)(s?s:"");*p&&valid;++p){
            if(*p=='"')raw("\\\"");else if(*p=='\\')raw("\\\\");
            else if(*p<0x20)format("\\u%04x",(unsigned)*p);
            else {char c[2]={(char)*p,0};raw(c);}
        }raw("\"");
    }
    void prefix(const char* key) {
        if(!depth||depth>16){valid=false;return;}
        if(!first[depth-1])raw(",");first[depth-1]=false;
        if(key){quoted(key);raw(":");}
    }
    void str(const char* k,const char* v){prefix(k);quoted(v);}
    void integer(const char* k,int64_t v){prefix(k);format("%lld",(long long)v);}
    void number(const char* k,double v){prefix(k);if(std::isfinite(v)){char buffer[64];auto result=std::to_chars(buffer,buffer+sizeof(buffer)-1,v,std::chars_format::general,6);if(result.ec==std::errc{}){*result.ptr=0;raw(buffer);}else valid=false;}else raw("null");}
    void boolean(const char* k,bool v){prefix(k);raw(v?"true":"false");}
    void pointer(const char* k,const void* p){prefix(k);format("\"0x%llx\"",(unsigned long long)(uintptr_t)p);}
    void object(const char* k){prefix(k);raw("{");if(depth>=16){valid=false;return;}first[depth++]=true;}
    void array(const char* k){prefix(k);raw("[");if(depth>=16){valid=false;return;}first[depth++]=true;}
    void endObject(){if(depth)depth--;else valid=false;raw("}");}
    void endArray(){if(depth)depth--;else valid=false;raw("]");}
    explicit Json(const char* event){
        data[0]=0;raw("{");depth=1;first[0]=true;
        SYSTEMTIME t;GetSystemTime(&t);char utc[40];
        sprintf_s(utc,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
        str("utc",utc);integer("tick_ms",(int64_t)GetTickCount64());
        integer("seq",InterlockedIncrement64(&sequence));integer("pid",GetCurrentProcessId());str("event",event);
    }
    bool finish(){if(depth!=1){valid=false;return false;}endObject();return valid;}
};

static void enqueue(Json& j) {
    if(!j.finish()){InterlockedIncrement(&dropped);return;}
    if(!TryAcquireSRWLockExclusive(&queueLock)){InterlockedIncrement(&dropped);return;}
    if(count==QueueSize){ReleaseSRWLockExclusive(&queueLock);InterlockedIncrement(&dropped);return;}
    Slot& s=queue[tail];s.length=(unsigned)j.used;memcpy(s.line,j.data,j.used);
    tail=(tail+1)%QueueSize;count++;ReleaseSRWLockExclusive(&queueLock);
}
static void message(const char* s){Json j("message");j.str("text",s);enqueue(j);}
static FILE* openPart(unsigned part) {
    wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\%s-part%03u.jsonl",directory,stem,part);
    return _wfsopen(path,L"ab",_SH_DENYNO);
}
static DWORD WINAPI writeLoop(void*) {
    unsigned part=0;FILE* file=openPart(part);InterlockedExchange(&ready,file?1:2);
    LONG reported=0;ULONGLONG heartbeat=0;
    for(;;){
        bool wrote=false;
        for(unsigned i=0;i<QueueSize;i++){
            Slot item;AcquireSRWLockExclusive(&queueLock);
            if(!count){ReleaseSRWLockExclusive(&queueLock);break;}
            item=queue[head];head=(head+1)%QueueSize;count--;ReleaseSRWLockExclusive(&queueLock);
            if(!file){InterlockedIncrement(&dropped);continue;}
            if(fwrite(item.line,1,item.length,file)!=item.length||fputc('\n',file)==EOF){
                InterlockedIncrement(&dropped);fclose(file);file=nullptr;InterlockedExchange(&ready,2);
            }else if(file)wrote=true;
        }
        LONG lost=InterlockedCompareExchange(&dropped,0,0);
        if(file&&lost!=reported){Json j("log_overflow");j.integer("dropped_total",lost);j.finish();fwrite(j.data,1,j.used,file);fputc('\n',file);reported=lost;wrote=true;}
        if(file&&GetTickCount64()-heartbeat>=10000){
            Json j("logger_heartbeat");j.integer("dropped_total",lost);j.finish();fwrite(j.data,1,j.used,file);fputc('\n',file);heartbeat=GetTickCount64();wrote=true;
        }
        if(file&&wrote&&fflush(file)!=0){fclose(file);file=nullptr;InterlockedExchange(&ready,2);}
        if(file&&_ftelli64(file)>=32LL*1024*1024){fclose(file);file=openPart(++part);InterlockedExchange(&ready,file?1:2);}
        if(InterlockedCompareExchange(&stopping,0,0)){
            AcquireSRWLockShared(&queueLock);bool empty=count==0;ReleaseSRWLockShared(&queueLock);if(empty)break;
        }
        Sleep(100);
    }
    if(file)fclose(file);return 0;
}
static bool start(const wchar_t* root) {
    swprintf_s(directory,L"%s\\logs",root);
    if(!CreateDirectoryW(directory,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){InterlockedExchange(&ready,2);return false;}
    SYSTEMTIME t;GetSystemTime(&t);
    swprintf_s(stem,L"combat-%04u%02u%02u-%02u%02u%02u-%03u-p%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId());
    worker=CreateThread(nullptr,0,writeLoop,nullptr,0,nullptr);
    if(!worker){InterlockedExchange(&ready,2);return false;}return true;
}
static void shutdown(){InterlockedExchange(&stopping,1);if(worker){WaitForSingleObject(worker,5000);CloseHandle(worker);worker=nullptr;}}
}
