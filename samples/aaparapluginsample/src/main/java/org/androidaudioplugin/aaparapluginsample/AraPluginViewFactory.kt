package org.androidaudioplugin.aaparapluginsample

import android.content.Context
import android.util.Size
import android.view.View
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import org.androidaudioplugin.AudioPluginViewFactory
import org.androidaudioplugin.AudioPluginServiceHelper

/** Compose bridge used by hosts through AudioPluginViewService/SurfaceControl. */
class AraPluginViewFactory : AudioPluginViewFactory() {
    override fun getPreferredSize(context: Context, pluginId: String, instanceId: Int): Size {
        val density = context.resources.displayMetrics.density
        return Size((720 * density).toInt(), (600 * density).toInt())
    }
    override fun createView(context: Context, pluginId: String, instanceId: Int): View {
        val token = AraPluginSampleNative.resolveSnapshot(AudioPluginServiceHelper.getServiceInstance(pluginId), instanceId)
        return ComposeView(context).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnDetachedFromWindow)
            setContent { ReceivedDocumentScreen(token, instanceId) }
        }
    }
    override fun maybeDestroyView(context: Context, pluginId: String, instanceId: Int, view: View) {
        (view as? ComposeView)?.disposeComposition()
    }
}
