package org.androidaudioplugin.aaparahostsample

import android.content.Context
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.Surface
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.hosting.AudioPluginClientBase
import org.androidaudioplugin.hosting.AudioPluginHostHelper
import org.androidaudioplugin.hosting.AudioPluginServiceConnector

/** Shared app workspace for launcher and SurfaceControl-hosted Compose editors. */
class ProjectEditorController private constructor(private val context: Context, snapshot: String?) {
    private val mainHandler = Handler(Looper.getMainLooper())
    private var disposed = false
    private var viewers = 0
    private val bootstrapClient by lazy { AudioPluginClientBase(context) }
    private val connectorDelegate = lazy { AudioPluginServiceConnector(context) }
    private val connector get() = connectorDelegate.value
    private var nativeSessionRequested = false
    private var privateBinder: android.os.IBinder? = null
    private var privateServiceClass = ""
    private var busy by mutableStateOf(false)
    private var connected by mutableStateOf(false)
    private var pluginInstanceId by mutableStateOf(-1)
    private var status by mutableStateOf("Edit offline or connect to the sample plugin.")
    private var document by mutableStateOf(ProjectDocument().apply {
        title = "ARA Demo Project"
        val lead = ProjectTrack(nextId++, "Lead")
        val bass = ProjectTrack(nextId++, "Bass")
        tracks.addAll(listOf(lead, bass))
        clips.add(ProjectClip(nextId++, lead.id, "Phrase A", frequency = 440.0))
        clips.add(ProjectClip(nextId++, lead.id, "Phrase B", start = 4.0, frequency = 660.0))
        clips.add(ProjectClip(nextId++, bass.id, "Bass tone", duration = 8.0, frequency = 110.0))
    })
    private val undo = java.util.ArrayDeque<String>()
    private val redo = java.util.ArrayDeque<String>()
    private var historyRevision by mutableStateOf(0)
    private val savedProject get() = java.io.File(context.filesDir, "project.json")

    init {
        bootstrapClient.sampleRate = bootstrapClient.sampleRate
        if (snapshot != null) document = ProjectDocument.decode(snapshot)
        else if (savedProject.exists()) runCatching { ProjectDocument.decode(savedProject.readText()) }
            .onSuccess { document = it }.onFailure { status = "Could not load saved project: ${it.message}" }
    }

    fun snapshot() = document.encode()

    @Composable
    fun Editor() {
        MaterialTheme(colorScheme = darkColorScheme()) {
            val revision = historyRevision
            var showPluginUi by remember { mutableStateOf(false) }
            val editorContent: @Composable () -> Unit = {
                ProjectEditorScreen(document, busy, connected, status,
                    canUndo = revision >= 0 && undo.isNotEmpty(), canRedo = redo.isNotEmpty(),
                    onChange = ::mutate, onConnect = ::connectEditor, onSave = ::save,
                    onLoad = ::load, onUndo = { history(undo, redo) },
                    onRedo = { history(redo, undo) }, onScenario = ::runScenario,
                    onShowPluginUi = if (context.packageName != SAMPLE_PACKAGE) ({ showPluginUi = true }) else null)
            }
            Surface(Modifier.fillMaxSize()) {
                BoxWithConstraints(Modifier.fillMaxSize()) {
                    if (!showPluginUi || pluginInstanceId < 0) editorContent()
                    else if (maxWidth >= 700.dp) Row(Modifier.fillMaxSize()) {
                        Box(Modifier.weight(1f)) { editorContent() }
                        Box(Modifier.weight(1f).safeDrawingPadding().padding(8.dp)) {
                            HostedPluginPanel(pluginInstanceId) { showPluginUi = false }
                        }
                    } else Column(Modifier.fillMaxSize()) {
                        Box(Modifier.weight(1f)) { editorContent() }
                        Box(Modifier.weight(1f).safeDrawingPadding().padding(8.dp)) {
                            HostedPluginPanel(pluginInstanceId) { showPluginUi = false }
                        }
                    }
                }
            }
        }
    }

    fun release() {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (--viewers != 0) return
        disposed = true
        current = null
        worker.execute {
            if (nativeSessionRequested) AraHostSampleNative.closeEditor()
            privateBinder?.let { AraHostSampleNative.closePrivateConnection(connector.serviceConnectionId,
                context.packageName, privateServiceClass, it) }
            if (connectorDelegate.isInitialized()) connector.close()
        }
    }

    private fun mutate(change: ProjectDocument.() -> Unit) {
        if (busy) return
        val next = ProjectDocument.decode(document.encode())
        next.change()
        replaceDocument(next)
    }
    private fun replaceDocument(next: ProjectDocument) {
        undo.addLast(document.encode()); redo.clear(); historyRevision++
        document = next; sync()
    }
    private fun history(from: java.util.ArrayDeque<String>, to: java.util.ArrayDeque<String>) {
        if (busy || from.isEmpty()) return
        to.addLast(document.encode()); document = ProjectDocument.decode(from.removeLast())
        historyRevision++; sync()
    }
    private fun save() {
        if (!busy) runCatching { savedProject.writeText(document.encode()) }
            .onSuccess { status = "Project saved" }.onFailure { status = "Save failed: ${it.message}" }
    }
    private fun load() {
        if (!busy) runCatching { ProjectDocument.decode(savedProject.readText()) }
            .onSuccess { replaceDocument(it) }.onFailure { status = "Load failed: ${it.message}" }
    }
    private fun sync() {
        if (!connected) { status = "${document.tracks.size} tracks • ${document.clips.size} clips • offline"; return }
        busy = true; status = "Synchronizing ARA document…"
        val snapshot = ProjectDocument.decode(document.encode())
        worker.execute {
            val result = runCatching { snapshot.sync() }.getOrElse { it.message ?: "Sync failed" }
            mainHandler.post { if (disposed) return@post; busy = false; status = if (result.isEmpty()) "ARA model synchronized • ${snapshot.clips.size} clips" else result }
        }
    }
    private fun connectEditor() {
        if (busy || connected) return
        busy = true; status = "Connecting to sample plugin…"
        worker.execute {
            val result = runCatching {
                bindPlugin()
                nativeSessionRequested = true
                AraHostSampleNative.openEditor(connector.serviceConnectionId, SAMPLE_PLUGIN_ID, sampleRate())
            }.getOrElse { it.message ?: "Connection failed" }
            val instanceId = if (result.isEmpty()) AraHostSampleNative.editorInstanceId() else -1
            mainHandler.post { if (disposed) return@post; busy = false; connected = result.isEmpty(); pluginInstanceId = instanceId; if (connected) sync() else status = result }
        }
    }
    private fun bindPlugin() {
        val plugin = AudioPluginHostHelper.queryAudioPlugins(context).firstOrNull { it.pluginId == SAMPLE_PLUGIN_ID }
            ?: error("Install the AAP ARA sample plugin first")
        if (plugin.packageName == context.packageName) {
            // An exported service has one host callback. Give the plugin app's own
            // workspace a separate Binder endpoint so external hosts cannot replace it.
            if (privateBinder == null) {
                privateServiceClass = AudioPluginHostHelper.queryAudioPluginService(context, plugin.packageName).className
                privateBinder = AraHostSampleNative.createPrivateConnection(connector.serviceConnectionId,
                    plugin.packageName, privateServiceClass)
            }
            return
        }
        runBlocking { withTimeout(10_000) {
            connector.bindAudioPluginService(AudioPluginHostHelper.queryAudioPluginService(context, plugin.packageName))
        } }
    }
    private fun sampleRate(): Int {
        val manager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        return manager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000
    }
    private fun runScenario() {
        if (busy) return
        busy = true; status = "Running native ARA scenario…"
        worker.execute {
            val report = runCatching {
                if (!connected) bindPlugin()
                AraHostSampleNative.runScenario(connector.serviceConnectionId, SAMPLE_PLUGIN_ID, sampleRate())
            }.getOrElse { "Scenario failed:\n${it.stackTraceToString()}" }
            mainHandler.post { if (disposed) return@post; busy = false; status = report }
        }
    }
    companion object {
        private var current: ProjectEditorController? = null
        // Preserve teardown/reconnect ordering when launcher and hosted views overlap.
        private val worker = java.util.concurrent.Executors.newSingleThreadExecutor()
        private const val SAMPLE_PLUGIN_ID = "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample"
        private const val SAMPLE_PACKAGE = "org.androidaudioplugin.aaparapluginsample"
        fun acquire(context: Context, snapshot: String? = null): ProjectEditorController {
            check(Looper.myLooper() == Looper.getMainLooper())
            val controller = current ?: ProjectEditorController(context.applicationContext, snapshot).also { current = it }
            controller.viewers++
            return controller
        }
    }
}
