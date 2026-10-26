#include "OnlineMenuAssets.h"
#include "SWFStructures.h"
#include "../Core/Logger.h"
#include "../UI/resource.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace HookCrashers::SWF::OnlineMenuAssets {
namespace {
constexpr std::size_t kLimit = 64u * 1024 * 1024;
struct Tag { std::uint16_t code; std::size_t begin, payload, end; };
std::atomic<std::uint32_t> g_Ids {0};
std::mutex g_BufferLock;
std::vector<std::unique_ptr<std::vector<std::uint8_t>>> g_Buffers;
unsigned U16(const std::uint8_t* p) { return p[0] | (unsigned(p[1]) << 8); }
std::uint32_t U32(const std::uint8_t* p) { return U16(p) | (U16(p+2) << 16); }
void Put16(std::vector<std::uint8_t>& b, unsigned n) { b.push_back(n & 255); b.push_back((n >> 8) & 255); }
void Set16(std::vector<std::uint8_t>& b, std::size_t p, unsigned n) { b[p] = n & 255; b[p+1] = (n >> 8) & 255; }
void Set32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t n) {
    for (unsigned i=0; i<4; ++i) b[p+i] = static_cast<std::uint8_t>(n >> (8*i));
}
bool ReadTag(const std::uint8_t* b, std::size_t bytes, std::size_t offset, Tag& t) {
    if (offset > bytes || bytes-offset < 2) return false;
    const auto word = U16(b+offset); t = {static_cast<std::uint16_t>(word >> 6), offset, offset+2, 0};
    std::size_t length = word & 63;
    if (length == 63) { if (bytes-t.payload < 4) return false; length=U32(b+t.payload); t.payload+=4; }
    if (length > bytes-t.payload) return false;
    t.end=t.payload+length; return true;
}
bool Header(const std::uint8_t* b, std::size_t n, std::size_t& tags) {
    if (!b || n<13 || n>kLimit || std::memcmp(b,"FWS",3)) return false;
    tags=8+(5+4*(b[8]>>3)+7)/8+4;
    return tags<=n;
}
bool Character(unsigned code) {
    switch (code) { case 2: case 6: case 7: case 10: case 11: case 20: case 21:
    case 22: case 32: case 33: case 34: case 35: case 36: case 37: case 39:
    case 46: case 48: case 60: case 75: case 83: case 84: case 87: case 90: case 91: return true; }
    return false;
}
void AppendTag(std::vector<std::uint8_t>& out, unsigned code, const std::vector<std::uint8_t>& payload) {
    Put16(out,(code<<6)|63); const auto p=out.size(); out.resize(p+4); Set32(out,p,static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(),payload.begin(),payload.end());
}
using Remap = std::array<std::uint16_t,65536>;
bool PatchReferences(std::vector<std::uint8_t>& payload, std::size_t start, const Remap& map) {
    for (Tag t {}; start<payload.size(); start=t.end) {
        if (!ReadTag(payload.data(),payload.size(),start,t)) return false;
        const auto n=t.end-t.payload;
        std::size_t idOffset=0;
        if (t.code==4 && n>=4) idOffset=t.payload;
        if (t.code==26 && n>=5 && (payload[t.payload]&2)) idOffset=t.payload+3;
        if (idOffset) { const auto old=U16(payload.data()+idOffset); if (!map[old]) return false; Set16(payload,idOffset,map[old]); }
        if (t.code==0) return n==0;
    }
    return false;
}
void PushString(std::vector<std::uint8_t>& actions,const char* value) {
    const auto length=std::strlen(value); actions.push_back(0x96); Put16(actions,static_cast<unsigned>(length+2));
    actions.push_back(0); actions.insert(actions.end(),value,value+length+1);
}
void SetVisible(std::vector<std::uint8_t>& actions,const char* object,bool visible) {
    PushString(actions,object); actions.push_back(0x1C); PushString(actions,"_visible");
    const std::uint8_t boolean[] {0x96,0x02,0x00,0x05,static_cast<std::uint8_t>(visible)};
    actions.insert(actions.end(),std::begin(boolean),std::end(boolean)); actions.push_back(0x4F);
}
bool StopFirstFrame(std::vector<std::uint8_t>& payload,bool initializeBrowser=false) {
    for (Tag t {}; ReadTag(payload.data(),payload.size(),t.end ? t.end : 4,t);) {
        if (t.code==1) {
            std::vector<std::uint8_t> actions;
            if (initializeBrowser) {
                // The restored screen has no live directory provider yet. Do not
                // display its design-time "Player name/0/4/FIXME" sample rows.
                for (const char* row : {"statusbar","statusbar2","statusbar3","statusbar4","baroverlayset"}) SetVisible(actions,row,false);
                SetVisible(actions,"centertext",true);
                PushString(actions,"centertext"); actions.push_back(0x1C); PushString(actions,"text");
                PushString(actions,"No lobbies available"); actions.push_back(0x4F);
            }
            actions.push_back(0x07); actions.push_back(0);
            std::vector<std::uint8_t> encoded; AppendTag(encoded,12,actions);
            payload.insert(payload.begin()+t.begin,encoded.begin(),encoded.end()); return true;
        }
        if (!t.code) return false;
    }
    return false;
}
bool Import(const std::uint8_t* data, std::size_t bytes, bool selector, unsigned& nextId,
            std::vector<std::uint8_t>& definitions, std::uint16_t& root) {
    std::size_t start=0; if (!Header(data,bytes,start) || U32(data+4)!=bytes) return false;
    Remap map {}; bool ended=false;
    for (Tag t {}; start<bytes; start=t.end) {
        if (!ReadTag(data,bytes,start,t)) return false;
        if (!t.code) { ended=t.end-t.payload==0; break; }
        if (!Character(t.code)) continue;
        if (t.end-t.payload<2 || !nextId || nextId>=65535) return false;
        const auto old=U16(data+t.payload); if (!old || map[old]) return false;
        map[old]=static_cast<std::uint16_t>(nextId++);
    }
    if (!ended) return false;
    Header(data,bytes,start);
    for (Tag t {}; start<bytes; start=t.end) {
        if (!ReadTag(data,bytes,start,t)) return false;
        if (!t.code) break;
        if (!Character(t.code) && t.code!=13 && t.code!=88) continue;
        if (t.end-t.payload<2) return false;
        const auto old=U16(data+t.payload);
        if (!map[old]) return false;
        std::vector<std::uint8_t> p(data+t.payload,data+t.end); Set16(p,0,map[old]);
        if (t.code==39) {
            if (p.size()<4 || !PatchReferences(p,4,map)) return false;
            if ((selector && old==292) || (!selector && (old==12 || old==15 || old==23 || old==26)))
                if (!StopFirstFrame(p,!selector && old==26)) return false;
        } else if (t.code==37) {
            if (p.size()<3) return false;
            const std::size_t flags=2+(5+4*(p[2]>>3)+7)/8;
            if (flags+2>p.size()) return false;
            // SWF flags are bit fields: HasFont is bit 0 of the first byte.
            if (p[flags]&1) {
                if (flags+4>p.size()) return false;
                const auto font=U16(p.data()+flags+2); if (!map[font]) return false;
                Set16(p,flags+2,map[font]);
            }
        }
        AppendTag(definitions,t.code,p);
    }
    root=map[selector ? 292 : 26]; if (!root) return false;
    std::vector<std::uint8_t> exports; Put16(exports,1); Put16(exports,root);
    const char* name=selector ? "onlinecompatibility" : "playerlobbies";
    exports.insert(exports.end(),name,name+std::strlen(name)+1); AppendTag(definitions,56,exports);
    return true;
}
// COK6's MENU has a legacy SWF FileLength smaller than its physical tag stream.
// Walk only checked tag boundaries to its root End; catch inaccessible memory.
std::size_t MeasurePhysical(const std::uint8_t* b) {
    __try {
        if (!b || std::memcmp(b,"FWS",3)) return 0;
        std::size_t p=8+(5+4*(b[8]>>3)+7)/8+4;
        for (Tag t {}; p<kLimit; p=t.end) {
            if (!ReadTag(b,kLimit,p,t)) return 0;
            // Touch the end too: no unvalidated copy of an inaccessible payload.
            if (t.end>t.payload) { volatile auto checked=b[t.end-1]; (void)checked; }
            if (!t.code) return t.payload==t.end ? t.end : 0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 0;
}
const std::uint8_t* Resource(unsigned id, std::size_t& bytes) {
    auto module=reinterpret_cast<HMODULE>(&__ImageBase);
    auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    bytes=resource ? SizeofResource(module,resource) : 0;
    return bytes ? static_cast<const std::uint8_t*>(LockResource(LoadResource(module,resource))) : nullptr;
}
}
bool Build(const std::uint8_t* menu,std::size_t bytes,const std::uint8_t* selector,std::size_t selectorBytes,
           const std::uint8_t* lobbies,std::size_t lobbyBytes,std::vector<std::uint8_t>& output,SpriteIds& ids) {
    std::size_t p=0; if (!Header(menu,bytes,p)) return false;
    unsigned highest=0; bool stockMenu=false,ended=false; std::size_t insertion=0;
    for (Tag t {}; p<bytes; p=t.end) {
        if (!ReadTag(menu,bytes,p,t)) return false;
        if (!t.code) { ended=t.end==t.payload; insertion=t.begin; break; }
        if (Character(t.code) && t.end-t.payload>=2) {
            const auto id=U16(menu+t.payload); highest=(std::max)(highest,id);
        }
        if (t.code==56 && t.end-t.payload>=2) {
            std::size_t entry=t.payload+2;
            for (unsigned count=U16(menu+t.payload); count; --count) {
                if (entry+2>t.end) return false;
                highest=(std::max)(highest,U16(menu+entry)); entry+=2;
                const auto* zero=static_cast<const std::uint8_t*>(std::memchr(menu+entry,0,t.end-entry));
                if (!zero) return false;
                const std::string name(reinterpret_cast<const char*>(menu+entry),static_cast<std::size_t>(zero-menu-entry));
                if (name=="playwithfriends") stockMenu=true;
                if (name=="onlinecompatibility" || name=="playerlobbies") return false;
                entry=static_cast<std::size_t>(zero-menu)+1;
            }
        }
    }
    if (!ended || !stockMenu || highest>=65000) return false;
    unsigned next=highest+1; SpriteIds staged {}; std::vector<std::uint8_t> definitions;
    if (!Import(selector,selectorBytes,true,next,definitions,staged.selector) ||
        !Import(lobbies,lobbyBytes,false,next,definitions,staged.lobbies)) return false;
    std::vector<std::uint8_t> patched(menu,menu+insertion);
    patched.insert(patched.end(),definitions.begin(),definitions.end());
    patched.insert(patched.end(),menu+insertion,menu+bytes);
    if (patched.size()>kLimit) return false;
    Set32(patched,4,static_cast<std::uint32_t>(patched.size()));
    output=std::move(patched); ids=staged; return true;
}
SpriteIds CurrentIds() { const auto ids=g_Ids.load(std::memory_order_acquire); return {static_cast<std::uint16_t>(ids),static_cast<std::uint16_t>(ids>>16)}; }
bool PatchScene(void* rawScene) {
    auto* scene=static_cast<SWFScene*>(rawScene); if (!scene || !scene->swfBuffer) return false;
    std::size_t selectorBytes=0,lobbyBytes=0;
    const auto* selector=Resource(IDR_HOOKCRASHERS_ONLINE_MENU,selectorBytes);
    const auto* lobby=Resource(IDR_HOOKCRASHERS_LOBBY_MENU,lobbyBytes);
    if (!selector || !lobby) return false;
    const auto bytes=MeasurePhysical(scene->swfBuffer); if (!bytes) return false;
    auto patched=std::make_unique<std::vector<std::uint8_t>>(); SpriteIds ids {};
    if (!Build(scene->swfBuffer,bytes,selector,selectorBytes,lobby,lobbyBytes,*patched,ids)) return false;
    scene->swfBuffer=patched->data();
    { std::lock_guard<std::mutex> guard(g_BufferLock); g_Buffers.push_back(std::move(patched)); }
    g_Ids.store(ids.selector | (std::uint32_t(ids.lobbies)<<16),std::memory_order_release);
    Core::Logger::Info("Online menus: imported original selector/lobby SWFs sprites=%u/%u physicalBytes=%u",ids.selector,ids.lobbies,static_cast<unsigned>(bytes));
    return true;
}
}
