package dev.switchauto.companion

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicInteger

data class ThemeOption(val id: String, val name: String, val builtIn: Boolean)
data class LayoutOption(val id: String, val name: String, val layout: JSONObject)

enum class CustomizationSyncState {
    SAVED_ON_PHONE,
    WAITING_FOR_SWITCH,
    SYNCED,
}

class CustomizationManager(context: Context) {
    private val preferences = context.getSharedPreferences(
        "switch_auto_customization", Context.MODE_PRIVATE)
    private val nextRevision = AtomicInteger(
        preferences.getInt("nextRevision", 1).coerceAtLeast(1))

    val themes = listOf(
        ThemeOption("default-dark", "Switch Auto Dark", true))

    val layouts = listOf(
        LayoutOption("automotive", "Automotive", layoutJson(
            placement("core.gps", 0, 0, 8, 8, 5, 4),
            placement("core.speed", 8, 0, 4, 2, 2, 2),
            placement("core.media", 8, 2, 4, 6, 3, 3))),
        LayoutOption("map-focus", "Map Focus", layoutJson(
            placement("core.gps", 0, 0, 9, 8, 5, 4),
            placement("core.speed", 9, 0, 3, 2, 2, 2),
            placement("core.media", 9, 2, 3, 6, 3, 3))))

    var selectedThemeIndex = themes.indexOfFirst {
        it.id == preferences.getString("selectedThemeId", themes.first().id)
    }.coerceAtLeast(0)
        private set

    var selectedLayoutIndex = layouts.indexOfFirst {
        it.id == preferences.getString("selectedLayoutId", layouts.first().id)
    }.coerceAtLeast(0)
        private set

    var state = if (preferences.getInt("pendingRevision", 0) > 0) {
        CustomizationSyncState.WAITING_FOR_SWITCH
    } else {
        CustomizationSyncState.SAVED_ON_PHONE
    }
        private set

    var onStateChanged: (() -> Unit)? = null

    val selectedTheme get() = themes[selectedThemeIndex]
    val selectedLayout get() = layouts[selectedLayoutIndex]

    fun selectNextTheme() {
        selectedThemeIndex = (selectedThemeIndex + 1) % themes.size
        saveSelection()
    }

    fun selectNextLayout() {
        selectedLayoutIndex = (selectedLayoutIndex + 1) % layouts.size
        saveSelection()
    }

    fun requestSync(): Int {
        val revision = nextRevision.getAndIncrement()
        preferences.edit()
            .putInt("nextRevision", nextRevision.get())
            .putInt("pendingRevision", revision)
            .putString("pendingThemeId", selectedTheme.id)
            .putString("pendingLayoutId", selectedLayout.id)
            .putString("pendingLayout", selectedLayout.layout.toString())
            .apply()
        state = CustomizationSyncState.WAITING_FOR_SWITCH
        onStateChanged?.invoke()
        return revision
    }

    fun contribute(target: JSONObject) {
        val revision = preferences.getInt("pendingRevision", 0)
        if (revision <= 0) return
        val layoutText = preferences.getString("pendingLayout", null) ?: return
        target.put("customization", JSONObject()
            .put("schemaVersion", 1)
            .put("revision", revision)
            .put("themeId", preferences.getString("pendingThemeId", "default-dark"))
            .put("layoutId", preferences.getString("pendingLayoutId", "automotive"))
            .put("layout", JSONObject(layoutText)))
    }

    fun onSwitchRevision(revision: Int) {
        val pending = preferences.getInt("pendingRevision", 0)
        if (pending <= 0 || revision != pending) return
        preferences.edit()
            .remove("pendingRevision")
            .remove("pendingThemeId")
            .remove("pendingLayoutId")
            .remove("pendingLayout")
            .putInt("lastSyncedRevision", revision)
            .apply()
        state = CustomizationSyncState.SYNCED
        onStateChanged?.invoke()
    }

    fun statusText(): String = when (state) {
        CustomizationSyncState.SAVED_ON_PHONE -> "Saved on phone"
        CustomizationSyncState.WAITING_FOR_SWITCH -> "Synchronizing with Switch..."
        CustomizationSyncState.SYNCED -> "Applied on Switch"
    }

    private fun saveSelection() {
        preferences.edit()
            .putString("selectedThemeId", selectedTheme.id)
            .putString("selectedLayoutId", selectedLayout.id)
            .apply()
        state = CustomizationSyncState.SAVED_ON_PHONE
        onStateChanged?.invoke()
    }

    companion object {
        private fun placement(
            id: String, column: Int, row: Int, columns: Int, rows: Int,
            minColumns: Int, minRows: Int
        ) = JSONObject()
            .put("id", id)
            .put("column", column)
            .put("row", row)
            .put("columnSpan", columns)
            .put("rowSpan", rows)
            .put("minColumns", minColumns)
            .put("minRows", minRows)
            .put("visible", 1)

        private fun layoutJson(vararg placements: JSONObject) = JSONObject()
            .put("schemaVersion", 1)
            .put("widgets", JSONArray().apply {
                placements.forEach(::put)
            })
    }
}
