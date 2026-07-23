package dev.switchauto.companion

import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import org.json.JSONObject

class BatteryWidget : BaseCompanionWidget() {
    override val id = "core.battery"
    override val title = "Battery"
    override val requiredCapabilities = emptySet<AppCapability>()

    @Volatile var switchBatteryPercent: Int? = null
        private set

    val phoneBatteryPercent: Int
        get() {
            val manager = context.activity.getSystemService(
                android.content.Context.BATTERY_SERVICE) as BatteryManager
            val capacity = manager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
            if (capacity in 0..100) return capacity
            val battery = context.activity.registerReceiver(
                null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val level = battery?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
            val scale = battery?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
            return if (level >= 0 && scale > 0) level * 100 / scale else 0
        }

    override fun onMessage(message: JSONObject) {
        if (message.optString("type") != "ping") return
        val value = message.optInt("switchBatteryPercent", -1)
        if (value in 0..100) {
            switchBatteryPercent = value
            context.stateChanged()
        }
    }

    override fun contributeTelemetry(target: JSONObject) {
        target.put("phoneBatteryPercent", phoneBatteryPercent)
    }

    override fun statusText(): String =
        "Phone $phoneBatteryPercent% · Switch ${switchBatteryPercent?.let { "$it%" } ?: "--"}"
}
