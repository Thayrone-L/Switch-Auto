package dev.switchauto.companion

import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import org.json.JSONObject
import org.maplibre.android.camera.CameraPosition
import org.maplibre.android.geometry.LatLng
import org.maplibre.android.maps.Style
import org.maplibre.android.snapshotter.MapSnapshotter
import java.io.ByteArrayOutputStream
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

class GpsWidget : BaseCompanionWidget(), LocationListener {
    override val id = "core.gps"
    override val title = "GPS"
    override val requiredCapabilities = setOf(AppCapability.LOCATION)

    private lateinit var locationManager: LocationManager
    private var enabled = false
    @Volatile private var lastLocation: Location? = null
    @Volatile private var latestMapFrame: ByteArray? = null
    private val mapFrameId = AtomicInteger(0)
    private val snapshotInProgress = AtomicBoolean(false)
    private var lastSnapshotTime = 0L
    private var snapshotter: MapSnapshotter? = null

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
            renderMap(location)
            context.stateChanged()
        }
    }

    @Synchronized
    override fun contributeTelemetry(target: JSONObject): ByteArray? {
        if (!enabled) return null
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
        latestMapFrame?.let { frame ->
            target.put("mapFrame", JSONObject()
                .put("id", mapFrameId.get())
                .put("contentType", "image/jpeg")
                .put("length", frame.size)
                .put("width", MAP_WIDTH)
                .put("height", MAP_HEIGHT))
        }
        return latestMapFrame
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
    override fun dispose() {
        stopUpdates()
        snapshotter?.cancel()
        snapshotter = null
        super.dispose()
    }
    override fun onProviderEnabled(provider: String) = startUpdates()
    override fun onProviderDisabled(provider: String) = context.stateChanged()

    private fun preferences() = context.activity.getSharedPreferences(
        "switch_auto_widget_gps", Context.MODE_PRIVATE)

    private fun renderMap(location: Location) {
        val now = System.currentTimeMillis()
        if (now - lastSnapshotTime < MAP_SNAPSHOT_INTERVAL_MS ||
            !snapshotInProgress.compareAndSet(false, true)) return
        lastSnapshotTime = now
        context.activity.runOnUiThread {
            val options = MapSnapshotter.Options(MAP_WIDTH, MAP_HEIGHT)
                .withStyleBuilder(Style.Builder().fromUri(MAP_STYLE_URI))
                .withCameraPosition(CameraPosition.Builder()
                    .target(LatLng(location.latitude, location.longitude))
                    .zoom(MAP_ZOOM)
                    .bearing(if (location.hasBearing()) location.bearing.toDouble() else 0.0)
                    .build())
            snapshotter?.cancel()
            snapshotter = MapSnapshotter(context.activity.applicationContext, options)
            snapshotter?.start({ snapshot ->
                val encoded = encodeSnapshot(snapshot.bitmap)
                if (encoded != null) {
                    synchronized(this@GpsWidget) {
                        latestMapFrame = encoded
                        mapFrameId.incrementAndGet()
                    }
                    context.log(
                        "Widget $id map frame ready: ${encoded.size} bytes (${MAP_WIDTH}x${MAP_HEIGHT})")
                } else {
                    context.log(
                        "Widget $id map frame discarded: could not fit protocol limit")
                }
                snapshotInProgress.set(false)
                context.stateChanged()
            }, { error ->
                snapshotInProgress.set(false)
                context.log("Widget $id map snapshot failed: $error")
            })
        }
    }

    private fun encodeSnapshot(source: Bitmap): ByteArray? {
        val scaled = if (source.width == MAP_WIDTH && source.height == MAP_HEIGHT) {
            source
        } else {
            Bitmap.createScaledBitmap(source, MAP_WIDTH, MAP_HEIGHT, true)
        }
        val mutable = scaled.copy(Bitmap.Config.ARGB_8888, true)
        val canvas = Canvas(mutable)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.rgb(240, 68, 85)
        }
        canvas.drawCircle(mutable.width / 2f, mutable.height / 2f, 13f, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = 5f
        paint.color = Color.WHITE
        canvas.drawCircle(mutable.width / 2f, mutable.height / 2f, 13f, paint)
        for (quality in MAP_JPEG_QUALITIES) {
            val encoded = ByteArrayOutputStream().use { output ->
                mutable.compress(Bitmap.CompressFormat.JPEG, quality, output)
                output.toByteArray()
            }
            if (encoded.size <= MAX_MAP_FRAME_BYTES) return encoded
        }
        return null
    }

    companion object {
        private const val MAP_WIDTH = 640
        private const val MAP_HEIGHT = 360
        private const val MAP_ZOOM = 16.0
        private val MAP_JPEG_QUALITIES = intArrayOf(72, 60, 48, 36)
        private const val MAX_MAP_FRAME_BYTES = 512 * 1024
        private const val MAP_SNAPSHOT_INTERVAL_MS = 3_000L
        private const val MAP_STYLE_URI = "https://demotiles.maplibre.org/style.json"
    }
}
