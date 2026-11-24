package com.realxi.data

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.realxi.engine.MatchConfig
import org.json.JSONObject

/** Player settings, persisted in SharedPreferences. Every field is Compose state, so screens update live. */
class AppSettings(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("realxi_settings", Context.MODE_PRIVATE)

    var difficulty by pref("difficulty", 1)
    var formationHome by pref("formation_home", 0)
    var formationAway by pref("formation_away", 0)
    var halfSeconds by pref("half_seconds", 180)
    var referee by pref("referee", 1)
    var assist by pref("assist", 0.25f)
    var offside by pref("offside", true)
    var fouls by pref("fouls", true)
    var adaptive by pref("adaptive", true)
    var showAiDebug by pref("ai_debug", false)
    var haptics by pref("haptics", true)
    var sound by pref("sound", true)
    var mirrorControls by pref("mirror_controls", false)
    var selectedSkill by pref("skill", 0)
    var buttonScale by pref("button_scale", 1.0f)        // 0.8 .. 1.25
    var buttonOpacity by pref("button_opacity", 1.0f)    // 0.4 .. 1.0
    var deadZone by pref("dead_zone", 0.12f)             // stick dead zone 0 .. 0.35
    var tipsSeen by pref("tips_seen", false)

    /** Accessibility preset: bigger, more opaque buttons and a wider dead zone for larger hands. */
    fun applyLargeButtons(on: Boolean) {
        buttonScale = if (on) 1.25f else 1.0f
        buttonOpacity = if (on) 1.0f else buttonOpacity
        deadZone = if (on) 0.18f else 0.12f
    }

    fun matchConfig(): MatchConfig = MatchConfig(
        difficulty = difficulty, formationHome = formationHome, formationAway = formationAway,
        halfSeconds = halfSeconds.toFloat(), referee = referee, assist = assist,
        offside = offside, fouls = fouls, adaptive = adaptive,
    )

    fun drillConfig(drill: DrillInfo): MatchConfig = MatchConfig(
        drillId = drill.id, difficulty = difficulty, formationHome = formationHome, formationAway = formationAway,
        referee = referee, assist = assist, offside = false, fouls = false, adaptive = adaptive,
        infiniteStamina = drill.infiniteStamina, drillAttempts = drill.attempts,
    )

    private fun pref(key: String, default: Int) = PrefDelegate(
        mutableStateOf(prefs.getInt(key, default)),
    ) { prefs.edit().putInt(key, it).apply() }

    private fun pref(key: String, default: Float) = PrefDelegate(
        mutableStateOf(prefs.getFloat(key, default)),
    ) { prefs.edit().putFloat(key, it).apply() }

    private fun pref(key: String, default: Boolean) = PrefDelegate(
        mutableStateOf(prefs.getBoolean(key, default)),
    ) { prefs.edit().putBoolean(key, it).apply() }
}

class PrefDelegate<T>(private val state: androidx.compose.runtime.MutableState<T>, private val save: (T) -> Unit) {
    operator fun getValue(thisRef: Any?, property: kotlin.reflect.KProperty<*>): T = state.value
    operator fun setValue(thisRef: Any?, property: kotlin.reflect.KProperty<*>, value: T) {
        state.value = value
        save(value)
    }
}

data class StickSpec(val x: Float, val y: Float, val radius: Float)
data class ButtonSpec(val action: String, val x: Float, val y: Float, val size: Float)
data class ControlLayout(val stick: StickSpec, val buttons: List<ButtonSpec>)

object ControlLayoutParser {
    fun parse(json: String): ControlLayout {
        val root = JSONObject(json)
        val s = root.getJSONObject("stick")
        val arr = root.getJSONArray("buttons")
        return ControlLayout(
            StickSpec(s.getDouble("x").toFloat(), s.getDouble("y").toFloat(), s.getDouble("radius").toFloat()),
            List(arr.length()) { i ->
                val b = arr.getJSONObject(i)
                ButtonSpec(b.getString("action"), b.getDouble("x").toFloat(), b.getDouble("y").toFloat(), b.getDouble("size").toFloat())
            },
        )
    }

    fun load(context: Context): ControlLayout =
        context.assets.open("data/default_control_layout.json").bufferedReader().use { parse(it.readText()) }
}
