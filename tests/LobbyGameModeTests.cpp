// Exercise production wire/receiver paths on an isolated native-layout model.
// No Steam or game process is started by this test.
#include "../src/Network/CharacterSyncExpansion.cpp"
#include <cstdio>
#include <cstdlib>
using namespace HookCrashers;
using namespace HookCrashers::Network;
namespace {
void Check(bool ok,const char* why) { if (!ok) { std::printf("FAIL %s\n",why); std::exit(1); } }
unsigned setterCalls=0,nativeReceives=0;
std::uint32_t* modeValue=nullptr;
std::vector<std::uint8_t> sentWire;
void __stdcall SetMode(std::uint8_t value) { ++setterCalls; *modeValue=value; }
struct Peer { void** vtable; std::uint32_t host=0; };
std::uint8_t __fastcall HostFlag(Peer* peer,void*) { return static_cast<std::uint8_t>(peer->host); }
std::uint8_t __fastcall NotLoopback(Peer*,void*) { return 0; }
Peer* channel=nullptr;
void* __fastcall ChannelForPlayer(Peer*,void*,std::uint32_t) { return channel; }
void __fastcall Send(Peer*,void*,std::uint32_t*,void*,const void* data,std::uint32_t size,std::uint32_t) {
    const auto* bytes=static_cast<const std::uint8_t*>(data); sentWire.assign(bytes,bytes+size);
}
void __fastcall NativeReceive(void*,void*,std::uint32_t* result,void*,std::uint8_t*) { ++nativeReceives; result[0]=result[1]=0; }
}
int main() {
    static_assert(sizeof(void*)==4,"Win32 required");
    auto* image=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x400000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"isolated image"); NativeDB::SetGameBase(reinterpret_cast<std::uintptr_t>(image));
    std::array<std::uint8_t,0x820> profile {};
    std::array<std::uint8_t,24> head {},node {};
    std::array<std::uint8_t,0x1600> manager {};
    std::array<std::uint8_t,0x29A8> state {};
    auto pointer=[](void* target,void* value){ *static_cast<void**>(target)=value; };
    pointer(image+0x391294,profile.data()); pointer(profile.data()+0x814,head.data());
    pointer(head.data()+4,node.data()); head[0xD]=1;
    pointer(node.data(),head.data()); pointer(node.data()+8,head.data());
    *reinterpret_cast<std::uint16_t*>(node.data()+0x10)=0x120;
    modeValue=reinterpret_cast<std::uint32_t*>(node.data()+0x14);
    pointer(image+kLobbyManagerGlobalRva,manager.data());
    pointer(manager.data()+0x1C,head.data());
    std::array<void*,28> peerVtable {},sessionVtable {},channelVtable {};
    peerVtable[0x20/4]=reinterpret_cast<void*>(HostFlag);
    peerVtable[0x1C/4]=reinterpret_cast<void*>(NotLoopback);
    sessionVtable[0x68/4]=reinterpret_cast<void*>(HostFlag);
    sessionVtable[0x4C/4]=reinterpret_cast<void*>(ChannelForPlayer);
    channelVtable[0x64/4]=reinterpret_cast<void*>(Send);
    Peer remote {peerVtable.data(),1},session {sessionVtable.data(),0},sendChannel {channelVtable.data(),0};
    channel=&sendChannel; pointer(state.data()+0x0C,&session); state[0x29A4]=1;
    g_SetFreshMode=SetMode;
    g_OriginalHandleIncomingCharacterSyncPacket=reinterpret_cast<HandleIncomingCharacterSyncPacketFn>(NativeReceive);
    Characters::Registry::Instance().Clear(); Characters::AddonCharacter addon {}; addon.name="test";
    Check(Characters::Registry::Instance().RegisterCharacter(addon),"fixture addon");
    std::uint32_t result[2] {};
    for (unsigned hostMode=0;hostMode<2;++hostMode) for (unsigned clientMode=0;clientMode<2;++clientMode) {
        *modeValue=clientMode; setterCalls=0; state[0x29A5]=0;
        const auto beforeProfile=profile; const auto beforeHead=head; const auto beforeNode=node;
        LobbyGameMode observed;
        Check(ReadLocalGameMode(observed) && static_cast<unsigned>(observed)==clientMode,"reads authoritative config key");
        Check(profile==beforeProfile && head==beforeHead && node==beforeNode,"read does not mutate native map");
        auto wire=BuildWireMessage(static_cast<LobbyGameMode>(hostMode));
        Check(wire[3]==hostMode && ReadUnaligned<std::uint16_t>(wire.data()+8)==6,"actual host mode and v6 on wire");
        ExpandedPacket parsed;
        Check(TryReadWirePacket(wire.data()+3,parsed) && static_cast<unsigned>(parsed.gameMode)==hostMode,"Classic zero is valid protocol data");
        DetouredHandleIncomingCharacterSyncPacket(state.data(),nullptr,result,&remote,wire.data()+3);
        Check(result[0]==0 && state[0x29A5]==1 && setterCalls==1 && *modeValue==hostMode,"client follows host before roster is ready");
    }
    Check(nativeReceives==0,"expanded packets never forwarded as vanilla");
    std::puts("PASS Classic/Fresh host-client matrix: all four combinations adopt host mode before ready");
    for (unsigned localHostMode=0;localHostMode<2;++localHostMode) {
        *modeValue=localHostMode; setterCalls=0; session.host=1; remote.host=0;
        auto wire=BuildWireMessage(static_cast<LobbyGameMode>(1-localHostMode));
        DetouredHandleIncomingCharacterSyncPacket(state.data(),nullptr,result,&remote,wire.data()+3);
        Check(result[0]==0 && !setterCalls && *modeValue==localHostMode,"client packet cannot change host mode");
        remote.host=1;
        DetouredHandleIncomingCharacterSyncPacket(state.data(),nullptr,result,&remote,wire.data()+3);
        Check(!setterCalls && *modeValue==localHostMode,"local host wins even against authoritative-peer flag");
    }
    session.host=0; remote.host=1; *modeValue=0; setterCalls=0; state[0x29A4]=0;
    auto fresh=BuildWireMessage(LobbyGameMode::Fresh);
    Check(ApplyAuthoritativeGameMode(state.data(),&remote,LobbyGameMode::Fresh) && !setterCalls && *modeValue==0,"native sync-disabled predicate retained");
    state[0x29A4]=1;
    for (unsigned malformed=0;malformed<3;++malformed) {
        auto bad=fresh; state[0x29A5]=1;
        if (malformed==0) bad[8]=5; // old hardcoded-Fresh protocol
        if (malformed==1) bad[3]=2; // invalid mode
        if (malformed==2) bad[10]++; // wrong addon layout
        DetouredHandleIncomingCharacterSyncPacket(state.data(),nullptr,result,&remote,bad.data()+3);
        Check(result[0]==1 && state[0x29A5]==0 && !setterCalls && *modeValue==0 && !nativeReceives,"invalid metadata cannot mutate mode or fall through");
    }
    std::puts("PASS host authority, native sync predicate, old-version/malformed mode/addon rejection without mutation");
    for (unsigned localMode=0;localMode<2;++localMode) {
        *modeValue=localMode; sentWire.clear();
        DetouredSendInitialCharacterSyncPacket(state.data(),nullptr,result,&remote);
        Check(result[0]==0 && !sentWire.empty() && sentWire[3]==localMode,"production sender advertises actual mode");
    }
    Characters::Registry::Instance().Clear();
    std::vector<std::uint8_t> vanilla(0x431,0); vanilla[0]=1;
    DetouredHandleIncomingCharacterSyncPacket(state.data(),nullptr,result,&remote,vanilla.data());
    Check(result[0]==0 && nativeReceives==1,"unmodified native packets delegate to native mode synchronization");
    *modeValue=42; LobbyGameMode unknown;
    Check(!ReadLocalGameMode(unknown) && unknown==LobbyGameMode::Unknown,"invalid config is not silently Classic");
    pointer(profile.data()+0x814,nullptr);
    Check(!ReadLocalGameMode(unknown),"missing config map unavailable");
    VirtualFree(image,0,MEM_RELEASE); NativeDB::SetGameBase(0);
    std::puts("PASS production sender, native fallback, read-only config access and invalid-state handling");
    std::puts("ALL OFFLINE LOBBY MODE TESTS PASSED (no game launched)");
}
