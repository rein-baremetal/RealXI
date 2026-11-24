package com.realxi.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.realxi.data.AppSettings
import com.realxi.data.CoachStore
import com.realxi.data.MatchStats
import com.realxi.data.Weakness
import com.realxi.data.Content
import com.realxi.data.ControlLayout
import com.realxi.data.DrillInfo
import com.realxi.engine.Cmd
import com.realxi.engine.EngineSession
import com.realxi.engine.Ev
import com.realxi.engine.MatchConfig
import com.realxi.engine.Phase
import com.realxi.engine.Snap
import com.realxi.ui.theme.RxColors
import kotlin.math.roundToInt

/** Values shown in the HUD, refreshed about ten times a second (equality keeps recomposition cheap). */
data class Hud(
    val home: Int = 0, val away: Int = 0, val clock: String = "00:00", val half: Int = 1, val phase: Int = 0,
    val replaying: Boolean = false, val replayProgress: Float = 0f, val stamina: Float = 1f,
    val drillAttempt: Int = 0, val drillMax: Int = 0, val drillSuccess: Int = 0, val drillFail: Int = 0,
    val drillLast: Int = 0, val drillTimeLeft: Float = 0f, val drillStreak: Int = 0, val drillBest: Int = 0,
    val drillComplete: Boolean = false, val endPending: Boolean = false,
)

private fun readHud(s: EngineSession): Hud {
    val t = s.f(Snap.CLOCK)
    val mm = (t / 60f).toInt()
    val ss = (t % 60f).toInt()
    return Hud(
        home = s.i(Snap.SCORE_HOME), away = s.i(Snap.SCORE_AWAY),
        clock = "%02d:%02d".format(mm, ss), half = s.i(Snap.HALF), phase = s.i(Snap.PHASE),
        replaying = s.i(Snap.REPLAYING) == 1, replayProgress = s.f(Snap.REPLAY_PROGRESS),
        stamina = s.f(Snap.ACTIVE_STAMINA),
        drillAttempt = s.i(Snap.DRILL_ATTEMPT), drillMax = s.i(Snap.DRILL_ATTEMPTS_MAX),
        drillSuccess = s.i(Snap.DRILL_SUCCESS), drillFail = s.i(Snap.DRILL_FAIL), drillLast = s.i(Snap.DRILL_LAST),
        drillTimeLeft = s.f(Snap.DRILL_TIME_LEFT), drillStreak = s.i(Snap.DRILL_STREAK), drillBest = s.i(Snap.DRILL_BEST),
        drillComplete = s.i(Snap.DRILL_COMPLETE) == 1, endPending = s.i(Snap.END_PENDING) == 1,
    )
}

private fun eventToast(type: Int, arg: Int = 0): String? = when (type) {
    Ev.GOAL -> "GOAL!"
    Ev.FOUL -> "Foul"
    Ev.OFFSIDE -> "Offside"
    Ev.YELLOW -> "Yellow card"
    Ev.RED -> "Red card"
    Ev.CORNER -> "Corner"
    Ev.THROW_IN -> "Throw-in"
    Ev.GOAL_KICK -> "Goal kick"
    Ev.SAVE -> "Save!"
    Ev.POST -> "Off the woodwork!"
    Ev.HALF_TIME -> "Half time"
    Ev.FULL_TIME -> "Full time"
    // arg = attempt * 10 + reason (1 goal, 2 saved, 3 missed, 4 lost the ball, 5 time up, 6 escaped, 7 held, 8 conceded)
    Ev.DRILL_SUCCESS -> when (arg % 10) { 1 -> "Goal!"; 6 -> "Escaped the press!"; 7 -> "Held firm!"; else -> "Success" }
    Ev.DRILL_FAIL -> when (arg % 10) { 2 -> "Saved"; 3 -> "Missed the target"; 4 -> "Lost the ball"; 5 -> "Time up"; 8 -> "Conceded"; else -> "Failed" }
    else -> null
}

@Composable
fun MatchScreen(
    startConfig: MatchConfig,
    settings: AppSettings,
    drill: DrillInfo?,
    layout: ControlLayout,
    coach: CoachStore,
    drills: List<DrillInfo>,
    onTrain: (DrillInfo) -> Unit,
    onExit: () -> Unit,
) {
    val context = LocalContext.current
    var config by remember(startConfig) { mutableStateOf(startConfig) }
    val session = remember(config) { EngineSession(config) }
    DisposableEffect(session) { onDispose { session.close() } }
    val view = LocalView.current
    DisposableEffect(view) { view.keepScreenOn = true; onDispose { view.keepScreenOn = false } }
    val controls = remember { ControlState() }
    val camera = remember(session) { CameraState() }
    val sprites = remember { SpriteAtlas.load(context) }
    val drawer = remember { SceneDrawer(sprites) }
    val sfx = remember { SoundFx(context) }
    DisposableEffect(sfx) { onDispose { sfx.release() } }
    val haptic = LocalHapticFeedback.current
    val tick = remember(session) { mutableLongStateOf(0L) }
    var hud by remember(session) { mutableStateOf(Hud()) }
    var paused by remember(session) { mutableStateOf(false) }
    var toast by remember(session) { mutableStateOf<String?>(null) }
    var toastUntil by remember(session) { mutableLongStateOf(0L) }
    var slowMo by remember(session) { mutableStateOf(1f) }

    LaunchedEffect(session) {
        var last = 0L
        var hudTimer = 1f
        session.command(Cmd.SELECT_SKILL, settings.selectedSkill.toFloat())
        while (true) {
            val now = androidx.compose.runtime.withFrameNanos { it }
            val dt = if (last == 0L) 1f / 60f else ((now - last) / 1e9f).coerceAtMost(0.05f)
            last = now
            session.setInput(
                controls.moveX, controls.moveY,
                controls.isOn(ButtonId.SPRINT), controls.isOn(ButtonId.PASS), controls.isOn(ButtonId.SHOOT),
                controls.isOn(ButtonId.LOB) || controls.isOn(ButtonId.LOFT), controls.isOn(ButtonId.TACKLE),
                controls.isOn(ButtonId.FINESSE) || controls.isOn(ButtonId.LOFT),
                controls.isOn(ButtonId.CALL), controls.isOn(ButtonId.SWITCH), controls.isOn(ButtonId.SKILL),
            )
            controls.endFrame()
            session.step(dt)
            session.forEachEvent { type, _, _, arg ->
                if (settings.sound) sfx.onEvent(type)
                if (settings.haptics && (type == Ev.GOAL || type == Ev.DRILL_SUCCESS)) haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                val msg = eventToast(type, arg)
                if (msg != null) { toast = msg; toastUntil = now + 1_400_000_000L }
            }
            camera.follow(session.f(Snap.BALL_X), session.f(Snap.BALL_VX), dt)
            tick.longValue = tick.longValue + 1
            hudTimer += dt
            if (hudTimer > 0.1f) { hudTimer = 0f; hud = readHud(session) }
            if (toast != null && now > toastUntil) toast = null
        }
    }

    fun setPaused(p: Boolean) {
        paused = p
        if (p) controls.reset()
        session.command(Cmd.PAUSE, if (p) 1f else 0f)
    }
    BackHandler { if (!paused) setPaused(true) else onExit() }

    val ended = hud.phase == Phase.FULL_TIME || (config.isDrill && hud.drillComplete)
    var findings by remember(session) { mutableStateOf<List<Weakness>>(emptyList()) }
    LaunchedEffect(session, ended) {
        if (!ended) return@LaunchedEffect
        if (config.isDrill && drill != null) {
            coach.recordDrill(drill.id, hud.drillSuccess, hud.drillSuccess + hud.drillFail, hud.drillBest)
        } else {
            val p0 = session.stat(0, Snap.S_POSSESSION); val p1 = session.stat(1, Snap.S_POSSESSION)
            val ms = MatchStats(
                shots = session.stat(0, Snap.S_SHOTS).toInt(), onTarget = session.stat(0, Snap.S_ON_TARGET).toInt(),
                passes = session.stat(0, Snap.S_PASSES).toInt(), passesOk = session.stat(0, Snap.S_PASSES_OK).toInt(),
                tackles = session.stat(0, Snap.S_TACKLES).toInt(), tacklesWon = session.stat(0, Snap.S_TACKLES_WON).toInt(),
                fouls = session.stat(0, Snap.S_FOULS).toInt(), offsides = session.stat(0, Snap.S_OFFSIDES).toInt(),
                xg = session.stat(0, Snap.S_XG), goals = hud.home,
                oppXg = session.stat(1, Snap.S_XG), oppShots = session.stat(1, Snap.S_SHOTS).toInt(),
                possessionShare = p0 / (p0 + p1).coerceAtLeast(1f),
            )
            findings = CoachStore.analyse(ms)
            coach.recordMatch(ms, config.difficulty, hud.away)
        }
    }

    Box(Modifier.fillMaxSize().background(RxColors.Night)) {
        Canvas(Modifier.fillMaxSize()) {
            val t = tick.longValue
            drawer.draw(this, session, camera, settings.showAiDebug, t / 60f)
        }
        if (!paused && !ended) {
            TouchControls(
                layout = layout, state = controls, mirror = settings.mirrorControls, tick = tick,
                ctx = {
                    val owner = session.i(Snap.OWNER)
                    val dead = session.i(Snap.PHASE) != Phase.LIVE && session.i(Snap.RESTART_TEAM) == 0
                    when {
                        owner in 0..10 || dead -> if (session.f(Snap.BALL_X) < 35f) Ctx.ATTACK_OWN_THIRD else Ctx.ATTACK
                        else -> Ctx.DEFEND
                    }
                },
                charge = { b ->
                    when (b) {
                        ButtonId.SHOOT -> session.f(Snap.CHARGE_SHOOT)
                        ButtonId.PASS, ButtonId.LOB, ButtonId.LOFT -> session.f(Snap.CHARGE_PASS)
                        else -> 0f
                    }
                },
                scale = settings.buttonScale, opacity = settings.buttonOpacity, deadZone = settings.deadZone,
            )
        }
        TopBar(hud, config, drill, Modifier.align(Alignment.TopCenter).windowInsetsPadding(WindowInsets.displayCutout))
        Text(
            "PAUSE", color = RxColors.Text, fontSize = 12.sp, fontWeight = FontWeight.Bold,
            modifier = Modifier.align(Alignment.TopEnd).windowInsetsPadding(WindowInsets.displayCutout).padding(10.dp)
                .background(RxColors.Panel, RoundedCornerShape(10.dp)).padding(horizontal = 14.dp, vertical = 10.dp)
                .clickableNoRipple { if (!paused && !ended) setPaused(true) },
        )
        // Stamina bar for the controlled player.
        Box(Modifier.align(Alignment.BottomCenter).padding(bottom = 6.dp).width(120.dp).padding(2.dp)
            .background(Color0.copy(alpha = 0.4f))) {
            Box(Modifier.fillMaxWidth(hud.stamina.coerceIn(0f, 1f)).background(if (hud.stamina > 0.25f) RxColors.Good else RxColors.Danger).padding(2.dp))
        }
        toast?.let {
            Text(
                it, color = RxColors.Accent, fontSize = 34.sp, fontWeight = FontWeight.ExtraBold,
                modifier = Modifier.align(Alignment.Center).background(Color0.copy(alpha = 0.45f), RoundedCornerShape(12.dp))
                    .padding(horizontal = 24.dp, vertical = 10.dp),
            )
        }
        if (hud.replaying) {
            // Clear REPLAY badge with a timeline bar, the playback speed and a stop button.
            Column(Modifier.align(Alignment.TopStart).windowInsetsPadding(WindowInsets.displayCutout).padding(12.dp, 56.dp).widthIn(max = 220.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("● REPLAY", color = Color.White, fontWeight = FontWeight.ExtraBold, fontSize = 14.sp,
                        modifier = Modifier.background(Ios.Red, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 3.dp))
                    Text(if (slowMo == 1f) "1x" else "${(slowMo * 100).roundToInt()}% speed", color = RxColors.Text, fontSize = 12.sp)
                }
                Box(Modifier.fillMaxWidth().height(5.dp).background(Color.White.copy(alpha = 0.25f), RoundedCornerShape(3.dp))) {
                    Box(Modifier.fillMaxWidth(hud.replayProgress.coerceIn(0f, 1f)).height(5.dp).background(Ios.Red, RoundedCornerShape(3.dp)))
                }
                IosButton("Stop replay", { session.command(Cmd.REPLAY_STOP) }, color = Ios.Red)
            }
        } else if (slowMo != 1f && !ended) {
            Text("SLOW MO ${(slowMo * 100).roundToInt()}%", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 12.sp,
                modifier = Modifier.align(Alignment.TopStart).windowInsetsPadding(WindowInsets.displayCutout).padding(12.dp, 56.dp)
                    .background(Ios.Orange, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 3.dp))
        }
        if (!settings.tipsSeen && !paused && !ended && !hud.replaying) FirstRunTips(onDone = { settings.tipsSeen = true })
        if (hud.phase == Phase.HALF_TIME) Banner("HALF TIME  ${hud.home} - ${hud.away}")
        if (paused && !ended) PauseMenu(
            settings = settings, config = config, slowMo = slowMo,
            onResume = { setPaused(false) },
            onSlowMo = { v -> slowMo = v; session.command(Cmd.TIME_SCALE, v) },
            onReplay = { session.command(Cmd.REPLAY_START, 10f); setPaused(false) },
            onSkill = { settings.selectedSkill = it; session.command(Cmd.SELECT_SKILL, it.toFloat()) },
            onRestartDrill = if (config.isDrill) ({ session.command(Cmd.DRILL_RESTART); setPaused(false) }) else null,
            onQuit = onExit,
        )
        if (ended) ResultPanel(
            session = session, hud = hud, config = config, drill = drill, findings = findings, drills = drills, onTrain = onTrain,
            onAgain = { config = config.copy(seed = (System.nanoTime() and 0x7fffffff).toInt()) },
            onExit = onExit,
        )
    }
}

private val Color0 = androidx.compose.ui.graphics.Color.Black

/** One-time overlay explaining the context buttons. Dismissed by tapping anywhere; shown again from Settings. */
@Composable
private fun FirstRunTips(onDone: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Color0.copy(alpha = 0.72f)).clickableNoRipple { onDone() }, contentAlignment = Alignment.Center) {
        Column(
            Modifier.widthIn(max = 560.dp).fillMaxWidth(0.92f).heightIn(max = 330.dp)
                .background(Ios.Background, RoundedCornerShape(18.dp)).verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text("Quick controls", color = Ios.Label, fontSize = 22.sp, fontWeight = FontWeight.Bold)
            Text("The buttons change when you win or lose the ball.", color = Ios.Secondary, fontSize = 13.sp)
            IosGroup("With the ball") {
                TipRow("PASS / THROUGH", "Tap for a ground pass, or a through ball into space. Hold for power.")
                IosDivider()
                TipRow("LOFT / CLEAR", "Long balls and crosses. In your own third it clears the danger.")
                IosDivider()
                TipRow("DASH, SKILL, 1-2", "Hold DASH to sprint, SKILL for a sharp touch past a defender, 1-2 then PASS for pass-and-run.")
            }
            IosGroup("Without the ball") {
                TipRow("PRESS", "Hold to close the carrier down. Do not hold it all game or you leave gaps.")
                IosDivider()
                TipRow("TEAM PRESS / SWITCH / TACKLE", "Send a team-mate to press, change player, or tackle when close.")
            }
            Text("Tap anywhere to start. Full list: Settings, Controller help.", color = Ios.Secondary, fontSize = 12.sp)
        }
    }
}

@Composable
private fun TipRow(title: String, body: String) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 8.dp)) {
        Text(title, color = Ios.Label, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
        Text(body, color = Ios.Secondary, fontSize = 13.sp)
    }
}

@Composable
private fun Banner(text: String) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Text(text, color = RxColors.Text, fontSize = 30.sp, fontWeight = FontWeight.ExtraBold,
            modifier = Modifier.background(RxColors.Panel, RoundedCornerShape(12.dp)).padding(24.dp, 12.dp))
    }
}

@Composable
private fun TopBar(hud: Hud, config: MatchConfig, drill: DrillInfo?, modifier: Modifier) {
    Row(
        modifier.padding(top = 6.dp).background(RxColors.Panel, RoundedCornerShape(10.dp)).padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        if (config.isDrill && drill != null) {
            Text(drill.title, color = RxColors.Accent, fontWeight = FontWeight.Bold, fontSize = 14.sp)
            Text("Attempt ${hud.drillAttempt.coerceAtLeast(1)}/${hud.drillMax}", color = RxColors.Text, fontSize = 13.sp)
            Text("Hit ${hud.drillSuccess}  Miss ${hud.drillFail}", color = RxColors.Text, fontSize = 13.sp)
            Text("Streak ${hud.drillStreak} (best ${hud.drillBest})", color = RxColors.Muted, fontSize = 12.sp)
            if (hud.drillTimeLeft > 0f) Text("%.0fs".format(hud.drillTimeLeft), color = RxColors.Muted, fontSize = 12.sp)
        } else {
            Text("YOU", color = RxColors.Home, fontWeight = FontWeight.Bold, fontSize = 14.sp)
            Text("${hud.home} - ${hud.away}", color = RxColors.Text, fontWeight = FontWeight.ExtraBold, fontSize = 22.sp)
            Text(Content.tiers[config.difficulty.coerceIn(0, 3)].label.uppercase(), color = RxColors.Away, fontWeight = FontWeight.Bold, fontSize = 14.sp)
            Text("${hud.clock}  ${if (hud.half == 1) "1H" else "2H"}${if (hud.endPending) "  +" else ""}", color = RxColors.Muted, fontSize = 13.sp)
        }
    }
}

@Composable
private fun PauseMenu(
    settings: AppSettings, config: MatchConfig, slowMo: Float,
    onResume: () -> Unit, onSlowMo: (Float) -> Unit, onReplay: () -> Unit, onSkill: (Int) -> Unit,
    onRestartDrill: (() -> Unit)?, onQuit: () -> Unit,
) {
    Box(Modifier.fillMaxSize().background(Color0.copy(alpha = 0.45f)).clickableNoRipple { }, contentAlignment = Alignment.Center) {
        Column(
            Modifier.widthIn(max = 520.dp).fillMaxWidth(0.92f).heightIn(max = 330.dp)
                .background(Ios.Background, RoundedCornerShape(18.dp)).verticalScroll(rememberScrollState()).padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text("Paused", color = Ios.Label, fontSize = 22.sp, fontWeight = FontWeight.Bold)
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                IosButton("Resume", onResume, Modifier.weight(1f))
                IosButton("Replay 10 s", onReplay, Modifier.weight(1f), color = Ios.Blue, filled = false)
                if (onRestartDrill != null) IosButton("Restart", onRestartDrill, Modifier.weight(1f), color = Ios.Blue, filled = false)
            }
            IosGroup {
                IosSegRow("Speed", listOf("Normal", "50%", "25%"), listOf(1f, 0.5f, 0.25f).indexOf(slowMo).coerceAtLeast(0)) {
                    onSlowMo(listOf(1f, 0.5f, 0.25f)[it])
                }
                IosDivider()
                IosPillRow("Skill move (SKILL button)", Content.skills, settings.selectedSkill) { onSkill(it) }
                IosDivider()
                IosToggleRow("System overlay", settings.showAiDebug) { settings.showAiDebug = it }
            }
            Text("vs ${Content.tiers[config.difficulty.coerceIn(0, 3)].label} System", color = Ios.Secondary, fontSize = 12.sp)
            IosButton("Quit to menu", onQuit, Modifier.fillMaxWidth(), color = Ios.Red, filled = false)
        }
    }
}

@Composable
private fun ResultPanel(
    session: EngineSession, hud: Hud, config: MatchConfig, drill: DrillInfo?, findings: List<Weakness>, drills: List<DrillInfo>,
    onTrain: (DrillInfo) -> Unit, onAgain: () -> Unit, onExit: () -> Unit,
) {
    Box(Modifier.fillMaxSize().background(Color0.copy(alpha = 0.5f)).clickableNoRipple { }, contentAlignment = Alignment.Center) {
        Column(
            Modifier.widthIn(max = 560.dp).fillMaxWidth(0.94f).heightIn(max = 340.dp).background(Ios.Background, RoundedCornerShape(18.dp))
                .verticalScroll(rememberScrollState()).padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (config.isDrill && drill != null) {
                val total = (hud.drillSuccess + hud.drillFail).coerceAtLeast(1)
                Text("${drill.title} complete", color = Ios.Label, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                Text("${hud.drillSuccess} of $total  (${(100f * hud.drillSuccess / total).roundToInt()}%)  ·  best streak ${hud.drillBest}", color = Ios.Secondary, fontSize = 15.sp)
                val pct = 100f * hud.drillSuccess / total
                Text(
                    when { pct >= 80f -> "Excellent. Raise the System level for a harder test."; pct >= 60f -> "Solid. One more run to lock it in."; else -> "Keep at it. Slow the game to 50% in the pause menu and focus on one thing." },
                    color = Ios.Label, fontSize = 14.sp,
                )
            } else {
                val verdict = when { hud.home > hud.away -> "You win"; hud.home < hud.away -> "You lose"; else -> "Draw" }
                Text("Full time: $verdict", color = Ios.Label, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                Text("${hud.home} - ${hud.away}  vs ${Content.tiers[config.difficulty.coerceIn(0, 3)].label} System", color = Ios.Secondary, fontSize = 15.sp)
                IosGroup {
                    StatRow("Shots (on target)", "${session.stat(0, Snap.S_SHOTS).toInt()} (${session.stat(0, Snap.S_ON_TARGET).toInt()})", "${session.stat(1, Snap.S_SHOTS).toInt()} (${session.stat(1, Snap.S_ON_TARGET).toInt()})")
                    val p0 = session.stat(0, Snap.S_POSSESSION); val p1 = session.stat(1, Snap.S_POSSESSION)
                    val tot = (p0 + p1).coerceAtLeast(1f)
                    StatRow("Possession", "${(100f * p0 / tot).roundToInt()}%", "${(100f * p1 / tot).roundToInt()}%")
                    StatRow("Passes", "${session.stat(0, Snap.S_PASSES_OK).toInt()}/${session.stat(0, Snap.S_PASSES).toInt()}", "${session.stat(1, Snap.S_PASSES_OK).toInt()}/${session.stat(1, Snap.S_PASSES).toInt()}")
                    StatRow("Tackles won", "${session.stat(0, Snap.S_TACKLES_WON).toInt()}", "${session.stat(1, Snap.S_TACKLES_WON).toInt()}")
                    StatRow("Fouls / offsides", "${session.stat(0, Snap.S_FOULS).toInt()} / ${session.stat(0, Snap.S_OFFSIDES).toInt()}", "${session.stat(1, Snap.S_FOULS).toInt()} / ${session.stat(1, Snap.S_OFFSIDES).toInt()}")
                    StatRow("Expected goals", "%.2f".format(session.stat(0, Snap.S_XG)), "%.2f".format(session.stat(1, Snap.S_XG)))
                }
                IosGroup("Coach: what to fix") {
                    if (findings.isEmpty()) Text("Clean game. Nothing stands out; step up a System level.", color = Ios.Secondary, fontSize = 14.sp, modifier = Modifier.padding(14.dp))
                    findings.take(2).forEachIndexed { i, w ->
                        if (i > 0) IosDivider()
                        WeaknessRow(w, drills.firstOrNull { it.id == w.drillId }, onTrain)
                    }
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                IosButton(if (config.isDrill) "Run again" else "Rematch", onAgain, Modifier.weight(1f))
                IosButton("Menu", onExit, Modifier.weight(1f), color = Ios.Blue, filled = false)
            }
        }
    }
}

@Composable
private fun StatRow(label: String, a: String, b: String) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 8.dp)) {
        Text(a, color = Ios.Red, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.width(80.dp))
        Text(label, color = Ios.Secondary, fontSize = 14.sp, modifier = Modifier.weight(1f), textAlign = androidx.compose.ui.text.style.TextAlign.Center)
        Text(b, color = Ios.Blue, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.width(80.dp), textAlign = androidx.compose.ui.text.style.TextAlign.End)
    }
}
