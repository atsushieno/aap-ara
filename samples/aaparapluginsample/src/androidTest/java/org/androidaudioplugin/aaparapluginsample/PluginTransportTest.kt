package org.androidaudioplugin.aaparapluginsample

import org.junit.Assert.assertEquals
import org.junit.Test

class PluginTransportTest {
    init { System.loadLibrary("aap-ara-plugin-sample") }
    private external fun verifyPortIsolation(): String
    @Test fun processPreservesHostInjectedMidiPorts() { assertEquals("", verifyPortIsolation()) }
}
