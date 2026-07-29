package dev.switchauto.companion

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import org.json.JSONObject
import java.net.BindException
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

class AndroidAutoBootstrapManager(private val context: Context) {
    enum class State(val wireValue: String) {
        CHECKING("checking"),
        READY("ready"),
        MANUAL_ACTION_REQUIRED("manual_action_required")
    }

    private val state = AtomicReference(State.CHECKING)
    private val trustedAddress = AtomicReference<String?>(null)
    private val relayRunning = AtomicBoolean(false)
    private val relayExecutor = Executors.newCachedThreadPool()
    @Volatile private var relayServer: ServerSocket? = null

    fun state(): State = state.get()

    fun setTrustedAddress(address: String) {
        trustedAddress.set(address)
    }

    fun probe(): State {
        val next = try {
            ServerSocket().use { socket ->
                socket.reuseAddress = false
                socket.bind(
                    InetSocketAddress(LOOPBACK_ADDRESS, HEAD_UNIT_SERVER_PORT))
            }
            State.MANUAL_ACTION_REQUIRED
        } catch (_: BindException) {
            State.READY
        } catch (_: Exception) {
            State.MANUAL_ACTION_REQUIRED
        }
        state.set(next)
        return next
    }

    fun contribute(target: JSONObject) {
        target.put("androidAutoBootstrap", JSONObject()
            .put("schemaVersion", 1)
            .put("state", state().wireValue)
            .put("mode", "developer_server_relay")
            .put("port", RELAY_PORT))
    }

    fun startRelay() {
        if (!relayRunning.compareAndSet(false, true)) return
        relayExecutor.execute {
            try {
                ServerSocket(RELAY_PORT).use { server ->
                    relayServer = server
                    server.soTimeout = ACCEPT_TIMEOUT_MS
                    while (relayRunning.get()) {
                        try {
                            val client = server.accept()
                            relayExecutor.execute { relay(client) }
                        } catch (_: SocketTimeoutException) {
                            // Periodically observe relayRunning.
                        }
                    }
                }
            } catch (_: Exception) {
                state.set(State.MANUAL_ACTION_REQUIRED)
            } finally {
                relayServer = null
                relayRunning.set(false)
            }
        }
    }

    fun stop() {
        relayRunning.set(false)
        runCatching { relayServer?.close() }
        relayExecutor.shutdownNow()
    }

    private fun relay(client: Socket) {
        client.use { external ->
            if (external.inetAddress.hostAddress != trustedAddress.get()) return
            try {
                Socket().use { androidAuto ->
                    androidAuto.connect(InetSocketAddress(
                        LOOPBACK_ADDRESS, HEAD_UNIT_SERVER_PORT),
                        CONNECT_TIMEOUT_MS)
                    val upstream = relayExecutor.submit {
                        runCatching {
                            external.getInputStream().copyTo(
                                androidAuto.getOutputStream())
                        }
                        runCatching { androidAuto.shutdownOutput() }
                    }
                    runCatching {
                        androidAuto.getInputStream().copyTo(
                            external.getOutputStream())
                    }
                    runCatching { external.shutdownOutput() }
                    runCatching { upstream.get() }
                }
            } catch (_: Exception) {
                state.set(State.MANUAL_ACTION_REQUIRED)
            }
        }
    }

    fun openAndroidAuto() {
        val settingsIntent = Intent(ANDROID_AUTO_SETTINGS_ACTION)
            .setPackage(ANDROID_AUTO_PACKAGE)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (settingsIntent.resolveActivity(context.packageManager) != null) {
            context.startActivity(settingsIntent)
            return
        }
        context.startActivity(Intent(
            Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
            Uri.parse("package:$ANDROID_AUTO_PACKAGE")
        ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    companion object {
        const val HEAD_UNIT_SERVER_PORT = 5277
        const val RELAY_PORT = 5288
        private const val LOOPBACK_ADDRESS = "127.0.0.1"
        private const val CONNECT_TIMEOUT_MS = 2_000
        private const val ACCEPT_TIMEOUT_MS = 1_000
        private const val ANDROID_AUTO_PACKAGE =
            "com.google.android.projection.gearhead"
        private const val ANDROID_AUTO_SETTINGS_ACTION =
            "com.google.android.projection.gearhead.SETTINGS"
    }
}
