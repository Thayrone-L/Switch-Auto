package dev.switchauto.companion.v2

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var stateView: TextView
    private lateinit var detailView: TextView
    private lateinit var metricsView: TextView
    private var lastRevision = -1L
    private val refresh = object : Runnable {
        override fun run() {
            renderState()
            stateView.postDelayed(this, 250)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(buildContent())
        requestNotificationPermission()
        ProjectionRelayService.start(this)
    }

    override fun onResume() {
        super.onResume()
        stateView.post(refresh)
    }

    override fun onPause() {
        stateView.removeCallbacks(refresh)
        super.onPause()
    }

    private fun buildContent(): LinearLayout {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(24), dp(32), dp(24), dp(24))
            setBackgroundColor(Color.rgb(9, 13, 20))
        }
        root.addView(TextView(this).apply {
            text = "Switch Auto V2"
            textSize = 30f
            setTextColor(Color.WHITE)
        }, matchWrap())
        root.addView(TextView(this).apply {
            text = "Android Auto transport"
            textSize = 15f
            setTextColor(Color.rgb(148, 163, 184))
        }, matchWrap(dp(4)))
        stateView = TextView(this).apply {
            textSize = 22f
            gravity = Gravity.CENTER
            setPadding(dp(18), dp(28), dp(18), dp(8))
        }
        root.addView(stateView, matchWrap(dp(20)))
        detailView = TextView(this).apply {
            textSize = 15f
            gravity = Gravity.CENTER
            setTextColor(Color.rgb(203, 213, 225))
        }
        root.addView(detailView, matchWrap())
        metricsView = TextView(this).apply {
            textSize = 14f
            gravity = Gravity.CENTER
            setTextColor(Color.rgb(148, 163, 184))
            setPadding(0, dp(18), 0, dp(24))
        }
        root.addView(metricsView, matchWrap())
        root.addView(Button(this).apply {
            text = "Abrir configurações do Android Auto"
            setOnClickListener { openAndroidAuto() }
        }, matchWrap(dp(12)))
        root.addView(TextView(this).apply {
            text = "O serviço inicia automaticamente e continua ativo com a tela bloqueada.\n" +
                "Switch autorizado: ${AndroidAutoRelay.TRUSTED_SWITCH_IP}:${AndroidAutoRelay.RELAY_PORT}"
            textSize = 13f
            gravity = Gravity.CENTER
            setTextColor(Color.rgb(100, 116, 139))
            setPadding(0, dp(24), 0, 0)
        }, matchWrap())
        return root
    }

    private fun renderState() {
        val revision = RelayRuntime.revision()
        if (revision == lastRevision) return
        lastRevision = revision
        val current = RelayRuntime.snapshot()
        stateView.text = when (current.state) {
            RelayState.PROJECTING -> "● PROJETANDO"
            RelayState.LISTENING -> "● PRONTO"
            RelayState.WAITING_FOR_ANDROID_AUTO -> "● AÇÃO NECESSÁRIA"
            RelayState.ERROR -> "● ERRO"
            RelayState.STARTING -> "● INICIANDO"
            RelayState.STOPPED -> "● PARADO"
        }
        stateView.setTextColor(when (current.state) {
            RelayState.PROJECTING -> Color.rgb(52, 211, 153)
            RelayState.LISTENING -> Color.rgb(96, 165, 250)
            RelayState.ERROR -> Color.rgb(248, 113, 113)
            else -> Color.rgb(251, 191, 36)
        })
        detailView.text = current.detail
        metricsView.text = "Sessões: ${current.sessions}  •  Android → Switch: ${formatBytes(current.bytesFromAndroid)}\n" +
            "Switch → Android: ${formatBytes(current.bytesFromSwitch)}"
    }

    private fun openAndroidAuto() {
        val intent = Intent("com.google.android.projection.gearhead.SETTINGS")
            .setPackage("com.google.android.projection.gearhead")
        if (intent.resolveActivity(packageManager) != null) startActivity(intent)
        else startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
            Uri.parse("package:com.google.android.projection.gearhead")))
    }

    private fun requestNotificationPermission() {
        if (android.os.Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 20)
        }
    }

    private fun formatBytes(value: Long): String = when {
        value >= 1024L * 1024L -> "%.1f MB".format(value / (1024.0 * 1024.0))
        value >= 1024L -> "%.1f KB".format(value / 1024.0)
        else -> "$value B"
    }

    private fun matchWrap(topMargin: Int = 0) = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
        this.topMargin = topMargin
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
}
