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
import kotlin.math.cos
import kotlin.math.pow
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
    private var lastSentMapFrameId = 0
    private val snapshotInProgress = AtomicBoolean(false)
    private var lastSnapshotTime = 0L
    private var snapshotter: MapSnapshotter? = null
    @Volatile private var cameraOffsetLatitude = 0.0
    @Volatile private var cameraOffsetLongitude = 0.0
    @Volatile private var explorationExpiresAt = 0L

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
                locationManager.requestLocationUpdates(
                    provider, LOCATION_UPDATE_INTERVAL_MS, 0f, this)
                locationManager.getLastKnownLocation(provider)?.let(::acceptLocation)
            }
        context.stateChanged()
    }

    private fun stopUpdates() = locationManager.removeUpdates(this)

    override fun onLocationChanged(location: Location) = acceptLocation(location)

    override fun onMessage(message: JSONObject) {
        if (message.optString("type") != "ping") return
        val viewport = message.optJSONObject("mapViewport") ?: return
        val location = lastLocation ?: return
        val deltaX = viewport.optDouble("deltaX", 0.0)
        val deltaY = viewport.optDouble("deltaY", 0.0)
        if (!deltaX.isFinite() || !deltaY.isFinite()) return
        val metersPerPixel = 156543.03392 *
            cos(Math.toRadians(location.latitude)) / 2.0.pow(MAP_ZOOM)
        cameraOffsetLatitude += deltaY * metersPerPixel / 111320.0
        val longitudeScale =
            (111320.0 * cos(Math.toRadians(location.latitude))).coerceAtLeast(1.0)
        cameraOffsetLongitude -= deltaX * metersPerPixel / longitudeScale
        explorationExpiresAt = System.currentTimeMillis() + EXPLORATION_TIMEOUT_MS
        renderMap(location, force = true)
    }

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
        val frameId = mapFrameId.get()
        latestMapFrame?.takeIf { frameId != lastSentMapFrameId }?.let { frame ->
            target.put("mapFrame", JSONObject()
                .put("id", frameId)
                .put("contentType", "image/jpeg")
                .put("length", frame.size)
                .put("width", MAP_WIDTH)
                .put("height", MAP_HEIGHT))
            lastSentMapFrameId = frameId
            return frame
        }
        return null
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

    private fun renderMap(location: Location, force: Boolean = false) {
        val now = System.currentTimeMillis()
        if ((!force && now - lastSnapshotTime < MAP_SNAPSHOT_INTERVAL_MS) ||
            !snapshotInProgress.compareAndSet(false, true)) return
        if (explorationExpiresAt > 0 && now >= explorationExpiresAt) {
            cameraOffsetLatitude = 0.0
            cameraOffsetLongitude = 0.0
            explorationExpiresAt = 0L
        }
        lastSnapshotTime = now
        context.activity.runOnUiThread {
            val camera = CameraPosition.Builder()
                .target(LatLng(
                    (location.latitude + cameraOffsetLatitude).coerceIn(-85.0, 85.0),
                    location.longitude + cameraOffsetLongitude))
                .zoom(MAP_ZOOM)
                .bearing(if (location.hasBearing()) location.bearing.toDouble() else 0.0)
                .build()
            val options = MapSnapshotter.Options(MAP_WIDTH, MAP_HEIGHT)
                .withPixelRatio(1f)
                .withStyleBuilder(Style.Builder().fromUri(MAP_STYLE_URI))
                .withCameraPosition(camera)
            val activeSnapshotter = snapshotter
                ?: MapSnapshotter(context.activity.applicationContext, options).also {
                    snapshotter = it
                }
            activeSnapshotter.setCameraPosition(camera)
            activeSnapshotter.start({ snapshot ->
                val encoded = encodeSnapshot(snapshot.bitmap)
                if (encoded != null) {
                    val frameId = synchronized(this@GpsWidget) {
                        latestMapFrame = encoded
                        mapFrameId.incrementAndGet()
                    }
                    if (frameId % MAP_LOG_EVERY_FRAMES == 1) {
                        context.log(
                            "Widget $id map stream: frame=$frameId size=${encoded.size} bytes")
                    }
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
        paint.style = Paint.Style.FILL
        paint.textSize = 11f
        paint.color = Color.argb(190, 0, 0, 0)
        canvas.drawRect(
            0f, mutable.height - 19f, mutable.width.toFloat(), mutable.height.toFloat(), paint)
        paint.color = Color.WHITE
        canvas.drawText(
            MAP_ATTRIBUTION, 7f, mutable.height - 6f, paint)
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
        private const val MAP_SNAPSHOT_INTERVAL_MS = 200L
        private const val LOCATION_UPDATE_INTERVAL_MS = 200L
        private const val EXPLORATION_TIMEOUT_MS = 8_000L
        private const val MAP_LOG_EVERY_FRAMES = 25
        private const val MAP_STYLE_URI =
            "https://tiles.openfreemap.org/styles/liberty"
        private const val MAP_ATTRIBUTION =
            "OpenFreeMap (c) OpenMapTiles | Data (c) OpenStreetMap contributors"
    }
}
