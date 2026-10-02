package org.androidaudioplugin.aaparapluginsample

import androidx.activity.ComponentActivity
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.AudioPluginServiceHelper
import org.androidaudioplugin.aaparahostsample.*
import org.androidaudioplugin.hosting.AudioPluginClientBase
import org.androidaudioplugin.hosting.AudioPluginHostHelper
import org.androidaudioplugin.hosting.AudioPluginServiceConnector
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class ReceivedDocumentTest {
    @get:Rule val compose = createAndroidComposeRule<ComponentActivity>()

    @Test fun hostedViewTracksSameInstanceEditsAndRemoval() {
        val context = compose.activity
        val bootstrap = AudioPluginClientBase(context)
        bootstrap.sampleRate = bootstrap.sampleRate
        val connector = AudioPluginServiceConnector(context)
        val pluginId = "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample"
        val service = AudioPluginHostHelper.queryAudioPluginService(context, context.packageName)
        var view: android.view.View? = null
        var instanceId = -1
        try {
            runBlocking { withTimeout(10_000) { connector.bindAudioPluginService(service) } }
            assertEquals("", AraHostSampleNative.openEditor(connector.serviceConnectionId, pluginId, 48000))
            instanceId = AraHostSampleNative.editorInstanceId()
            val project = ProjectDocument().apply {
                title = "Synchronization test"
                tracks.add(ProjectTrack(nextId++, "Host track"))
                clips.add(ProjectClip(nextId++, tracks[0].id, "Host phrase", start = 2.0))
            }
            assertEquals("", project.sync())
            var token = 0L
            compose.runOnUiThread {
                view = AudioPluginServiceHelper.createNativeView(context, pluginId, instanceId)
                token = AraPluginSampleNative.resolveSnapshot(AudioPluginServiceHelper.getServiceInstance(pluginId), instanceId)
            }
            assertTrue(token > 0)
            compose.setContent { AndroidView(factory = { view!! }, modifier = Modifier.fillMaxSize()) }
            compose.waitUntil(5_000) {
                compose.onAllNodesWithText("Host phrase").fetchSemanticsNodes().isNotEmpty()
            }
            compose.onNodeWithText("Host phrase").assertIsDisplayed()
            val initial = JSONObject(AraPluginSampleNative.snapshot(token))
            assertEquals(project.clips[0].id, initial.getJSONArray("clips").getJSONObject(0).getInt("id"))
            // Another instance's scenario must not replace this view's model.
            val scenario = AraHostSampleNative.runScenario(connector.serviceConnectionId, pluginId, 48000)
            assertTrue(scenario, scenario.contains("Result: PASS"))
            assertEquals(initial.toString(), JSONObject(AraPluginSampleNative.snapshot(token)).toString())
            project.title = "Host renamed project"
            project.tracks[0].name = "Renamed host track"
            project.clips[0].apply { name = "Edited host phrase"; start = 5.0; offset = 1.0; duration = 2.0; frequency = 330.0 }
            assertEquals("", project.sync())
            compose.waitUntil(5_000) { compose.onAllNodesWithText("Edited host phrase").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("Host renamed project").assertIsDisplayed()
            val edited = JSONObject(AraPluginSampleNative.snapshot(token))
            assertTrue(edited.getLong("revision") > initial.getLong("revision"))
            assertEquals(1, edited.getLong("contentChanges"))
            val clip = edited.getJSONArray("clips").getJSONObject(0)
            assertEquals(5.0, clip.getDouble("start"), 0.0)
            assertEquals(1.0, clip.getDouble("offset"), 0.0)
            assertEquals(2.0, clip.getDouble("duration"), 0.0)
            assertEquals("", AraHostSampleNative.verifyEditorAudio(project.clips[0].id, true))
            project.clips.clear(); project.tracks.clear()
            assertEquals("", project.sync())
            compose.waitUntil(5_000) { compose.onAllNodesWithText("Edited host phrase").fetchSemanticsNodes().isEmpty() }
            val removed = JSONObject(AraPluginSampleNative.snapshot(token))
            assertEquals(0, removed.getJSONArray("clips").length())
            assertEquals(0, removed.getJSONArray("sources").length())
            AraHostSampleNative.closeEditor()
            assertEquals("null", AraPluginSampleNative.snapshot(token))
            compose.waitUntil(5_000) {
                compose.onAllNodesWithText("The host has released this instance.").fetchSemanticsNodes().isNotEmpty()
            }
        } finally {
            view?.let { compose.runOnUiThread { AudioPluginServiceHelper.maybeDestroyNativeView(context, pluginId, instanceId, it) } }
            AraHostSampleNative.closeEditor()
            connector.close()
        }
    }
}
