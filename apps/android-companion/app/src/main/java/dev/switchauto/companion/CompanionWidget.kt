package dev.switchauto.companion

import android.app.Activity
import org.json.JSONObject
import java.util.concurrent.CopyOnWriteArrayList

enum class CompanionWidgetState { REGISTERED, INITIALIZED, ACTIVE, SUSPENDED, ERROR, DISPOSED }

data class CompanionWidgetContext(
    val activity: Activity,
    val permissions: AndroidPermissionManager,
    val log: (String) -> Unit,
    val stateChanged: () -> Unit,
)

interface CompanionWidget {
    val id: String
    val title: String
    val requiredCapabilities: Set<AppCapability>
    var state: CompanionWidgetState
    fun initialize(context: CompanionWidgetContext)
    fun activate()
    fun onMessage(message: JSONObject)
    fun contributeTelemetry(target: JSONObject): ByteArray?
    fun suspend()
    fun resume()
    fun dispose()
    fun statusText(): String
}

abstract class BaseCompanionWidget : CompanionWidget {
    protected lateinit var context: CompanionWidgetContext
    final override var state = CompanionWidgetState.REGISTERED

    override fun initialize(context: CompanionWidgetContext) {
        this.context = context
        state = CompanionWidgetState.INITIALIZED
    }
    override fun activate() { state = CompanionWidgetState.ACTIVE }
    override fun onMessage(message: JSONObject) = Unit
    override fun contributeTelemetry(target: JSONObject): ByteArray? = null
    override fun suspend() { state = CompanionWidgetState.SUSPENDED }
    override fun resume() { state = CompanionWidgetState.ACTIVE }
    override fun dispose() { state = CompanionWidgetState.DISPOSED }
}

class CompanionWidgetRegistry(private val context: CompanionWidgetContext) {
    private val widgets = CopyOnWriteArrayList<CompanionWidget>()

    fun register(widget: CompanionWidget): Boolean {
        if (widgets.any { it.id == widget.id }) return false
        widgets += widget
        return true
    }

    fun initializeAndActivate() {
        widgets.forEach { widget ->
            runCatching {
                widget.initialize(context)
            }.onFailure { error ->
                widget.state = CompanionWidgetState.ERROR
                context.log("Widget ${widget.id} initialization failed: ${error.message}")
            }
        }
        widgets.filter { it.state == CompanionWidgetState.INITIALIZED }.forEach { widget ->
            runCatching {
                widget.activate()
                context.log("Widget ${widget.id} active")
            }.onFailure { error ->
                widget.state = CompanionWidgetState.ERROR
                context.log("Widget ${widget.id} activation failed: ${error.message}")
            }
        }
        context.stateChanged()
    }

    fun handleMessage(message: JSONObject) {
        activeWidgets().forEach { widget ->
            runCatching { widget.onMessage(message) }
                .onFailure { context.log("Widget ${widget.id} message error: ${it.message}") }
        }
    }

    fun contributeTelemetry(target: JSONObject): ByteArray? {
        var binaryPayload: ByteArray? = null
        activeWidgets().forEach { widget ->
            runCatching { widget.contributeTelemetry(target) }
                .onSuccess { contribution ->
                    if (binaryPayload == null) binaryPayload = contribution
                }
                .onFailure { context.log("Widget ${widget.id} telemetry error: ${it.message}") }
        }
        return binaryPayload
    }

    fun all(): List<CompanionWidget> = widgets.toList()

    fun dispose() {
        widgets.asReversed().forEach { runCatching { it.dispose() } }
    }

    private fun activeWidgets() = widgets.filter { it.state == CompanionWidgetState.ACTIVE }
}
