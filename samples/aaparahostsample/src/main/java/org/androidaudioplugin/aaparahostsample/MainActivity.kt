package org.androidaudioplugin.aaparahostsample

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge

open class MainActivity : ComponentActivity() {
    private lateinit var editor: ProjectEditorController
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        editor = ProjectEditorController.acquire(this, savedInstanceState?.getString("document"))
        enableEdgeToEdge(
            statusBarStyle = androidx.activity.SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
            navigationBarStyle = androidx.activity.SystemBarStyle.dark(android.graphics.Color.TRANSPARENT))
        setContent { editor.Editor() }
    }
    override fun onSaveInstanceState(outState: Bundle) {
        outState.putString("document", editor.snapshot())
        super.onSaveInstanceState(outState)
    }
    override fun onDestroy() {
        editor.release()
        super.onDestroy()
    }
}
