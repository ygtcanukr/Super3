package com.izzy2lost.super3

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.*
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.documentfile.provider.DocumentFile
import java.io.File
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {
    private val prefs by lazy { getSharedPreferences("super3_prefs", MODE_PRIVATE) }

    private lateinit var gamesFolderText: TextView
    private lateinit var userFolderText: TextView
    private lateinit var gamesList: ListView
    private lateinit var statusText: TextView

    private var gamesTreeUri: Uri? = null
    private var userTreeUri: Uri? = null

    private var games: List<GameDef> = emptyList()
    private var zipDocs: Map<String, DocumentFile> = emptyMap()

    private val pickGamesFolder =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) {
                persistTreePermission(uri)
                gamesTreeUri = uri
                prefs.edit().putString("gamesTreeUri", uri.toString()).apply()
                refreshUi()
            }
        }

    private val pickUserFolder =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) {
                persistTreePermission(uri)
                userTreeUri = uri
                prefs.edit().putString("userTreeUri", uri.toString()).apply()
                refreshUi()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        gamesFolderText = findViewById(R.id.games_folder_text)
        userFolderText = findViewById(R.id.user_folder_text)
        gamesList = findViewById(R.id.games_list)
        statusText = findViewById(R.id.status_text)

        findViewById<Button>(R.id.pick_games_folder).setOnClickListener { pickGamesFolder.launch(null) }
        findViewById<Button>(R.id.pick_user_folder).setOnClickListener { pickUserFolder.launch(null) }
        findViewById<Button>(R.id.rescan_button).setOnClickListener { refreshUi() }

        gamesList.onItemClickListener = AdapterView.OnItemClickListener { _, _, position, _ ->
            val item = (gamesList.adapter as GameAdapter).getItem(position) ?: return@OnItemClickListener
            if (!item.launchable) {
                Toast.makeText(this, item.status, Toast.LENGTH_SHORT).show()
                return@OnItemClickListener
            }
            launchGame(item.game)
        }

        loadPrefs()
        games = GameXml.parseGamesXmlFromAssets(this)

        // Ensure internal user root has the required files from APK assets (Config/Games.xml, Assets/, etc.).
        AssetInstaller.ensureInstalled(this, internalUserRoot())

        refreshUi()
    }

    private fun loadPrefs() {
        gamesTreeUri = prefs.getString("gamesTreeUri", null)?.let(Uri::parse)
        userTreeUri = prefs.getString("userTreeUri", null)?.let(Uri::parse)
    }

    private fun internalUserRoot(): File {
        // Matches native default: <externalFiles>/super3
        return File(getExternalFilesDir(null), "super3")
    }

    private fun persistTreePermission(uri: Uri) {
        val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        try {
            contentResolver.takePersistableUriPermission(uri, flags)
        } catch (_: SecurityException) {
        }
    }

    private fun refreshUi() {
        gamesFolderText.text = gamesTreeUri?.toString() ?: "Not set"
        userFolderText.text = userTreeUri?.toString() ?: "Not set"
        statusText.text = "Scanning…"

        thread(name = "Super3Scanner") {
            val zips = scanZipDocs(gamesTreeUri)
            runOnUiThread {
                zipDocs = zips
                statusText.text = "Found ${zipDocs.size} ZIP(s). Tap a game to launch."
                gamesList.adapter = GameAdapter(this, games, zipDocs)
            }
        }
    }

    private fun scanZipDocs(treeUri: Uri?): Map<String, DocumentFile> {
        if (treeUri == null) return emptyMap()
        val tree = DocumentFile.fromTreeUri(this, treeUri) ?: return emptyMap()
        val map = HashMap<String, DocumentFile>(256)
        for (child in tree.listFiles()) {
            if (!child.isFile) continue
            val name = child.name ?: continue
            if (!name.endsWith(".zip", ignoreCase = true)) continue
            map[name.substring(0, name.length - 4)] = child
        }
        return map
    }

    private fun launchGame(game: GameDef) {
        val gamesUri = gamesTreeUri
        if (gamesUri == null) {
            Toast.makeText(this, "Pick a games folder first", Toast.LENGTH_SHORT).show()
            return
        }

        statusText.text = "Preparing ${game.displayName}…"

        thread(name = "Super3Prep") {
            val internalRoot = internalUserRoot()
            val userUri = userTreeUri

            if (userUri != null) {
                // Pull saved NVRAM/Saves/Config from user folder into internal root before launch.
                UserDataSync.syncFromTreeIntoInternal(this, userUri, internalRoot)
            }

            val cacheDir = File(internalRoot, "romcache")
            val required = resolveRequiredRomZips(game)
            val missing = required.filter { !zipDocs.containsKey(it) }
            if (missing.isNotEmpty()) {
                runOnUiThread {
                    statusText.text = "Missing required ZIP(s): ${missing.joinToString(", ")}"
                    Toast.makeText(this, "Missing required ZIP(s): ${missing.joinToString(", ")}", Toast.LENGTH_LONG).show()
                }
                return@thread
            }

            // Copy selected game zip + required parent chain zips into cacheDir so GameLoader can find them.
            for (zipBase in required) {
                val doc = zipDocs[zipBase] ?: continue
                val ok = copyDocToCacheIfNeeded(doc, File(cacheDir, "$zipBase.zip"))
                if (!ok) {
                    runOnUiThread {
                        statusText.text = "Failed to copy $zipBase.zip"
                        Toast.makeText(this, "Failed to copy $zipBase.zip", Toast.LENGTH_SHORT).show()
                    }
                    return@thread
                }
            }

            val romPath = File(cacheDir, "${game.name}.zip").absolutePath

            // Ensure Games.xml exists on disk for the native loader.
            val gamesXmlPath = File(internalRoot, "Config/Games.xml").absolutePath
            val userDataRoot = internalRoot.absolutePath

            runOnUiThread {
                val intent = Intent(this, Super3Activity::class.java).apply {
                    putExtra("romZipPath", romPath)
                    putExtra("gameName", game.name)
                    putExtra("gamesXmlPath", gamesXmlPath)
                    putExtra("userDataRoot", userDataRoot)
                }
                startActivity(intent)
            }
        }
    }

    private fun resolveRequiredRomZips(game: GameDef): List<String> {
        val mapByName = games.associateBy { it.name }
        val required = ArrayList<String>(4)
        val visited = HashSet<String>(8)

        var current: GameDef? = game
        while (current != null) {
            if (!visited.add(current.name)) break
            required.add(current.name)
            val parentName = current.parent
            if (parentName.isNullOrBlank()) break
            current = mapByName[parentName]
        }

        return required
    }

    private fun copyDocToCacheIfNeeded(doc: DocumentFile, outFile: File): Boolean {
        outFile.parentFile?.mkdirs()
        val expectedSize = doc.length()
        if (outFile.exists() && expectedSize > 0 && outFile.length() == expectedSize) {
            return true
        }
        val uri = doc.uri
        val input = contentResolver.openInputStream(uri) ?: return false
        input.use { ins ->
            outFile.outputStream().use { outs ->
                ins.copyTo(outs)
            }
        }
        return true
    }
}

private class GameAdapter(
    context: MainActivity,
    private val games: List<GameDef>,
    zipDocs: Map<String, DocumentFile>,
) : ArrayAdapter<GameItem>(context, android.R.layout.simple_list_item_2, buildItems(games, zipDocs)) {
    override fun getView(position: Int, convertView: android.view.View?, parent: android.view.ViewGroup): android.view.View {
        val v = convertView ?: android.view.LayoutInflater.from(context)
            .inflate(android.R.layout.simple_list_item_2, parent, false)
        val item = getItem(position)!!
        val t1 = v.findViewById<TextView>(android.R.id.text1)
        val t2 = v.findViewById<TextView>(android.R.id.text2)
        t1.text = item.game.displayName
        t2.text = item.status
        t1.isEnabled = item.launchable
        t2.isEnabled = item.launchable
        return v
    }
}

private data class GameItem(val game: GameDef, val launchable: Boolean, val status: String)

private fun buildItems(games: List<GameDef>, zipDocs: Map<String, DocumentFile>): List<GameItem> {
    val byName = games.associateBy { it.name }

    fun requiredZips(g: GameDef): List<String> {
        val required = ArrayList<String>(4)
        val visited = HashSet<String>(8)
        var cur: GameDef? = g
        while (cur != null) {
            if (!visited.add(cur.name)) break
            required.add(cur.name)
            val parent = cur.parent
            if (parent.isNullOrBlank()) break
            cur = byName[parent]
        }
        return required
    }

    val items = games.map { g ->
        val req = requiredZips(g)
        val missing = req.filter { !zipDocs.containsKey(it) }
        val launchable = missing.isEmpty() && zipDocs.containsKey(g.name)
        val status = if (missing.isEmpty()) {
            if (req.size == 1) "${g.name}.zip found"
            else "needs ${req.joinToString(" + ") { "${it}.zip" }}"
        } else {
            "missing ${missing.joinToString(" + ") { "${it}.zip" }}"
        }
        GameItem(game = g, launchable = launchable, status = status)
    }

    return items.sortedWith(compareByDescending<GameItem> { it.launchable }.thenBy { it.game.displayName })
}
