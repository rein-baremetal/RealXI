package com.realxi.data

import android.content.Context
import android.content.SharedPreferences

/** Final-whistle numbers for the human side (team 0) and the System (team 1). */
data class MatchStats(
    val shots: Int, val onTarget: Int, val passes: Int, val passesOk: Int, val tackles: Int, val tacklesWon: Int,
    val fouls: Int, val offsides: Int, val xg: Float, val goals: Int,
    val oppXg: Float, val oppShots: Int, val possessionShare: Float,
)

/** A recurring weak spot and the drill that trains it. [severity] is 0..1. */
data class Weakness(val key: String, val title: String, val detail: String, val drillId: Int, val severity: Float)

/**
 * Practice coach. Every finished match and drill updates a slowly-moving average per skill, so a one-off bad game does not
 * label the player but a repeated problem rises to the top of the home screen. All data stays on the device.
 */
class CoachStore(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("realxi_coach", Context.MODE_PRIVATE)

    val matchesPlayed: Int get() = prefs.getInt("matches", 0)
    val drillRuns: Int get() = prefs.getInt("drill_runs", 0)
    val streakDays: Int get() = prefs.getInt("streak_days", 0)

    private fun ema(key: String) = prefs.getFloat("ema_$key", 0f)
    private fun setEma(e: SharedPreferences.Editor, key: String, sev: Float) {
        val old = if (prefs.contains("ema_$key")) ema(key) else sev
        e.putFloat("ema_$key", 0.6f * old + 0.4f * sev)
    }

    /** Career record against one System tier (0..3): wins, draws, losses, goals for, goals against. */
    fun tierRecord(tier: Int): IntArray = intArrayOf(
        prefs.getInt("t${tier}_w", 0), prefs.getInt("t${tier}_d", 0), prefs.getInt("t${tier}_l", 0),
        prefs.getInt("t${tier}_gf", 0), prefs.getInt("t${tier}_ga", 0),
    )
    val bestWinStreak: Int get() = prefs.getInt("best_win_streak", 0)
    val currentWinStreak: Int get() = prefs.getInt("win_streak", 0)

    /** Called once when a match ends. [tier] is the System level (0..3) and [goalsAgainst] the System's goals. */
    fun recordMatch(s: MatchStats, tier: Int = 1, goalsAgainst: Int = 0) {
        val e = prefs.edit()
        val t = tier.coerceIn(0, 3)
        val key = if (s.goals > goalsAgainst) "w" else if (s.goals == goalsAgainst) "d" else "l"
        e.putInt("t${t}_$key", prefs.getInt("t${t}_$key", 0) + 1)
        e.putInt("t${t}_gf", prefs.getInt("t${t}_gf", 0) + s.goals)
        e.putInt("t${t}_ga", prefs.getInt("t${t}_ga", 0) + goalsAgainst)
        val streak = if (key == "w") currentWinStreak + 1 else 0
        e.putInt("win_streak", streak)
        e.putInt("best_win_streak", maxOf(bestWinStreak, streak))
        for (w in analyse(s)) setEma(e, w.key, w.severity)
        e.putInt("matches", matchesPlayed + 1)
        touchDay(e)
        e.apply()
    }

    /** Called once when a drill ends. [hits] of [total] attempts, [bestStreak] in a row. */
    fun recordDrill(id: Int, hits: Int, total: Int, bestStreak: Int) {
        if (total <= 0) return
        val e = prefs.edit()
        e.putInt("d${id}_runs", drillRunsOf(id) + 1)
        e.putInt("d${id}_hits", prefs.getInt("d${id}_hits", 0) + hits)
        e.putInt("d${id}_total", prefs.getInt("d${id}_total", 0) + total)
        e.putInt("d${id}_best", maxOf(prefs.getInt("d${id}_best", 0), hits))
        e.putInt("d${id}_streak", maxOf(prefs.getInt("d${id}_streak", 0), bestStreak))
        e.putInt("drill_runs", drillRuns + 1)
        val rate = hits.toFloat() / total
        setEma(e, "drill$id", ((0.7f - rate) / 0.5f).coerceIn(0f, 1f))
        touchDay(e)
        e.apply()
    }

    fun drillStreakBest(id: Int) = prefs.getInt("d${id}_streak", 0)

    /** Mastery from lifetime hit rate and number of runs: Unrated, Rookie, Regular, Pro, Master. */
    fun drillMastery(id: Int): String {
        val runs = drillRunsOf(id); val rate = drillRate(id)
        return when {
            runs == 0 || rate < 0f -> "Unrated"
            runs >= 6 && rate >= 0.85f -> "Master"
            runs >= 4 && rate >= 0.70f -> "Pro"
            runs >= 2 && rate >= 0.50f -> "Regular"
            else -> "Rookie"
        }
    }

    fun drillRunsOf(id: Int) = prefs.getInt("d${id}_runs", 0)
    fun drillBest(id: Int) = prefs.getInt("d${id}_best", 0)
    fun drillRate(id: Int): Float {
        val t = prefs.getInt("d${id}_total", 0)
        return if (t == 0) -1f else prefs.getInt("d${id}_hits", 0).toFloat() / t
    }

    private fun touchDay(e: SharedPreferences.Editor) {
        val today = (System.currentTimeMillis() / 86_400_000L).toInt()
        val last = prefs.getInt("last_day", -1)
        if (last == today) return
        e.putInt("streak_days", if (last == today - 1) streakDays + 1 else 1)
        e.putInt("last_day", today)
    }

    /** Weak spots ranked by their long-run average (needs at least a little history to speak up). */
    fun focus(drills: List<DrillInfo>, limit: Int = 3): List<Weakness> {
        val out = ArrayList<Weakness>()
        for ((key, meta) in META) {
            if (!prefs.contains("ema_$key")) continue
            val sev = ema(key)
            if (sev < 0.22f) continue
            out += Weakness(key, meta.title, meta.longRun, meta.drill, sev)
        }
        for (d in drills) {
            val key = "drill${d.id}"
            if (!prefs.contains("ema_$key")) continue
            val sev = ema(key)
            if (sev < 0.22f) continue
            val rate = (drillRate(d.id) * 100).toInt().coerceAtLeast(0)
            out += Weakness(key, d.title, "You hit $rate% over ${drillRunsOf(d.id)} runs. Aim for 70%.", d.id, sev)
        }
        return out.sortedByDescending { it.severity }.take(limit)
    }

    companion object {
        private class Meta(val title: String, val longRun: String, val drill: Int)

        // Drill ids match assets/data/drills.json.
        private val META = linkedMapOf(
            "passing" to Meta("Passing under pressure", "Too many passes are being lost. Play to the safe foot, move after you pass.", 6),
            "finishing" to Meta("Finishing accuracy", "Shots keep missing the target. Aim for a corner and pick your moment.", 5),
            "chances" to Meta("Creating chances", "You are not getting shots off. Break lines with THRU balls and run at the System.", 3),
            "offside" to Meta("Offside timing", "You are caught offside often. Release the ball before the runner crosses the line.", 3),
            "discipline" to Meta("Reckless tackling", "Fouls are giving the System set pieces. Jockey first, tackle late.", 7),
            "defending" to Meta("Defending the box", "The System is getting good chances. Stay goal-side and press with cover.", 7),
            "possession" to Meta("Keeping the ball", "The System has most of the ball. Use triangles and release early.", 6),
        )

        /** Severity per skill for one match. Needs a minimum sample before it will judge. */
        fun analyse(s: MatchStats): List<Weakness> {
            fun w(key: String, sev: Float, detail: String): Weakness {
                val m = META.getValue(key)
                return Weakness(key, m.title, detail, m.drill, sev.coerceIn(0f, 1f))
            }
            val out = ArrayList<Weakness>()
            if (s.passes >= 10) {
                val acc = s.passesOk.toFloat() / s.passes
                out += w("passing", (0.80f - acc) / 0.30f, "Only ${(acc * 100).toInt()}% of your passes found a team-mate.")
            }
            if (s.shots >= 3) {
                val acc = s.onTarget.toFloat() / s.shots
                var sev = (0.50f - acc) / 0.35f
                if (s.goals < s.xg - 0.7f) sev += 0.3f
                out += w("finishing", sev, "${s.onTarget} of ${s.shots} shots were on target.")
            }
            if (s.possessionShare >= 0.40f) out += w("chances", (3 - s.shots) / 3f * 0.9f, "You only managed ${s.shots} shots.")
            out += w("offside", s.offsides / 4f, "Caught offside ${s.offsides} times.")
            if (s.fouls >= 2) out += w("discipline", (s.fouls - 1) / 5f, "${s.fouls} fouls conceded.")
            if (s.tackles >= 4) {
                val win = s.tacklesWon.toFloat() / s.tackles
                out += w("defending", maxOf((0.45f - win) / 0.35f, (s.oppXg - 0.6f) / 1.5f), "You won ${s.tacklesWon} of ${s.tackles} tackles; the System created ${"%.1f".format(s.oppXg)} xG.")
            } else {
                out += w("defending", (s.oppXg - 0.6f) / 1.5f, "The System created ${"%.1f".format(s.oppXg)} xG.")
            }
            out += w("possession", (0.45f - s.possessionShare) / 0.2f, "You had ${(s.possessionShare * 100).toInt()}% of the ball.")
            return out.filter { it.severity > 0.22f }.sortedByDescending { it.severity }
        }
    }
}
