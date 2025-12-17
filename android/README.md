# Super3 Android (Scaffold)

This folder is a **starter Android Studio project** for the Super3 ARM codebase.

What works right now:
- Builds an Android app (API 36) + a native shared library (`libsuper3.so`) using **NDK r29**.
- App loads the native library and shows a text string from JNI.

What still needs real porting work:
- The emulator uses the SDL OSD/renderer on desktop (`Src/OSD/SDL/Main.cpp`). Android needs an Android OSD layer:
  - Option A: bring in **SDL2 for Android** and adapt the SDL OSD to use SDL2 Android activity.
  - Option B: port the OSD to **GameActivity/NativeActivity** and EGL/OpenGL/Vulkan as needed.

## Build requirements
- Android Studio (Otter era or newer)
- SDK Platform: API 36
- NDK (Side by side): `29.0.14206865`

## Project layout
- `android/app/` - Kotlin app + CMake build
- `android/app/src/main/cpp/CMakeLists.txt` - builds most of `Src/` into a shared library (excludes desktop mains/tools)

## Wiring SDL2 (recommended next step)
1. Add SDL2 source under `android/third_party/SDL` (or as a git submodule).
2. In `android/app/src/main/cpp/CMakeLists.txt`, uncomment the `add_subdirectory()` example and link SDL2.
3. Decide how you want to expose your emulator entry point on Android (JNI, `android_main`, or SDLActivity).

