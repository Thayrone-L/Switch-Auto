package dev.switchauto.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder

class CompanionForegroundService : Service() {
    override fun onCreate() {
        super.onCreate()
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(
            CHANNEL_ID,
            "Switch Auto connection",
            NotificationManager.IMPORTANCE_LOW))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val locationActive = intent?.getBooleanExtra(EXTRA_LOCATION_ACTIVE, false) == true
        val notification = buildNotification(locationActive)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val type = if (locationActive) {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            } else {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            }
            startForeground(NOTIFICATION_ID, notification, type)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun buildNotification(locationActive: Boolean): Notification {
        val openApp = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_upload)
            .setContentTitle("Switch Auto Companion")
            .setContentText(if (locationActive) {
                "Connected services and location are active"
            } else {
                "Waiting for the Nintendo Switch"
            })
            .setContentIntent(openApp)
            .setOngoing(true)
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "switch_auto_runtime"
        private const val NOTIFICATION_ID = 1001
        private const val EXTRA_LOCATION_ACTIVE = "locationActive"

        fun start(context: Context, locationActive: Boolean) {
            val intent = Intent(context, CompanionForegroundService::class.java)
                .putExtra(EXTRA_LOCATION_ACTIVE, locationActive)
            context.startForegroundService(intent)
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, CompanionForegroundService::class.java))
        }
    }
}
