package dev.switchauto.companion.v2

import android.content.Context
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object V2Log {
    private const val TAG = "SwitchAutoV2"
    private val lock = Any()
    private val timestamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)

    fun write(context: Context, message: String) {
        Log.i(TAG, message)
        synchronized(lock) {
            runCatching {
                val file = File(context.getExternalFilesDir(null), "switch-auto-v2.log")
                file.parentFile?.mkdirs()
                file.appendText("${timestamp.format(Date())} $message\n")
                if (file.length() > 2L * 1024L * 1024L) {
                    val previous = File(file.parentFile, "switch-auto-v2.previous.log")
                    previous.delete()
                    file.renameTo(previous)
                }
            }
        }
    }
}
