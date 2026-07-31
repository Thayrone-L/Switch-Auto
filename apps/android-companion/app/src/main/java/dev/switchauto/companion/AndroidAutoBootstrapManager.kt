package dev.switchauto.companion

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.util.Log
import org.json.JSONObject
import java.net.BindException
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

class AndroidAutoBootstrapManager(
    private val context: Context,
    private val diagnosticLog: (String) -> Unit = {}
) {
    enum class State(val wireValue: String) {
        CHECKING("checking"),
        READY("ready"),
        MANUAL_ACTION_REQUIRED("manual_action_required")
    }

    private val state = AtomicReference(State.CHECKING)
    private val trustedAddress = AtomicReference<String?>(null)
    private val relayRunning = AtomicBoolean(false)
    private val projectionSessions = AtomicInteger(0)
    private val relayExecutor = Executors.newCachedThreadPool()
    private val nextRelaySession = AtomicLong(1)
    @Volatile private var relayServer: ServerSocket? = null

    fun state(): State = state.get()

    fun isProjectionActive(): Boolean = projectionSessions.get() > 0

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
                    diagnostic("relay listening port=$RELAY_PORT")
                    while (relayRunning.get()) {
                        try {
                            val client = server.accept()
                            relayExecutor.execute { relay(client) }
                        } catch (_: SocketTimeoutException) {
                            // Periodically observe relayRunning.
                        }
                    }
                }
            } catch (error: Exception) {
                diagnostic("relay server failed: ${error.javaClass.simpleName}: ${error.message}")
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
        val session = nextRelaySession.getAndIncrement()
        client.use { external ->
            val remote = external.inetAddress.hostAddress ?: "unknown"
            if (remote != trustedAddress.get()) {
                diagnostic("relay[$session] rejected peer=$remote")
                return
            }
            diagnostic("relay[$session] accepted peer=$remote")
            projectionSessions.incrementAndGet()
            try {
                Socket().use { androidAuto ->
                    androidAuto.connect(InetSocketAddress(
                        LOOPBACK_ADDRESS, HEAD_UNIT_SERVER_PORT),
                        CONNECT_TIMEOUT_MS)
                    diagnostic("relay[$session] connected to Android Auto")
                    val upstream = relayExecutor.submit {
                        pump(session, "switch->android",
                            external.getInputStream(), androidAuto.getOutputStream())
                        runCatching { androidAuto.shutdownOutput() }
                    }
                    pump(session, "android->switch",
                        androidAuto.getInputStream(), external.getOutputStream())
                    runCatching { external.shutdownOutput() }
                    runCatching { upstream.get() }
                }
            } catch (error: Exception) {
                diagnostic("relay[$session] failed: ${error.javaClass.simpleName}: ${error.message}")
                state.set(State.MANUAL_ACTION_REQUIRED)
            } finally {
                projectionSessions.decrementAndGet()
                diagnostic("relay[$session] closed")
            }
        }
    }

    private fun pump(
        session: Long,
        direction: String,
        input: java.io.InputStream,
        output: java.io.OutputStream
    ) {
        var total = 0L
        val preview = ArrayList<Byte>(32)
        val buffer = ByteArray(8192)
        try {
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                if (count == 0) continue
                val previewCount = minOf(count, 32 - preview.size)
                repeat(previewCount) { preview.add(buffer[it]) }
                output.write(buffer, 0, count)
                output.flush()
                total += count
            }
            diagnostic("relay[$session] $direction eof bytes=$total first=${hex(preview)}")
        } catch (error: Exception) {
            diagnostic(
                "relay[$session] $direction error bytes=$total first=${hex(preview)} " +
                    "${error.javaClass.simpleName}: ${error.message}")
        }
    }

    private fun hex(bytes: List<Byte>): String =
        if (bytes.isEmpty()) "-" else bytes.joinToString("") {
            "%02x".format(it.toInt() and 0xff)
        }

    private fun diagnostic(message: String) {
        Log.i(LOG_TAG, message)
        diagnosticLog("Android Auto $message")
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
        private const val LOG_TAG = "SwitchAutoAA"
    }
}
