package org.androidaudioplugin.aaparahostsample

import androidx.activity.ComponentActivity
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.hosting.AudioPluginSurfaceControlClient
import org.androidaudioplugin.hosting.AudioPluginClientBase
import org.androidaudioplugin.hosting.AudioPluginHostHelper
import org.androidaudioplugin.hosting.AudioPluginServiceConnector
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.util.concurrent.atomic.AtomicBoolean

/** Exercises the same preferred-size and embedded-surface service used by AAP hosts. */
class ProjectHostedEditorTest {
    @get:Rule val compose = createAndroidComposeRule<ComponentActivity>()

    @Test fun hostCanRequestSizeAndDisplayComposeSurface() {
        lateinit var client: AudioPluginSurfaceControlClient
        val context = compose.activity
        val bootstrap = AudioPluginClientBase(context)
        bootstrap.sampleRate = bootstrap.sampleRate
        val connector = AudioPluginServiceConnector(context)
        val laidOut = AtomicBoolean(false)
        compose.runOnUiThread {
            client = AudioPluginSurfaceControlClient(compose.activity)
            client.contentSizeChangedListeners.add { width, height -> if (width > 0 && height > 0) laidOut.set(true) }
        }
        try {
            val pluginId = "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample"
            val packageName = "org.androidaudioplugin.aaparapluginsample"
            runBlocking { withTimeout(10_000) {
                connector.bindAudioPluginService(AudioPluginHostHelper.queryAudioPluginService(context, packageName))
            } }
            assertEquals("", AraHostSampleNative.openEditor(connector.serviceConnectionId, pluginId, 48000))
            val instanceId = AraHostSampleNative.editorInstanceId()
            val document = ProjectDocument().apply {
                tracks.add(ProjectTrack(nextId++, "Surface track"))
                clips.add(ProjectClip(nextId++, tracks[0].id, "Surface phrase"))
            }
            assertEquals("", document.sync())
            val size = runBlocking { withTimeout(10_000) { client.getPreferredSizeNoHandler(packageName, pluginId, instanceId) } }
            assertTrue(size[0] > 0 && size[1] > 0)
            compose.setContent { AndroidView(factory = { client.surfaceView }, modifier = Modifier.fillMaxSize()) }
            compose.waitForIdle()
            runBlocking { withTimeout(10_000) { withContext(Dispatchers.Main) { client.connectUINoHandler(packageName, pluginId, instanceId, size[0], size[1]) } } }
            compose.waitUntil(10_000) { laidOut.get() }
        } finally {
            compose.runOnUiThread { client.close() }
            AraHostSampleNative.closeEditor()
            connector.close()
        }
    }
}
