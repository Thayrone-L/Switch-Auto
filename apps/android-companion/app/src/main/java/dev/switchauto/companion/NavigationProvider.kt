package dev.switchauto.companion

import android.app.Activity
import android.content.Intent
import android.net.Uri

data class NavigationDestination(
    val latitude: Double,
    val longitude: Double,
    val label: String
)

interface NavigationProvider {
    val id: String
    val displayName: String

    fun isAvailable(activity: Activity): Boolean
    fun startNavigation(
        activity: Activity,
        destination: NavigationDestination
    ): Result<Unit>
}

class WazeNavigationProvider : NavigationProvider {
    override val id = "waze"
    override val displayName = "Waze"

    override fun isAvailable(activity: Activity): Boolean =
        runCatching {
            activity.packageManager.getApplicationInfo(WAZE_PACKAGE, 0)
        }.isSuccess

    override fun startNavigation(
        activity: Activity,
        destination: NavigationDestination
    ): Result<Unit> = runCatching {
        val uri = Uri.Builder()
            .scheme("https")
            .authority("waze.com")
            .path("ul")
            .appendQueryParameter(
                "ll",
                "${destination.latitude},${destination.longitude}"
            )
            .appendQueryParameter("navigate", "yes")
            .appendQueryParameter("utm_source", activity.packageName)
            .build()
        val intent = Intent(Intent.ACTION_VIEW, uri)
        if (isAvailable(activity)) {
            intent.setPackage(WAZE_PACKAGE)
        }
        activity.startActivity(intent)
    }

    private companion object {
        const val WAZE_PACKAGE = "com.waze"
    }
}
