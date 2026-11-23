// Executes isolated native dimension routines, never starts the game.
#include "../src/Core/GraphicsQuality.cpp"
#include "../src/NativeDB/Natives.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
using namespace HookCrashers::Core;
using namespace HookCrashers::Core::GraphicsQuality;
void Check(bool value,const char* message) { if (!value) { std::printf("FAIL %s\n",message); std::exit(1); } }
std::map<std::string,int> settings;
std::vector<int> options;
std::string Key(const char* section,const char* key) { return std::string(section)+"/"+key; }
int __fastcall FakeRead(void*,void*,const char* section,const char* key,int fallback,std::uint32_t) {
    auto item=settings.find(Key(section,key)); return item==settings.end() ? fallback : item->second;
}
void __fastcall FakeWrite(void*,void*,const char* section,const char* key,int value,std::uint32_t,std::uint32_t,std::uint32_t) {
    settings[Key(section,key)]=value;
}
void __fastcall FakeAdd(void*,void*,int id) { options.push_back(id); }
void* ExecutableCopy(const void* bytes,std::size_t size) {
    void* p=VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    Check(p!=nullptr,"allocate isolated routine"); std::memcpy(p,bytes,size); return p;
}
void FixAddress(std::uint8_t* bytes,std::size_t size,std::uint32_t from,std::uint32_t to) {
    unsigned count=0;
    for (std::size_t i=0;i+4<=size;++i) if (!std::memcmp(bytes+i,&from,4)) { std::memcpy(bytes+i,&to,4); ++count; i+=3; }
    Check(count>0,"native absolute operand relocated");
}
int main(int argc,char** argv) {
    Check(argc==2,"private fixture path supplied");
    std::ifstream f(argv[1],std::ios::binary); std::vector<std::uint8_t> input{std::istreambuf_iterator<char>(f),{}};
    Check(input.size()>24 && !std::memcmp(input.data(),"HCGQ512",7),"fixture header");
    auto U32=[&](std::size_t at) { std::uint32_t n;std::memcpy(&n,input.data()+at,4);return n; };
    const auto originalBase=U32(12),imageBytes=U32(16),oldBytes=U32(20);
    Check(input.size()==24+imageBytes+oldBytes && U32(8)==1,"fixture bounds");
    auto* image=static_cast<std::uint8_t*>(ExecutableCopy(input.data()+24,imageBytes));
    HookCrashers::NativeDB::SetGameBase(reinterpret_cast<std::uintptr_t>(image));
    const auto fakeBase=reinterpret_cast<std::uintptr_t>(image);
    std::array<std::uint8_t,0x270> platform{};
    std::array<std::uint8_t,0x680> renderer{};
    auto& mode=*reinterpret_cast<int*>(platform.data()+0x258);
    *reinterpret_cast<int*>(platform.data()+0x1FC)=1;
    *reinterpret_cast<std::uint32_t*>(renderer.data()+0x594)=100;
    *reinterpret_cast<std::uint32_t*>(renderer.data()+0x670)=100;
    *reinterpret_cast<void**>(image+kPlatformGlobal)=platform.data();
    *reinterpret_cast<std::uintptr_t*>(image+kRaster+5)=fakeBase+kPlatformGlobal;

    g_ReadInt=reinterpret_cast<ReadIntFn>(&FakeRead);
    g_WriteInt=reinterpret_cast<WriteIntFn>(&FakeWrite);
    g_AddOption=reinterpret_cast<AddOptionFn>(&FakeAdd);
    g_Maximum=3;
    for (int legacy=0;legacy<=2;++legacy) {
        settings.clear();settings["video/tex_quality"]=legacy;
        const int expected=legacy==2 ? 3 : legacy;
        Check(ReadInt(nullptr,nullptr,"video","tex_quality",3,0)==expected,"legacy quality migrated");
        Check(settings["video/tex_quality"]==expected && settings[Key("video",kSchemaKey)]==1,"migration value/schema stored together");
        Check(ReadInt(nullptr,nullptr,"video","tex_quality",3,0)==expected,"migration is idempotent");
    }
    for (int value=0;value<=3;++value) {
        WriteInt(nullptr,nullptr,"video","tex_quality",value,0xAA,0xBB,0xCC);
        Check(ReadInt(nullptr,nullptr,"video","tex_quality",3,0)==value,"four presets survive native INI round trip");
    }
    WriteInt(nullptr,nullptr,"other","value",17,0,0,0);
    Check(ReadInt(nullptr,nullptr,"other","value",3,0)==17,"unrelated settings unchanged");
    settings.clear();Check(ReadInt(nullptr,nullptr,"video","tex_quality",3,0)==3,"fresh configuration defaults to Ultra");
    g_Maximum=2;settings.clear();settings["video/tex_quality"]=2;
    Check(ReadInt(nullptr,nullptr,"video","tex_quality",3,0)==2,"32-bit OS fallback retains High");g_Maximum=3;
    AddMediumAndHigh(nullptr,nullptr,0x4A5);
    Check(options==std::vector<int>({0x4A5,0x31E}),"localized High inserted immediately after Medium");
    std::puts("PASS production INI migration/round trips, native stack signatures and localized menu insertion");

    auto* old=static_cast<std::uint8_t*>(ExecutableCopy(input.data()+24+imageBytes,oldBytes));
    FixAddress(old,oldBytes,0x6481E0,static_cast<std::uint32_t>(fakeBase+kPlatformGlobal));
    using OldFn=void(__thiscall*)(void*,std::uint32_t,std::uint32_t,std::uint32_t*,std::uint32_t*,std::uint8_t);
    auto historical=reinterpret_cast<OldFn>(old);
    constexpr std::size_t currentBytes=0x195;
    auto* vanilla=static_cast<std::uint8_t*>(ExecutableCopy(image+kRaster,currentBytes));
    // The source image global operand is already relocated above.
    for (const auto rva:{0x233B20u,0x233B24u,0x233ABCu})
        FixAddress(vanilla,currentBytes,originalBase+rva,static_cast<std::uint32_t>(fakeBase+rva));
    auto* expanded=static_cast<std::uint8_t*>(ExecutableCopy(vanilla,currentBytes));
    Check(expanded[0x2A]==2,"isolated native Ultra comparison"); expanded[0x2A]=3;
    auto original=reinterpret_cast<RasterFn>(vanilla);g_Raster=reinterpret_cast<RasterFn>(expanded);
    const std::uint32_t sizes[]{0,1,33,64,99,100,127,256,400,511,512,513,800,1024,2048,8192};
    unsigned cases=0;
    for (auto width:sizes) for (auto height:sizes) for (auto power:std::array<std::uint8_t,2>{0,1}) {
        std::uint32_t a,b,c,d;
        historical(renderer.data(),width,height,&a,&b,power);
        mode=2;Raster(renderer.data(),nullptr,width,height,&c,&d,power,4096);
        Check(a==c && b==d,"High matches actual pre-Painter routine, including power-of-two path");++cases;
        for (auto threshold:{0u,512u,1024u,4096u}) {
            mode=2;original(renderer.data(),width,height,&a,&b,power,threshold);
            mode=3;Raster(renderer.data(),nullptr,width,height,&c,&d,power,threshold);
            Check(a==c && b==d,"Ultra 3 matches unmodified Ultra 2");++cases;
            for (int quality:{0,1}) {
                mode=quality;original(renderer.data(),width,height,&a,&b,power,threshold);
                Raster(renderer.data(),nullptr,width,height,&c,&d,power,threshold);
                Check(a==c && b==d,"Low/Medium native rendering retained");++cases;
            }
        }
    }
    std::printf("PASS %u isolated native rendering comparisons: historical High, relocated Ultra, unchanged Low/Medium\n",cases);
    mode=2;std::uint32_t w,h;Raster(renderer.data(),nullptr,400,800,&w,&h,0,512);
    Check(w==400 && h==512,"historical High differs from aspect-preserving Medium");

    Check(VerifiedNatives(),"supported executable native entry/return signatures");
    for (const auto& p:Patches(3)) Check(Match(p.rva,p.original),"all patch sites match actual snapshot");
    const auto before=std::vector<std::uint8_t>(image,image+imageBytes);
    image[0x31375]^=1;
    Check(!Install() && !IsInstalled(),"signature mismatch refuses entire feature");image[0x31375]^=1;
    Check(!std::memcmp(image,before.data(),imageBytes),"failed validation made no other writes");
    Check(Install() && IsInstalled(),"production hooks and instruction patches attach to native fixture");
    for (const auto& p:g_Patches) Check(Match(p.rva,p.replacement),"all four-preset instruction patches applied");
    Shutdown();Check(!IsInstalled(),"graphics hooks detach");
    Check(!std::memcmp(image,before.data(),imageBytes),"shutdown restores every instruction and detoured entry");
    std::puts("PASS native compatibility, atomic rejection and Detours attach/patch/detach restoration");
    VirtualFree(old,0,MEM_RELEASE);VirtualFree(vanilla,0,MEM_RELEASE);VirtualFree(expanded,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    std::puts("ALL OFFLINE GRAPHICS TESTS PASSED (game not launched)");
}
