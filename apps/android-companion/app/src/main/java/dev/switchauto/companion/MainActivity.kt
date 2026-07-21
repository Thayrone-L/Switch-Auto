package dev.switchauto.companion

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject
import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.Inet4Address
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.security.SecureRandom
import java.util.UUID
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

private const val PROTOCOL_VERSION = 1
private const val SERVER_PORT = 38471

class MainActivity : Activity() {
    private val executor: ExecutorService = Executors.newCachedThreadPool()
    private val running = AtomicBoolean(false)
    private val random = SecureRandom()

    private lateinit var statusView: TextView
    private lateinit var addressView: TextView
    private lateinit var codeView: TextView
    private lateinit var logView: TextView
    private lateinit var toggleButton: Button

    @Volatile private var serverSocket: ServerSocket? = null
    @Volatile private var pairingCode: String = generatePairingCode()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(buildUi())
        refreshConnectionDetails()
        appendLog("Ready. Start the server, then connect the Switch.")
    }

    override fun onDestroy() {
        stopServer()
        executor.shutdownNow()
        super.onDestroy()
    }

    private fun buildUi(): ScrollView {
        val density = resources.displayMetrics.density
        val padding = (24 * density).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(padding, padding, padding, padding)
        }

        root.addView(label("Switch Auto", 32f).apply {
            setTextColor(Color.rgb(230, 0, 18))
        })
        root.addView(label("Handshake prototype", 18f))

        statusView = label("Stopped", 20f)
        addressView = label("IP: unavailable", 18f)
        codeView = label("------", 40f).apply {
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(0, padding, 0, padding)
        }
        toggleButton = Button(this).apply {
            text = "Start server"
            setOnClickListener {
                if (running.get()) stopServer() else startServer()
            }
        }
        logView = label("", 14f).apply {
            typeface = android.graphics.Typeface.MONOSPACE
            movementMethod = ScrollingMovementMethod()
            setTextIsSelectable(true)
        }

        root.addView(statusView)
        root.addView(addressView)
        root.addView(codeView)
        root.addView(toggleButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        root.addView(label("Event log", 20f).apply { setPadding(0, padding, 0, 0) })
        root.addView(logView)

        return ScrollView(this).apply { addView(root) }
    }

    private fun label(value: String, size: Float) = TextView(this).apply {
        text = value
        textSize = size
        setTextColor(Color.DKGRAY)
        setPadding(0, 8, 0, 8)
    }

    private fun startServer() {
        if (!running.compareAndSet(false, true)) return
        pairingCode = generatePairingCode()
        refreshConnectionDetails()
        statusView.text = "Starting..."
        toggleButton.text = "Stop server"

        executor.execute {
            try {
                ServerSocket(SERVER_PORT).use { server ->
                    serverSocket = server
                    runOnUiThread {
                        statusView.text = "Listening on port $SERVER_PORT"
                        appendLog("Server started with a new pairing code.")
                    }
                    while (running.get()) {
                        val client = server.accept()
                        executor.execute { handleClient(client) }
                    }
                }
            } catch (error: Exception) {
                if (running.get()) appendLog("Server error: ${error.message}")
            } finally {
                running.set(false)
                serverSocket = null
                runOnUiThread {
                    statusView.text = "Stopped"
                    toggleButton.text = "Start server"
                }
            }
        }
    }

    private fun stopServer() {
        running.set(false)
        serverSocket?.close()
        serverSocket = null
        if (::statusView.isInitialized) {
            statusView.text = "Stopped"
            toggleButton.text = "Start server"
            appendLog("Server stopped.")
        }
    }

    private fun handleClient(socket: Socket) {
        val remote = socket.inetAddress.hostAddress ?: "unknown"
        appendLog("Client connected: $remote")
        try {
            socket.use { client ->
                client.soTimeout = 15_000
                val reader = BufferedReader(InputStreamReader(client.getInputStream(), Charsets.UTF_8))
                val writer = BufferedWriter(OutputStreamWriter(client.getOutputStream(), Charsets.UTF_8))
                var paired = false

                while (running.get()) {
                    val line = reader.readLine() ?: break
                    appendLog("RX $line")
                    val request = try {
                        JSONObject(line)
                    } catch (_: Exception) {
                        send(writer, message("error").put("code", "invalid_json"))
                        continue
                    }

                    if (request.optInt("version") != PROTOCOL_VERSION) {
                        send(writer, message("error").put("code", "unsupported_version"))
                        continue
                    }

                    when (request.optString("type")) {
                        "pair.request" -> {
                            if (request.optString("code") == pairingCode) {
                                paired = true
                                send(writer, message("pair.accepted")
                                    .put("sessionToken", UUID.randomUUID().toString()))
                                runOnUiThread { statusView.text = "Switch paired: $remote" }
                            } else {
                                send(writer, message("pair.rejected").put("reason", "invalid_code"))
                            }
                        }
                        "hello" -> if (paired) {
                            send(writer, message("hello.ack").put("server", "android-companion"))
                        } else {
                            send(writer, message("error").put("code", "not_paired"))
                        }
                        "ping" -> if (paired) {
                            send(writer, message("pong")
                                .put("sequence", request.optInt("sequence")))
                        } else {
                            send(writer, message("error").put("code", "not_paired"))
                        }
                        else -> send(writer, message("error").put("code", "unknown_type"))
                    }
                }
            }
        } catch (error: Exception) {
            appendLog("Client error: ${error.message}")
        }
        appendLog("Client disconnected: $remote")
    }

    private fun message(type: String) = JSONObject()
        .put("version", PROTOCOL_VERSION)
        .put("type", type)
        .put("timestamp", System.currentTimeMillis())

    private fun send(writer: BufferedWriter, payload: JSONObject) {
        val line = payload.toString()
        writer.write(line)
        writer.newLine()
        writer.flush()
        appendLog("TX $line")
    }

    private fun appendLog(message: String) {
        runOnUiThread {
            if (::logView.isInitialized) logView.append("$message\n")
        }
    }

    private fun refreshConnectionDetails() {
        if (!::addressView.isInitialized) return
        addressView.text = "IP: ${findIpv4Address() ?: "unavailable"}   Port: $SERVER_PORT"
        codeView.text = pairingCode
    }

    private fun generatePairingCode(): String = "%06d".format(random.nextInt(1_000_000))

    private fun findIpv4Address(): String? = try {
        NetworkInterface.getNetworkInterfaces().toList()
            .flatMap { it.inetAddresses.toList() }
            .filterIsInstance<Inet4Address>()
            .firstOrNull { !it.isLoopbackAddress && it.isSiteLocalAddress }
            ?.hostAddress
    } catch (_: Exception) {
        null
    }
}

