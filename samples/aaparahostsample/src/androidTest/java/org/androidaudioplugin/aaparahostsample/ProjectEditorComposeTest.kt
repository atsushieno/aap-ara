package org.androidaudioplugin.aaparahostsample

import androidx.compose.material3.MaterialTheme
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class ProjectEditorComposeTest {
    @get:Rule val compose = createComposeRule()

    @Test fun dragMovesFreelyBetweenTracksAndCancellationDoesNotCommit() {
        val project = ProjectDocument().apply {
            tracks.add(ProjectTrack(nextId++, "One")); tracks.add(ProjectTrack(nextId++, "Two"))
            clips.add(ProjectClip(nextId++, tracks[0].id, "Drag me", start = 1.0))
        }
        val state = mutableStateOf(project)
        val history = mutableListOf<String>()
        compose.setContent {
            MaterialTheme {
                ProjectTimeline(state.value, null, project.clips[0].id, Modifier.fillMaxSize(), {}, {},
                    onMove = { id, track, start ->
                        history.add(state.value.encode())
                        state.value = ProjectDocument.decode(state.value.encode()).apply {
                            clips.first { it.id == id }.apply { this.track = track; this.start = start }
                        }
                    })
            }
        }
        val clip = compose.onNodeWithTag("timeline-clip-${project.clips[0].id}")
        val bounds = clip.fetchSemanticsNode().boundsInRoot
        // 37% of a four-second clip width is deliberately off any beat grid.
        clip.performTouchInput {
            down(center)
            moveBy(Offset(bounds.width * 0.37f, bounds.height * 1.1f), 500)
            up()
        }
        compose.runOnIdle {
            assertEquals(1, history.size)
            assertEquals(project.tracks[1].id, state.value.clips[0].track)
            assertTrue(state.value.clips[0].start > 1.2)
            val start = state.value.clips[0].start
            assertTrue(kotlin.math.abs(start * 2 - kotlin.math.round(start * 2)) > 0.01)
        }
        val moved = state.value.encode()
        clip.performTouchInput { down(center); moveBy(Offset(80f, 0f), 500); cancel() }
        compose.runOnIdle {
            assertEquals(moved, state.value.encode())
            assertEquals(1, history.size)
            state.value = ProjectDocument.decode(history.removeLast())
            assertEquals(project.clips, state.value.clips)
        }
    }

    @Test fun addTrackEditSourceAndRemoveClip() {
        val state = mutableStateOf(ProjectDocument())
        compose.setContent {
            MaterialTheme {
                ProjectEditorScreen(state.value, false, false, "Offline", false, false,
                    onChange = { change ->
                        val next = ProjectDocument.decode(state.value.encode())
                        next.change(); state.value = next
                    }, onConnect = {}, onSave = {}, onLoad = {}, onUndo = {}, onRedo = {}, onScenario = {})
            }
        }
        compose.onNodeWithText("Add track").performClick()
        compose.onNode(hasSetTextAction()).performTextReplacement("Guitar")
        compose.onNodeWithText("Apply").performClick()
        compose.runOnIdle { assertEquals("Guitar", state.value.tracks.single().name) }
        compose.onNodeWithText("Add clip").performClick()
        compose.onNode(hasSetTextAction() and hasText("Frequency (Hz)")).performScrollTo().performTextReplacement("330")
        compose.onNodeWithText("Apply").performClick()
        compose.runOnIdle {
            assertEquals(330.0, state.value.clips.single().frequency, 0.0)
            assertEquals(state.value.tracks.single().id, state.value.clips.single().track)
            state.value = ProjectDocument.decode(state.value.encode()).apply { clips[0].pluginState = "6761696e2d76313a302e35" }
        }
        compose.onNodeWithText("Edit clip").performClick()
        compose.onNode(hasSetTextAction() and hasText("Frequency (Hz)")).performScrollTo().performTextReplacement("440")
        compose.onNodeWithText("Apply").performClick()
        compose.runOnIdle { assertEquals("6761696e2d76313a302e35", state.value.clips.single().pluginState) }
        compose.onNodeWithText("Remove clip").performClick()
        compose.runOnIdle { assertTrue(state.value.clips.isEmpty()) }
    }
}
