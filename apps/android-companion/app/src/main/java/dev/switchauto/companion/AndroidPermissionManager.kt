package dev.switchauto.companion

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.content.ComponentName
import android.os.Build
import android.provider.Settings
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

enum class AppCapability {
    NETWORK,
    LOCATION,
    MOTION,
    MEDIA_CONTROL,
    PHONE_STATE,
    ANSWER_CALLS,
    CALL_LOG,
    NOTIFICATIONS,
    BACKGROUND_EXECUTION,
    STORAGE,
}

enum class AppPermissionState {
    UNKNOWN,
    NOT_REQUIRED,
    GRANTED,
    DENIED,
    PERMANENTLY_DENIED,
    REQUIRES_SETTINGS,
    UNAVAILABLE,
}

class AndroidPermissionManager(private val activity: Activity) {
    private val nextRequestCode = AtomicInteger(4_100)
    private val pending =
        ConcurrentHashMap<Int, Pair<AppCapability, (AppPermissionState) -> Unit>>()
    private val history = activity.getSharedPreferences(
        "switch_auto_permission_history", Activity.MODE_PRIVATE)

    fun state(capability: AppCapability): AppPermissionState {
        if (capability == AppCapability.MEDIA_CONTROL) {
            val enabled = Settings.Secure.getString(
                activity.contentResolver, "enabled_notification_listeners")
                .orEmpty().split(':').any {
                    ComponentName.unflattenFromString(it)?.packageName ==
                        activity.packageName
                }
            return if (enabled) AppPermissionState.GRANTED
            else AppPermissionState.REQUIRES_SETTINGS
        }
        val permissions = runtimePermissions(capability)
            ?: return AppPermissionState.UNAVAILABLE
        if (permissions.isEmpty()) return AppPermissionState.NOT_REQUIRED
        if (!permissions.all(::isDeclared)) return AppPermissionState.UNAVAILABLE
        if (permissions.all {
                activity.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
            }) return AppPermissionState.GRANTED
        val requestedBefore = history.getBoolean(capability.name, false)
        val canExplain = permissions.any(activity::shouldShowRequestPermissionRationale)
        return if (requestedBefore && !canExplain) {
            AppPermissionState.PERMANENTLY_DENIED
        } else {
            AppPermissionState.DENIED
        }
    }

    fun request(
        capability: AppCapability,
        callback: (AppPermissionState) -> Unit
    ) {
        val current = state(capability)
        if (current != AppPermissionState.DENIED &&
            current != AppPermissionState.UNKNOWN) {
            callback(current)
            return
        }
        val permissions = runtimePermissions(capability)
        if (permissions == null || permissions.isEmpty()) {
            callback(state(capability))
            return
        }
        val requestCode = nextRequestCode.getAndIncrement()
        pending[requestCode] = capability to callback
        history.edit().putBoolean(capability.name, true).apply()
        activity.requestPermissions(permissions.toTypedArray(), requestCode)
    }

    fun onRequestPermissionsResult(requestCode: Int) {
        val request = pending.remove(requestCode) ?: return
        request.second(state(request.first))
    }

    fun summary(): Map<AppCapability, AppPermissionState> =
        AppCapability.entries.associateWith(::state)

    private fun runtimePermissions(capability: AppCapability): List<String>? =
        when (capability) {
            AppCapability.NETWORK,
            AppCapability.MOTION,
            AppCapability.STORAGE -> emptyList()
            AppCapability.LOCATION -> listOf(
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.ACCESS_FINE_LOCATION)
            AppCapability.PHONE_STATE ->
                listOf(Manifest.permission.READ_PHONE_STATE)
            AppCapability.ANSWER_CALLS ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    listOf(Manifest.permission.ANSWER_PHONE_CALLS)
                } else null
            AppCapability.CALL_LOG -> listOf(Manifest.permission.READ_CALL_LOG)
            AppCapability.NOTIFICATIONS,
            AppCapability.BACKGROUND_EXECUTION ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    listOf(Manifest.permission.POST_NOTIFICATIONS)
                } else emptyList()
            // Notification-listener access is represented as REQUIRES_SETTINGS.
            AppCapability.MEDIA_CONTROL -> null
        }

    private fun isDeclared(permission: String): Boolean {
        val info = activity.packageManager.getPackageInfo(
            activity.packageName, PackageManager.GET_PERMISSIONS)
        return info.requestedPermissions?.contains(permission) == true
    }
}
