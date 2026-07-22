package dev.switchauto.companion

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject
import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.Inet4Address
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.util.UUID
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

private const val PROTOCOL_VERSION = 1
private const val SERVER_PORT = 38471
private const val DISCOVERY_PORT = 38472

class MainActivity : Activity() {
    private val executor: ExecutorService = Executors.newCachedThreadPool()
    private val running = AtomicBoolean(false)

    private lateinit var statusView: TextView
    private lateinit var addressView: TextView
    private lateinit var codeInput: EditText
    private lateinit var logView: TextView
    private lateinit var toggleButton: Button

    @Volatile private var serverSocket: ServerSocket? = null
    @Volatile private var discoverySocket: DatagramSocket? = null
    @Volatile private var pairingCode: String = ""

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
        codeInput = EditText(this).apply {
            hint = "Switch pairing code"
            inputType = android.text.InputType.TYPE_CLASS_NUMBER
            filters = arrayOf(android.text.InputFilter.LengthFilter(6))
            gravity = Gravity.CENTER_HORIZONTAL
            textSize = 30f
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
        root.addView(codeInput)
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
        pairingCode = codeInput.text.toString()
        if (pairingCode.isNotEmpty() && !pairingCode.matches(Regex("\\d{6}"))) {
            running.set(false)
            statusView.text = "Enter a valid 6-digit Switch code"
            return
        }
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
        executor.execute { runDiscoveryResponder() }
    }

    private fun stopServer() {
        running.set(false)
        serverSocket?.close()
        discoverySocket?.close()
        serverSocket = null
        discoverySocket = null
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
                            val deviceId = request.optString("deviceId")
                            val suppliedCode = request.optString("code")
                            val savedCode = getPreferences(MODE_PRIVATE)
                                .getString("paired.$deviceId", null)
                            if (deviceId.isNotBlank() &&
                                (suppliedCode == savedCode ||
                                 (pairingCode.isNotEmpty() && suppliedCode == pairingCode))) {
                                paired = true
                                getPreferences(MODE_PRIVATE).edit()
                                    .putString("paired.$deviceId", suppliedCode).apply()
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

    private fun runDiscoveryResponder() {
        try {
            DatagramSocket(DISCOVERY_PORT).use { socket ->
                discoverySocket = socket
                socket.broadcast = true
                val buffer = ByteArray(1024)
                while (running.get()) {
                    val request = DatagramPacket(buffer, buffer.size)
                    socket.receive(request)
                    val payload = String(request.data, 0, request.length, Charsets.UTF_8)
                    val json = try { JSONObject(payload.trim()) } catch (_: Exception) { continue }
                    if (json.optInt("version") != PROTOCOL_VERSION ||
                        json.optString("type") != "discover.request") continue
                    val response = message("discover.reply")
                        .put("server", "android-companion")
                        .toString().toByteArray(Charsets.UTF_8)
                    socket.send(DatagramPacket(response, response.size, request.address, request.port))
                    appendLog("Discovery reply sent to ${request.address.hostAddress}")
                }
            }
        } catch (error: Exception) {
            if (running.get()) appendLog("Discovery error: ${error.message}")
        } finally {
            discoverySocket = null
        }
    }

    private fun appendLog(message: String) {
        runOnUiThread {
            if (::logView.isInitialized) logView.append("$message\n")
        }
    }

    private fun refreshConnectionDetails() {
        if (!::addressView.isInitialized) return
        addressView.text = "IP: ${findIpv4Address() ?: "unavailable"}   Port: $SERVER_PORT"
    }

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
