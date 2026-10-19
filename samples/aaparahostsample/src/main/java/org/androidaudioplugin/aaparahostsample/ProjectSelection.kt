package org.androidaudioplugin.aaparahostsample

/** Transient editor state. Never serialized into a project or its undo history. */
data class ProjectSelection(
    val clips: List<Int> = emptyList(),
    val tracks: List<Int> = emptyList(),
    val timeRange: Pair<Double, Double>? = null
) {
    fun reconciled(document: ProjectDocument): ProjectSelection {
        val remainingTracks = tracks.distinct().filter { id -> document.tracks.any { it.id == id } }
        return copy(clips = clips.distinct().filter { id -> document.clips.any { it.id == id } },
            tracks = remainingTracks,
            timeRange = timeRange.takeUnless { tracks.isNotEmpty() && remainingTracks.isEmpty() })
    }

    fun notifyPlugin(): String = AraHostSampleNative.notifyEditorSelection(clips.toIntArray(), tracks.toIntArray(),
        timeRange != null, timeRange?.first ?: 0.0, timeRange?.let { it.second - it.first } ?: 0.0)
}
