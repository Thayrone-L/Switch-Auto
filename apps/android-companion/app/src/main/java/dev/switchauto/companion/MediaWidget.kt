package dev.switchauto.companion

import android.content.ComponentName
import android.content.Context
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import org.json.JSONObject

class MediaWidget : BaseCompanionWidget() {
    override val id = "core.media"
    override val title = "Mídia"
    override val requiredCapabilities = setOf(AppCapability.MEDIA_CONTROL)

    private lateinit var sessions: MediaSessionManager

    override fun initialize(context: CompanionWidgetContext) {
        super.initialize(context)
        sessions = context.activity.getSystemService(
            Context.MEDIA_SESSION_SERVICE) as MediaSessionManager
    }

    override fun onMessage(message: JSONObject) {
        if (message.optString("type") != "ping") return
        val action = message.optJSONObject("mediaControl")
            ?.optString("action")
            ?.takeIf(String::isNotBlank) ?: return
        val controls = activeController()?.transportControls ?: return
        when (action) {
            "play_pause" -> {
                if (activeController()?.playbackState?.state ==
                    PlaybackState.STATE_PLAYING) controls.pause() else controls.play()
            }
            "previous" -> controls.skipToPrevious()
            "next" -> controls.skipToNext()
        }
        context.log("Widget $id command: $action")
    }

    override fun contributeTelemetry(target: JSONObject): ByteArray? {
        val controller = activeController() ?: return null
        val metadata = controller.metadata
        val title = metadata?.getText(MediaMetadata.METADATA_KEY_TITLE)
            ?.toString()?.takeIf(String::isNotBlank)
            ?: metadata?.getText(MediaMetadata.METADATA_KEY_DISPLAY_TITLE)
                ?.toString()?.takeIf(String::isNotBlank)
            ?: "Mídia em reprodução"
        val artist = metadata?.getText(MediaMetadata.METADATA_KEY_ARTIST)
            ?.toString()?.takeIf(String::isNotBlank)
            ?: metadata?.getText(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
                ?.toString()?.takeIf(String::isNotBlank)
            ?: controller.packageName
        target.put("media", JSONObject()
            .put("title", title)
            .put("artist", artist)
            .put("playing",
                controller.playbackState?.state == PlaybackState.STATE_PLAYING)
            .put("packageName", controller.packageName))
        return null
    }

    override fun statusText(): String {
        if (context.permissions.state(AppCapability.MEDIA_CONTROL) !=
            AppPermissionState.GRANTED) return "Acesso necessário"
        val controller = activeController() ?: return "Nenhuma mídia ativa"
        return controller.metadata?.getText(MediaMetadata.METADATA_KEY_TITLE)
            ?.toString() ?: "Sessão ativa"
    }

    private fun activeController(): MediaController? {
        if (context.permissions.state(AppCapability.MEDIA_CONTROL) !=
            AppPermissionState.GRANTED) return null
        return runCatching {
            sessions.getActiveSessions(ComponentName(
                context.activity, MediaNotificationListener::class.java))
                .maxByOrNull {
                    if (it.playbackState?.state == PlaybackState.STATE_PLAYING) 1 else 0
                }
        }.getOrNull()
    }
}
