package dev.switchauto.companion

import org.json.JSONObject
import org.maplibre.android.geometry.LatLng
import java.net.HttpURLConnection
import java.net.URL

data class NavigationRoute(
    val points: List<LatLng>,
    val distanceMeters: Double,
    val durationSeconds: Double,
)

object RoutePlanner {
    fun calculate(origin: LatLng, destination: LatLng): NavigationRoute {
        val coordinates =
            "${origin.longitude},${origin.latitude};" +
                "${destination.longitude},${destination.latitude}"
        val endpoint = URL(
            "https://router.project-osrm.org/route/v1/driving/$coordinates" +
                "?overview=full&geometries=geojson&steps=false")
        val connection = endpoint.openConnection() as HttpURLConnection
        connection.connectTimeout = 8_000
        connection.readTimeout = 12_000
        connection.setRequestProperty("User-Agent", "SwitchAuto/0.1")
        try {
            if (connection.responseCode !in 200..299) {
                throw IllegalStateException("OSRM HTTP ${connection.responseCode}")
            }
            val root = connection.inputStream.bufferedReader().use { it.readText() }
                .let(::JSONObject)
            if (root.optString("code") != "Ok") {
                throw IllegalStateException(root.optString("message", "Route unavailable"))
            }
            val route = root.getJSONArray("routes").getJSONObject(0)
            val coordinatesJson = route.getJSONObject("geometry")
                .getJSONArray("coordinates")
            val points = buildList {
                for (index in 0 until coordinatesJson.length()) {
                    val point = coordinatesJson.getJSONArray(index)
                    add(LatLng(point.getDouble(1), point.getDouble(0)))
                }
            }
            require(points.size >= 2) { "Route contains too few points" }
            return NavigationRoute(
                points,
                route.getDouble("distance"),
                route.getDouble("duration"))
        } finally {
            connection.disconnect()
        }
    }
}
