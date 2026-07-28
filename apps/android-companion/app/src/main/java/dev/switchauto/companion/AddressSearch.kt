package dev.switchauto.companion

import android.content.Context
import android.location.Geocoder
import org.json.JSONArray
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap

data class AddressResult(
    val label: String,
    val latitude: Double,
    val longitude: Double,
)

object AddressSearch {
    private val cache = ConcurrentHashMap<String, List<AddressResult>>()
    private var lastRequestAt = 0L

    fun search(context: Context, query: String): List<AddressResult> {
        val normalized = query.trim().lowercase()
        cache[normalized]?.let { return it }
        val androidResults = searchWithAndroid(context, query)
        synchronized(this) {
            val delay = 1_100L - (System.currentTimeMillis() - lastRequestAt)
            if (delay > 0) Thread.sleep(delay)
            lastRequestAt = System.currentTimeMillis()
        }
        val encoded = URLEncoder.encode(query.trim(), Charsets.UTF_8.name())
        val connection = URL(
            "https://nominatim.openstreetmap.org/search" +
                "?q=$encoded&format=jsonv2&limit=5&addressdetails=1")
            .openConnection() as HttpURLConnection
        connection.connectTimeout = 8_000
        connection.readTimeout = 12_000
        connection.setRequestProperty("User-Agent", "SwitchAuto/0.1 (Android Companion)")
        connection.setRequestProperty("Accept-Language", "pt-BR,pt;q=0.9,en;q=0.7")
        try {
            if (connection.responseCode !in 200..299) {
                throw IllegalStateException("Address search HTTP ${connection.responseCode}")
            }
            val array = connection.inputStream.bufferedReader().use { it.readText() }
                .let(::JSONArray)
            val osmResults = buildList {
                for (index in 0 until array.length()) {
                    val item = array.getJSONObject(index)
                    val latitude = item.optString("lat").toDoubleOrNull() ?: continue
                    val longitude = item.optString("lon").toDoubleOrNull() ?: continue
                    add(AddressResult(
                        item.optString("display_name", query),
                        latitude,
                        longitude))
                }
            }
            val houseNumber = Regex("""\b\d+[A-Za-z]?\b""")
                .find(query)?.value?.lowercase()
            val results = (androidResults + osmResults)
                .distinctBy { "${it.latitude},${it.longitude}" }
                .sortedByDescending { result ->
                    houseNumber != null && result.label.lowercase().contains(houseNumber)
                }
                .take(5)
            cache[normalized] = results
            return results
        } finally {
            connection.disconnect()
        }
    }

    @Suppress("DEPRECATION")
    private fun searchWithAndroid(
        context: Context,
        query: String
    ): List<AddressResult> {
        if (!Geocoder.isPresent()) return emptyList()
        return runCatching {
            Geocoder(context.applicationContext, Locale("pt", "BR"))
                .getFromLocationName(query.trim(), 5)
                .orEmpty()
                .map { address ->
                    val label = address.getAddressLine(0)
                        ?: listOfNotNull(
                            address.thoroughfare,
                            address.subThoroughfare,
                            address.locality,
                            address.adminArea)
                            .joinToString(", ")
                    AddressResult(label, address.latitude, address.longitude)
                }
                .distinctBy { "${it.latitude},${it.longitude}" }
        }.getOrDefault(emptyList())
    }
}
