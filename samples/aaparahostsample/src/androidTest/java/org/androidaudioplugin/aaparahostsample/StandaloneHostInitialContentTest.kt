package org.androidaudioplugin.aaparahostsample

import org.androidaudioplugin.arasample.*
import android.view.accessibility.AccessibilityNodeInfo
import androidx.compose.ui.test.junit4.createEmptyComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test

/** Run against the actual host APK and read the plug-in's separate embedded surface. */
class StandaloneHostInitialContentTest {
    @get:Rule val compose = createEmptyComposeRule()

    @Test fun openingExistingProjectShowsEveryClipAndRestoresLegacyGain() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        // This fixture covers the standalone host binary.
        assumeTrue(context.packageName == "org.androidaudioplugin.aaparahostsample")
        val project = ProjectDocument().apply {
            title = "Initial SDK project"
            tracks.add(ProjectTrack(nextId++, "Lead"))
            tracks.add(ProjectTrack(nextId++, "Bass"))
            clips.add(ProjectClip(nextId++, tracks[0].id, "Initial first"))
            clips.add(ProjectClip(nextId++, tracks[1].id, "Initial edited", start = 5.0,
                pluginState = "gain-v1:0.5".toByteArray().joinToString("") { "%02x".format(it) }))
            clips.add(ProjectClip(nextId++, tracks[1].id, "Initial last", start = 10.0, duration = 8.0))
        }
        // Supply activity state without overwriting the user's saved project.
        var fixtureLease: ProjectEditorController? = null
        instrumentation.runOnMainSync {
            fixtureLease = ProjectEditorController.acquire(context, project.encode())
        }
        try {
            ActivityScenario.launch(MainActivity::class.java).use {
                instrumentation.runOnMainSync { fixtureLease!!.release(); fixtureLease = null }
                compose.onNodeWithText("Connect").performClick()
                compose.waitUntil(15_000) {
                    compose.onAllNodesWithText("ARA Project Editor · Connected").fetchSemanticsNodes().isNotEmpty() &&
                        compose.onAllNodesWithText("Synchronizing ARA document…").fetchSemanticsNodes().isEmpty()
                }
                compose.onNodeWithText("Show plugin UI").performClick()
                compose.waitUntil(15_000) {
                    "2 tracks · 3 clips · 3 sources" in visibleText()
                }
                val text = visibleText()
                assertTrue(text.toString(), "Initial SDK project" in text)
                assertTrue(text.toString(), text.none { it.contains("rejected modification archive") })
                compose.onNodeWithText("Select all clips").performClick()
                compose.waitUntil(10_000) {
                    visibleText().any { it.startsWith("Host selection: 3 clips") }
                }
                // The first selected clip is the unedited one; select the migrated clip explicitly.
                compose.onNodeWithText("Hide plugin UI").performClick()
                compose.onNodeWithText("Initial edited").performClick()
                compose.onNodeWithText("Show plugin UI").performClick()
                compose.waitUntil(10_000) {
                    visibleText().any { it.startsWith("Plugin gain 0.50×") }
                }
            }
        } finally {
            instrumentation.runOnMainSync { fixtureLease?.release() }
        }
    }

    @Suppress("DEPRECATION")
    private fun visibleText(): List<String> {
        val root = InstrumentationRegistry.getInstrumentation().uiAutomation.rootInActiveWindow ?: return emptyList()
        val result = mutableListOf<String>()
        fun visit(node: AccessibilityNodeInfo) {
            try {
                node.text?.toString()?.let(result::add)
                for (index in 0 until node.childCount) node.getChild(index)?.let(::visit)
            } finally { node.recycle() }
        }
        visit(root)
        return result
    }
}
