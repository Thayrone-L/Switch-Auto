package dev.switchauto.companion

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.media.projection.MediaProjectionManager
import android.provider.Settings
import android.net.Uri
import android.text.method.ScrollingMovementMethod
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
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
import org.maplibre.android.MapLibre

private const val PROTOCOL_VERSION = 3
private const val SERVER_PORT = 38471
private const val DISCOVERY_PORT = 38472
private const val SETTINGS_SCHEMA_VERSION = 1
private const val ACCEPT_POLL_TIMEOUT_MS = 1_000
private const val SESSION_IO_TIMEOUT_MS = 8_000
private const val DISCOVERY_POLL_TIMEOUT_MS = 1_000

class MainActivity : Activity() {
    private enum class AppPage { HOME, CONNECTION, CUSTOMIZATION, DIAGNOSTICS }

    private val executor: ExecutorService = Executors.newCachedThreadPool()
    private val running = AtomicBoolean(false)
    private lateinit var permissionManager: AndroidPermissionManager
    private lateinit var widgetRegistry: CompanionWidgetRegistry
    private lateinit var batteryWidget: BatteryWidget
    private lateinit var gpsWidget: GpsWidget
    private lateinit var mediaWidget: MediaWidget
    private lateinit var customizationManager: CustomizationManager

    private lateinit var statusView: TextView
    private lateinit var addressView: TextView
    private lateinit var pairingStatusView: TextView
    private lateinit var logView: TextView
    private lateinit var toggleButton: Button
    private lateinit var scanButton: Button
    private lateinit var forgetButton: Button
    private lateinit var widgetStatusView: TextView
    private lateinit var gpsButton: Button
    private lateinit var mediaButton: Button
    private lateinit var routeStatusView: TextView
    private lateinit var routeButton: Button
    private lateinit var wazeButton: Button
    private lateinit var streamWazeButton: Button
    private lateinit var clearRouteButton: Button
    private lateinit var customizationStatusView: TextView
    private lateinit var themeButton: Button
    private lateinit var layoutButton: Button
    private lateinit var applyCustomizationButton: Button
    private lateinit var homePage: LinearLayout
    private lateinit var connectionPage: LinearLayout
    private lateinit var customizationPage: LinearLayout
    private lateinit var diagnosticsPage: LinearLayout
    private lateinit var homeConnectionDot: View

    @Volatile private var serverSocket: ServerSocket? = null
    @Volatile private var discoverySocket: DatagramSocket? = null
    @Volatile private var foregroundLocationActive: Boolean? = null
    private val logLock = Any()
    private val activeClients = ConcurrentHashMap.newKeySet<Socket>()
    private val sessionContext = ThreadLocal<String>()
    private val wazeNavigationProvider = WazeNavigationProvider()
    private var selectedNavigationDestination: NavigationDestination? = null
    private var screenCaptureController: ScreenCaptureController? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        MapLibre.getInstance(this)
        customizationManager = CustomizationManager(this)
        setContentView(buildUi())
        customizationManager.onStateChanged = {
            runOnUiThread(::refreshCustomizationViews)
        }
        permissionManager = AndroidPermissionManager(this)
        initializeWidgets()
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
        appendLog("Capabilities: " + permissionManager.summary().entries.joinToString {
            "${it.key.name.lowercase(Locale.US)}=${it.value.name.lowercase(Locale.US)}"
        })
        startServer()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        permissionManager.onRequestPermissionsResult(requestCode)
    }

    override fun onDestroy() {
        screenCaptureController?.stop()
        if (::widgetRegistry.isInitialized) widgetRegistry.dispose()
        stopServer()
        executor.shutdownNow()
        super.onDestroy()
    }

    override fun onResume() {
        super.onResume()
        if (::widgetRegistry.isInitialized) refreshWidgetViews()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != SCREEN_CAPTURE_REQUEST_CODE) return
        if (resultCode != RESULT_OK || data == null) {
            appendLog("Waze screen capture permission denied")
            refreshWidgetViews()
            return
        }
        CompanionForegroundService.start(
            this,
            ::gpsWidget.isInitialized && gpsWidget.isEnabled(),
            mediaProjectionActive = true
        )
        val controller = ScreenCaptureController(
            activity = this,
            onFrame = { frame -> gpsWidget.submitExternalMapFrame(frame) },
            onStopped = {
                runOnUiThread {
                    gpsWidget.stopExternalMapStream()
                    screenCaptureController = null
                    refreshWidgetViews()
                    appendLog("Waze screen stream stopped; MapLibre fallback restored")
                }
            },
            log = ::appendLog
        )
        controller.start(resultCode, data)
            .onSuccess {
                screenCaptureController = controller
                refreshWidgetViews()
                selectedNavigationDestination?.let { startWazeNavigation() }
            }
            .onFailure { error ->
                appendLog("Could not start Waze screen stream: ${error.message}")
                gpsWidget.stopExternalMapStream()
                refreshWidgetViews()
            }
    }

    @Deprecated("The Companion stays active while driving")
    override fun onBackPressed() {
        moveTaskToBack(true)
    }

    private fun buildUi(): ScrollView {
        val density = resources.displayMetrics.density
        val padding = (20 * density).toInt()
        val gap = (12 * density).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(padding, padding, padding, padding)
            setBackgroundColor(Color.rgb(9, 13, 20))
        }

        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val brand = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(label("Switch Auto", 30f).apply {
                setTextColor(Color.rgb(240, 68, 85))
                setTypeface(typeface, Typeface.BOLD)
            })
            addView(label("Seu carro, do seu jeito", 14f).apply {
                setTextColor(Color.rgb(145, 160, 181))
            })
        }
        header.addView(brand, LinearLayout.LayoutParams(
            0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        homeConnectionDot = View(this).apply {
            background = connectionDot(false)
            contentDescription = "Switch desconectado"
        }
        val dotSize = (16 * density).toInt()
        header.addView(homeConnectionDot, LinearLayout.LayoutParams(
            dotSize, dotSize).apply {
            marginEnd = (14 * density).toInt()
        })
        header.addView(Button(this).apply {
            text = "Menu"
            styleSecondaryButton(this)
            setOnClickListener { showAppMenu() }
        })
        root.addView(header)

        fun page() = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, gap, 0, 0)
        }
        homePage = page()
        connectionPage = page()
        customizationPage = page()
        diagnosticsPage = page()

        val statusCard = card(padding).apply {
            addView(label("CONEXÃO", 13f).apply {
                setTextColor(Color.rgb(145, 160, 181))
            })
        }
        statusView = label("Starting Companion…", 24f).apply {
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(Color.WHITE)
        }
        addressView = label("Preparing local network", 15f).apply {
            setTextColor(Color.rgb(145, 160, 181))
        }
        statusCard.addView(statusView)
        statusCard.addView(addressView)
        connectionPage.addView(statusCard)

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
            setTextColor(Color.rgb(190, 200, 214))
            visibility = View.GONE
        }

        val pairingCard = card(padding).apply {
            val params = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT)
            params.topMargin = gap
            layoutParams = params
            addView(label("SEU NINTENDO SWITCH", 13f).apply {
                setTextColor(Color.rgb(145, 160, 181))
            })
            pairingStatusView = label(
                if (hasStoredPairing()) "Pronto para conexão automática"
                else "Nenhum Switch pareado", 19f).apply {
                setTextColor(Color.WHITE)
            }
            addView(pairingStatusView)
        }
        scanButton = Button(this).apply {
            text = if (hasStoredPairing()) "Conectar outro Switch" else "Ler QR do Switch"
            stylePrimaryButton(this)
            setOnClickListener { scanSwitchQr() }
        }
        pairingCard.addView(scanButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        forgetButton = Button(this).apply {
            text = "Remover pareamento"
            styleSecondaryButton(this)
            isEnabled = hasStoredPairing()
            setOnClickListener { confirmForgetPairing() }
        }
        pairingCard.addView(forgetButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        connectionPage.addView(pairingCard)

        val widgetsCard = card(padding).apply {
            val params = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT)
            params.topMargin = gap
            layoutParams = params
            addView(label("NAVEGAÇÃO", 13f).apply {
                setTextColor(Color.rgb(145, 160, 181))
            })
        }
        widgetStatusView = label("Loading widgets…", 16f).apply {
            setTextColor(Color.WHITE)
        }
        widgetStatusView.visibility = View.GONE
        widgetsCard.addView(widgetStatusView)
        widgetsCard.addView(label("Para onde vamos?", 24f).apply {
            setTypeface(typeface, Typeface.BOLD)
        })
        routeStatusView = label("Nenhum destino selecionado", 15f).apply {
            setTextColor(Color.rgb(170, 182, 199))
        }
        widgetsCard.addView(routeStatusView)
        routeButton = Button(this).apply {
            text = "Escolher destino"
            stylePrimaryButton(this)
            setOnClickListener { showDestinationDialog() }
        }
        widgetsCard.addView(routeButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        wazeButton = Button(this).apply {
            text = "Iniciar no Waze"
            stylePrimaryButton(this)
            visibility = View.GONE
            setOnClickListener { startWazeNavigation() }
        }
        widgetsCard.addView(wazeButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        streamWazeButton = Button(this).apply {
            text = "Transmitir Waze para o Switch"
            styleSecondaryButton(this)
            visibility = View.GONE
            setOnClickListener { toggleWazeScreenStream() }
        }
        widgetsCard.addView(streamWazeButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        clearRouteButton = Button(this).apply {
            text = "Encerrar rota"
            styleSecondaryButton(this)
            setOnClickListener {
                gpsWidget.clearDestination()
                selectedNavigationDestination = null
                refreshWidgetViews()
            }
        }
        widgetsCard.addView(clearRouteButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        gpsButton = Button(this).apply {
            text = "Ativar localização"
            styleSecondaryButton(this)
            setOnClickListener {
                if (::gpsWidget.isInitialized) gpsWidget.setEnabled(!gpsWidget.isEnabled())
            }
        }
        widgetsCard.addView(gpsButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        mediaButton = Button(this).apply {
            text = "Permitir controle de mídia"
            styleSecondaryButton(this)
            setOnClickListener {
                startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
            }
        }
        widgetsCard.addView(mediaButton, ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        homePage.addView(widgetsCard)

        val customizationCard = card(padding).apply {
            val params = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT)
            params.topMargin = gap
            layoutParams = params
            addView(label("PERSONALIZAR", 13f).apply {
                setTextColor(Color.rgb(145, 160, 181))
            })
        }
        customizationStatusView = label("", 16f)
        customizationCard.addView(customizationStatusView)
        themeButton = Button(this).apply {
            setOnClickListener { customizationManager.selectNextTheme() }
        }
        layoutButton = Button(this).apply {
            setOnClickListener { customizationManager.selectNextLayout() }
        }
        applyCustomizationButton = Button(this).apply {
            text = "APPLY ON SWITCH"
            backgroundTintList = ColorStateList.valueOf(Color.rgb(240, 68, 85))
            setTextColor(Color.WHITE)
            setOnClickListener {
                val revision = customizationManager.requestSync()
                appendLog("Customization revision $revision queued")
            }
        }
        customizationCard.addView(themeButton)
        customizationCard.addView(layoutButton)
        customizationCard.addView(applyCustomizationButton)
        customizationPage.addView(customizationCard)
        refreshCustomizationViews()

        val diagnosticsButton = Button(this).apply {
            text = "Diagnósticos"
            styleSecondaryButton(this)
            setOnClickListener {
                val show = logView.visibility != View.VISIBLE
                logView.visibility = if (show) View.VISIBLE else View.GONE
                toggleButton.visibility = if (show) View.VISIBLE else View.GONE
                text = if (show) "Ocultar diagnósticos" else "Diagnósticos"
            }
        }
        diagnosticsPage.addView(diagnosticsButton)
        toggleButton.visibility = View.GONE
        diagnosticsPage.addView(toggleButton)
        diagnosticsPage.addView(logView)

        root.addView(homePage)
        root.addView(connectionPage)
        root.addView(customizationPage)
        root.addView(diagnosticsPage)
        showPage(AppPage.HOME)

        return ScrollView(this).apply {
            setBackgroundColor(Color.rgb(9, 13, 20))
            addView(root)
        }
    }

    private fun label(value: String, size: Float) = TextView(this).apply {
        text = value
        textSize = size
        setTextColor(Color.WHITE)
        setPadding(0, 8, 0, 8)
    }

    private fun card(padding: Int) = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(padding, padding, padding, padding)
        background = GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = 22 * resources.displayMetrics.density
            setColor(Color.rgb(17, 25, 37))
            setStroke((1 * resources.displayMetrics.density).toInt(),
                Color.rgb(36, 48, 65))
        }
    }

    private fun stylePrimaryButton(button: Button) {
        button.backgroundTintList = ColorStateList.valueOf(Color.rgb(240, 68, 85))
        button.setTextColor(Color.WHITE)
        button.setTypeface(button.typeface, Typeface.BOLD)
        button.isAllCaps = false
    }

    private fun styleSecondaryButton(button: Button) {
        button.backgroundTintList = ColorStateList.valueOf(Color.rgb(35, 47, 64))
        button.setTextColor(Color.rgb(224, 230, 239))
        button.isAllCaps = false
    }

    private fun connectionDot(connected: Boolean) = GradientDrawable().apply {
        shape = GradientDrawable.OVAL
        setColor(if (connected) Color.rgb(57, 211, 137) else Color.rgb(110, 124, 145))
    }

    private fun refreshHomeConnection() {
        if (!::homeConnectionDot.isInitialized) return
        val connected = activeClients.isNotEmpty()
        homeConnectionDot.background = connectionDot(connected)
        homeConnectionDot.contentDescription =
            if (connected) "Switch conectado" else "Switch desconectado"
    }

    private fun showAppMenu() {
        val labels = arrayOf("Início", "Conexão", "Personalizar", "Diagnósticos")
        AlertDialog.Builder(this)
            .setTitle("Switch Auto")
            .setItems(labels) { _, index -> showPage(AppPage.entries[index]) }
            .setNegativeButton("Fechar", null)
            .show()
    }

    private fun showPage(page: AppPage) {
        if (!::homePage.isInitialized) return
        homePage.visibility = if (page == AppPage.HOME) View.VISIBLE else View.GONE
        connectionPage.visibility =
            if (page == AppPage.CONNECTION) View.VISIBLE else View.GONE
        customizationPage.visibility =
            if (page == AppPage.CUSTOMIZATION) View.VISIBLE else View.GONE
        diagnosticsPage.visibility =
            if (page == AppPage.DIAGNOSTICS) View.VISIBLE else View.GONE
    }

    private fun startServer() {
        if (!running.compareAndSet(false, true)) return
        CompanionForegroundService.start(
            this, ::gpsWidget.isInitialized && gpsWidget.isEnabled())
        foregroundLocationActive =
            ::gpsWidget.isInitialized && gpsWidget.isEnabled()
        refreshConnectionDetails()
        statusView.text = "Iniciando..."
        toggleButton.text = "Stop server"

        executor.execute {
            try {
                ServerSocket(SERVER_PORT).use { server ->
                    server.soTimeout = ACCEPT_POLL_TIMEOUT_MS
                    serverSocket = server
                    runOnUiThread {
                        statusView.text = "Pronto para conectar"
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
                    statusView.text = "Servidor pausado"
                    toggleButton.text = "Start server"
                }
            }
        }
        executor.execute { runDiscoveryResponder() }
    }

    private fun stopServer() {
        running.set(false)
        CompanionForegroundService.stop(this)
        foregroundLocationActive = null
        serverSocket?.close()
        discoverySocket?.close()
        activeClients.forEach { client -> runCatching { client.close() } }
        activeClients.clear()
        refreshHomeConnection()
        serverSocket = null
        discoverySocket = null
        if (::statusView.isInitialized) {
            statusView.text = "Servidor pausado"
            toggleButton.text = "Start server"
            appendLog("Server stopped.")
        }
    }

    private fun handleClient(socket: Socket) {
        var sessionId = UUID.randomUUID().toString().substring(0, 12)
        sessionContext.set(sessionId)
        var outcome = "connection_closed"
        activeClients.add(socket)
        runOnUiThread(::refreshHomeConnection)
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
                                runOnUiThread { statusView.text = "Switch pareado" }
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
                            customizationManager.onSwitchRevision(
                                request.optInt("customizationRevision", -1))
                            widgetRegistry.handleMessage(request)
                            val response = message("pong")
                                .put("sequence", request.optInt("sequence"))
                            val binaryPayload =
                                widgetRegistry.contributeTelemetry(response)
                            customizationManager.contribute(response)
                            runOnUiThread {
                                statusView.text = "Switch conectado"
                            }
                            send(writer, response, client.getOutputStream(), binaryPayload)
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
            runOnUiThread(::refreshHomeConnection)
            sessionContext.remove()
        }
    }

    private fun message(type: String) = JSONObject()
        .put("version", PROTOCOL_VERSION)
        .put("type", type)
        .put("timestamp", System.currentTimeMillis())

    private fun send(
        writer: BufferedWriter,
        payload: JSONObject,
        output: java.io.OutputStream? = null,
        binaryPayload: ByteArray? = null
    ) {
        val line = payload.toString()
        writer.write(line)
        writer.newLine()
        writer.flush()
        if (output != null && binaryPayload != null) {
            output.write(binaryPayload)
            output.flush()
        }
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
            pairingStatusView.text = "Switch $deviceId is trusted"
            scanButton.text = "PAIR ANOTHER SWITCH"
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
        addressView.text = "Companion ativo em segundo plano"
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
        pairingStatusView.text = "No Switch paired yet"
        scanButton.text = "SCAN SWITCH QR"
        statusView.text = "Pairing removed — scan the Switch QR to pair again"
        appendLog("All trusted Switch credentials were revoked")
    }

    private fun initializeWidgets() {
        val context = CompanionWidgetContext(
            activity = this,
            permissions = permissionManager,
            log = ::appendLog,
            stateChanged = {
                runOnUiThread {
                    refreshWidgetViews()
                    if (running.get()) {
                        val locationActive =
                            ::gpsWidget.isInitialized && gpsWidget.isEnabled()
                        if (foregroundLocationActive != locationActive) {
                            foregroundLocationActive = locationActive
                            CompanionForegroundService.start(this, locationActive)
                        }
                    }
                }
            })
        widgetRegistry = CompanionWidgetRegistry(context)
        batteryWidget = BatteryWidget()
        gpsWidget = GpsWidget()
        mediaWidget = MediaWidget()
        check(widgetRegistry.register(batteryWidget))
        check(widgetRegistry.register(gpsWidget))
        check(widgetRegistry.register(mediaWidget))
        widgetRegistry.initializeAndActivate()
    }

    private fun refreshWidgetViews() {
        if (!::widgetStatusView.isInitialized || !::widgetRegistry.isInitialized) return
        widgetStatusView.text = widgetRegistry.all().joinToString("\n") {
            "${it.title}: ${it.statusText()}"
        }
        gpsButton.text =
            if (gpsWidget.isEnabled()) "Localização ativa" else "Ativar localização"
        mediaButton.text =
            if (permissionManager.state(AppCapability.MEDIA_CONTROL) ==
                AppPermissionState.GRANTED) {
                "Controle de mídia ativo"
            } else {
                "Permitir controle de mídia"
            }
        if (::routeStatusView.isInitialized) {
            routeStatusView.text = gpsWidget.navigationStatus()
            clearRouteButton.visibility =
                if (gpsWidget.navigationStatus() != "No destination") View.VISIBLE else View.GONE
            wazeButton.visibility =
                if (selectedNavigationDestination != null) View.VISIBLE else View.GONE
            wazeButton.text =
                if (wazeNavigationProvider.isAvailable(this)) {
                    "Iniciar no Waze"
                } else {
                    "Abrir rota pelo Waze"
                }
            streamWazeButton.visibility =
                if (selectedNavigationDestination != null) View.VISIBLE else View.GONE
            streamWazeButton.text =
                if (screenCaptureController?.isActive() == true) {
                    "Parar transmissÃ£o do Waze"
                } else {
                    "Transmitir Waze para o Switch"
                }
        }
    }

    private fun toggleWazeScreenStream() {
        screenCaptureController?.takeIf { it.isActive() }?.let {
            it.stop()
            return
        }
        val manager = getSystemService(MediaProjectionManager::class.java)
        startActivityForResult(
            manager.createScreenCaptureIntent(),
            SCREEN_CAPTURE_REQUEST_CODE
        )
    }

    private fun startWazeNavigation() {
        val destination = selectedNavigationDestination ?: return
        wazeNavigationProvider.startNavigation(this, destination)
            .onSuccess {
                appendLog("Navigation handed off to Waze: ${destination.label}")
            }
            .onFailure { error ->
                appendLog("Waze handoff failed: ${error.message}")
                AlertDialog.Builder(this)
                    .setTitle("NÃ£o foi possÃ­vel abrir o Waze")
                    .setMessage(
                        "A rota interna continuarÃ¡ ativa no Switch.\n\n" +
                            (error.message ?: "Nenhum aplicativo compatÃ­vel foi encontrado.")
                    )
                    .setPositiveButton("OK", null)
                    .show()
            }
    }

    private fun showDestinationDialog() {
        val input = EditText(this).apply {
            hint = "Rua, número, cidade"
            setSingleLine(true)
            setTextColor(Color.WHITE)
            setHintTextColor(Color.rgb(145, 160, 181))
        }
        val container = LinearLayout(this).apply {
            setPadding(48, 8, 48, 0)
            addView(input, ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT)
        }
        val dialog = AlertDialog.Builder(this)
            .setTitle("Escolher destino")
            .setMessage("Digite o endereço completo, incluindo número e cidade.")
            .setView(container)
            .setNegativeButton("Cancelar", null)
            .setPositiveButton("Buscar", null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val query = input.text.toString().trim()
                if (query.length < 3) {
                    input.error = "Enter at least 3 characters"
                    return@setOnClickListener
                }
                val searchButton = dialog.getButton(AlertDialog.BUTTON_POSITIVE)
                searchButton.isEnabled = false
                searchButton.text = "Buscando..."
                executor.execute {
                    runCatching { AddressSearch.search(this, query) }
                        .onSuccess { results ->
                            runOnUiThread {
                                searchButton.isEnabled = true
                                searchButton.text = "Buscar"
                                if (results.isEmpty()) {
                                    input.error = "No address found"
                                    return@runOnUiThread
                                }
                                AlertDialog.Builder(this)
                                    .setTitle("Confirme o destino")
                                    .setItems(results.map { it.label }.toTypedArray()) {
                                            _, selected ->
                                        val result = results[selected]
                                        gpsWidget.setDestination(
                                            result.latitude, result.longitude)
                                        selectedNavigationDestination =
                                            NavigationDestination(
                                                result.latitude,
                                                result.longitude,
                                                result.label
                                            )
                                        appendLog(
                                            "Route destination selected: ${result.label}")
                                        dialog.dismiss()
                                        refreshWidgetViews()
                                        AlertDialog.Builder(this)
                                            .setTitle("Destino pronto")
                                            .setMessage(
                                                "A rota interna foi enviada ao Switch. " +
                                                    "Deseja iniciar a navegaÃ§Ã£o no Waze?"
                                            )
                                            .setNegativeButton("Agora nÃ£o", null)
                                            .setPositiveButton("Abrir Waze") { _, _ ->
                                                startWazeNavigation()
                                            }
                                            .show()
                                    }
                                    .setNegativeButton("Voltar", null)
                                    .show()
                            }
                        }
                        .onFailure { error ->
                            runOnUiThread {
                                searchButton.isEnabled = true
                                searchButton.text = "Buscar"
                                input.error = "Search failed: ${error.message}"
                            }
                        }
                }
            }
        }
        dialog.show()
    }

    private fun refreshCustomizationViews() {
        if (!::customizationStatusView.isInitialized) return
        customizationStatusView.text = customizationManager.statusText()
        themeButton.text = "THEME: ${customizationManager.selectedTheme.name}"
        layoutButton.text = "LAYOUT: ${customizationManager.selectedLayout.name}"
        val canEdit =
            customizationManager.state != CustomizationSyncState.WAITING_FOR_SWITCH
        themeButton.isEnabled = canEdit
        layoutButton.isEnabled = canEdit
        applyCustomizationButton.isEnabled = canEdit
    }

    private fun randomNonce(): String {
        val bytes = ByteArray(16)
        SecureRandom().nextBytes(bytes)
        return bytes.joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }

    private companion object {
        const val SCREEN_CAPTURE_REQUEST_CODE = 4201
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
