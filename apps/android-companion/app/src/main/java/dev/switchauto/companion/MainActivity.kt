package dev.switchauto.companion

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.net.Uri
import android.text.method.ScrollingMovementMethod
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning
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
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
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
    private lateinit var logView: TextView
    private lateinit var toggleButton: Button
    private lateinit var scanButton: Button

    @Volatile private var serverSocket: ServerSocket? = null
    @Volatile private var discoverySocket: DatagramSocket? = null
    private val logLock = Any()

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
        scanButton = Button(this).apply {
            text = "Scan Switch QR"
            setOnClickListener { scanSwitchQr() }
        }
        root.addView(scanButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
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
                    val request = try {
                        JSONObject(line)
                    } catch (_: Exception) {
                        appendLog("RX invalid JSON from $remote")
                        send(writer, message("error").put("code", "invalid_json"))
                        continue
                    }
                    logProtocol("RX", request, remote)

                    if (request.optInt("version") != PROTOCOL_VERSION) {
                        send(writer, message("error").put("code", "unsupported_version"))
                        continue
                    }

                    when (request.optString("type")) {
                        "pair.request" -> {
                            val deviceId = request.optString("deviceId")
                            val suppliedToken = request.optString("pairingToken")
                            val savedToken = getPreferences(MODE_PRIVATE)
                                .getString("pairedToken.$deviceId", null)
                            if (deviceId.isNotBlank() &&
                                savedToken != null && suppliedToken == savedToken) {
                                paired = true
                                send(writer, message("pair.accepted")
                                    .put("sessionToken", UUID.randomUUID().toString()))
                                runOnUiThread { statusView.text = "Switch paired: $remote" }
                            } else {
                                appendLog("Pair rejected for deviceId=$deviceId: credential mismatch")
                                send(writer, message("pair.rejected").put("reason", "invalid_credential"))
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
        logProtocol("TX", payload, "Switch")
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
        val timestamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US).format(Date())
        val line = "$timestamp $message\n"
        synchronized(logLock) {
            try {
                val directory = getExternalFilesDir(null) ?: filesDir
                directory.mkdirs()
                val file = java.io.File(directory, "switch-auto.log")
                if (file.length() > 1024 * 1024) {
                    val previous = java.io.File(directory, "switch-auto.log.1")
                    previous.delete()
                    file.renameTo(previous)
                }
                file.appendText(line)
            } catch (_: Exception) {
            }
        }
        runOnUiThread {
            if (::logView.isInitialized) logView.append(line)
        }
    }

    private fun logProtocol(direction: String, payload: JSONObject, peer: String) {
        val type = payload.optString("type", "unknown")
        val deviceId = payload.optString("deviceId")
        val suffix = if (deviceId.isBlank()) "" else " deviceId=$deviceId"
        appendLog("$direction $type peer=$peer$suffix")
    }

    private fun scanSwitchQr() {
        appendLog("Opening QR scanner")
        val options = GmsBarcodeScannerOptions.Builder()
            .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
            .enableAutoZoom()
            .build()
        GmsBarcodeScanning.getClient(this, options).startScan()
            .addOnSuccessListener { barcode ->
                val raw = barcode.rawValue
                if (raw == null || !storePairingQr(raw)) {
                    statusView.text = "Invalid Switch Auto QR"
                    appendLog("QR rejected: invalid payload")
                }
            }
            .addOnCanceledListener { appendLog("QR scan canceled") }
            .addOnFailureListener { error ->
                statusView.text = "QR scanner error"
                appendLog("QR scanner error: ${error.message}")
            }
    }

    private fun storePairingQr(raw: String): Boolean {
        return try {
            val uri = Uri.parse(raw)
            val version = uri.getQueryParameter("v")
            val deviceId = uri.getQueryParameter("id") ?: return false
            val token = uri.getQueryParameter("token") ?: return false
            if (uri.scheme != "switchauto" || uri.host != "pair" || version != "1" ||
                !deviceId.matches(Regex("[A-Z0-9]{8,32}")) ||
                !token.matches(Regex("[A-F0-9]{64}"))) return false
            getPreferences(MODE_PRIVATE).edit()
                .putString("pairedToken.$deviceId", token)
                .putString("lastDeviceId", deviceId)
                .apply()
            statusView.text = "Switch QR paired: $deviceId"
            appendLog("QR accepted for deviceId=$deviceId; secret stored in app preferences")
            if (!running.get()) startServer()
            true
        } catch (error: Exception) {
            appendLog("QR parse error: ${error.message}")
            false
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
