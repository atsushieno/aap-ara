package org.androidaudioplugin.aaparahostsample

import org.json.JSONArray
import org.json.JSONObject

/** Stable identities keep ARA objects alive when names, positions or content change. */
data class ProjectTrack(val id: Int, var name: String)
data class ProjectClip(val id: Int, var track: Int, var name: String,
                       var start: Double = 0.0, var offset: Double = 0.0,
                       var duration: Double = 4.0, var frequency: Double = 220.0,
                       var gain: Double = 0.2)
class ProjectDocument {
    var title = "Untitled Project"
    var nextId = 10
    val tracks = mutableListOf<ProjectTrack>()
    val clips = mutableListOf<ProjectClip>()
    fun encode(): String = JSONObject().apply {
        put("version", 1); put("title", title); put("nextId", nextId)
        put("tracks", JSONArray().apply { tracks.forEach { put(JSONObject().put("id", it.id).put("name", it.name)) } })
        put("clips", JSONArray().apply { clips.forEach {
            put(JSONObject().put("id", it.id).put("track", it.track).put("name", it.name)
                .put("start", it.start).put("offset", it.offset).put("duration", it.duration)
                .put("frequency", it.frequency).put("gain", it.gain))
        } })
    }.toString(2)
    fun sync(): String = AraHostSampleNative.syncEditor(title, tracks.map { it.id }.toIntArray(),
        tracks.map { it.name }.toTypedArray(), clips.map { it.id }.toIntArray(),
        clips.map { it.track }.toIntArray(), clips.map { it.name }.toTypedArray(),
        clips.flatMap { listOf(it.start, it.offset, it.duration, it.frequency, it.gain) }.toDoubleArray())
    companion object {
        fun decode(text: String): ProjectDocument {
            val json = JSONObject(text)
            require(json.getInt("version") == 1) { "Unsupported project version" }
            return ProjectDocument().apply {
                title = json.getString("title")
                val ts = json.getJSONArray("tracks")
                for (i in 0 until ts.length()) ts.getJSONObject(i).let { tracks.add(ProjectTrack(it.getInt("id"), it.getString("name"))) }
                val cs = json.getJSONArray("clips")
                for (i in 0 until cs.length()) cs.getJSONObject(i).let {
                    clips.add(ProjectClip(it.getInt("id"), it.getInt("track"), it.getString("name"),
                        it.getDouble("start"), it.getDouble("offset"), it.getDouble("duration"),
                        it.getDouble("frequency"), it.getDouble("gain")))
                }
                val ids = tracks.map { it.id } + clips.map { it.id }
                require(ids.all { it >= 10 && it < Int.MAX_VALUE } && ids.distinct().size == ids.size) { "Invalid object identities" }
                require(clips.all { c -> tracks.any { it.id == c.track } &&
                    listOf(c.start, c.offset, c.duration, c.frequency, c.gain).all { it.isFinite() } &&
                    c.start >= 0 && c.offset >= 0 && c.duration > 0 && c.offset + c.duration <= 60 &&
                    c.frequency in 20.0..20000.0 && c.gain in 0.0..1.0 }) { "Invalid clip properties" }
                nextId = maxOf(json.getInt("nextId"), (ids.maxOrNull() ?: 9) + 1)
                require(nextId < Int.MAX_VALUE) { "Object identities exhausted" }
            }
        }
    }
}
