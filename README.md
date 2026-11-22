# SDL Template

This is my personal SDL 3 template which runs on Windows, ideally on Visual Studio IDE.

Why does this template exist? Because I love low-level programming, but I hate the plumbing parts such as build system, headers, asset pipeline, etc.

For anyone of you who screams _**"JUST LET ME PROGRAM"**_ then hopefully this template is for you.

**Though if you're a Linux or Mac user (which is like, a huge portion of SDL users), sorry to say: this template isn't for you.** It is very Windows-oriented (filled with .bats and .ps1) and even then it's ideally done inside Visual Studio IDE. I did this because I live in Windows, and I think it's better for building to other platforms such as consoles in the future.

**Notice**: a lot of this template is vibe-coded. Why? Because I want to focus on actually programming and making this kind of stuff is extremely fragile and filled with gotchas. I've tested, revised, and heavily fine-tuned the outputted template for the last 6 months, and I am confident to say that it's stable.

**To use it, download the latest `SDL Template.zip` from releases and copy it to**

```text
Documents\Visual Studio\Templates\ProjectTemplates
```

When you restart Visual Studio, the template should appear.

If not, go to `%localappdata%/Microsoft/VisualStudio/[Version]/` and try deleting these cache folders:

- `ComponentModelCache`
- `ItemTemplatesCache_`
- `ProjectTemplatesCache_`

It includes features such as...

## Custom incremental build system

By default it supports builds to these platforms:

- **Windows**: supports both x64 and x86, with an option to make it a single exe using `EMBED_ASSETS` and compression using `COMPRESS_WITH_UPX`.
- **Linux**: exports to Linux via Zig which can be packaged as a tar.gz or AppImage. Like Windows, it can also be a single executable and compressed.
- **Web**: exports to web via Emscripten.
- **Android**: exports to APK and/or AAB without a Gradle project or Android Studio. It also allows you to set up your keystore easily during setup. Also supports USB debugging.

It doesn't support macOS (because I don't have a Mac) nor consoles (because of NDAs). However, the template is built for modularity so that if you do have the SDK, you should be able to write a new platform build under `Framework/Automations/Platforms/[Your Platform Name]` and run

```text
Framework/Automations/Build.bat [Your Platform Name]
```

## Hot reload

Easily iterate your programming process using hot reload. (Debug only, not available on Release)

Any code you wrote will be reloaded via a .dll and any data put inside `Program/Types/Program`'s Program State will be preserved upon the next reload.

## Asset Pipeline

Importing assets (images, audio, HLSL, and really anything) is as simple as putting it under `Program/Assets`. For HLSL shaders the template will convert it to the target platform's shader format such as .dxil for Windows and .spirv for Android and Linux.

All files inside that folder will be put inside `Assets.data` which will be embedded/separated from the exe depending on `EMBED_ASSETS`.

`Assets.data` can also be encrypted using the `ENABLE_ENCRYPTION` option so it could not be pulled with ordinary tools.

Using an asset with SDL is as simple as using the load functions inside the `Assets::` namespace which will load from the root of the `Program/Assets` folder.

```cpp
SDL_IOStream *stream = Assets::Open("Fonts/Main.ttf");
void *data = Assets::Load("Levels/One.bin", &size);
char *text = Assets::LoadText("Datas/Dialogue.txt");
SDL_Surface *surface = Assets::LoadSurface("Sprites/Player.png");
SDL_Texture *texture = Assets::LoadTexture(renderer, "Sprites/Player.png");
bool loaded = Assets::LoadWAV("Audio/Jump.wav", &spec, &buffer, &length);
SDL_GPUShader *shader = Assets::LoadShader(device, "Shaders/Main.frag", 0, 0, 0, 1);
SDL_Surface *icon = Assets::LoadIcon("Icon.png");
```

## Automatic header generation

Hate headers? Me too. Stop writing headers and start writing actual code using the template's custom C++ parser.

Behind the scenes it will recreate a header that will extern all of your functions and variables whenever it detects a saved script under `Program/Code`. A function or variable could be marked as private by putting it inside an empty namespace or with the `static` keyword.

The Systems/Types/Datas framework split exists for this very reason. More on that in the Custom Framework section.

Of course, like any custom parser, it has limitations, but it has been tested and iterated, and for 90% of your usage, as long as you don't do very weird and niche C++, you'll be fine.

## .dmp error reports

Whenever your program crashes, it will create a .dmp under `%localappdata%/[Program Name]` which you can drag and drop to Visual Studio to inspect its cause.

## Utilities

A tiny utility script I wrote because I personally love doing C-like C++ and need some kind of type/macro and utilities.

- **String**: Basically a char array with utilities to process it (e.g., `ToUpper()`, `Length()`, `Contains()`, etc.)
- **Array**: My custom fixed array type which supports empty elements and has list-like utilities (e.g., `Reverse()`, `FirstIndex()`, `RemoveAll()`, etc.)
- **CArray**: This is basically just a utility for normal C arrays, because `CArray` and `Array` are two different types. (e.g., `Length()`, `SetFromTo()`, `SetAll()`, etc.)

## Custom Framework

All game code lives in one of three folders under `Program\Code`. If you know C++ projects: Types are the headers and Systems are the source files, except that you never write a declaration twice.

- **Types**: What every script needs whole: structs, classes, enums, unions, templates, constants, and inline or constexpr functions. Types files are included in every script, so a function defined in one must be inline, constexpr or a template. A Types file that uses another includes it: `#include <Program/Code/Types/Other.hpp>`.
- **Datas**: Data definitions: databases, settings and similar tables.
- **Systems**: Logic. Any script can use another script's functions and variables by name, with no includes and in any order.

Every script must start with

```cpp
#pragma once
#include <Framework/Framework.hpp>
```

for all the magic to work.

## Stress Test

This template also comes with `Stress Test.zip` whose `Library/` and `Program/` you can copy to stress-test SDL and this template.

It should display a duck model on top of a brick plane with a day-and-night cycle, particles, and the ability to spawn waves of ducks around it.

## Final Words

There are many more to discuss, but I suggest that you try out the template yourself and read through `Guide.pdf`.

Let me know if there's any issue, and I hope this template helps you in skipping the bullshit parts of low-level programming :)

You can use this template for whatever you like as it uses the Unlicense license.

Commercial, non-commercial, list me in the credits, list me not, I don't really care. I just want to help other devs.

And... oh yeah, there's also a version of this template but for [Raylib](https://github.com/nicholaschristianchandra/Raylib-Template)! Might check that out also if you use it.
