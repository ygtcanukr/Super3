package com.izzy2lost.super3

import org.libsdl.app.SDLActivity

/**
 * SDL-driven activity shell. SDLActivity handles loading SDL2 and the native
 * library specified by SDL_MAIN_LIBRARY (set to "super3" in the manifest).
 */
class Super3Activity : SDLActivity()
{
    override fun getLibraries(): Array<String> = arrayOf(
        "SDL2",
        "super3", // built from CMake add_library(super3 SHARED …)
    )
}
