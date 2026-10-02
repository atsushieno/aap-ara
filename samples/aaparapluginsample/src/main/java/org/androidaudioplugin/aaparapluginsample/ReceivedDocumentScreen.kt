package org.androidaudioplugin.aaparapluginsample

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale
import org.androidaudioplugin.aaparahostsample.ProjectClip
import org.androidaudioplugin.aaparahostsample.ProjectDocument
import org.androidaudioplugin.aaparahostsample.ProjectTimeline
import org.androidaudioplugin.aaparahostsample.ProjectTrack
import org.json.JSONObject

/** Observes the ARA graph of the exact plugin instance selected by the host. */
@Composable
fun ReceivedDocumentScreen(token: Long, instanceId: Int) {
    var snapshot by remember(token) { mutableStateOf<JSONObject?>(null) }
    var alive by remember(token) { mutableStateOf(true) }
    var selectedClip by remember(token) { mutableStateOf<Int?>(null) }
    var edit by remember(token) { mutableStateOf<JSONObject?>(null) }
    var editing by remember(token) { mutableStateOf(false) }
    var editStatus by remember(token) { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    LaunchedEffect(token) {
        while (true) {
            val text = AraPluginSampleNative.snapshot(token)
            alive = text != "null"
            val next = if (alive) JSONObject(text) else null
            if (next?.optLong("revision") != snapshot?.optLong("revision")) snapshot = next
            delay(150)
        }
    }
    val document = remember(snapshot) {
        ProjectDocument().apply {
            title = snapshot?.optString("title", "Waiting for host document") ?: "Instance unavailable"
            snapshot?.optJSONArray("tracks")?.let { array ->
                (0 until array.length()).map { array.getJSONObject(it) }.sortedBy { it.getInt("order") }.forEach {
                    tracks.add(ProjectTrack(it.getInt("id"), it.getString("name")))
                }
            }
            snapshot?.optJSONArray("clips")?.let { array ->
                for (i in 0 until array.length()) array.getJSONObject(i).let {
                    clips.add(ProjectClip(it.getInt("id"), it.getInt("track"), it.getString("name"),
                        it.getDouble("start"), it.getDouble("offset"), it.getDouble("duration")))
                }
            }
        }
    }
    MaterialTheme(colorScheme = darkColorScheme()) {
        Surface(Modifier.fillMaxSize()) {
            Box(Modifier.fillMaxSize()) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Plugin · Received ARA document", style = MaterialTheme.typography.titleMedium)
                    Text(document.title, style = MaterialTheme.typography.headlineSmall)
                    Text("Instance $instanceId · Revision ${snapshot?.optLong("revision") ?: 0}",
                        color = MaterialTheme.colorScheme.primary)
                    Text("${document.tracks.size} tracks · ${document.clips.size} clips · ${snapshot?.optJSONArray("sources")?.length() ?: 0} sources")
                    Text(if (alive) "Host arrangement and plugin sound edits synchronize automatically." else "The host has released this instance.")
                    ProjectTimeline(document, null, selectedClip, Modifier.weight(1f).fillMaxWidth(),
                        onTrack = {}, onClip = { selectedClip = it.id }, showFrequency = false)
                    document.clips.find { it.id == selectedClip }?.let {
                        Text(String.format(Locale.ROOT, "%s · start %.3fs · source offset %.3fs · duration %.3fs",
                            it.name, it.start, it.offset, it.duration))
                        val clips = snapshot?.optJSONArray("clips")
                        val model = clips?.let { array -> (0 until array.length()).map { array.getJSONObject(it) }
                            .firstOrNull { model -> model.getInt("id") == selectedClip } }
                        model?.let { model ->
                            Text(String.format(Locale.ROOT, "Plugin gain %.2f× · processed RMS %.4f",
                                model.optDouble("modificationGain", 1.0), model.optDouble("processedRms", 0.0)))
                            Button(enabled = alive && !editing && snapshot?.optBoolean("canEdit") == true,
                                onClick = { editStatus = ""; edit = model }) { Text("Edit sound") }
                            if (snapshot?.optBoolean("canEdit") != true) Text("Host content-update support is required for sound edits.")
                        }
                    }
                    if (editStatus.isNotEmpty()) Text(editStatus)
                    Text("Content updates: ${snapshot?.optLong("contentChanges") ?: 0}")
                    Text(snapshot?.optString("analysis").orEmpty(), Modifier.heightIn(max = 110.dp)
                        .verticalScroll(rememberScrollState()), style = MaterialTheme.typography.bodySmall)
                }
                edit?.let { model ->
                    var gainText by remember(model) { mutableStateOf(model.optDouble("modificationGain", 1.0).toString()) }
                    val gain = gainText.toDoubleOrNull()?.takeIf { it.isFinite() && it in 0.0..2.0 }
                    // The view is served by a Service/SurfaceControlViewHost, which
                    // has no activity window token. Keep modal UI inside this surface.
                    Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.7f)), contentAlignment = Alignment.Center) {
                        Box(Modifier.matchParentSize().clickable { if (!editing) edit = null })
                        Card(Modifier.padding(16.dp).widthIn(max = 420.dp).pointerInput(Unit) { detectTapGestures(onTap = {}) }) {
                            Column(Modifier.padding(20.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                Text("Edit ${model.getString("name")}", style = MaterialTheme.typography.titleLarge)
                                Text("Non-destructive gain for this audio modification.")
                                OutlinedTextField(gainText, { gainText = it }, label = { Text("Plugin gain (0–2)") }, singleLine = true, enabled = !editing)
                                Slider(value = (gain ?: 1.0).toFloat(), onValueChange = { gainText = it.toString() }, valueRange = 0f..2f, enabled = !editing)
                                if (editStatus.isNotEmpty()) Text(editStatus)
                                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                                    TextButton(enabled = !editing, onClick = { edit = null }) { Text("Cancel") }
                                    TextButton(enabled = gain != null && !editing && alive, onClick = {
                                        editing = true
                                        scope.launch {
                                            val result = withContext(Dispatchers.IO) {
                                                AraPluginSampleNative.setModificationGain(token, model.getLong("modificationId"), gain!!)
                                            }
                                            editing = false
                                            editStatus = if (result.isEmpty()) "Edit sent to host." else result
                                            if (result.isEmpty()) edit = null
                                        }
                                    }) { Text("Apply sound edit") }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
