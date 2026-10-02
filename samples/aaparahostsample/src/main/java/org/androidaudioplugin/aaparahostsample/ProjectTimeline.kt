package org.androidaudioplugin.aaparahostsample

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import java.util.Locale
import kotlin.math.max

@Composable
fun ProjectTimeline(document: ProjectDocument, selectedTrackId: Int?, selectedClipId: Int?, modifier: Modifier,
                    onTrack: (Int) -> Unit, onClip: (ProjectClip) -> Unit, showFrequency: Boolean = true) {
    val extent = max(16.0, document.clips.maxOfOrNull { it.start + it.duration } ?: 16.0)
    LazyColumn(modifier.background(Color(0xFF141A24)).clip(RoundedCornerShape(8.dp))) {
        item {
            Row(Modifier.fillMaxWidth().padding(top = 8.dp, bottom = 8.dp)) {
                Spacer(Modifier.width(100.dp))
                for (i in 0..3) Text(String.format(Locale.ROOT, "%.1fs", i * extent / 4),
                    Modifier.weight(1f), style = MaterialTheme.typography.labelSmall)
            }
        }
        if (document.tracks.isEmpty()) item { Text("Add a track to begin your project.", Modifier.padding(16.dp)) }
        items(document.tracks, key = { it.id }) { track ->
            Row(Modifier.fillMaxWidth().height(78.dp).padding(bottom = 4.dp)) {
                Text(track.name, Modifier.width(100.dp).fillMaxHeight()
                    .background(if (track.id == selectedTrackId) Color(0xFF344356) else Color(0xFF202A37))
                    .clickable { onTrack(track.id) }.padding(10.dp), maxLines = 2, overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.labelLarge)
                BoxWithConstraints(Modifier.weight(1f).fillMaxHeight().clipToBounds()) {
                    val laneWidth = maxWidth
                    Canvas(Modifier.fillMaxSize()) {
                        for (i in 0..4) {
                            val x = i * size.width / 4
                            drawLine(Color(0xFF374252), Offset(x, 0f), Offset(x, size.height))
                        }
                    }
                    document.clips.filter { it.track == track.id }.forEach { clip ->
                        val x = laneWidth * (clip.start / extent).toFloat()
                        val width = (laneWidth * (clip.duration / extent).toFloat()).coerceAtLeast(6.dp)
                        Column(Modifier.offset(x = x).width(width).fillMaxHeight().padding(3.dp)
                            .clip(RoundedCornerShape(6.dp))
                            .background(if (clip.id == selectedClipId) Color(0xFFF8BA48) else Color(0xFF37AEA6))
                            .clickable { onClip(clip) }.padding(6.dp)) {
                            Text(clip.name, color = Color(0xFF0F1E26), maxLines = 1, overflow = TextOverflow.Ellipsis,
                                style = MaterialTheme.typography.labelLarge)
                            Text(if (showFrequency) "${clip.frequency.toInt()} Hz" else "${clip.duration}s", color = Color(0xFF0F1E26), maxLines = 1,
                                overflow = TextOverflow.Ellipsis, style = MaterialTheme.typography.labelSmall)
                        }
                    }
                }
            }
        }
    }
}
