// Auth.cpp — проверка ключа через HTTP.
// Использует простой блокирующий HTTP в отдельном потоке. HTTPS не встроен;
// в дефолте ходит на URL из .env (http://…), на рендере используй обратный
// прокси или включи системный curl (см. readme).
#include "Auth.h"
#include "Vars.h"
#include "RemotePatch.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>

#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>

// ---- дефолтный URL (переопределяется cmd.txt: auth_url http://...) ---------
static std::string g_url = "http://encore-dlc.onrender.com";
static const char* kKeyPath = "/data/local/tmp/67/key.txt";

namespace auth {
namespace {

std::atomic<Status> g_status{UNKNOWN};
std::string g_statusText = "проверка...";
std::string g_key;
std::string g_hwid;
std::mutex g_mu;
std::atomic<bool> g_inFlight{false};
double g_lastCheck = 0;
double g_lastSuccess = 0;

static double nowSec(){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec*1e-9;
}

// Чтение/запись файла с ключом.
static std::string readFile(const char* path){
    std::string out;
    FILE* f = fopen(path,"rb");
    if(!f) return out;
    char buf[128]; size_t n;
    while((n=fread(buf,1,sizeof(buf),f))>0) out.append(buf,n);
    fclose(f);
    while(!out.empty()&&(out.back()=='\n'||out.back()=='\r'||out.back()==' '||out.back()=='\t')) out.pop_back();
    return out;
}
static bool writeFile(const char* path, const std::string& s){
    mkdir("/data/local/tmp/67",0755);
    FILE* f = fopen(path,"wb"); if(!f) return false;
    fwrite(s.data(),1,s.size(),f); fputc('\n',f); fclose(f);
    return true;
}

// Генерация HWID: ANDROID_ID + ro.serialno + ro.product.model → sha1-like 16 hex.
// Достаточно чтобы различать устройства и нельзя было легко подделать без рута
// на уровне приложений (root у нас и так есть, но пользователь не заинтересован
// подделывать HWID на своём же телефоне — защита от расшаривания ключа).
static std::string hex(uint8_t* d, size_t n){
    static const char h[]="0123456789abcdef"; std::string r; r.reserve(n*2);
    for(size_t i=0;i<n;i++){ r+=h[d[i]>>4]; r+=h[d[i]&0xf]; }
    return r;
}
static uint32_t rotl(uint32_t x,int k){return (x<<k)|(x>>(32-k));}
static void sha1_compress(uint32_t state[5], const uint8_t* block){
    uint32_t w[80];
    for(int i=0;i<16;i++) w[i]=((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|((uint32_t)block[i*4+2]<<8)|block[i*4+3];
    for(int i=16;i<80;i++) w[i]=rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4];
    for(int i=0;i<80;i++){
        uint32_t f,k;
        if(i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
        else if(i<40){f=b^c^d;k=0x6ED9EBA1;}
        else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
        else{f=b^c^d;k=0xCA62C1D6;}
        uint32_t t=rotl(a,5)+f+e+k+w[i]; e=d;d=c;c=rotl(b,30);b=a;a=t;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;
}
static std::string sha1(const std::string& in){
    uint32_t st[5]={0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    uint64_t bits=in.size()*8;
    size_t padded = ((in.size()+9+63)/64)*64;
    uint8_t* buf=(uint8_t*)calloc(padded,1); if(!buf) return std::string(16,'0');
    memcpy(buf,in.data(),in.size());
    buf[in.size()]=0x80;
    for(int i=0;i<8;i++) buf[padded-1-i]=(bits>>(i*8))&0xff;
    for(size_t i=0;i<padded;i+=64) sha1_compress(st,buf+i);
    free(buf);
    uint8_t out[20]; for(int i=0;i<5;i++){out[i*4]=(st[i]>>24)&0xff;out[i*4+1]=(st[i]>>16)&0xff;out[i*4+2]=(st[i]>>8)&0xff;out[i*4+3]=st[i]&0xff;}
    return hex(out,20);
}
static std::string buildHwid(){
    std::string seed;
    char prop[PROP_VALUE_MAX]={0};
    __system_property_get("ro.serialno",prop); seed+=prop; seed+='|';
    prop[0]=0; __system_property_get("ro.build.fingerprint",prop); seed+=prop; seed+='|';
    prop[0]=0; __system_property_get("ro.product.model",prop); seed+=prop; seed+='|';
    prop[0]=0; __system_property_get("net.hostname",prop); seed+=prop;
    if(seed.size()<5) seed="encore-default";
    return sha1(seed).substr(0,16);
}

// ---- очень простой HTTP/1.1 POST ------------------------------------------
struct Url{
    bool ssl=false; std::string host; int port=80; std::string path="/";
};
static bool parseUrl(const std::string& u, Url& out){
    out.ssl=false; out.port=80; out.path="/";
    size_t p=0;
    if(u.find("http://")==0) p=7;
    else if(u.find("https://")==0){p=8;out.ssl=true;out.port=443;}
    else return false;
    size_t sl=u.find('/',p);
    std::string hp = (sl==std::string::npos)?u.substr(p):u.substr(p,sl-p);
    if(sl!=std::string::npos) out.path = u.substr(sl);
    size_t cp=hp.find(':');
    if(cp!=std::string::npos){out.host=hp.substr(0,cp);out.port=atoi(hp.substr(cp+1).c_str());}
    else out.host=hp;
    return !out.host.empty();
}
// Синхронный HTTP POST (без TLS). Если URL https — возвращает ошибку.
static bool httpPost(const std::string& url, const std::string& body, std::string& resp){
    Url u; if(!parseUrl(url,u)) return false;
    if(u.ssl){ resp="{\"ok\":false,\"reason\":\"no_tls_in_build\"}"; return false; }
    struct addrinfo hints={0},*res=nullptr; hints.ai_socktype=SOCK_STREAM;
    int s=-1; bool ok=false;
    char portS[8]; snprintf(portS,sizeof(portS),"%d",u.port);
    if(getaddrinfo(u.host.c_str(),portS,&hints,&res)!=0||!res) return false;
    s=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
    if(s<0) goto done;
    {
        struct timeval tv={5,0}; // таймаут 5 сек
        setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    }
    if(connect(s,res->ai_addr,res->ai_addrlen)<0) goto done;
    char hdr[1024];
    int n=snprintf(hdr,sizeof(hdr),
        "POST %s/api/verify HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        u.path.c_str(),u.host.c_str(),body.size());
    if(send(s,hdr,n,0)<=0) goto done;
    if(send(s,body.data(),body.size(),0)<=0) goto done;
    char rb[2048]; ssize_t r; std::string raw;
    while((r=recv(s,rb,sizeof(rb),0))>0) raw.append(rb,r);
    size_t hdrEnd=raw.find("\r\n\r\n");
    if(hdrEnd==std::string::npos) goto done;
    resp=raw.substr(hdrEnd+4);
    ok=true;
done:
    if(s>=0) close(s);
    freeaddrinfo(res);
    return ok;
}

// Попытаться вызвать системный curl (на многих рут-девайсах он есть в /system/bin
// или toybox wget с -O-). Это даёт HTTPS "бесплатно" без встраивания TLS-стека.
static bool httpPostViaCurl(const std::string& url, const std::string& body, std::string& resp){
    // curl если есть
    {
        FILE* f = popen(("curl -s -m 6 -X POST -H 'Content-Type: application/json' -d '"+body+"' '"+url+"/api/verify' 2>/dev/null").c_str(),"r");
        if(f){
            char b[1024]; size_t n; resp.clear();
            while((n=fread(b,1,sizeof(b),f))>0) resp.append(b,n);
            int r=pclose(f);
            if(r==0 && resp.size()>2 && resp[0]=='{') return true;
        }
    }
    // wget фоллбэк
    {
        char tmp[]="/data/local/tmp/67/auth_XXXXXX";
        int fd=mkstemp(tmp); if(fd>=0) close(fd);
        std::string cmd = "wget -q -O '"+std::string(tmp)+"' --post-data='"+body+"' --header='Content-Type: application/json' --timeout=6 '"+url+"/api/verify' 2>/dev/null";
        int r=system(cmd.c_str());
        if(r==0){
            FILE* f=fopen(tmp,"rb"); if(f){char b[1024];size_t n;resp.clear();while((n=fread(b,1,sizeof(b),f))>0)resp.append(b,n);fclose(f);unlink(tmp);if(resp.size()>2&&resp[0]=='{')return true;}
        }
        unlink(tmp);
    }
    return false;
}

static bool postJson(const std::string& key, const std::string& hwid, std::string& resp){
    char body[256];
    snprintf(body,sizeof(body),"{\"key\":\"%s\",\"hwid\":\"%s\",\"version\":\"3.0\"}",key.c_str(),hwid.c_str());
    // Сначала пробуем curl/wget (работает и по HTTPS), иначе — голый сокет.
    if(httpPostViaCurl(g_url,body,resp)) return true;
    std::string url = g_url;
    // Сокетный клиент работает только по http, для https — через curl выше.
    if(url.find("https://")!=0) return httpPost(url,std::string(body),resp);
    resp = "{\"ok\":false,\"reason\":\"no_https_client\"}";
    return false;
}

static void parseAndApply(const std::string& resp){
    // Простейший парсер JSON-ответов сервера: ищет поля "ok", "reason".
    auto findBool = [&](const char* k) -> int {
        std::string key = std::string("\"")+k+"\""; size_t p=resp.find(key); if(p==std::string::npos) return -1;
        p=resp.find(':',p+key.size()); if(p==std::string::npos) return -1;
        p=resp.find_first_of("tf01",p+1); if(p==std::string::npos) return -1;
        if(resp[p]=='t') return 1; if(resp[p]=='f') return 0; return -1;
    };
    auto findStr = [&](const char* k) -> std::string {
        std::string key = std::string("\"")+k+"\""; size_t p=resp.find(key); if(p==std::string::npos) return "";
        p=resp.find(':',p+key.size()); if(p==std::string::npos) return "";
        p=resp.find('"',p+1); if(p==std::string::npos) return "";
        size_t e=p+1; while(e<resp.size()&&resp[e]!='"'){if(resp[e]=='\\'&&e+1<resp.size())e++;e++;}
        std::string out;
        for(size_t i=p+1;i<e;i++){if(resp[i]=='\\'&&i+1<e){char c=resp[i+1];if(c=='n')out.push_back('\n');else if(c=='t')out.push_back('\t');else if(c=='"'||c=='\\'||c=='/')out.push_back(c);else out.push_back(c);i++;}else out.push_back(resp[i]);}
        return out;
    };
    int ok=findBool("ok"); std::string reason=findStr("reason");
    std::lock_guard<std::mutex> lk(g_mu);
    if(ok==1){
        g_status=OK; g_statusText="Активирован"; g_lastSuccess=nowSec();
    } else if(ok==0){
        g_status=BAD_KEY;
        if(reason=="hwid_mismatch") g_statusText="Ключ привязан к другому устройству";
        else if(reason=="banned") g_statusText="Ключ заблокирован";
        else if(reason=="expired") g_statusText="Ключ истёк";
        else if(reason=="invalid_key") g_statusText="Неверный ключ";
        else g_statusText = reason.size()?reason.c_str():"Ключ недействителен";
    } else {
        g_status=NET_ERROR; g_statusText="Сетевая ошибка";
    }
}

static void doCheck(){
    if(g_inFlight.exchange(true)) return;
    std::string key = g_key, hw = g_hwid;
    std::thread([key,hw](){
        std::string resp;
        bool ok = false;
        if(key.size()>=10) ok = postJson(key,hw,resp);
        if(ok) parseAndApply(resp);
        else { std::lock_guard<std::mutex> lk(g_mu); g_status=NET_ERROR; g_statusText="Нет соединения с сервером"; }
        g_inFlight=false;
    }).detach();
}

} // namespace

void init(){
    g_key = readFile(kKeyPath);
    g_hwid = buildHwid();
    printf("[auth] hwid=%s key_len=%zu url=%s\n",g_hwid.c_str(),g_key.size(),g_url.c_str()); fflush(stdout);
    if(!g_key.empty()) doCheck();
    else { std::lock_guard<std::mutex> lk(g_mu); g_status=NO_KEY; g_statusText="Введи ключ в меню (Settings → Key)"; }
    g_lastCheck = nowSec();
}
void tick(){
    double t=nowSec();
    Status s = g_status.load();
    if(s==NO_KEY || s==BAD_KEY) return; // ждём ввода/исправления от пользователя
    double interval = (s==OK)?120.0:15.0; // ok — раз в 2 мин; wait/err — раз в 15 сек
    if(t - g_lastCheck > interval){ g_lastCheck = t; doCheck(); }
}
Status status(){ return g_status.load(); }
const char* statusText(){ std::lock_guard<std::mutex> lk(g_mu); return g_statusText.c_str(); }
bool needsInput(){ Status s=g_status.load(); return s==NO_KEY||s==BAD_KEY; }
void setKey(const std::string& k){
    std::string kk = k; while(!kk.empty()&&(kk.back()=='\n'||kk.back()=='\r'||kk.back()==' '))kk.pop_back();
    if(kk.empty())return;
    g_key = kk;
    writeFile(kKeyPath,kk);
    g_lastCheck = 0;
    doCheck();
}
std::string getKey(){ return g_key; }
std::string hwid(){ return g_hwid; }
void setServerUrl(const std::string& u){ if(!u.empty()) g_url=u; }
std::string getServerUrl(){ return g_url; }

} // namespace auth
