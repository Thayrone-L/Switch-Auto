package dev.switchauto.companion

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Handler
import android.os.HandlerThread
import java.io.ByteArrayOutputStream
import java.util.concurrent.atomic.AtomicBoolean

class ScreenCaptureController(
    private val activity: Activity,
    private val onFrame: (ByteArray) -> Unit,
    private val onStopped: () -> Unit,
    private val log: (String) -> Unit
) {
    private val processingFrame = AtomicBoolean(false)
    private var projection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private var handlerThread: HandlerThread? = null
    private var lastFrameAt = 0L

    fun isActive() = projection != null

    fun start(resultCode: Int, data: Intent): Result<Unit> = runCatching {
        stop(notify = false)
        val manager = activity.getSystemService(MediaProjectionManager::class.java)
        val activeProjection = checkNotNull(manager.getMediaProjection(resultCode, data))
        activeProjection.registerCallback(object : MediaProjection.Callback() {
            override fun onStop() {
                stop(notify = true, stopProjection = false)
            }
        }, Handler(activity.mainLooper))

        val thread = HandlerThread("switch-auto-screen-capture").also { it.start() }
        val reader = ImageReader.newInstance(
            FRAME_WIDTH, FRAME_HEIGHT, PixelFormat.RGBA_8888, 2)
        reader.setOnImageAvailableListener({ source ->
            val image = source.acquireLatestImage() ?: return@setOnImageAvailableListener
            try {
                val now = System.currentTimeMillis()
                if (now - lastFrameAt < FRAME_INTERVAL_MS ||
                    !processingFrame.compareAndSet(false, true)) {
                    return@setOnImageAvailableListener
                }
                lastFrameAt = now
                val plane = image.planes[0]
                val rowPixels = plane.rowStride / plane.pixelStride
                val padded = Bitmap.createBitmap(
                    rowPixels, image.height, Bitmap.Config.ARGB_8888)
                padded.copyPixelsFromBuffer(plane.buffer)
                val visible = Bitmap.createBitmap(
                    padded, 0, 0, image.width.coerceAtMost(padded.width), image.height)
                encode(visible)?.let(onFrame)
                if (visible !== padded) visible.recycle()
                padded.recycle()
            } catch (error: Exception) {
                log("Screen capture frame failed: ${error.message}")
            } finally {
                processingFrame.set(false)
                image.close()
            }
        }, Handler(thread.looper))

        projection = activeProjection
        imageReader = reader
        handlerThread = thread
        virtualDisplay = activeProjection.createVirtualDisplay(
            "Switch Auto navigation",
            FRAME_WIDTH,
            FRAME_HEIGHT,
            activity.resources.displayMetrics.densityDpi,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            reader.surface,
            null,
            Handler(thread.looper)
        )
        log("Waze screen stream started at ${1000 / FRAME_INTERVAL_MS} FPS")
    }

    fun stop() = stop(notify = true)

    private fun stop(notify: Boolean, stopProjection: Boolean = true) {
        val wasActive = projection != null
        virtualDisplay?.release()
        virtualDisplay = null
        imageReader?.close()
        imageReader = null
        if (stopProjection) projection?.stop()
        projection = null
        handlerThread?.quitSafely()
        handlerThread = null
        if (notify && wasActive) onStopped()
    }

    private fun encode(bitmap: Bitmap): ByteArray? {
        for (quality in JPEG_QUALITIES) {
            val bytes = ByteArrayOutputStream().use { output ->
                bitmap.compress(Bitmap.CompressFormat.JPEG, quality, output)
                output.toByteArray()
            }
            if (bytes.size <= MAX_FRAME_BYTES) return bytes
        }
        return null
    }

    companion object {
        const val FRAME_WIDTH = 640
        const val FRAME_HEIGHT = 360
        private const val FRAME_INTERVAL_MS = 200L
        private const val MAX_FRAME_BYTES = 512 * 1024
        private val JPEG_QUALITIES = intArrayOf(70, 58, 46, 34)
    }
}
