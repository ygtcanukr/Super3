package com.izzy2lost.super3

import android.content.Context
import org.xmlpull.v1.XmlPullParser

data class GameDef(
    val name: String,
    val parent: String?,
    val title: String,
    val version: String?,
) {
    val displayName: String = buildString {
        append(title)
        if (!version.isNullOrBlank()) {
            append(" (")
            append(version)
            append(")")
        }
    }
}

object GameXml {
    fun parseGamesXmlFromAssets(context: Context, assetPath: String = "Config/Games.xml"): List<GameDef> {
        context.assets.open(assetPath).use { input ->
            val parser = android.util.Xml.newPullParser()
            parser.setInput(input, null)

            val games = ArrayList<GameDef>(256)
            var event = parser.eventType

            var currentGameName: String? = null
            var currentParent: String? = null
            var inIdentity = false
            var title: String? = null
            var version: String? = null

            fun finishGame() {
                val name = currentGameName ?: return
                val t = title ?: name
                games.add(GameDef(name = name, parent = currentParent, title = t, version = version))
            }

            while (event != XmlPullParser.END_DOCUMENT) {
                when (event) {
                    XmlPullParser.START_TAG -> {
                        when (parser.name) {
                            "game" -> {
                                currentGameName = parser.getAttributeValue(null, "name")
                                currentParent = parser.getAttributeValue(null, "parent")
                                inIdentity = false
                                title = null
                                version = null
                            }
                            "identity" -> inIdentity = true
                            "title" -> if (inIdentity) title = parser.nextText()
                            "version" -> if (inIdentity) version = parser.nextText()
                        }
                    }

                    XmlPullParser.END_TAG -> {
                        when (parser.name) {
                            "identity" -> inIdentity = false
                            "game" -> {
                                finishGame()
                                currentGameName = null
                                currentParent = null
                                inIdentity = false
                                title = null
                                version = null
                            }
                        }
                    }
                }
                event = parser.next()
            }

            return games
        }
    }
}
