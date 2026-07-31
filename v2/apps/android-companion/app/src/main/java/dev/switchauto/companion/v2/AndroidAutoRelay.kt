package dev.switchauto.companion.v2

import android.content.Context
import java.net.BindException
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

class AndroidAutoRelay(private val context: Context) {
    private val running = AtomicBoolean(false)
    private val nextSession = AtomicLong(1)
    private val executor = Executors.newCachedThreadPool()
    @Volatile private var server: ServerSocket? = null

    fun start() {
        if (!running.compareAndSet(false, true)) return
        RelayRuntime.update { it.copy(state = RelayState.STARTING, detail = "Verificando Android Auto") }
        executor.execute(::serve)
    }

    fun stop() {
        running.set(false)
        runCatching { server?.close() }
        executor.shutdownNow()
        RelayRuntime.update { it.copy(state = RelayState.STOPPED, detail = "Serviço parado") }
    }

    private fun serve() {
        if (!androidAutoServerReady()) {
            RelayRuntime.update {
                it.copy(state = RelayState.WAITING_FOR_ANDROID_AUTO,
                    detail = "Ative o servidor da unidade principal no Android Auto")
            }
        }
        try {
            ServerSocket().use { listening ->
                listening.reuseAddress = true
                listening.bind(InetSocketAddress(RELAY_PORT))
                listening.soTimeout = ACCEPT_TIMEOUT_MS
                server = listening
                log("relay listening port=$RELAY_PORT trusted=$TRUSTED_SWITCH_IP")
                RelayRuntime.update { it.copy(state = RelayState.LISTENING, detail = "Aguardando o Switch") }
                while (running.get()) {
                    try {
                        val client = listening.accept()
                        executor.execute { relay(client) }
                    } catch (_: SocketTimeoutException) {
                        // Check the lifecycle flag without blocking service shutdown.
                    }
                }
            }
        } catch (error: Exception) {
            if (running.get()) {
                log("relay server failed ${error.javaClass.simpleName}: ${error.message}")
                RelayRuntime.update { it.copy(state = RelayState.ERROR, detail = "Falha no relay: ${error.message}") }
            }
        } finally {
            server = null
            running.set(false)
        }
    }

    private fun androidAutoServerReady(): Boolean = try {
        ServerSocket().use { probe ->
            probe.bind(InetSocketAddress(LOOPBACK_ADDRESS, HEAD_UNIT_SERVER_PORT))
        }
        false
    } catch (_: BindException) {
        true
    } catch (_: Exception) {
        false
    }

    private fun relay(client: Socket) {
        val id = nextSession.getAndIncrement()
        client.use { external ->
            val remote = external.inetAddress.hostAddress ?: "unknown"
            if (remote != TRUSTED_SWITCH_IP) {
                log("relay[$id] rejected peer=$remote")
                return
            }
            log("relay[$id] accepted peer=$remote")
            try {
                configure(external)
                Socket().use { androidAuto ->
                    configure(androidAuto)
                    androidAuto.connect(InetSocketAddress(LOOPBACK_ADDRESS, HEAD_UNIT_SERVER_PORT), CONNECT_TIMEOUT_MS)
                    RelayRuntime.update {
                        it.copy(state = RelayState.PROJECTING, detail = "Android Auto conectado", sessions = it.sessions + 1)
                    }
                    log("relay[$id] connected to Android Auto")
                    val upstream = executor.submit {
                        pump(id, "switch->android", external, androidAuto, false)
                        runCatching { androidAuto.shutdownOutput() }
                    }
                    pump(id, "android->switch", androidAuto, external, true)
                    runCatching { external.shutdownOutput() }
                    awaitPump(upstream)
                }
            } catch (error: Exception) {
                log("relay[$id] failed ${error.javaClass.simpleName}: ${error.message}")
            } finally {
                if (running.get()) {
                    RelayRuntime.update { it.copy(state = RelayState.LISTENING, detail = "Reconectando ao Switch") }
                }
                log("relay[$id] closed")
            }
        }
    }

    private fun pump(id: Long, direction: String, source: Socket, target: Socket, fromAndroid: Boolean) {
        var total = 0L
        var published = 0L
        var nextMetricsAt = System.nanoTime() + METRICS_INTERVAL_NS
        var windowStartedAt = System.nanoTime()
        var lastReadAt = 0L
        var windowBytes = 0L
        var windowReads = 0L
        var totalReadGapNs = 0L
        var maxReadGapNs = 0L
        var totalWriteNs = 0L
        var maxWriteNs = 0L
        var completion = "stopped"
        val buffer = ByteArray(RELAY_BUFFER_BYTES)
        try {
            val input = source.getInputStream()
            val output = target.getOutputStream()
            while (running.get()) {
                val count = input.read(buffer)
                if (count < 0) {
                    completion = "eof"
                    break
                }
                if (count == 0) continue
                val readAt = System.nanoTime()
                if (lastReadAt != 0L) {
                    val gap = readAt - lastReadAt
                    totalReadGapNs += gap
                    maxReadGapNs = maxOf(maxReadGapNs, gap)
                }
                lastReadAt = readAt
                val writeStartedAt = System.nanoTime()
                output.write(buffer, 0, count)
                val writeNs = System.nanoTime() - writeStartedAt
                totalWriteNs += writeNs
                maxWriteNs = maxOf(maxWriteNs, writeNs)
                total += count
                windowBytes += count
                windowReads++
                val now = System.nanoTime()
                if (now >= nextMetricsAt) {
                    publishBytes(fromAndroid, total - published)
                    published = total
                    val elapsedSeconds = (now - windowStartedAt) / 1_000_000_000.0
                    val gaps = maxOf(0L, windowReads - 1L)
                    val averageGapMs = if (gaps > 0) totalReadGapNs / gaps / 1_000_000.0 else 0.0
                    val averageWriteMs = if (windowReads > 0) totalWriteNs / windowReads / 1_000_000.0 else 0.0
                    log("relay[$id] metric direction=$direction reads=$windowReads " +
                        "bytes=$windowBytes mbps=${"%.2f".format(windowBytes * 8.0 / elapsedSeconds / 1_000_000.0)} " +
                        "read_gap_avg_ms=${"%.2f".format(averageGapMs)} " +
                        "read_gap_max_ms=${"%.2f".format(maxReadGapNs / 1_000_000.0)} " +
                        "write_avg_ms=${"%.3f".format(averageWriteMs)} " +
                        "write_max_ms=${"%.3f".format(maxWriteNs / 1_000_000.0)}")
                    windowStartedAt = now
                    windowBytes = 0
                    windowReads = 0
                    totalReadGapNs = 0
                    maxReadGapNs = 0
                    totalWriteNs = 0
                    maxWriteNs = 0
                    nextMetricsAt = now + METRICS_INTERVAL_NS
                }
            }
        } catch (error: Exception) {
            completion = "error:${error.javaClass.simpleName}:${error.message}"
        } finally {
            publishBytes(fromAndroid, total - published)
            log("relay[$id] $direction completion=$completion bytes=$total")
        }
    }

    private fun publishBytes(fromAndroid: Boolean, count: Long) {
        if (count <= 0) return
        RelayRuntime.update {
            if (fromAndroid) it.copy(bytesFromAndroid = it.bytesFromAndroid + count)
            else it.copy(bytesFromSwitch = it.bytesFromSwitch + count)
        }
    }

    private fun awaitPump(future: Future<*>) {
        runCatching { future.get() }
    }

    private fun configure(socket: Socket) {
        socket.tcpNoDelay = true
        socket.keepAlive = true
        socket.receiveBufferSize = SOCKET_BUFFER_BYTES
        socket.sendBufferSize = SOCKET_BUFFER_BYTES
    }

    private fun log(message: String) = V2Log.write(context, message)

    companion object {
        const val RELAY_PORT = 5288
        const val TRUSTED_SWITCH_IP = "192.168.15.7"
        private const val HEAD_UNIT_SERVER_PORT = 5277
        private const val LOOPBACK_ADDRESS = "127.0.0.1"
        private const val CONNECT_TIMEOUT_MS = 1_000
        private const val ACCEPT_TIMEOUT_MS = 500
        private const val RELAY_BUFFER_BYTES = 64 * 1024
        private const val SOCKET_BUFFER_BYTES = 1024 * 1024
        private const val METRICS_INTERVAL_NS = 2_000_000_000L
    }
}
