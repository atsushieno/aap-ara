package org.androidaudioplugin.aaparahostsample

import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.hosting.AudioPluginClientBase
import org.androidaudioplugin.hosting.AudioPluginHostHelper
import org.androidaudioplugin.hosting.AudioPluginServiceConnector
import org.junit.Assert.*
import org.junit.Test

class ProjectEditorTest {
    @Test fun projectRoundTripPreservesIdentityAndRejectsInvalidReferences() {
        val project = ProjectDocument().apply {
            tracks.add(ProjectTrack(nextId++, "Vocal"))
            clips.add(ProjectClip(nextId++, tracks[0].id, "Take", start = 2.5, offset = 1.0))
        }
        val restored = ProjectDocument.decode(project.encode())
        assertEquals(project.tracks, restored.tracks)
        assertEquals(project.clips, restored.clips)
        assertEquals(project.nextId, restored.nextId)
        restored.clips[0].track = 999
        assertThrows(IllegalArgumentException::class.java) { ProjectDocument.decode(restored.encode()) }
        restored.clips[0].track = restored.tracks[0].id
        restored.clips[0].offset = 59.0
        assertThrows(IllegalArgumentException::class.java) { ProjectDocument.decode(restored.encode()) }
    }

    @Test fun connectedEditingAndOriginalScenario() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val bootstrap = AudioPluginClientBase(context)
        bootstrap.sampleRate = bootstrap.sampleRate
        val connector = AudioPluginServiceConnector(context)
        val pluginId = "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample"
        val info = AudioPluginHostHelper.queryAudioPlugins(context).first { it.pluginId == pluginId }
        try {
            runBlocking { withTimeout(10000) {
                connector.bindAudioPluginService(AudioPluginHostHelper.queryAudioPluginService(context, info.packageName))
            } }
            assertEquals("", AraHostSampleNative.openEditor(connector.serviceConnectionId, pluginId, 48000))
            val project = ProjectDocument().apply {
                tracks.add(ProjectTrack(nextId++, "Track one"))
                tracks.add(ProjectTrack(nextId++, "Track two"))
                clips.add(ProjectClip(nextId++, tracks[0].id, "Tone one"))
                clips.add(ProjectClip(nextId++, tracks[0].id, "Tone two", start = 5.0))
            }
            assertEquals("", project.sync())
            assertEquals("", AraHostSampleNative.verifyEditorAudio(project.clips.last().id, false))
            project.title = "Edited project"
            project.clips[0].apply { track = project.tracks[1].id; start = 2.0; offset = 1.0; duration = 3.0; frequency = 440.0; gain = 0.4 }
            project.clips.removeAt(1)
            project.tracks.removeAt(0)
            assertEquals("", project.sync())
            assertEquals("", AraHostSampleNative.verifyEditorAudio(project.clips[0].id, true))
            val report = AraHostSampleNative.runScenario(connector.serviceConnectionId, pluginId, 48000)
            assertTrue(report, report.contains("Result: PASS"))
            project.clips.clear(); project.tracks.clear()
            assertEquals("", project.sync())
        } finally {
            AraHostSampleNative.closeEditor()
            connector.close()
        }
    }
}
