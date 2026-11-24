package com.realxi.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.sp
import com.realxi.data.ControlLayout
import com.realxi.ui.theme.RxColors
import kotlin.math.hypot

/** Logical inputs sent to the engine. [LOFT] is virtual: it presses the through-ball input together with the loft modifier. */
enum class ButtonId(val label: String) {
    PASS("PASS"), SHOOT("SHOOT"), LOB("THROUGH"), TACKLE("TACKLE"), SPRINT("DASH"), CALL("CALL"),
    FINESSE("PRESS"), SWITCH("SWITCH"), SKILL("SKILL"), LOFT("LOFT"),
}

/** What the player is doing right now decides what the buttons mean (eFootball-style context controls). */
enum class Ctx { ATTACK, ATTACK_OWN_THIRD, DEFEND }

class SlotRole(val id: ButtonId, val label: String)

/**
 * Fixed slots (tl, tr, bl, br big; s1, s2, s3 small) change role with and without the ball:
 *  with the ball   THROUGH / SHOOT / PASS / DASH, LOFT (CLEAR in our own third), SKILL (sharp touch), 1-2 (pass and run)
 *  without the ball TEAM PRESS (call for pressure) / SWITCH / PRESS (hold to close down and jockey) / DASH, TACKLE
 */
fun roleFor(slot: String, ctx: Ctx): SlotRole? {
    val att = ctx != Ctx.DEFEND
    return when (slot) {
        "tl" -> if (att) SlotRole(ButtonId.LOB, "THROUGH") else SlotRole(ButtonId.CALL, "TEAM\nPRESS")
        "tr" -> if (att) SlotRole(ButtonId.SHOOT, "SHOOT") else SlotRole(ButtonId.SWITCH, "SWITCH")
        "bl" -> if (att) SlotRole(ButtonId.PASS, "PASS") else SlotRole(ButtonId.FINESSE, "PRESS")
        "br" -> SlotRole(ButtonId.SPRINT, "DASH")
        "s1" -> if (!att) SlotRole(ButtonId.TACKLE, "TACKLE") else if (ctx == Ctx.ATTACK_OWN_THIRD) SlotRole(ButtonId.LOFT, "CLEAR") else SlotRole(ButtonId.LOFT, "LOFT")
        "s2" -> if (att) SlotRole(ButtonId.SKILL, "SKILL") else null
        "s3" -> if (att) SlotRole(ButtonId.CALL, "1-2") else null
        else -> null
    }
}

/**
 * Controller state shared between the touch layer and the game loop. [latched] guarantees that a tap shorter than one
 * frame is still delivered to the engine for at least one step.
 */
class ControlState {
    var moveX = 0f
    var moveY = 0f
    val held = BooleanArray(ButtonId.entries.size)
    val latched = BooleanArray(ButtonId.entries.size)

    fun down(b: ButtonId) { held[b.ordinal] = true; latched[b.ordinal] = true }
    fun up(b: ButtonId) { held[b.ordinal] = false }
    fun isOn(b: ButtonId): Boolean = held[b.ordinal] || latched[b.ordinal]
    fun endFrame() { latched.fill(false) }
    fun reset() { moveX = 0f; moveY = 0f; held.fill(false); latched.fill(false) }
}

/** Layout unit: buttons scale with the short side (capped at 2x height) so they keep their shape on every aspect ratio. */
private fun unitOf(w: Float, h: Float): Float = minOf(w, 2f * h).coerceAtLeast(1f)

private const val NO_ROLE = -2
private const val STICK_ROLE = -1

/**
 * Full-screen multi-touch layer. Touches on a button press it; touches in the "stick half" of the screen create a
 * floating stick centred where the finger landed. [tick] is read in the draw phase so pressed states redraw every frame.
 * [ctx] is read live so button roles swap the instant possession changes. [scale], [opacity] and [deadZone] are accessibility settings.
 */
@Composable
fun TouchControls(
    layout: ControlLayout,
    state: ControlState,
    mirror: Boolean,
    tick: State<Long>,
    ctx: () -> Ctx,
    charge: (ButtonId) -> Float,
    scale: Float = 1f,
    opacity: Float = 1f,
    deadZone: Float = 0.12f,
    modifier: Modifier = Modifier,
) {
    val measurer = rememberTextMeasurer()
    val density = LocalDensity.current
    var canvasSize by remember { mutableStateOf(IntSize.Zero) }
    // Labels are sized from each slot's own radius, so text always fits inside the circle on any screen.
    val labels: Map<String, TextLayoutResult> = remember(layout, canvasSize, scale) {
        val unit = unitOf(canvasSize.width.toFloat(), canvasSize.height.toFloat()) * scale
        val out = HashMap<String, TextLayoutResult>()
        for (spec in layout.buttons) for (c in Ctx.entries) {
            val role = roleFor(spec.action, c) ?: continue
            val key = spec.action + "|" + role.label
            if (key in out) continue
            val r = spec.size * unit * 0.5f
            val lines = role.label.split("\n").maxOf { it.length }.coerceAtLeast(4)
            val px = (r * 2f * 0.80f / (lines * 0.66f)).coerceAtMost(r * 0.42f).coerceIn(9f * density.density, 19f * density.density)
            out[key] = measurer.measure(
                role.label,
                TextStyle(color = RxColors.Text, fontSize = with(density) { px.toSp() }, fontWeight = FontWeight.Bold, textAlign = androidx.compose.ui.text.style.TextAlign.Center),
            )
        }
        out
    }
    var stickBase by remember { mutableStateOf<Offset?>(null) }
    val slots = layout.buttons

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .onSizeChanged { canvasSize = it }
            .pointerInput(layout, mirror, scale, deadZone) {
                val w = size.width.toFloat()
                val h = size.height.toFloat()
                val unit = unitOf(w, h) * scale
                val stickRadius = layout.stick.radius * unit
                fun cx(x: Float) = (if (mirror) 1f - x else x) * w
                // value = ButtonId ordinal for a button finger, STICK_ROLE for the stick finger
                val roles = HashMap<Long, Int>()
                var origin = Offset.Zero
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent(PointerEventPass.Initial)
                        for (c in event.changes) {
                            val id = c.id.value
                            val isDown = !c.previousPressed && c.pressed
                            val isUp = c.previousPressed && !c.pressed
                            if (isDown) {
                                var best: ButtonId? = null
                                var bestD = Float.MAX_VALUE
                                val now = ctx()
                                for (spec in slots) {
                                    val role = roleFor(spec.action, now) ?: continue
                                    val d = hypot(c.position.x - cx(spec.x), c.position.y - spec.y * h)
                                    val r = spec.size * unit * 0.5f * 1.25f
                                    if (d < r && d < bestD) { bestD = d; best = role.id }
                                }
                                val stickSide = if (mirror) c.position.x > w * 0.5f else c.position.x < w * 0.5f
                                if (best != null) {
                                    roles[id] = best.ordinal
                                    state.down(best)
                                    c.consume()
                                } else if (stickSide && !roles.containsValue(STICK_ROLE)) {
                                    roles[id] = STICK_ROLE
                                    origin = c.position
                                    stickBase = origin
                                    state.moveX = 0f; state.moveY = 0f
                                    c.consume()
                                }
                            } else if (isUp) {
                                val role = roles.remove(id) ?: NO_ROLE
                                if (role == STICK_ROLE) { state.moveX = 0f; state.moveY = 0f; stickBase = null }
                                else if (role >= 0) state.up(ButtonId.entries[role])
                                if (role != NO_ROLE) c.consume()
                            } else if (c.pressed) {
                                val role = roles[id] ?: NO_ROLE
                                if (role == STICK_ROLE) {
                                    val dx = (c.position.x - origin.x) / stickRadius
                                    val dy = (c.position.y - origin.y) / stickRadius
                                    val len = hypot(dx, dy)
                                    if (len < deadZone) { state.moveX = 0f; state.moveY = 0f }
                                    else {
                                        // Re-scale so movement still ramps from 0 at the dead-zone edge to 1 at the rim.
                                        val m = ((len - deadZone) / (1f - deadZone)).coerceIn(0f, 1f)
                                        state.moveX = dx / len * m
                                        state.moveY = dy / len * m
                                    }
                                    c.consume()
                                } else if (role >= 0) {
                                    c.consume()
                                }
                            }
                        }
                    }
                }
            },
    ) {
        @Suppress("UNUSED_VARIABLE") val redraw = tick.value
        val w = size.width
        val h = size.height
        val unit = unitOf(w, h) * scale
        val a = opacity.coerceIn(0.3f, 1f)
        fun cx(x: Float) = (if (mirror) 1f - x else x) * w
        // Stick
        val stickR = layout.stick.radius * unit
        val base = stickBase ?: Offset(cx(layout.stick.x), layout.stick.y * h)
        val active = stickBase != null
        drawCircle(Color.White.copy(alpha = (if (active) 0.22f else 0.10f) * a), stickR, base)
        drawCircle(Color.White.copy(alpha = 0.45f * a), stickR, base, style = Stroke(width = 3f))
        val thumb = Offset(base.x + state.moveX * stickR, base.y + state.moveY * stickR)
        drawCircle(Color.White.copy(alpha = (if (active) 0.55f else 0.25f) * a), stickR * 0.42f, thumb)
        // Buttons: role depends on whether we have the ball
        val now = ctx()
        for (spec in slots) {
            val role = roleFor(spec.action, now) ?: continue
            val c = Offset(cx(spec.x), spec.y * h)
            val r = spec.size * unit * 0.5f
            val on = state.isOn(role.id)
            val defend = now == Ctx.DEFEND
            val fill = if (on) RxColors.Accent.copy(alpha = 0.55f * a) else (if (defend) Color(0xFF0B3D91) else Color(0xFF0B3D1E)).copy(alpha = 0.55f * a)
            drawCircle(fill, r, c)
            drawCircle(Color.White.copy(alpha = 0.70f * a), r, c, style = Stroke(width = 3f))
            val ch = charge(role.id)
            if (ch > 0.02f) {
                drawArc(
                    RxColors.Accent, -90f, 360f * ch, false,
                    topLeft = Offset(c.x - r - 8f, c.y - r - 8f), size = Size((r + 8f) * 2, (r + 8f) * 2),
                    style = Stroke(width = 7f),
                )
            }
            val t = labels[spec.action + "|" + role.label]
            if (t != null) drawText(t, topLeft = Offset(c.x - t.size.width / 2f, c.y - t.size.height / 2f), alpha = a.coerceAtLeast(0.6f))
        }
    }
}
