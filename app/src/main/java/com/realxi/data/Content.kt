package com.realxi.data

import android.content.Context
import org.json.JSONObject

/** System difficulty tiers (indices match the engine's aiProfile table). */
data class Tier(val label: String, val blurb: String)

/** One practice drill, loaded from assets/data/drills.json (ids match engine/src/drills.cpp). */
data class DrillInfo(
    val id: Int,
    val title: String,
    val blurb: String,
    val goal: String,
    val attempts: Int,
    val infiniteStamina: Boolean,
)

object Content {
    val tiers = listOf(
        Tier("Regular", "Warm-up pace. Loose marking, slow reactions."),
        Tier("Hard", "Default. Presses, marks goal-side, plays through balls, hunts your weak channel."),
        Tier("Pro", "Sharp reactions, offside trap, lane-cutting midfield, keeper reads spin."),
        Tier("Legend", "Near-perfect positioning and execution. No cheating, just no mistakes."),
    )

    /** Same order as engine/src/formations_table.inc (generated from assets/data/formations.json). */
    val formations = listOf("4-4-2", "4-3-3", "4-2-3-1", "3-5-2", "5-3-2", "4-1-4-1", "3-4-3", "3-4-2-1", "3-4-1-2", "3-1-4-2", "4-1-2-1-2", "4-1-3-2", "4-2-2-2", "4-3-1-2", "4-3-2-1", "4-4-1-1", "4-5-1", "4-2-4", "5-2-3", "5-2-1-2", "5-4-1")

    val referees = listOf("Lenient", "Standard", "Strict")

    /** Skill move ids selectable in the pause menu (engine skill indices 0..4). */
    val skills = listOf("Step Over", "Body Feint", "Quick Turn", "Drag Back", "Fake Shot")

    val halfLengthsSeconds = listOf(60, 120, 180, 240, 300)

    fun parseDrills(json: String): List<DrillInfo> {
        val arr = JSONObject(json).getJSONArray("drills")
        return List(arr.length()) { i ->
            val o = arr.getJSONObject(i)
            DrillInfo(
                id = o.getInt("id"),
                title = o.getString("title"),
                blurb = o.getString("blurb"),
                goal = o.getString("goal"),
                attempts = o.optInt("attempts", 10),
                infiniteStamina = o.optBoolean("infinite_stamina", false),
            )
        }
    }

    fun loadDrills(context: Context): List<DrillInfo> =
        context.assets.open("data/drills.json").bufferedReader().use { parseDrills(it.readText()) }
}
