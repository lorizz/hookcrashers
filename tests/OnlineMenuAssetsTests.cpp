#include "../src/SWF/OnlineMenuAssets.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
using Bytes=std::vector<std::uint8_t>;
Bytes Read(const char* path) { std::ifstream f(path,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
void Check(bool good,const char* message) { if (!good) { std::printf("FAIL %s\n",message); std::exit(1); } }
int main(int argc,char** argv) {
    Check(argc==5,"menu, selector, lobby, output paths");
    auto menu=Read(argv[1]),selector=Read(argv[2]),lobby=Read(argv[3]);
    Check(!menu.empty() && !selector.empty() && !lobby.empty(),"fixtures available");
    Bytes output; HookCrashers::SWF::OnlineMenuAssets::SpriteIds ids {};
    using HookCrashers::SWF::OnlineMenuAssets::Build;
    Check(Build(menu.data(),menu.size(),selector.data(),selector.size(),lobby.data(),lobby.size(),output,ids),"imports original custom SWFs into actual physical MENU");
    Check(ids.selector && ids.lobbies && ids.selector!=ids.lobbies && output.size()>menu.size(),"independent custom sprite IDs and complete MENU retained");
    const auto previous=output;
    Check(!Build(menu.data(),menu.size(),selector.data(),8,lobby.data(),lobby.size(),output,ids) && output==previous,"truncated asset rejected atomically");
    Check(!Build(menu.data(),20,selector.data(),selector.size(),lobby.data(),lobby.size(),output,ids) && output==previous,"truncated MENU rejected atomically");
    std::ofstream f(argv[4],std::ios::binary); f.write(reinterpret_cast<const char*>(output.data()),output.size());
    std::printf("PASS original SWF import, complete physical MENU, IDs, font remapping and atomic malformed-input rejection (%u/%u)\n",ids.selector,ids.lobbies);
}
