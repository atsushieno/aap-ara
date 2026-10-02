package org.androidaudioplugin.aaparahostsample

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout
import org.androidaudioplugin.hosting.AudioPluginSurfaceControlClient

/** Embeds the UI of the same remote instance that receives this host's edits. */
@Composable
fun HostedPluginPanel(instanceId: Int, onClose: () -> Unit) {
    val context = LocalContext.current
    val client = remember(instanceId, context) { AudioPluginSurfaceControlClient(context) }
    var size by remember { mutableStateOf(IntSize.Zero) }
    var connected by remember(client) { mutableStateOf(false) }
    var error by remember(client) { mutableStateOf<String?>(null) }
    DisposableEffect(client) { onDispose { client.close() } }
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Hosted plugin", style = MaterialTheme.typography.titleMedium)
            TextButton(onClick = onClose) { Text("Hide plugin UI") }
        }
        error?.let { Text("Plugin UI: $it", color = MaterialTheme.colorScheme.error) }
        AndroidView(factory = { client.surfaceView }, modifier = Modifier.weight(1f).fillMaxWidth()
            .onSizeChanged { size = it })
    }
    LaunchedEffect(client, size) {
        if (size.width <= 0 || size.height <= 0) return@LaunchedEffect
        if (connected) client.resizeUI(instanceId, size.width, size.height)
        else try {
            withTimeout(10_000) {
                withFrameNanos { }
                client.connectUINoHandler("org.androidaudioplugin.aaparapluginsample",
                    "urn:org.androidaudioplugin/samples/aaparapluginsample/EffectSample", instanceId, size.width, size.height)
            }
            connected = true
        } catch (ex: TimeoutCancellationException) { error = "Connection timed out" }
        catch (ex: CancellationException) { throw ex }
        catch (ex: Exception) { error = ex.message ?: "Connection failed" }
    }
}
