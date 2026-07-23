package dev.switchauto.companion

import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import org.json.JSONObject

class GpsWidget : BaseCompanionWidget(), LocationListener {
    override val id = "core.gps"
    override val title = "GPS"
    override val requiredCapabilities = setOf(AppCapability.LOCATION)

    private lateinit var locationManager: LocationManager
    private var enabled = false
    @Volatile private var lastLocation: Location? = null

    override fun initialize(context: CompanionWidgetContext) {
        super.initialize(context)
        locationManager = context.activity.getSystemService(
            Context.LOCATION_SERVICE) as LocationManager
        enabled = preferences().getBoolean("enabled", false)
    }

    override fun activate() {
        super.activate()
        if (enabled && context.permissions.state(AppCapability.LOCATION) ==
            AppPermissionState.GRANTED) startUpdates()
    }

    fun isEnabled() = enabled

    fun setEnabled(value: Boolean) {
        if (!value) {
            enabled = false
            preferences().edit().putBoolean("enabled", false).apply()
            stopUpdates()
            context.log("Widget $id disabled")
            context.stateChanged()
            return
        }
        context.permissions.request(AppCapability.LOCATION) { result ->
            if (result == AppPermissionState.GRANTED) {
                enabled = true
                preferences().edit().putBoolean("enabled", true).apply()
                startUpdates()
                context.log("Widget $id enabled")
            } else {
                enabled = false
                context.log("Widget $id needs location permission: ${result.name.lowercase()}")
            }
            context.stateChanged()
        }
    }

    @SuppressLint("MissingPermission")
    private fun startUpdates() {
        if (!enabled || state != CompanionWidgetState.ACTIVE) return
        listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER)
            .filter(locationManager::isProviderEnabled)
            .forEach { provider ->
                locationManager.requestLocationUpdates(provider, 1_000L, 1f, this)
                locationManager.getLastKnownLocation(provider)?.let(::acceptLocation)
            }
        context.stateChanged()
    }

    private fun stopUpdates() = locationManager.removeUpdates(this)

    override fun onLocationChanged(location: Location) = acceptLocation(location)

    private fun acceptLocation(location: Location) {
        val current = lastLocation
        if (current == null || location.time >= current.time || location.accuracy < current.accuracy) {
            lastLocation = location
            context.stateChanged()
        }
    }

    override fun contributeTelemetry(target: JSONObject) {
        if (!enabled) return
        lastLocation?.let { location ->
            target.put("location", JSONObject()
                .put("latitude", location.latitude)
                .put("longitude", location.longitude)
                .put("accuracyMeters", location.accuracy.toDouble())
                .put("speedMetersPerSecond",
                    if (location.hasSpeed()) location.speed.toDouble() else JSONObject.NULL)
                .put("bearingDegrees",
                    if (location.hasBearing()) location.bearing.toDouble() else JSONObject.NULL)
                .put("timestamp", location.time))
        }
    }

    override fun statusText(): String {
        if (!enabled) return "Disabled"
        val permission = context.permissions.state(AppCapability.LOCATION)
        if (permission != AppPermissionState.GRANTED) return "Permission: ${permission.name.lowercase()}"
        val location = lastLocation ?: return "Waiting for location"
        return "%.5f, %.5f · ±%.0f m".format(
            location.latitude, location.longitude, location.accuracy)
    }

    override fun suspend() { stopUpdates(); super.suspend() }
    override fun resume() { super.resume(); if (enabled) startUpdates() }
    override fun dispose() { stopUpdates(); super.dispose() }
    override fun onProviderEnabled(provider: String) = startUpdates()
    override fun onProviderDisabled(provider: String) = context.stateChanged()

    private fun preferences() = context.activity.getSharedPreferences(
        "switch_auto_widget_gps", Context.MODE_PRIVATE)
}
