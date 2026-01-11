package com.izzy2lost.super3

import android.content.Context
import android.os.Build
import java.io.File

object AssetInstaller {
    private const val PREFS_NAME = "super3_prefs"
    private const val KEY_ASSET_VERSION_CODE = "asset_installed_version_code"

    fun ensureInstalled(context: Context, internalUserRoot: File) {
        internalUserRoot.mkdirs()

        // Copy the standard Supermodel folder layout from APK assets into the internal user root.
        // We generally avoid overwriting existing files so user data persists.
        val topLevel = listOf("Assets", "Config", "GraphicsAnalysis", "NVRAM", "Saves")
        for (dir in topLevel) {
            copyAssetTree(context, dir, File(internalUserRoot, dir))
        }

        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val lastInstalledVersionCode = (prefs.all[KEY_ASSET_VERSION_CODE] as? Number)?.toLong() ?: -1L
        val currentVersionCode =
            runCatching {
                val info = context.packageManager.getPackageInfo(context.packageName, 0)
                if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else info.versionCode.toLong()
            }.getOrDefault(-1L)

        // If the app was updated, overwrite Supermodel.ini with the new defaults from the APK.
        // (Other files are still preserved by default.)
        if (lastInstalledVersionCode != currentVersionCode) {
            copyAssetFile(
                context,
                "Config/Supermodel.ini",
                File(File(internalUserRoot, "Config"), "Supermodel.ini"),
                overwrite = true,
            )
            prefs.edit().putLong(KEY_ASSET_VERSION_CODE, currentVersionCode).apply()
        }

        migrateSupermodelIni(File(File(internalUserRoot, "Config"), "Supermodel.ini"))
    }

    private fun copyAssetTree(context: Context, assetPath: String, dest: File) {
        val assets = context.assets
        val children = assets.list(assetPath) ?: return

        // If list() returns empty, it's a file.
        if (children.isEmpty()) {
            if (!dest.exists()) {
                dest.parentFile?.mkdirs()
                assets.open(assetPath).use { input ->
                    dest.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
            }
            return
        }

        if (!dest.exists()) dest.mkdirs()
        for (child in children) {
            copyAssetTree(context, "$assetPath/$child", File(dest, child))
        }
    }

    private fun copyAssetFile(context: Context, assetPath: String, dest: File, overwrite: Boolean) {
        if (!overwrite && dest.exists()) return
        dest.parentFile?.mkdirs()
        context.assets.open(assetPath).use { input ->
            dest.outputStream().use { output ->
                input.copyTo(output)
            }
        }
    }

    private fun migrateSupermodelIni(ini: File) {
        if (!ini.exists()) return

        val lines = runCatching { ini.readLines() }.getOrNull() ?: return
        var changed = false
        val replacements =
            mapOf(
                "InputBrake = KEY_S,JOY1_ZAXIS_POS" to "InputBrake = KEY_X,JOY1_ZAXIS_POS",
                "InputGearShiftUp = KEY_Y,JOY1_BUTTON6" to "InputGearShiftUp = KEY_I,JOY1_BUTTON6",
                "InputGearShiftDown = KEY_H,JOY1_BUTTON5" to "InputGearShiftDown = KEY_K,JOY1_BUTTON5",
                "InputGearShift1 = KEY_Q,JOY1_BUTTON3" to "InputGearShift1 = KEY_7,JOY1_BUTTON3",
                "InputGearShift2 = KEY_W,JOY1_BUTTON1" to "InputGearShift2 = KEY_8,JOY1_BUTTON1",
                "InputGearShift3 = KEY_E,JOY1_BUTTON4" to "InputGearShift3 = KEY_9,JOY1_BUTTON4",
                "InputGearShift4 = KEY_R,JOY1_BUTTON2" to "InputGearShift4 = KEY_0,JOY1_BUTTON2",
                "InputGearShiftN = KEY_T" to "InputGearShiftN = KEY_6",
                "InputAutoTrigger = 0" to "InputAutoTrigger = 1",
                "InputAutoTrigger2 = 0" to "InputAutoTrigger2 = 1",
            )
        val updated =
            lines.map { line ->
                val trimmed = line.trim()
                val repl = replacements[trimmed]
                if (repl != null) {
                    changed = true
                    repl
                } else {
                    line
                }
            }

        if (!changed) return
        runCatching { ini.writeText(updated.joinToString(System.lineSeparator())) }
    }
}
