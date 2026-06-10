package org.androidaudioplugin.aaparahostsample

import android.content.Context
import android.media.AudioManager
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.hosting.AudioPluginClientBase
import org.androidaudioplugin.hosting.AudioPluginHostHelper
import org.androidaudioplugin.hosting.AudioPluginServiceConnector

class MainActivity : AppCompatActivity() {
    private val bootstrapClient by lazy { AudioPluginClientBase(this) }
    private val connector by lazy { AudioPluginServiceConnector(this) }
    private lateinit var statusView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bootstrapClient.sampleRate = bootstrapClient.sampleRate

        statusView = TextView(this).apply {
            movementMethod = ScrollingMovementMethod()
            typeface = android.graphics.Typeface.MONOSPACE
            text = "Ready.\nInstall the sample plugin app, then tap Run Scenario."
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1f
            )
        }

        val runButton = Button(this).apply {
            text = "Run Scenario"
            setOnClickListener { runScenario() }
        }

        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 32)
            addView(runButton, LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ))
            addView(statusView)
        })
    }

    override fun onDestroy() {
        connector.close()
        super.onDestroy()
    }

    private fun updateStatus(text: String) {
        runOnUiThread {
            statusView.text = text
        }
    }

    private fun presentScenarioResult(report: String): String {
        return when {
            report.contains("\nResult: PASS\n") || report.startsWith("Result: PASS\n") ->
                "Scenario completed successfully.\n\n$report"
            report.contains("\nResult: FAIL\n") || report.startsWith("Result: FAIL\n") ->
                "Scenario completed with verification failure.\n\n$report"
            report.startsWith("Scenario failed:") || report.startsWith("Failed ") ->
                report
            else ->
                "Scenario completed.\n\n$report"
        }
    }

    private fun runScenario() {
        statusView.text = "Resolving sample plugin..."
        Thread {
            val report = runCatching {
                val plugin = AudioPluginHostHelper.queryAudioPlugins(applicationContext)
                    .firstOrNull { it.pluginId == SAMPLE_PLUGIN_ID }
                    ?: return@runCatching "Sample plugin not found.\nInstall org.androidaudioplugin.aaparapluginsample first."

                updateStatus("Binding sample plugin service...")

                runBlocking {
                    withTimeout(10_000) {
                        runCatching {
                            connector.unbindAudioPluginService(plugin.packageName)
                        }
                        connector.bindAudioPluginService(
                            AudioPluginHostHelper.queryAudioPluginService(applicationContext, plugin.packageName)
                        )
                    }
                }

                updateStatus("Sample plugin service connected.\nRunning native ARA scenario...")

                val audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
                val sampleRate = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull()
                    ?: 48000

                AraHostSampleNative.runScenario(
                    connector.serviceConnectionId,
                    plugin.pluginId!!,
                    sampleRate
                )
            }.getOrElse { throwable ->
                "Scenario failed:\n${throwable.stackTraceToString()}"
            }

            runOnUiThread {
                statusView.text = presentScenarioResult(report)
            }
        }.start()
    }

    companion object {
        private const val SAMPLE_PLUGIN_ID =
            "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample"
    }
}
