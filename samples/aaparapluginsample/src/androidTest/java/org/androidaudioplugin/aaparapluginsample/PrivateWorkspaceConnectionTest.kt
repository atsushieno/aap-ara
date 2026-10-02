package org.androidaudioplugin.aaparapluginsample

import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.aaparahostsample.AraHostSampleNative
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
        val binder = AraHostSampleNative.createPrivateConnection(workspace.serviceConnectionId, info.packageName, service.className)
        try {
            assertEquals("", AraHostSampleNative.openEditor(workspace.serviceConnectionId, pluginId, 48000))
            runBlocking { withTimeout(10000) { externalHost.bindAudioPluginService(service) } }
            val report = AraHostSampleNative.runScenario(workspace.serviceConnectionId, pluginId, 48000)
            assertTrue(report, report.contains("Result: PASS"))
            val externalReport = AraHostSampleNative.runScenario(externalHost.serviceConnectionId, pluginId, 48000)
            assertTrue(externalReport, externalReport.contains("Result: PASS"))
        } finally {
            AraHostSampleNative.closeEditor()
            externalHost.close()
            AraHostSampleNative.closePrivateConnection(workspace.serviceConnectionId, info.packageName, service.className, binder)
            workspace.close()
        }
    }
}
