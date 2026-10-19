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
    onShowPluginUi: (() -> Unit)? = null,
    selection: ProjectSelection? = null, onSelection: ((ProjectSelection) -> Unit)? = null
) {
    var localSelection by remember { mutableStateOf(ProjectSelection()) }
    val currentSelection = selection ?: localSelection
    fun select(next: ProjectSelection) { localSelection = next; onSelection?.invoke(next) }
    val selectedClipId = currentSelection.clips.firstOrNull()
    val selectedTrackId = currentSelection.tracks.firstOrNull()
        ?: document.clips.find { it.id == selectedClipId }?.track
    var multiple by remember { mutableStateOf(false) }
    var editRange by remember { mutableStateOf(false) }
    fun selectTrack(id: Int) {
        select(if (multiple) currentSelection.copy(tracks =
            if (id in currentSelection.tracks) currentSelection.tracks - id else listOf(id) + currentSelection.tracks)
        else ProjectSelection(tracks = listOf(id)))
    }
    fun selectClip(c: ProjectClip) {
        select(if (multiple) currentSelection.copy(clips =
            if (c.id in currentSelection.clips) currentSelection.clips - c.id else listOf(c.id) + currentSelection.clips)
        else ProjectSelection(clips = listOf(c.id)))
    }
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
                EditorButton("New", !busy) { select(ProjectSelection()); onChange { title = "Untitled Project"; clips.clear(); tracks.clear() } }
                EditorButton("Rename project", !busy) { nameEdit = null to document.title }
                EditorButton("Save", !busy, onSave)
                EditorButton("Load", !busy, onLoad)
            }
            Toolbar {
                Box {
                    EditorButton(track?.name ?: "Choose track", !busy && document.tracks.isNotEmpty()) { trackMenu = true }
                    DropdownMenu(trackMenu, { trackMenu = false }) {
                        document.tracks.forEach { t -> DropdownMenuItem(text = { Text(t.name) }, onClick = { selectTrack(t.id); trackMenu = false }) }
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
                        onChange { clips.find { it.id == c.id }?.let { source ->
                            clips.add(source.copy(id = nextId++, start = source.start + source.duration, name = "${source.name} copy"))
                        } }
                        select(ProjectSelection(clips = listOf(id)))
                    }
                }
                EditorButton("Remove clip", !busy && clip != null) {
                    clip?.let { c -> onChange { clips.removeAll { it.id == c.id } }; select(currentSelection.copy(clips = currentSelection.clips - c.id)) }
                }
            }
            Toolbar {
                EditorButton("Undo", !busy && canUndo, onUndo)
                EditorButton("Redo", !busy && canRedo, onRedo)
                EditorButton("Run Scenario", !busy, onScenario)
                onShowPluginUi?.let { EditorButton("Show plugin UI", !busy && connected, it) }
            }
            Toolbar {
                EditorButton(if (multiple) "Multiple: on" else "Select multiple", !busy) { multiple = !multiple }
                EditorButton("Select all clips", !busy) { select(currentSelection.copy(clips = document.clips.map { it.id })) }
                EditorButton("Time range", !busy) { editRange = true }
                EditorButton("Clear selection", !busy) { select(ProjectSelection()) }
            }
            Text("Selection: ${currentSelection.clips.size} clips · ${currentSelection.tracks.size} tracks" +
                (currentSelection.timeRange?.let { " · ${it.first}–${it.second}s" } ?: ""), style = MaterialTheme.typography.labelSmall)
            Text("Generated stereo tones · source length 60s · diagnostic audio output", style = MaterialTheme.typography.labelSmall)
            Text("Drag clips to move them in time or between tracks · Plugin edits: ${document.clips.count { it.pluginState.isNotEmpty() }} clips",
                style = MaterialTheme.typography.labelSmall)
            ProjectTimeline(document, selectedTrackId = track?.id, selectedClipId = selectedClipId,
                modifier = Modifier.weight(1f).fillMaxWidth(), onTrack = { if (!busy) selectTrack(it) },
                onClip = { if (!busy) selectClip(it) },
                selectedClipIds = currentSelection.clips.toSet(), selectedTrackIds = currentSelection.tracks.toSet(),
                timeRange = currentSelection.timeRange,
                onClipDragStart = { c -> if (c.id !in currentSelection.clips) selectClip(c) },
                onMove = if (busy) null else { id, target, position ->
                    onChange { clips.find { it.id == id }?.let { it.track = target; it.start = position } }
                })
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
                if (id == -1) select(ProjectSelection(tracks = listOf(document.nextId)))
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
                if (index >= 0) clips[index] = edited.copy(pluginState = clips[index].pluginState) else clips.add(edited)
                if (fresh) nextId++
            }
            select(ProjectSelection(clips = listOf(edited.id))); clipEdit = null
        })
    }
    if (editRange) {
        var start by remember { mutableStateOf((currentSelection.timeRange?.first ?: 0.0).toString()) }
        var end by remember { mutableStateOf((currentSelection.timeRange?.second ?: 4.0).toString()) }
        val a = start.toDoubleOrNull()
        val b = end.toDoubleOrNull()
        val valid = a != null && b != null && a.isFinite() && b.isFinite() && a >= 0 && b >= a
        AlertDialog(onDismissRequest = { editRange = false }, title = { Text("Time range") }, text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(if (currentSelection.tracks.isEmpty()) "All tracks" else "Selected tracks")
                NumberField("Range start (seconds)", start) { start = it }
                NumberField("Range end (seconds)", end) { end = it }
                Text("Equal endpoints select a point.")
            }
        }, confirmButton = { TextButton(enabled = valid && !busy, onClick = {
            select(currentSelection.copy(timeRange = a!! to b!!)); editRange = false
        }) { Text("Apply range") } }, dismissButton = { TextButton(onClick = { editRange = false }) { Text("Cancel") } })
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
