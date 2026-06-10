package org.androidaudioplugin.aaparahostsample

object AraHostSampleNative {
    init {
        System.loadLibrary("aap-ara-host-sample")
    }

    external fun runScenario(serviceConnectionId: Int, pluginId: String, sampleRate: Int): String
}
