package org.androidaudioplugin.ara

import android.content.Context
import org.androidaudioplugin.AudioPluginService

class AraAudioPluginServiceExtension : AudioPluginService.Extension {
    override fun initialize(ctx: Context) {
        AraAudioPluginNatives.installAraExtensions()
    }

    override fun cleanup() {
    }
}
