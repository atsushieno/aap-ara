package org.androidaudioplugin.aaparapluginsample

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
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
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Plugin · Received ARA document", style = MaterialTheme.typography.titleMedium)
                Text(document.title, style = MaterialTheme.typography.headlineSmall)
                Text("Instance $instanceId · Revision ${snapshot?.optLong("revision") ?: 0}",
                    color = MaterialTheme.colorScheme.primary)
                Text("${document.tracks.size} tracks · ${document.clips.size} clips · ${snapshot?.optJSONArray("sources")?.length() ?: 0} sources")
                Text(if (alive) "Changes made in the host appear here automatically." else "The host has released this instance.")
                ProjectTimeline(document, null, selectedClip, Modifier.weight(1f).fillMaxWidth(),
                    onTrack = {}, onClip = { selectedClip = it.id }, showFrequency = false)
                document.clips.find { it.id == selectedClip }?.let {
                    Text("${it.name} · start ${it.start}s · source offset ${it.offset}s · duration ${it.duration}s")
                }
                Text("Content updates: ${snapshot?.optLong("contentChanges") ?: 0}")
                Text(snapshot?.optString("analysis").orEmpty(), Modifier.heightIn(max = 110.dp)
                    .verticalScroll(rememberScrollState()), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}
