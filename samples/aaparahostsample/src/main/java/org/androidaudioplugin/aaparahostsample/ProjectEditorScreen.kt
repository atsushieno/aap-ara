package org.androidaudioplugin.aaparahostsample

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

@Composable
fun ProjectEditorScreen(
    document: ProjectDocument, busy: Boolean, connected: Boolean, status: String,
    canUndo: Boolean, canRedo: Boolean, onChange: (ProjectDocument.() -> Unit) -> Unit,
    onConnect: () -> Unit, onSave: () -> Unit, onLoad: () -> Unit,
    onUndo: () -> Unit, onRedo: () -> Unit, onScenario: () -> Unit,
    onShowPluginUi: (() -> Unit)? = null
) {
    var selectedTrackId by remember { mutableStateOf<Int?>(null) }
    var selectedClipId by remember { mutableStateOf<Int?>(null) }
    val track = document.tracks.find { it.id == selectedTrackId } ?: document.tracks.firstOrNull()
    val clip = document.clips.find { it.id == selectedClipId }
    var nameEdit by remember { mutableStateOf<Pair<Int?, String>?>(null) }
    var clipEdit by remember { mutableStateOf<ProjectClip?>(null) }
    var removeTrack by remember { mutableStateOf<ProjectTrack?>(null) }
    var trackMenu by remember { mutableStateOf(false) }

    Surface(Modifier.fillMaxSize()) {
        Column(Modifier.safeDrawingPadding().padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(document.title, style = MaterialTheme.typography.headlineSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text("ARA Project Editor · ${if (connected) "Connected" else "Offline"}", style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.primary)
            Toolbar {
                EditorButton(if (connected) "Connected" else "Connect", !busy && !connected, onConnect)
                EditorButton("New", !busy) { onChange { title = "Untitled Project"; clips.clear(); tracks.clear() }; selectedClipId = null }
                EditorButton("Rename project", !busy) { nameEdit = null to document.title }
                EditorButton("Save", !busy, onSave)
                EditorButton("Load", !busy, onLoad)
            }
            Toolbar {
                Box {
                    EditorButton(track?.name ?: "Choose track", !busy && document.tracks.isNotEmpty()) { trackMenu = true }
                    DropdownMenu(trackMenu, { trackMenu = false }) {
                        document.tracks.forEach { t -> DropdownMenuItem(text = { Text(t.name) }, onClick = { selectedTrackId = t.id; trackMenu = false }) }
                    }
                }
                EditorButton("Add track", !busy) { nameEdit = -1 to "Track ${document.tracks.size + 1}" }
                EditorButton("Rename track", !busy && track != null) { track?.let { nameEdit = it.id to it.name } }
                EditorButton("Remove track", !busy && track != null) { removeTrack = track }
            }
            Toolbar {
                EditorButton("Add clip", !busy && track != null) { track?.let { clipEdit = ProjectClip(document.nextId, it.id, "Tone ${document.clips.size + 1}") } }
                EditorButton("Edit clip", !busy && clip != null) { clipEdit = clip?.copy() }
                EditorButton("Duplicate", !busy && clip != null) {
                    clip?.let { c -> val id = document.nextId
                        onChange { clips.add(c.copy(id = nextId++, start = c.start + c.duration, name = "${c.name} copy")) }
                        selectedClipId = id
                    }
                }
                EditorButton("Remove clip", !busy && clip != null) {
                    clip?.let { c -> onChange { clips.removeAll { it.id == c.id } }; selectedClipId = null }
                }
            }
            Toolbar {
                EditorButton("Undo", !busy && canUndo, onUndo)
                EditorButton("Redo", !busy && canRedo, onRedo)
                EditorButton("Run Scenario", !busy, onScenario)
                onShowPluginUi?.let { EditorButton("Show plugin UI", !busy && connected, it) }
            }
            Text("Generated stereo tones · source length 60s · diagnostic audio output", style = MaterialTheme.typography.labelSmall)
            ProjectTimeline(document, selectedTrackId = track?.id, selectedClipId = selectedClipId,
                modifier = Modifier.weight(1f).fillMaxWidth(), onTrack = { selectedTrackId = it },
                onClip = { selectedClipId = it.id; selectedTrackId = it.track })
            if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
            Surface(color = MaterialTheme.colorScheme.surfaceContainer, shape = RoundedCornerShape(8.dp)) {
                Text(status, Modifier.fillMaxWidth().heightIn(min = 48.dp, max = 140.dp)
                    .verticalScroll(rememberScrollState()).padding(10.dp), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
    nameEdit?.let { (id, original) ->
        var value by remember(id, original) { mutableStateOf(original) }
        AlertDialog(onDismissRequest = { nameEdit = null }, title = { Text(if (id == null) "Project name" else "Track name") },
            text = { OutlinedTextField(value, { value = it }, label = { Text("Name") }, singleLine = true) },
            confirmButton = { TextButton(enabled = value.isNotBlank() && !busy, onClick = {
                if (id == -1) selectedTrackId = document.nextId
                onChange {
                    when (id) {
                        null -> title = value.trim()
                        -1 -> tracks.add(ProjectTrack(nextId++, value.trim()))
                        else -> tracks.find { it.id == id }?.name = value.trim()
                    }
                }; nameEdit = null
            }) { Text("Apply") } }, dismissButton = { TextButton(onClick = { nameEdit = null }) { Text("Cancel") } })
    }
    removeTrack?.let { t ->
        AlertDialog(onDismissRequest = { removeTrack = null }, title = { Text("Remove track") },
            text = { Text("Remove ${t.name} and all its clips?") },
            confirmButton = { TextButton(enabled = !busy, onClick = {
                onChange { clips.removeAll { it.track == t.id }; tracks.removeAll { it.id == t.id } }; removeTrack = null
            }) { Text("Remove") } }, dismissButton = { TextButton(onClick = { removeTrack = null }) { Text("Cancel") } })
    }
    clipEdit?.let { c ->
        ClipEditorDialog(c, document.tracks, busy, onDismiss = { clipEdit = null }, onApply = { edited ->
            onChange {
                val fresh = clips.none { it.id == edited.id }
                val index = clips.indexOfFirst { it.id == edited.id }
                if (index >= 0) clips[index] = edited else clips.add(edited)
                if (fresh) nextId++
            }
            selectedClipId = edited.id; selectedTrackId = edited.track; clipEdit = null
        })
    }
}

@Composable
private fun Toolbar(content: @Composable RowScope.() -> Unit) {
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp), content = content)
}
@Composable
private fun EditorButton(label: String, enabled: Boolean, action: () -> Unit) {
    OutlinedButton(onClick = action, enabled = enabled, contentPadding = PaddingValues(horizontal = 12.dp, vertical = 6.dp)) { Text(label, maxLines = 1) }
}

@Composable
private fun ClipEditorDialog(clip: ProjectClip, tracks: List<ProjectTrack>, busy: Boolean,
                             onDismiss: () -> Unit, onApply: (ProjectClip) -> Unit) {
    var name by remember { mutableStateOf(clip.name) }
    var start by remember { mutableStateOf(clip.start.toString()) }
    var offset by remember { mutableStateOf(clip.offset.toString()) }
    var duration by remember { mutableStateOf(clip.duration.toString()) }
    var frequency by remember { mutableStateOf(clip.frequency.toString()) }
    var gain by remember { mutableStateOf(clip.gain.toString()) }
    var track by remember { mutableStateOf(clip.track) }
    var expanded by remember { mutableStateOf(false) }
    val values = listOf(start, offset, duration, frequency, gain).map { it.toDoubleOrNull() }
    val valid = name.isNotBlank() && values.all { it != null && it.isFinite() } &&
        values[0]!! >= 0 && values[1]!! >= 0 && values[2]!! > 0 && values[1]!! + values[2]!! <= 60 &&
        values[3]!! in 20.0..20000.0 && values[4]!! in 0.0..1.0 && tracks.any { it.id == track }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Clip properties") }, text = {
        Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(name, { name = it }, label = { Text("Name") }, singleLine = true)
            NumberField("Start (seconds)", start) { start = it }
            NumberField("Source offset (seconds)", offset) { offset = it }
            NumberField("Duration (seconds)", duration) { duration = it }
            NumberField("Frequency (Hz)", frequency) { frequency = it }
            NumberField("Gain (0–1)", gain) { gain = it }
            Box {
                OutlinedButton(onClick = { expanded = true }) { Text(tracks.find { it.id == track }?.name ?: "Destination track") }
                DropdownMenu(expanded, { expanded = false }) {
                    tracks.forEach { t -> DropdownMenuItem(text = { Text(t.name) }, onClick = { track = t.id; expanded = false }) }
                }
            }
            if (!valid) Text("Use nonnegative times, source end ≤ 60s, 20–20000Hz and gain 0–1.", color = MaterialTheme.colorScheme.error)
        }
    }, confirmButton = { TextButton(enabled = valid && !busy, onClick = {
        onApply(ProjectClip(clip.id, track, name.trim(), values[0]!!, values[1]!!, values[2]!!, values[3]!!, values[4]!!))
    }) { Text("Apply") } }, dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
}
@Composable
private fun NumberField(label: String, value: String, change: (String) -> Unit) {
    OutlinedTextField(value, change, label = { Text(label) }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal))
}
