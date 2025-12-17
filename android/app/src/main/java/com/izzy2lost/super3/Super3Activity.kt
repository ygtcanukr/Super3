package com.izzy2lost.super3

import android.net.Uri
import java.io.File
import kotlin.concurrent.thread
import org.libsdl.app.SDLActivity

/**
 * SDL-driven activity shell. SDLActivity handles loading SDL2 and the native
 * library specified by SDL_MAIN_LIBRARY (set to "super3" in the manifest).
 */
class Super3Activity : SDLActivity() {
    override fun getLibraries(): Array<String> = arrayOf(
        "SDL2",
        "super3",
    )

    override fun getArguments(): Array<String> {
        val rom = intent.getStringExtra("romZipPath") ?: return emptyArray()
        val game = intent.getStringExtra("gameName") ?: ""
        val gamesXml = intent.getStringExtra("gamesXmlPath") ?: ""
        val userDataRoot = intent.getStringExtra("userDataRoot") ?: ""

        val args = ArrayList<String>(4)
        args.add(rom)
        if (game.isNotBlank()) args.add(game)
        if (gamesXml.isNotBlank()) args.add(gamesXml)
        if (userDataRoot.isNotBlank()) args.add(userDataRoot)
        return args.toTypedArray()
    }

    override fun onStop() {
        super.onStop()
        val uriStr = getSharedPreferences("super3_prefs", MODE_PRIVATE).getString("userTreeUri", null) ?: return
        val treeUri = Uri.parse(uriStr)
        val internalRoot = File(getExternalFilesDir(null), "super3")
        thread(name = "Super3UserSync") {
            UserDataSync.syncInternalIntoTree(this, internalRoot, treeUri)
        }
    }
}

