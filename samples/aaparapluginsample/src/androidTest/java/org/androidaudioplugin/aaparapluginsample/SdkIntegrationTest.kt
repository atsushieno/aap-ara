package org.androidaudioplugin.aaparapluginsample

import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.AudioPluginServiceHelper
import org.androidaudioplugin.arasample.SampleEditorNative
import org.androidaudioplugin.arasample.ProjectClip
import org.androidaudioplugin.arasample.ProjectDocument
import org.androidaudioplugin.arasample.ProjectTrack
import org.androidaudioplugin.hosting.AudioPluginClientBase
import org.androidaudioplugin.hosting.AudioPluginHostHelper
import org.androidaudioplugin.hosting.AudioPluginServiceConnector
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** Real Binder/SDK integration without requiring an unlocked foreground display. */
class SdkIntegrationTest {
    @Test fun legacyGainImportPreservesInitialGraphAndRejectsInvalidArchives() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val bootstrap = AudioPluginClientBase(context)
        bootstrap.sampleRate = bootstrap.sampleRate
        val connector = AudioPluginServiceConnector(context)
        val pluginId = "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample"
        fun hex(text: String) = text.toByteArray().joinToString("") { "%02x".format(it) }
        try {
            val service = AudioPluginHostHelper.queryAudioPluginService(context, context.packageName)
            runBlocking { withTimeout(10_000) { connector.bindAudioPluginService(service) } }
            assertEquals("", SampleEditorNative.openEditor(connector.serviceConnectionId, pluginId, 48000))
            val project = ProjectDocument().apply {
                tracks.add(ProjectTrack(nextId++, "Legacy track"))
                clips.add(ProjectClip(nextId++, tracks[0].id, "Imported gain", pluginState = hex("gain-v1:0.5")))
                clips.add(ProjectClip(nextId++, tracks[0].id, "After imported clip", start = 5.0))
            }
            assertEquals("", project.sync())
            var token = 0L
            instrumentation.runOnMainSync {
                val id = SampleEditorNative.editorInstanceId()
                val view = AudioPluginServiceHelper.createNativeView(context, pluginId, id)
                token = view.tag as Long
                AudioPluginServiceHelper.maybeDestroyNativeView(context, pluginId, id, view)
            }
            val clips = JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips")
            assertEquals(2, clips.length())
            assertEquals(0.5, clips.getJSONObject(0).getDouble("modificationGain"), 0.0)
            assertEquals(1.0, clips.getJSONObject(1).getDouble("modificationGain"), 0.0)
            assertTrue(SampleEditorNative.drainPluginUpdates().isEmpty())
            for (invalid in listOf("", "NaN", "-0.1", "2.1", "0.5junk")) {
                val corrupted = ProjectDocument.decode(project.encode()).apply {
                    this.clips[0].pluginState = hex("gain-v1:$invalid")
                }
                assertEquals("Plugin rejected modification archive", corrupted.sync())
                assertEquals(0.5, JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips")
                    .getJSONObject(0).getDouble("modificationGain"), 0.0)
            }
            assertTrue(SampleEditorNative.drainPluginUpdates().isEmpty())
        } finally {
            SampleEditorNative.closeEditor()
            connector.close()
        }
    }

    @Test fun sdkEditsArchivesRemapAndSelectionRoundTrip() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val bootstrap = AudioPluginClientBase(context)
        bootstrap.sampleRate = bootstrap.sampleRate
        val connector = AudioPluginServiceConnector(context)
        val pluginId = "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample"
        try {
            val service = AudioPluginHostHelper.queryAudioPluginService(context, context.packageName)
            runBlocking { withTimeout(10_000) { connector.bindAudioPluginService(service) } }
            assertEquals("", SampleEditorNative.openEditor(connector.serviceConnectionId, pluginId, 48000))
            val project = ProjectDocument().apply {
                tracks.add(ProjectTrack(nextId++, "SDK track"))
                clips.add(ProjectClip(nextId++, tracks[0].id, "SDK original"))
            }
            assertEquals("", project.sync())
            var token = 0L
            InstrumentationRegistry.getInstrumentation().runOnMainSync {
                val instance = SampleEditorNative.editorInstanceId()
                val view = AudioPluginServiceHelper.createNativeView(context, pluginId, instance)
                token = view.tag as Long
                AudioPluginServiceHelper.maybeDestroyNativeView(context, pluginId, instance, view)
            }
            assertTrue("SDK-backed companion instance must publish its model", token > 0)
            assertEquals("", SampleEditorNative.verifyEditorAudio(project.clips[0].id, false))
            val modification = JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips").getJSONObject(0).getLong("modificationId")
            assertEquals("", AraPluginSampleNative.setModificationGain(token, modification, 0.375))
            val updates = SampleEditorNative.drainPluginUpdates()
            assertEquals(2, updates.size)
            assertTrue(project.applyPluginUpdates(updates))
            assertTrue(project.clips[0].pluginState.isNotEmpty())
            assertTrue(SampleEditorNative.drainPluginUpdates().isEmpty())
            project.clips.add(project.clips[0].copy(id = project.nextId++, name = "SDK duplicate", start = 4.0))
            assertEquals("", project.sync())
            val duplicated = JSONObject(AraPluginSampleNative.snapshot(token)).getJSONArray("clips")
            assertEquals(2, duplicated.length())
            for (i in 0 until duplicated.length()) assertEquals(0.375, duplicated.getJSONObject(i).getDouble("modificationGain"), 0.0)
            assertTrue("Host restoration must not echo SDK notifications", SampleEditorNative.drainPluginUpdates().isEmpty())
            assertEquals("", SampleEditorNative.notifyEditorSelection(
                intArrayOf(project.clips[1].id), intArrayOf(project.tracks[0].id), true, 4.0, 1.0))
            val selection = JSONObject(AraPluginSampleNative.snapshot(token)).getJSONObject("selection")
            assertEquals(project.clips[1].id, selection.getJSONArray("clips").getInt(0))
            assertEquals(4.0, selection.getJSONObject("range").getDouble("start"), 0.0)
            project.clips.clear(); project.tracks.clear()
            assertEquals("", project.sync())
            SampleEditorNative.closeEditor()
            assertEquals("null", AraPluginSampleNative.snapshot(token))
        } finally {
            SampleEditorNative.closeEditor()
            connector.close()
        }
    }
}
