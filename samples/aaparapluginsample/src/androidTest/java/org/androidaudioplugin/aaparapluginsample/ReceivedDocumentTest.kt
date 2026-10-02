package org.androidaudioplugin.aaparapluginsample

import androidx.activity.ComponentActivity
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
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
                // The real SurfaceControl view service also supplies a non-activity
                // context. Sound forms must not require an activity window token.
                view = AudioPluginServiceHelper.createNativeView(context.applicationContext, pluginId, instanceId)
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
            // Plugin-owned content edits travel through the reverse AAPXS path.
            // Save the opaque archive in the host project and restore it on undo/redo.
            assertTrue(edited.toString(), edited.getBoolean("canEdit"))
            compose.onNodeWithText("Edited host phrase").performClick()
            compose.onNodeWithText("Edit sound").assertIsEnabled().performClick()
            compose.waitUntil(5_000) { compose.onAllNodes(hasSetTextAction()).fetchSemanticsNodes().isNotEmpty() }
            compose.onNode(hasSetTextAction() and hasText("Plugin gain (0–2)")).performTextReplacement("0.5")
            compose.onNodeWithText("Apply sound edit").performClick()
            compose.waitUntil(5_000) {
                JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips").getJSONObject(0).getDouble("modificationGain") == 0.5
            }
            compose.waitUntil(5_000) { compose.onAllNodesWithText("Edit sent to host.").fetchSemanticsNodes().isNotEmpty() }
            val beforePluginEdit = project.encode()
            val updates = AraHostSampleNative.drainPluginUpdates()
            assertEquals(updates.toList().toString(), 2, updates.size)
            assertEquals(project.clips[0].id.toString(), updates[0])
            assertTrue(project.applyPluginUpdates(updates))
            val savedPluginEdit = project.encode()
            assertEquals(project.clips[0].pluginState, ProjectDocument.decode(savedPluginEdit).clips[0].pluginState)
            val soundEdit = JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips").getJSONObject(0)
            assertEquals(clip.getDouble("processedRms") * 0.5, soundEdit.getDouble("processedRms"), 0.00001)
            assertEquals("", AraHostSampleNative.verifyEditorAudio(project.clips[0].id, true, 0.5))
            val invalidArchive = ProjectDocument.decode(savedPluginEdit).apply { clips[0].pluginState = "ff" }
            assertEquals("Plugin rejected modification archive", invalidArchive.sync())
            assertEquals(0.5, JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips").getJSONObject(0).getDouble("modificationGain"), 0.0)
            // Host position changes preserve the existing sound edit and do not echo it.
            project.clips[0].start = 7.125
            assertEquals("", project.sync())
            assertTrue(AraHostSampleNative.drainPluginUpdates().isEmpty())
            assertEquals(0.5, JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips").getJSONObject(0).getDouble("modificationGain"), 0.0)
            assertEquals("", ProjectDocument.decode(beforePluginEdit).sync())
            assertEquals(1.0, JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips").getJSONObject(0).getDouble("modificationGain"), 0.0)
            assertTrue(AraHostSampleNative.drainPluginUpdates().isEmpty())
            assertEquals("", ProjectDocument.decode(savedPluginEdit).sync())
            assertEquals(0.5, JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips").getJSONObject(0).getDouble("modificationGain"), 0.0)
            assertTrue(AraHostSampleNative.drainPluginUpdates().isEmpty())
            project.clips.clear(); project.tracks.clear()
            assertEquals("", project.sync())
            compose.waitUntil(5_000) { compose.onAllNodesWithText("Edited host phrase").fetchSemanticsNodes().isEmpty() }
            val removed = JSONObject(AraPluginSampleNative.snapshot(token))
            assertEquals(0, removed.getJSONArray("clips").length())
            assertEquals(0, removed.getJSONArray("sources").length())
            AraHostSampleNative.closeEditor()
            assertEquals("null", AraPluginSampleNative.snapshot(token))
            assertEquals("The host has released this instance.", AraPluginSampleNative.setModificationGain(token, clip.getLong("modificationId"), 1.0))
            compose.waitUntil(5_000) {
                compose.onAllNodesWithText("The host has released this instance.").fetchSemanticsNodes().isNotEmpty()
            }
            // Reconstruct a new ARA session from the saved host document.
            assertEquals("", AraHostSampleNative.openEditor(connector.serviceConnectionId, pluginId, 48000))
            val loaded = ProjectDocument.decode(savedPluginEdit)
            assertEquals("", loaded.sync())
            val restoredToken = AraPluginSampleNative.resolveSnapshot(AudioPluginServiceHelper.getServiceInstance(pluginId), AraHostSampleNative.editorInstanceId())
            assertNotEquals(token, restoredToken)
            assertEquals(0.5, JSONObject(AraPluginSampleNative.snapshot(restoredToken)).getJSONArray("clips").getJSONObject(0).getDouble("modificationGain"), 0.0)
            assertEquals("", AraHostSampleNative.verifyEditorAudio(loaded.clips[0].id, false, 0.5))
            assertTrue(AraHostSampleNative.drainPluginUpdates().isEmpty())
        } finally {
            view?.let { compose.runOnUiThread { AudioPluginServiceHelper.maybeDestroyNativeView(context, pluginId, instanceId, it) } }
            AraHostSampleNative.closeEditor()
            connector.close()
        }
    }
}
