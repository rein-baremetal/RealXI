package com.realxi.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.composed
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.realxi.data.AppSettings
import com.realxi.data.CoachStore
import com.realxi.data.Content
import com.realxi.data.DrillInfo
import com.realxi.data.Weakness

/** Click handler without a ripple (used for full-screen overlays that must swallow touches). */
fun Modifier.clickableNoRipple(onClick: () -> Unit): Modifier = composed {
    clickable(interactionSource = remember { MutableInteractionSource() }, indication = null, onClick = onClick)
}

/** One coach finding with its Train button. Shared by the home screen and the full-time panel. */
@Composable
fun WeaknessRow(w: Weakness, drill: DrillInfo?, onTrain: (DrillInfo) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f).padding(end = 10.dp)) {
            Text(w.title, color = Ios.Label, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Text(w.detail, color = Ios.Secondary, fontSize = 13.sp)
        }
        if (drill != null) IosTag("Train: ${drill.title}", Ios.Blue) { onTrain(drill) }
    }
}

@Composable
fun HomeScreen(
    coach: CoachStore, drills: List<DrillInfo>,
    onQuickMatch: () -> Unit, onPractice: () -> Unit, onSettings: () -> Unit, onTrain: (DrillInfo) -> Unit, onCareer: () -> Unit,
) {
    val focus = remember(coach.matchesPlayed, coach.drillRuns) { coach.focus(drills, 2) }
    IosScreen("RealXI", null) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val wide = maxWidth >= 560.dp
            val left: @Composable () -> Unit = {
                Text("Practice arena vs the System. Offline. Built to sharpen your game.", color = Ios.Secondary, fontSize = 15.sp)
                IosGroup("Your progress") {
                    StatLine("Matches played", "${coach.matchesPlayed}")
                    IosDivider()
                    StatLine("Drill runs", "${coach.drillRuns}")
                    IosDivider()
                    StatLine("Practice streak", "${coach.streakDays} day${if (coach.streakDays == 1) "" else "s"}")
                }
            }
            val right: @Composable () -> Unit = {
                IosGroup("Coach") {
                    if (focus.isEmpty()) {
                        Text(
                            if (coach.matchesPlayed + coach.drillRuns == 0) "Play a match or a drill. The coach finds your weak spots and the System will keep testing them."
                            else "No weak spot stands out. Raise the System level to find the next one.",
                            color = Ios.Secondary, fontSize = 14.sp, modifier = Modifier.padding(14.dp),
                        )
                    } else {
                        focus.forEachIndexed { i, w ->
                            if (i > 0) IosDivider()
                            WeaknessRow(w, drills.firstOrNull { it.id == w.drillId }, onTrain)
                        }
                    }
                }
                IosGroup {
                    IosNavRow("Match vs System", "1 to 5 minute halves", onClick = onQuickMatch)
                    IosDivider()
                    IosNavRow("Practice arena", "${drills.size} drills", onClick = onPractice)
                    IosDivider()
                    IosNavRow("Career", "Record vs each System level, drill mastery", onClick = onCareer)
                    IosDivider()
                    IosNavRow("Settings", onClick = onSettings)
                }
            }
            if (wide) {
                Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                    Column(Modifier.weight(0.8f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(16.dp)) { left() }
                    Column(Modifier.weight(1.2f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(16.dp)) { right() }
                }
            } else {
                Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(16.dp)) { right(); left() }
            }
        }
    }
}

@Composable
private fun StatLine(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 11.dp)) {
        Text(label, color = Ios.Label, fontSize = 16.sp, modifier = Modifier.weight(1f))
        Text(value, color = Ios.Secondary, fontSize = 16.sp)
    }
}

@Composable
fun PracticeScreen(drills: List<DrillInfo>, coach: CoachStore, onDrill: (DrillInfo) -> Unit, onBack: () -> Unit) {
    val picks = remember(coach.drillRuns, coach.matchesPlayed) { coach.focus(drills, 3).map { it.drillId }.toSet() }
    IosScreen("Practice arena", onBack) {
        LazyVerticalGrid(
            columns = GridCells.Adaptive(260.dp), horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxSize(),
        ) {
            items(drills.sortedByDescending { it.id in picks }) { d ->
                val rate = coach.drillRate(d.id)
                Column(
                    Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(Ios.Card).clickable { onDrill(d) }.padding(14.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(d.title, color = Ios.Label, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                        if (d.id in picks) IosTag("Coach pick", Ios.Orange)
                    }
                    Text(d.blurb, color = Ios.Secondary, fontSize = 13.sp)
                    Text(
                        if (rate < 0f) "Goal: ${d.goal} · ${d.attempts} attempts · not tried yet"
                        else "Goal: ${d.goal} · best ${coach.drillBest(d.id)}/${d.attempts} · ${(rate * 100).toInt()}% overall",
                        color = Ios.Blue, fontSize = 12.sp,
                    )
                }
            }
        }
    }
}

/** Setup for a full match (drill == null) or for a drill's System strength. Kick off stays pinned at the bottom. */
@Composable
fun SetupScreen(settings: AppSettings, drill: DrillInfo?, onStart: () -> Unit, onBack: () -> Unit) {
    IosScreen(if (drill == null) "Match setup" else drill.title, onBack) {
        Column(Modifier.fillMaxSize()) {
            BoxWithConstraints(Modifier.weight(1f).fillMaxWidth()) {
                val wide = maxWidth >= 560.dp
                val left: @Composable () -> Unit = {
                    if (drill != null) Text(drill.blurb, color = Ios.Secondary, fontSize = 14.sp)
                    IosGroup("System") {
                        IosSegRow("Level", Content.tiers.map { it.label }, settings.difficulty.coerceIn(0, 3), Content.tiers[settings.difficulty.coerceIn(0, 3)].blurb) { settings.difficulty = it }
                        if (drill == null) {
                            IosDivider()
                            IosPillRow("System formation", Content.formations, settings.formationAway) { settings.formationAway = it }
                        }
                    }
                    if (drill == null) {
                        IosGroup("Your team") {
                            IosPillRow("Formation", Content.formations, settings.formationHome) { settings.formationHome = it }
                        }
                        IosGroup("Match") {
                            IosSegRow("Half length", Content.halfLengthsSeconds.map { "${it / 60} min" },
                                Content.halfLengthsSeconds.indexOf(settings.halfSeconds).coerceAtLeast(0)) { settings.halfSeconds = Content.halfLengthsSeconds[it] }
                            IosDivider()
                            IosSegRow("Referee", Content.referees, settings.referee) { settings.referee = it }
                        }
                    }
                }
                val right: @Composable () -> Unit = {
                    IosGroup("Rules and help") {
                        if (drill == null) {
                            IosToggleRow("Offside", settings.offside) { settings.offside = it }
                            IosDivider()
                            IosToggleRow("Fouls and cards", settings.fouls) { settings.fouls = it }
                            IosDivider()
                        }
                        IosToggleRow("System adapts to my habits", settings.adaptive) { settings.adaptive = it }
                        IosDivider()
                        IosSliderRow("Aim assist", settings.assist, "${(settings.assist * 100).toInt()}%") { settings.assist = it }
                    }
                    Text("The System learns where you attack, how you finish and how you pass, then goes after it. Beat that and you improve.",
                        color = Ios.Secondary, fontSize = 12.sp, modifier = Modifier.padding(horizontal = 14.dp))
                }
                if (wide) {
                    Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.spacedBy(20.dp)) {
                        Column(Modifier.weight(1.2f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(14.dp)) { left() }
                        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(14.dp)) { right() }
                    }
                } else {
                    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(14.dp)) { left(); right() }
                }
            }
            IosButton(if (drill == null) "Kick off" else "Start drill", onStart, Modifier.fillMaxWidth().padding(top = 8.dp, bottom = 4.dp))
        }
    }
}

@Composable
fun SettingsScreen(settings: AppSettings, onHelp: () -> Unit, onBack: () -> Unit) {
    IosScreen("Settings", onBack) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            IosGroup("Feel") {
                IosToggleRow("Vibration (goals)", settings.haptics) { settings.haptics = it }
                IosDivider()
                IosToggleRow("Sound effects", settings.sound) { settings.sound = it }
                IosDivider()
                IosToggleRow("Mirror controls (stick on the right)", settings.mirrorControls) { settings.mirrorControls = it }
            }
            IosGroup("Controls") {
                IosSliderRow("Button size", (settings.buttonScale - 0.8f) / 0.45f, "${(settings.buttonScale * 100).toInt()}%") { settings.buttonScale = 0.8f + 0.45f * it }
                IosDivider()
                IosSliderRow("Button opacity", (settings.buttonOpacity - 0.4f) / 0.6f, "${(settings.buttonOpacity * 100).toInt()}%") { settings.buttonOpacity = 0.4f + 0.6f * it }
                IosDivider()
                IosSliderRow("Stick dead zone", settings.deadZone / 0.35f, "${(settings.deadZone * 100).toInt()}%") { settings.deadZone = 0.35f * it }
                IosDivider()
                IosToggleRow("Large buttons preset", settings.buttonScale >= 1.2f) { settings.applyLargeButtons(it) }
                IosDivider()
                IosNavRow("Controller help", "What every button does, with and without the ball", onClick = onHelp)
                IosDivider()
                IosNavRow("Show first-run tips again", onClick = { settings.tipsSeen = false })
            }
            IosGroup("Training") {
                IosToggleRow("Show System debug overlay by default", settings.showAiDebug) { settings.showAiDebug = it }
                IosDivider()
                IosToggleRow("System adapts to my habits", settings.adaptive) { settings.adaptive = it }
                IosDivider()
                IosSliderRow("Aim assist", settings.assist, "${(settings.assist * 100).toInt()}%") { settings.assist = it }
            }
            Text(
                "RealXI is fully offline: it has no network permission, no accounts and no analytics. Your coach notes stay on this device.",
                color = Ios.Secondary, fontSize = 12.sp, modifier = Modifier.padding(horizontal = 14.dp),
            )
            Spacer(Modifier.height(12.dp))
        }
    }
}


@Composable
private fun HelpRow(title: String, body: String) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 9.dp)) {
        Text(title, color = Ios.Label, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
        Text(body, color = Ios.Secondary, fontSize = 13.sp)
    }
}

/** Every control, by situation. Mirrors the in-match button roles in Controls.kt (roleFor). */
@Composable
fun ControllerHelpScreen(onBack: () -> Unit) {
    IosScreen("Controller help", onBack) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text("Buttons change by themselves: green when you have the ball, blue when you do not.", color = Ios.Secondary, fontSize = 14.sp)
            IosGroup("With the ball (attacking)") {
                HelpRow("DASH", "Hold to sprint into open space. Release to tighten control before turning or passing.")
                IosDivider()
                HelpRow("PASS", "Tap for a direct ground pass to the team-mate you aim at. Hold longer for a firmer pass.")
                IosDivider()
                HelpRow("THROUGH", "Sends the ball into space ahead of a runner. Hold for power.")
                IosDivider()
                HelpRow("LOFT", "Lofted pass or cross into the box. In your own third the same button is CLEAR and boots the ball away from danger.")
                IosDivider()
                HelpRow("SHOOT", "Hold to charge, release to strike. Wide-angle shots bend toward goal when aim assist is on.")
                IosDivider()
                HelpRow("SKILL", "Sharp touch: pushes the ball past a defender. Pick the move in the pause menu.")
                IosDivider()
                HelpRow("1-2", "Tap, then PASS: the passer sprints into the channel for the return ball (pass-and-run).")
            }
            IosGroup("Without the ball (defending)") {
                HelpRow("PRESS", "Hold to close the ball carrier down goal-side. Close up you slow to a jockey. Do not hold it all match or you drag your defence out of shape.")
                IosDivider()
                HelpRow("TEAM PRESS", "Sends nearby team-mates to close the ball down for a few seconds (call for pressure).")
                IosDivider()
                HelpRow("SWITCH", "Selects the defender closest to the ball or the passing lane.")
                IosDivider()
                HelpRow("TACKLE", "Tap for a stand tackle when close, hold for a slide. A mistimed tackle is a foul or lets the carrier past.")
                IosDivider()
                HelpRow("DASH", "Sprint to recover your shape or reach the ball first.")
            }
            IosGroup("Good habits") {
                HelpRow("Fighting for the ball", "Players cannot stop and turn instantly. Run at the ball and reach for it: it will not pass through a body.")
                IosDivider()
                HelpRow("Goalkeepers", "Keepers stay in their box and collect easy shots. Place shots away from them, not at them.")
            }
            Spacer(Modifier.height(12.dp))
        }
    }
}

/** Long-term, on-device career summary. */
@Composable
fun CareerScreen(coach: CoachStore, drills: List<DrillInfo>, onBack: () -> Unit) {
    IosScreen("Career", onBack) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            IosGroup("Overview") {
                StatLine("Matches played", "${coach.matchesPlayed}")
                IosDivider()
                StatLine("Best winning streak", "${coach.bestWinStreak}")
                IosDivider()
                StatLine("Drill runs", "${coach.drillRuns}")
                IosDivider()
                StatLine("Practice streak", "${coach.streakDays} day${if (coach.streakDays == 1) "" else "s"}")
            }
            IosGroup("Record vs the System") {
                Content.tiers.forEachIndexed { i, t ->
                    if (i > 0) IosDivider()
                    val r = coach.tierRecord(i)
                    val played = r[0] + r[1] + r[2]
                    val rate = if (played == 0) "-" else "${(100f * r[0] / played).toInt()}% wins"
                    Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp)) {
                        Row(Modifier.fillMaxWidth()) {
                            Text(t.label, color = Ios.Label, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                            Text(rate, color = Ios.Blue, fontSize = 15.sp)
                        }
                        Text("W ${r[0]}  D ${r[1]}  L ${r[2]}   goals ${r[3]}-${r[4]}", color = Ios.Secondary, fontSize = 13.sp)
                    }
                }
            }
            IosGroup("Drill mastery") {
                drills.forEachIndexed { i, d ->
                    if (i > 0) IosDivider()
                    val rate = coach.drillRate(d.id)
                    Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(d.title, color = Ios.Label, fontSize = 16.sp)
                            Text(
                                if (rate < 0f) "Not tried yet" else "${coach.drillRunsOf(d.id)} runs · ${(rate * 100).toInt()}% · best ${coach.drillBest(d.id)}/${d.attempts} · streak ${coach.drillStreakBest(d.id)}",
                                color = Ios.Secondary, fontSize = 12.sp,
                            )
                        }
                        val m = coach.drillMastery(d.id)
                        IosTag(m, when (m) { "Master" -> Ios.Green; "Pro" -> Ios.Blue; "Regular" -> Ios.Orange; else -> Ios.Secondary })
                    }
                }
            }
            Text("Everything here is stored on this device only.", color = Ios.Secondary, fontSize = 12.sp, modifier = Modifier.padding(horizontal = 14.dp))
            Spacer(Modifier.height(12.dp))
        }
    }
}
