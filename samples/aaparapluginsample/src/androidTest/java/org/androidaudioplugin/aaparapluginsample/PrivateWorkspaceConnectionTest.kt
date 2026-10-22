package org.androidaudioplugin.aaparapluginsample

import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.arasample.SampleEditorNative
import org.androidaudioplugin.hosting.AudioPluginClientBase
import org.androidaudioplugin.hosting.AudioPluginHostHelper
import org.androidaudioplugin.hosting.AudioPluginServiceConnector
import org.junit.Assert.*
import org.junit.Test

/** Only the plugin app self-hosts through a private Binder endpoint. */
class PrivateWorkspaceConnectionTest {
    @Test fun privateWorkspaceConnectionSurvivesExternalHostBinding() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val bootstrap = AudioPluginClientBase(context)
        bootstrap.sampleRate = bootstrap.sampleRate
        val workspace = AudioPluginServiceConnector(context)
        val externalHost = AudioPluginServiceConnector(context)
        val pluginId = "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample"
        val info = AudioPluginHostHelper.queryAudioPlugins(context).first { it.pluginId == pluginId }
        val service = AudioPluginHostHelper.queryAudioPluginService(context, info.packageName)
        val binder = SampleEditorNative.createPrivateConnection(workspace.serviceConnectionId, info.packageName, service.className)
        try {
            assertEquals("", SampleEditorNative.openEditor(workspace.serviceConnectionId, pluginId, 48000))
            runBlocking { withTimeout(10000) { externalHost.bindAudioPluginService(service) } }
            val report = SampleEditorNative.runScenario(workspace.serviceConnectionId, pluginId, 48000)
            assertTrue(report, report.contains("Result: PASS"))
            val externalReport = SampleEditorNative.runScenario(externalHost.serviceConnectionId, pluginId, 48000)
            assertTrue(externalReport, externalReport.contains("Result: PASS"))
        } finally {
            SampleEditorNative.closeEditor()
            externalHost.close()
            SampleEditorNative.closePrivateConnection(workspace.serviceConnectionId, info.packageName, service.className, binder)
            workspace.close()
        }
    }
}
