package dev.switchauto.companion.v2

import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

enum class RelayState { STARTING, WAITING_FOR_ANDROID_AUTO, LISTENING, PROJECTING, ERROR, STOPPED }

data class RelaySnapshot(
    val state: RelayState = RelayState.STOPPED,
    val detail: String = "Serviço parado",
    val sessions: Long = 0,
    val bytesFromAndroid: Long = 0,
    val bytesFromSwitch: Long = 0
)

object RelayRuntime {
    private val snapshot = AtomicReference(RelaySnapshot())
    private val revision = AtomicLong(0)

    fun snapshot(): RelaySnapshot = snapshot.get()
    fun revision(): Long = revision.get()

    fun update(transform: (RelaySnapshot) -> RelaySnapshot) {
        snapshot.updateAndGet(transform)
        revision.incrementAndGet()
    }
}
