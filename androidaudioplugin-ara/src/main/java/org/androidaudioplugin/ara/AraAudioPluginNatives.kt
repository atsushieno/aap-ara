package org.androidaudioplugin.ara

internal object AraAudioPluginNatives {
    init {
        System.loadLibrary("androidaudioplugin-ara")
    }

    @JvmStatic
    external fun installAraExtensions()
}
