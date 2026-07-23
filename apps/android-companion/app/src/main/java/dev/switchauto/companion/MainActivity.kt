package dev.switchauto.companion

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Color
import android.os.BatteryManager
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
import java.net.SocketTimeoutException
import java.util.UUID
import java.security.MessageDigest
import java.security.SecureRandom
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

private const val PROTOCOL_VERSION = 2
private const val SERVER_PORT = 38471
private const val DISCOVERY_PORT = 38472
private const val SETTINGS_SCHEMA_VERSION = 1
private const val ACCEPT_POLL_TIMEOUT_MS = 1_000
private const val SESSION_IO_TIMEOUT_MS = 8_000
private const val DISCOVERY_POLL_TIMEOUT_MS = 1_000

class MainActivity : Activity() {
    private val executor: ExecutorService = Executors.newCachedThreadPool()
    private val running = AtomicBoolean(false)

    private lateinit var statusView: TextView
    private lateinit var addressView: TextView
    private lateinit var logView: TextView
    private lateinit var toggleButton: Button
    private lateinit var scanButton: Button
    private lateinit var forgetButton: Button

    @Volatile private var serverSocket: ServerSocket? = null
    @Volatile private var discoverySocket: DatagramSocket? = null
    private val logLock = Any()
    private val activeClients = ConcurrentHashMap.newKeySet<Socket>()
    private val sessionContext = ThreadLocal<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(buildUi())
        refreshConnectionDetails()
        migratePreferences()
        val hasTrustedSwitch = getPreferences(MODE_PRIVATE).all.keys
            .any { it.startsWith("pairedToken.") }
        if (hasTrustedSwitch) {
            appendLog("Trusted Switch found")
        } else {
            appendLog("No trusted Switch yet; server will accept discovery but reject unknown devices")
        }
        appendLog("Starting Companion server automatically")
        startServer()
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
        forgetButton = Button(this).apply {
            text = "Forget paired Switch"
            isEnabled = hasStoredPairing()
            setOnClickListener { confirmForgetPairing() }
        }
        root.addView(forgetButton, ViewGroup.LayoutParams.MATCH_PARENT,
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
                    server.soTimeout = ACCEPT_POLL_TIMEOUT_MS
                    serverSocket = server
                    runOnUiThread {
                        statusView.text = "Listening on port $SERVER_PORT"
                        appendLog("Companion server ready")
                    }
                    while (running.get()) {
                        try {
                            val client = server.accept()
                            executor.execute { handleClient(client) }
                        } catch (_: SocketTimeoutException) {
                            // Poll running so shutdown never depends on a new client.
                        }
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
        activeClients.forEach { client -> runCatching { client.close() } }
        activeClients.clear()
        serverSocket = null
        discoverySocket = null
        if (::statusView.isInitialized) {
            statusView.text = "Stopped"
            toggleButton.text = "Start server"
            appendLog("Server stopped.")
        }
    }

    private fun handleClient(socket: Socket) {
        var sessionId = UUID.randomUUID().toString().substring(0, 12)
        sessionContext.set(sessionId)
        var outcome = "connection_closed"
        activeClients.add(socket)
        val remote = socket.inetAddress.hostAddress ?: "unknown"
        appendLog("[session=$sessionId] Client connected: $remote")
        try {
            socket.use { client ->
                client.soTimeout = SESSION_IO_TIMEOUT_MS
                val reader = BufferedReader(InputStreamReader(client.getInputStream(), Charsets.UTF_8))
                val writer = BufferedWriter(OutputStreamWriter(client.getOutputStream(), Charsets.UTF_8))
                var paired = false
                var pendingDeviceId: String? = null
                var pendingToken: String? = null
                var pendingClientNonce: String? = null
                var pendingServerNonce: String? = null

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
                            val clientNonce = request.optString("clientNonce")
                            val sharedSessionId = request.optString("sessionId")
                            if (sharedSessionId.matches(Regex("[a-fA-F0-9]{12}"))) {
                                sessionId = sharedSessionId.lowercase(Locale.US)
                                sessionContext.set(sessionId)
                            }
                            val savedToken = getPreferences(MODE_PRIVATE)
                                .getString("pairedToken.$deviceId", null)
                            if (deviceId.isNotBlank() && savedToken != null &&
                                clientNonce.matches(Regex("[a-fA-F0-9]{32}"))) {
                                val serverNonce = randomNonce()
                                pendingDeviceId = deviceId
                                pendingToken = savedToken
                                pendingClientNonce = clientNonce.lowercase(Locale.US)
                                pendingServerNonce = serverNonce
                                val transcript =
                                    "server|$deviceId|${pendingClientNonce}|$serverNonce"
                                send(writer, message("pair.challenge")
                                    .put("serverNonce", serverNonce)
                                    .put("proof", hmacSha256(savedToken, transcript)))
                            } else {
                                appendLog("Pair rejected for deviceId=$deviceId: unknown device or invalid nonce")
                                send(writer, message("pair.rejected").put("reason", "unknown_device"))
                            }
                        }
                        "pair.proof" -> {
                            val deviceId = request.optString("deviceId")
                            val suppliedProof = request.optString("proof")
                            val token = pendingToken
                            val clientNonce = pendingClientNonce
                            val serverNonce = pendingServerNonce
                            val expected = if (token != null && clientNonce != null &&
                                serverNonce != null && deviceId == pendingDeviceId) {
                                hmacSha256(token,
                                    "client|$deviceId|$clientNonce|$serverNonce")
                            } else ""
                            if (expected.isNotEmpty() && secureEquals(expected, suppliedProof)) {
                                paired = true
                                outcome = "online"
                                send(writer, message("pair.accepted")
                                    .put("sessionToken", UUID.randomUUID().toString()))
                                runOnUiThread { statusView.text = "Switch paired: $remote" }
                            } else {
                                appendLog("Authentication rejected for deviceId=$deviceId: invalid proof")
                                send(writer, message("pair.rejected").put("reason", "invalid_proof"))
                            }
                            pendingToken = null
                            pendingClientNonce = null
                            pendingServerNonce = null
                        }
                        "hello" -> if (paired) {
                            send(writer, message("hello.ack").put("server", "android-companion"))
                        } else {
                            send(writer, message("error").put("code", "not_paired"))
                        }
                        "ping" -> if (paired) {
                            val switchBattery = request.optInt("switchBatteryPercent", -1)
                            if (switchBattery in 0..100) {
                                runOnUiThread {
                                    statusView.text =
                                        "Connected — Phone ${phoneBatteryPercent()}% | Switch $switchBattery%"
                                }
                            }
                            send(writer, message("pong")
                                .put("sequence", request.optInt("sequence"))
                                .put("phoneBatteryPercent", phoneBatteryPercent()))
                        } else {
                            send(writer, message("error").put("code", "not_paired"))
                        }
                        else -> send(writer, message("error").put("code", "unknown_type"))
                    }
                }
            }
        } catch (_: SocketTimeoutException) {
            outcome = "connection_timeout"
            if (running.get()) appendLog("[session=$sessionId] Client timed out after ${SESSION_IO_TIMEOUT_MS}ms: $remote")
        } catch (error: Exception) {
            outcome = if (running.get()) "transport_error" else "cancelled"
            if (running.get()) appendLog("[session=$sessionId] Client error: ${error.message}")
        } finally {
            appendLog("[session=$sessionId] Client disconnected: $remote outcome=$outcome")
            writeDiagnosticReport(sessionId, remote, outcome)
            activeClients.remove(socket)
            sessionContext.remove()
        }
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
                socket.soTimeout = DISCOVERY_POLL_TIMEOUT_MS
                val buffer = ByteArray(1024)
                while (running.get()) {
                    val request = DatagramPacket(buffer, buffer.size)
                    try {
                        socket.receive(request)
                    } catch (_: SocketTimeoutException) {
                        continue
                    }
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
        val session = sessionContext.get()
        val prefix = if (session == null) "" else "[session=$session] "
        appendLog("$prefix$direction $type peer=$peer$suffix")
    }

    private fun writeDiagnosticReport(sessionId: String, peer: String, outcome: String) {
        synchronized(logLock) {
            try {
                val directory = java.io.File(
                    getExternalFilesDir(null) ?: filesDir, "diagnostics")
                directory.mkdirs()
                val target = java.io.File(directory, "latest.json")
                val temporary = java.io.File(directory, "latest.json.tmp")
                temporary.writeText(JSONObject()
                    .put("schemaVersion", 1)
                    .put("platform", "android")
                    .put("protocolVersion", PROTOCOL_VERSION)
                    .put("sessionId", sessionId)
                    .put("peer", peer)
                    .put("outcome", outcome)
                    .put("timestamp", System.currentTimeMillis())
                    .toString())
                if (target.exists()) target.delete()
                temporary.renameTo(target)
            } catch (_: Exception) {
            }
        }
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
                .putInt("schemaVersion", SETTINGS_SCHEMA_VERSION)
                .apply()
            forgetButton.isEnabled = true
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

    private fun hasStoredPairing(): Boolean = getPreferences(MODE_PRIVATE).all.keys
        .any { it.startsWith("pairedToken.") }

    private fun migratePreferences() {
        val preferences = getPreferences(MODE_PRIVATE)
        val version = try {
            preferences.getInt("schemaVersion", 0)
        } catch (_: ClassCastException) {
            appendLog("Settings schema was corrupt; resetting version metadata")
            preferences.edit().remove("schemaVersion").apply()
            0
        }
        val invalidPairings = preferences.all
            .filter { (key, value) ->
                key.startsWith("pairedToken.") &&
                    (value !is String || !value.matches(Regex("[A-F0-9]{64}")))
            }
            .keys
        if (invalidPairings.isNotEmpty()) {
            val editor = preferences.edit()
            invalidPairings.forEach(editor::remove)
            editor.apply()
            appendLog("Removed ${invalidPairings.size} corrupt pairing credential(s)")
        }
        when {
            version == 0 -> {
                preferences.edit().putInt("schemaVersion", SETTINGS_SCHEMA_VERSION).apply()
                appendLog("Settings migrated to schema $SETTINGS_SCHEMA_VERSION")
            }
            version > SETTINGS_SCHEMA_VERSION ->
                appendLog("Settings schema $version is newer than supported $SETTINGS_SCHEMA_VERSION")
        }
    }

    private fun confirmForgetPairing() {
        AlertDialog.Builder(this)
            .setTitle("Forget paired Switch?")
            .setMessage("The Switch will need to scan its QR again before it can reconnect.")
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Forget") { _, _ -> forgetPairing() }
            .show()
    }

    private fun forgetPairing() {
        val preferences = getPreferences(MODE_PRIVATE)
        val editor = preferences.edit()
        preferences.all.keys.filter { it.startsWith("pairedToken.") }
            .forEach { editor.remove(it) }
        editor.remove("lastDeviceId")
            .putInt("schemaVersion", SETTINGS_SCHEMA_VERSION)
            .apply()
        activeClients.forEach { client -> runCatching { client.close() } }
        activeClients.clear()
        forgetButton.isEnabled = false
        statusView.text = "Pairing removed — scan the Switch QR to pair again"
        appendLog("All trusted Switch credentials were revoked")
    }

    private fun phoneBatteryPercent(): Int {
        val manager = getSystemService(BATTERY_SERVICE) as BatteryManager
        val capacity = manager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
        if (capacity in 0..100) return capacity
        val battery = registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val level = battery?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale = battery?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
        return if (level >= 0 && scale > 0) level * 100 / scale else 0
    }

    private fun randomNonce(): String {
        val bytes = ByteArray(16)
        SecureRandom().nextBytes(bytes)
        return bytes.joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }

    private fun hmacSha256(hexKey: String, message: String): String {
        val key = hexKey.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        return mac.doFinal(message.toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }

    private fun secureEquals(left: String, right: String): Boolean =
        MessageDigest.isEqual(
            left.lowercase(Locale.US).toByteArray(Charsets.US_ASCII),
            right.lowercase(Locale.US).toByteArray(Charsets.US_ASCII))

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
