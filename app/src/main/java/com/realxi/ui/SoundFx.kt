package com.realxi.ui

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool
import com.realxi.engine.Ev

/**
 * Tiny SoundPool wrapper. It looks for assets/audio/sfx/<name>.ogg (kick, whistle, goal, post, save, tackle) and silently
 * skips any file that is missing, so the game runs without audio assets (see assets/audio/README.md).
 */
class SoundFx(context: Context) {
    private val pool: SoundPool? = try {
        SoundPool.Builder()
            .setMaxStreams(6)
            .setAudioAttributes(
                AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build(),
            ).build()
    } catch (_: Exception) { null }

    private val ids = HashMap<String, Int>()

    init {
        for (name in listOf("kick", "whistle", "goal", "post", "save", "tackle")) {
            try {
                context.assets.openFd("audio/sfx/$name.ogg").use { fd -> pool?.load(fd, 1)?.let { ids[name] = it } }
            } catch (_: Exception) { /* asset not provided */ }
        }
    }

    fun play(name: String, volume: Float = 1f) {
        val id = ids[name] ?: return
        pool?.play(id, volume, volume, 1, 0, 1f)
    }

    fun onEvent(type: Int) {
        when (type) {
            Ev.KICK, Ev.PASS, Ev.SHOT, Ev.HEADER -> play("kick", 0.7f)
            Ev.WHISTLE, Ev.HALF_TIME, Ev.FULL_TIME, Ev.FOUL, Ev.OFFSIDE -> play("whistle")
            Ev.GOAL, Ev.DRILL_SUCCESS -> play("goal")
            Ev.POST -> play("post")
            Ev.SAVE, Ev.CATCH -> play("save")
            Ev.TACKLE, Ev.TACKLE_WON -> play("tackle", 0.6f)
        }
    }

    fun release() { pool?.release() }
}
