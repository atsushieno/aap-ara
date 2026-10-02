package org.androidaudioplugin.aaparahostsample

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class ProjectEditorComposeTest {
    @get:Rule val compose = createComposeRule()

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
        }
        compose.onNodeWithText("Remove clip").performClick()
        compose.runOnIdle { assertTrue(state.value.clips.isEmpty()) }
    }
}
