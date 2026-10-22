package org.androidaudioplugin.arasample

object SampleEditorNative {
    init {
        System.loadLibrary("aap-ara-sample-editor-jni")
    }

    external fun createPrivateConnection(serviceConnectionId: Int, packageName: String, serviceClass: String): android.os.IBinder
    external fun closePrivateConnection(serviceConnectionId: Int, packageName: String, serviceClass: String, binder: android.os.IBinder)

    external fun openEditor(serviceConnectionId: Int, pluginId: String, sampleRate: Int): String
    external fun closeEditor()
    external fun editorInstanceId(): Int
    external fun verifyEditorAudio(clipId: Int, postChange: Boolean, modificationGain: Double = 1.0): String
    external fun drainPluginUpdates(): Array<String>
    external fun notifyEditorSelection(clipIds: IntArray, trackIds: IntArray,
                                       hasRange: Boolean, start: Double, duration: Double): String
    external fun syncEditor(title: String, trackIds: IntArray, trackNames: Array<String>,
                            clipIds: IntArray, clipTracks: IntArray, clipNames: Array<String>,
                            values: DoubleArray, pluginStates: Array<String>): String

    external fun runScenario(serviceConnectionId: Int, pluginId: String, sampleRate: Int): String
}
