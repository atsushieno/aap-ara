package org.androidaudioplugin.aaparapluginsample

object AraPluginSampleNative {
    init { System.loadLibrary("aap-ara-plugin-sample") }
    external fun resolveSnapshot(servicePointer: Long, instanceId: Int): Long
    external fun snapshot(token: Long): String
}
