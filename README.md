# HookCrashers

HookCrashers is an advanced modding platform for the game **Castle Crashers**. Developed as an ASI mod, it provides a **Mod Loader** and a powerful **API** that acts as a bridge between C++ code and the game's ActionScript engine. This allows modders to create, register, and execute new custom functions directly within the game's SWF files.

In essence, HookCrashers transforms how the game can be modified, opening the door to complex mods and entirely new features that were previously impossible to implement.

## How It Works

Castle Crashers is built on an interesting technology: the game's executable acts as a custom compiler and interpreter for the `.swf` files that contain its logic and interface. HookCrashers hooks into this process through two main components:

1.  **The Mod Loader**: On startup, HookCrashers scans a `/mods` directory in your game folder. It automatically loads any mod file with an `.asi` extension, allowing for simple drag-and-drop installation of mods.
2.  **The API**: Once loaded, a mod can use the HookCrashers API to:
    *   **Register new C++ functions**: You can write a function in C++ and register it with a specific name.
    *   **Execute it in ActionScript**: That same function becomes callable from within the game's `.swf` files, just like a native function.
    *   **Log to the in-game console**: The API exposes a logging system for easy debugging.

## Installation

Installing the HookCrashers platform is very simple.

1.  Download the latest version from the [Releases](https://github.com/lorizz/hookcrashers/releases) section of this repository.
2.  Extract the contents of the archive directly into the main Castle Crashers game folder.
    *   *Typically found at: `C:\Program Files (x86)\Steam\steamapps\common\Castle Crashers`*
3.  Create a folder named `mods` in the game's root directory if it doesn't already exist.

That's it. The game will automatically load the platform and any mods inside the `/mods` folder on the next launch.

## Creating Your First Mod: An Example

Let's walk through a basic example of how to build a mod. The goal is to create a `HelloWorld()` function that can be called from ActionScript and will print a message to the in-game console. All mods are ASI files, which start as C++ DLL projects.

#### 1. C++ Code (Your Mod)

Create a C++ DLL project and include the HookCrashers dependencies. The following code sets up your mod's info, registers a function, and logs a message.

```cpp
// MyFirstMod.cpp
#include <windows.h>
#include "HookCrashersAPI.h" // Include the API header

// Export metadata for the Mod Loader
extern "C" __declspec(dllexport) const char* GetModName() { return "My First Mod"; }
extern "C" __declspec(dllexport) const char* GetModAuthor() { return "Your Name"; }
extern "C" __declspec(dllexport) const char* GetModVersion() { return "1.0"; }

// This is our custom function. It will be called from ActionScript.
void HelloWorldHandler()
{
    // Use the API's logging system. The message appears in the in-game console.
    HookCrashers::Log("[MyFirstMod] Hello World was called from ActionScript!");
}

// The entry point for your ASI mod
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        // We register our function.
        // The first parameter, "HelloWorld", is the name we'll use in ActionScript.
        // The second is a pointer to our C++ function.
        HookCrashers::Register("HelloWorld", HelloWorldHandler);
    }
    return TRUE;
}
```

#### 2. Compilation and Installation

1.  Compile your project as a **DLL** file.
2.  Rename the compiled file from `.dll` to `.asi` (e.g., `MyFirstMod.asi`).
3.  Copy `MyFirstMod.asi` into the `mods` folder you created in your Castle Crashers directory.

#### 3. ActionScript Code (Inside a .swf file)

Now, in any script within a `.swf` file loaded by the game, you can call the function you just registered.

```actionscript
// Somewhere in the game's logic, for example, when a menu is activated.

// We call the function we registered from our C++ mod.
// The game will execute the C++ code in HelloWorldHandler.
HelloWorld();
```

## Future Plans

HookCrashers is an evolving project. Here are some of the planned features:
-   **In-Game Mod UI**: A button will be added to the main menu to open a screen listing installed mods, their authors, and versions. In the future, this will also allow users to download/update mods directly from within the game.
-   **Advanced Lobby System**: A matchmaking system that differentiates between "vanilla" and "modded" players. It will analyze installed mods (e.g., `customsave`, `customlocalizations`) to verify compatibility between players in a lobby, ensuring stable multiplayer sessions.

---

## ⚠️ Important Disclaimer

The author of HookCrashers is in no way responsible for the misuse of this API. This tool was created for educational purposes and to foster creativity within the modding community.

> **IT IS STRICTLY FORBIDDEN** to use HookCrashers to create hacks, cheats, trainers, or any mod that provides an unfair advantage in the game, especially in multiplayer contexts.

> **IT IS EQUALLY FORBIDDEN** to develop or distribute mods made with this API that unlock, "crack," or otherwise enable access to DLCs or other paid content without a legitimate purchase. Supporting the original developers (The Behemoth) is paramount.

HookCrashers was designed as a tool to extend the game in new and fun ways. Any use that violates these terms runs contrary to the spirit of the project and is not endorsed or condoned by the author. Using mods to cheat or pirate content harms the community and the developers.

**Please use this tool responsibly.**
