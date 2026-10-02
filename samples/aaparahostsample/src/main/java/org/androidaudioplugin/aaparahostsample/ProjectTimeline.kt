package org.androidaudioplugin.aaparahostsample

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import java.util.Locale
import kotlin.math.max
import kotlin.math.roundToInt

private data class ClipDrag(val clip: ProjectClip, val trackIndex: Int, val extent: Double,
                            val laneWidth: Float, val delta: Offset = Offset.Zero)

@Composable
fun ProjectTimeline(document: ProjectDocument, selectedTrackId: Int?, selectedClipId: Int?, modifier: Modifier,
                    onTrack: (Int) -> Unit, onClip: (ProjectClip) -> Unit, showFrequency: Boolean = true,
                    onMove: ((clipId: Int, trackId: Int, start: Double) -> Unit)? = null) {
    var drag by remember { mutableStateOf<ClipDrag?>(null) }
    val currentDocument by rememberUpdatedState(document)
    val selectClip by rememberUpdatedState(onClip)
    val moveClip by rememberUpdatedState(onMove)
    val density = LocalDensity.current
    val rowHeight = with(density) { 78.dp.toPx() }
    // Freeze the scale for the gesture. A move past the old end expands it on release.
    val extent = drag?.extent ?: max(16.0, (document.clips.maxOfOrNull { it.start + it.duration } ?: 0.0) * 1.25)
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
            Row(Modifier.fillMaxWidth().height(78.dp).zIndex(if (drag?.clip?.track == track.id) 1f else 0f).padding(bottom = 4.dp)) {
                Text(track.name, Modifier.width(100.dp).fillMaxHeight()
                    .background(if (track.id == selectedTrackId) Color(0xFF344356) else Color(0xFF202A37))
                    .clickable { onTrack(track.id) }.padding(10.dp), maxLines = 2, overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.labelLarge)
                BoxWithConstraints(Modifier.weight(1f).fillMaxHeight()) {
                    val laneWidth = maxWidth
                    val laneWidthPx = with(density) { laneWidth.toPx() }
                    Canvas(Modifier.fillMaxSize()) {
                        for (i in 0..4) {
                            val x = i * size.width / 4
                            drawLine(Color(0xFF374252), Offset(x, 0f), Offset(x, size.height))
                        }
                    }
                    document.clips.filter { it.track == track.id }.forEach { clip ->
                        val active = drag?.takeIf { it.clip.id == clip.id }
                        val start = active?.let { max(0.0, it.clip.start + it.delta.x / it.laneWidth * it.extent) } ?: clip.start
                        val x = laneWidth * (start / extent).toFloat()
                        val y = active?.let { with(density) { it.delta.y.toDp() } } ?: 0.dp
                        val width = (laneWidth * (clip.duration / extent).toFloat()).coerceAtLeast(6.dp)
                        Column(Modifier.offset(x = x, y = y).width(width).fillMaxHeight().padding(3.dp)
                            .zIndex(if (active != null) 1f else 0f).testTag("timeline-clip-${clip.id}")
                            .pointerInput(clip.id, onMove != null, laneWidthPx, extent) {
                                if (moveClip != null) detectDragGestures(
                                    onDragStart = {
                                        currentDocument.clips.find { it.id == clip.id }?.let { source ->
                                            selectClip(source)
                                            drag = ClipDrag(source.copy(), currentDocument.tracks.indexOfFirst { it.id == source.track },
                                                extent, laneWidthPx.coerceAtLeast(1f))
                                        }
                                    },
                                    onDrag = { change, amount ->
                                        change.consume()
                                        drag = drag?.let { it.copy(delta = it.delta + amount) }
                                    },
                                    onDragCancel = { drag = null },
                                    onDragEnd = {
                                        val finished = drag
                                        drag = null
                                        if (finished != null && currentDocument.tracks.isNotEmpty()) {
                                            val index = (finished.trackIndex + (finished.delta.y / rowHeight).roundToInt())
                                                .coerceIn(currentDocument.tracks.indices)
                                            val target = currentDocument.tracks[index].id
                                            val position = max(0.0, finished.clip.start + finished.delta.x / finished.laneWidth * finished.extent)
                                            if (position != finished.clip.start || target != finished.clip.track)
                                                moveClip?.invoke(finished.clip.id, target, position)
                                        }
                                    })
                            }
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
