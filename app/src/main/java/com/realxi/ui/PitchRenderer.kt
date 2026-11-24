package com.realxi.ui

import android.content.Context
import android.graphics.BitmapFactory
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import com.realxi.engine.EngineSession
import com.realxi.engine.PState
import com.realxi.engine.Phase
import com.realxi.engine.Snap
import com.realxi.ui.theme.RxColors
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/** Smoothly follows the ball along the pitch. */
class CameraState {
    var x = 52.5f
    private var started = false
    fun follow(ballX: Float, ballVx: Float, dt: Float) {
        // Look ahead of fast balls and catch up quicker when the play switches: a snappier, broadcast-style camera.
        val target = (ballX + ballVx * 0.45f).coerceIn(24f, 81f)
        val k = 6.5f + 0.12f * kotlin.math.abs(ballVx)
        if (!started) { x = target; started = true } else x += (target - x) * (1f - exp(-k * dt))
    }
}

/**
 * Optional player sprite sheets. Drop `sprites/players/sheet_home.png` and `sheet_away.png` into assets: six equally
 * wide frames in a row (idle, run A, run B, kick, slide, dive), all facing right. Without them the procedural pixel
 * figures below are used as a placeholder.
 */
class SpriteAtlas(private val home: ImageBitmap?, private val away: ImageBitmap?) {
    fun sheet(team: Int): ImageBitmap? = if (team == 0) home else away
    val available: Boolean get() = home != null || away != null

    companion object {
        fun load(context: Context): SpriteAtlas {
            fun read(name: String): ImageBitmap? = try {
                context.assets.open("sprites/players/$name").use { BitmapFactory.decodeStream(it)?.asImageBitmap() }
            } catch (_: Exception) { null }
            return SpriteAtlas(read("sheet_home.png"), read("sheet_away.png"))
        }
    }
}

/** Maps pitch metres to screen pixels with a simple foreshortened side-on perspective. */
class Projection(val w: Float, val h: Float, val camX: Float, visibleMetres: Float = 58f) {
    private val top = h * 0.17f
    private val pitchH = h * 0.70f
    private val near = w / visibleMetres
    fun scale(y: Float): Float { val t = (y / 68f).coerceIn(-0.2f, 1.2f); return near * (0.66f + 0.34f * t) }
    fun sx(x: Float, y: Float) = w / 2f + (x - camX) * scale(y)
    fun sy(y: Float, z: Float = 0f): Float {
        val t = y / 68f
        return top + pitchH * ((0.66f * t + 0.17f * t * t) / 0.83f) - z * scale(y) * 0.9f
    }
}

class SceneDrawer(private val sprites: SpriteAtlas?) {
    private val path = Path()
    private val order = IntArray(23)
    private val depth = FloatArray(23)
    private val trailX = FloatArray(8)
    private val trailY = FloatArray(8)
    private val trailZ = FloatArray(8)
    private var trailN = 0

    fun draw(scope: DrawScope, s: EngineSession, cam: CameraState, aiDebug: Boolean, timeSec: Float) = with(scope) {
        val p = Projection(size.width, size.height, cam.x)
        drawBackdrop(p, timeSec)
        drawGrass(p)
        drawMarkings(p)
        drawGoal(p, 0f, -1f)
        drawGoal(p, 105f, 1f)
        if (aiDebug) drawDebugUnder(p, s)

        // Shadows first, then everything depth-sorted by pitch y.
        val ballZ = s.f(Snap.BALL_Z)
        val ballX = s.f(Snap.BALL_X)
        val ballY = s.f(Snap.BALL_Y)
        var n = 0
        for (i in 0 until 22) {
            val flags = s.player(i, Snap.P_FLAGS).toInt()
            if (flags and Snap.FLAG_ACTIVE == 0) continue
            order[n] = i; depth[n] = s.player(i, Snap.P_Y); n++
        }
        order[n] = 22; depth[n] = ballY; n++
        // Ball speed trail (HyperMotion-style streak) for passes and shots.
        val ballSpeed = kotlin.math.hypot(s.f(Snap.BALL_VX), s.f(Snap.BALL_VY))
        if (ballSpeed > 12f && s.i(Snap.PAUSED) == 0) {
            for (i in 7 downTo 1) { trailX[i] = trailX[i - 1]; trailY[i] = trailY[i - 1]; trailZ[i] = trailZ[i - 1] }
            trailX[0] = ballX; trailY[0] = ballY; trailZ[0] = ballZ
            if (trailN < 8) trailN++
        } else if (trailN > 0) trailN--
        for (i in 1 until trailN) {
            val a = (1f - i / 8f) * 0.35f
            val sc = p.scale(trailY[i])
            drawLine(Color.White.copy(alpha = a), Offset(p.sx(trailX[i - 1], trailY[i - 1]), p.sy(trailY[i - 1], trailZ[i - 1]) - sc * 0.22f),
                Offset(p.sx(trailX[i], trailY[i]), p.sy(trailY[i], trailZ[i]) - sc * 0.22f), strokeWidth = sc * 0.20f * (1f - i / 9f))
        }
        for (a in 1 until n) { // insertion sort (23 items)
            val o = order[a]; val d = depth[a]; var b = a - 1
            while (b >= 0 && depth[b] > d) { order[b + 1] = order[b]; depth[b + 1] = depth[b]; b-- }
            order[b + 1] = o; depth[b + 1] = d
        }
        for (k in 0 until n) {
            val id = order[k]
            if (id == 22) drawBallShadow(p, ballX, ballY, ballZ) else drawShadow(p, s.player(id, Snap.P_X), s.player(id, Snap.P_Y))
        }
        val active = s.i(Snap.ACTIVE)
        val owner = s.i(Snap.OWNER)
        for (k in 0 until n) {
            val id = order[k]
            if (id == 22) drawBall(p, ballX, ballY, ballZ, timeSec) else drawPlayer(p, s, id, id == active, id == owner, timeSec)
        }
        if (s.i(Snap.PREVIEW) == 1) drawPreview(p, s)
        if (aiDebug) drawDebugOver(p, s)
        if (s.i(Snap.PHASE) == Phase.DEAD_BALL || s.i(Snap.PHASE) == Phase.KICKOFF) {
            val bx = p.sx(ballX, ballY); val by = p.sy(ballY)
            val pulse = 1f + 0.15f * sin(timeSec * 6f)
            drawCircle(RxColors.Accent.copy(alpha = 0.8f), p.scale(ballY) * 0.9f * pulse, Offset(bx, by), style = Stroke(width = 3f))
        }
    }

    // ---------------------------------------------------------------- environment
    private fun DrawScope.drawBackdrop(p: Projection, t: Float) {
        drawRect(Color(0xFF0A1410), Offset.Zero, Size(size.width, size.height))
        // Stands: dark band with deterministic "crowd" dots scrolling with the camera.
        val standBottom = p.sy(-3f)
        drawRect(Color(0xFF16231B), Offset.Zero, Size(size.width, standBottom))
        val rows = 5
        for (r in 0 until rows) {
            val yy = standBottom * (0.25f + 0.15f * r)
            var x = -((p.camX * p.scale(0f) * 0.35f) % 18f)
            var i = r * 7
            while (x < size.width) {
                val hue = (i * 37) % 6
                val col = when (hue) { 0 -> Color(0xFFB84B4B); 1 -> Color(0xFF4B6BB8); 2 -> Color(0xFFD8D8D8); 3 -> Color(0xFFD8B84B); 4 -> Color(0xFF4BB87A); else -> Color(0xFF8A8A8A) }
                val bob = if ((i + (t * 2f).toInt()) % 11 == 0) 3f else 0f
                drawRect(col.copy(alpha = 0.55f), Offset(x, yy - bob), Size(9f, 12f))
                x += 18f; i++
            }
        }
        drawRect(Color(0xFF0E1A13), Offset(0f, standBottom - 14f), Size(size.width, 14f))
    }

    private fun DrawScope.quad(p: Projection, x0: Float, x1: Float, y0: Float, y1: Float, color: Color) {
        path.reset()
        path.moveTo(p.sx(x0, y0), p.sy(y0)); path.lineTo(p.sx(x1, y0), p.sy(y0))
        path.lineTo(p.sx(x1, y1), p.sy(y1)); path.lineTo(p.sx(x0, y1), p.sy(y1)); path.close()
        drawPath(path, color)
    }

    private fun DrawScope.drawGrass(p: Projection) {
        // Run-off area then mown stripes every 7.5 m.
        quad(p, -12f, 117f, -4f, 72f, RxColors.GrassDark)
        val first = max(0, ((p.camX - 40f) / 7.5f).toInt())
        val last = min(13, ((p.camX + 40f) / 7.5f).toInt())
        for (k in first..last) {
            val x0 = k * 7.5f
            quad(p, x0, min(105f, x0 + 7.5f), 0f, 68f, if (k % 2 == 0) RxColors.Grass else Color(0xFF34983F))
        }
    }

    private fun DrawScope.ln(p: Projection, x0: Float, y0: Float, x1: Float, y1: Float, w: Float = 2.5f) {
        drawLine(RxColors.Line.copy(alpha = 0.9f), Offset(p.sx(x0, y0), p.sy(y0)), Offset(p.sx(x1, y1), p.sy(y1)), w)
    }

    private fun DrawScope.arc(p: Projection, cx: Float, cy: Float, r: Float, a0: Float, a1: Float) {
        val steps = 24
        var px = cx + r * cos(a0); var py = cy + r * sin(a0)
        for (i in 1..steps) {
            val a = a0 + (a1 - a0) * i / steps
            val nx = cx + r * cos(a); val ny = cy + r * sin(a)
            ln(p, px, py, nx, ny); px = nx; py = ny
        }
    }

    private fun DrawScope.drawMarkings(p: Projection) {
        ln(p, 0f, 0f, 105f, 0f); ln(p, 0f, 68f, 105f, 68f); ln(p, 0f, 0f, 0f, 68f); ln(p, 105f, 0f, 105f, 68f)
        ln(p, 52.5f, 0f, 52.5f, 68f)
        arc(p, 52.5f, 34f, 9.15f, 0f, (2 * PI).toFloat())
        for (side in 0..1) {
            val gx = if (side == 0) 0f else 105f
            val sgn = if (side == 0) 1f else -1f
            val bx = gx + sgn * 16.5f; val sx = gx + sgn * 5.5f
            ln(p, gx, 13.84f, bx, 13.84f); ln(p, bx, 13.84f, bx, 54.16f); ln(p, bx, 54.16f, gx, 54.16f)
            ln(p, gx, 24.84f, sx, 24.84f); ln(p, sx, 24.84f, sx, 43.16f); ln(p, sx, 43.16f, gx, 43.16f)
            val spot = gx + sgn * 11f
            drawCircle(RxColors.Line, 3f, Offset(p.sx(spot, 34f), p.sy(34f)))
            val a0 = if (side == 0) -0.93f else (PI - 0.93).toFloat()
            arc(p, spot, 34f, 9.15f, a0, a0 + 1.86f)
        }
        drawCircle(RxColors.Line, 3f, Offset(p.sx(52.5f, 34f), p.sy(34f)))
    }

    private fun DrawScope.drawGoal(p: Projection, gx: Float, outward: Float) {
        val back = gx + outward * 2.2f
        val netCol = Color.White.copy(alpha = 0.18f)
        // Net: back panel and side panels as light quads.
        for (yy in listOf(30.34f, 37.66f)) {
            path.reset()
            path.moveTo(p.sx(gx, yy), p.sy(yy)); path.lineTo(p.sx(back, yy), p.sy(yy))
            path.lineTo(p.sx(back, yy), p.sy(yy, 2.44f)); path.lineTo(p.sx(gx, yy), p.sy(yy, 2.44f)); path.close()
            drawPath(path, netCol)
        }
        path.reset()
        path.moveTo(p.sx(back, 30.34f), p.sy(30.34f)); path.lineTo(p.sx(back, 37.66f), p.sy(37.66f))
        path.lineTo(p.sx(back, 37.66f), p.sy(37.66f, 2.44f)); path.lineTo(p.sx(back, 30.34f), p.sy(30.34f, 2.44f)); path.close()
        drawPath(path, netCol)
        val post = Color.White
        for (yy in listOf(30.34f, 37.66f)) drawLine(post, Offset(p.sx(gx, yy), p.sy(yy)), Offset(p.sx(gx, yy), p.sy(yy, 2.44f)), 5f)
        drawLine(post, Offset(p.sx(gx, 30.34f), p.sy(30.34f, 2.44f)), Offset(p.sx(gx, 37.66f), p.sy(37.66f, 2.44f)), 5f)
    }

    // ---------------------------------------------------------------- entities
    private fun DrawScope.drawShadow(p: Projection, x: Float, y: Float) {
        val sc = p.scale(y)
        drawOval(Color.Black.copy(alpha = 0.28f), Offset(p.sx(x, y) - sc * 0.55f, p.sy(y) - sc * 0.16f), Size(sc * 1.1f, sc * 0.32f))
    }

    private fun DrawScope.drawBallShadow(p: Projection, x: Float, y: Float, z: Float) {
        val sc = p.scale(y)
        val r = sc * 0.30f * (1f / (1f + z * 0.15f))
        drawOval(Color.Black.copy(alpha = 0.30f), Offset(p.sx(x, y) - r, p.sy(y) - r * 0.35f), Size(r * 2, r * 0.7f))
    }

    private fun DrawScope.drawBall(p: Projection, x: Float, y: Float, z: Float, t: Float) {
        val sc = p.scale(y)
        val c = Offset(p.sx(x, y), p.sy(y, z) - sc * 0.22f)
        val r = sc * 0.30f
        drawCircle(Color.White, r, c)
        drawCircle(Color(0xFF222222), r, c, style = Stroke(width = 2f))
        val a = x * 0.9f + t * 0.0f
        for (k in 0 until 3) {
            val ang = a + k * 2.094f
            drawCircle(Color(0xFF222222), r * 0.22f, Offset(c.x + cos(ang) * r * 0.55f, c.y + sin(ang) * r * 0.45f))
        }
    }

    private fun DrawScope.drawPlayer(p: Projection, s: EngineSession, id: Int, active: Boolean, hasBall: Boolean, t: Float) {
        val x = s.player(id, Snap.P_X); val y = s.player(id, Snap.P_Y)
        val vx = s.player(id, Snap.P_VX); val vy = s.player(id, Snap.P_VY)
        val facing = s.player(id, Snap.P_FACING)
        val state = s.player(id, Snap.P_STATE).toInt()
        val flags = s.player(id, Snap.P_FLAGS).toInt()
        val team = if (id < 11) 0 else 1
        val isGk = (id % 11) == 0
        val sc = p.scale(y)
        val fx = p.sx(x, y); val fy = p.sy(y)
        val dir = if (cos(facing) >= 0f) 1f else -1f
        val speed = hypot(vx, vy)
        val unit = sc * 1.15f   // pixels per "figure unit" (~1.8 m player is 2 units tall)
        val kit = when {
            isGk -> if (team == 0) Color(0xFFE8C820) else Color(0xFF39C26B)
            team == 0 -> RxColors.Home
            else -> RxColors.Away
        }
        val trim = if (team == 0) RxColors.HomeTrim else RxColors.AwayTrim

        val sheet = sprites?.sheet(team)
        if (sheet != null) {
            val frames = 6
            val fw = sheet.width / frames; val fh = sheet.height
            val frame = when (state) {
                PState.SLIDE -> 4; PState.DIVE, PState.RECOVER -> 5; PState.KICK -> 3
                PState.RUN, PState.SPRINT -> 1 + ((t * (6f + speed)).toInt() and 1)
                else -> 0
            }
            val dh = unit * 2.3f; val dw = dh * fw / fh
            withTransform({ if (dir < 0f) scale(-1f, 1f, Offset(fx, fy)) }) {
                drawImage(
                    sheet, IntOffset(frame * fw, 0), IntSize(fw, fh),
                    IntOffset((fx - dw / 2f).toInt(), (fy - dh).toInt()), IntSize(dw.toInt(), dh.toInt()),
                    filterQuality = FilterQuality.None,
                )
            }
        } else {
            drawPixelFigure(fx, fy, unit, dir, state, speed, t, kit, trim, id)
        }
        if (flags and Snap.FLAG_YELLOW != 0) {
            drawRect(Color(0xFFFFE04A), Offset(fx + unit * 0.45f, fy - unit * 2.3f), Size(unit * 0.22f, unit * 0.3f))
        }
        if (active) {
            val ty = fy - unit * 2.9f
            path.reset()
            path.moveTo(fx, ty + unit * 0.35f); path.lineTo(fx - unit * 0.32f, ty); path.lineTo(fx + unit * 0.32f, ty); path.close()
            drawPath(path, RxColors.Accent)
            drawOval(RxColors.Accent.copy(alpha = 0.9f), Offset(fx - unit * 0.7f, fy - unit * 0.14f), Size(unit * 1.4f, unit * 0.34f), style = Stroke(width = 3f))
        }
        if (hasBall && !active) drawCircle(Color.White.copy(alpha = 0.7f), unit * 0.12f, Offset(fx, fy - unit * 2.7f))
    }

    /** Chunky pixel-style placeholder figure, snapped to a pixel grid so it reads as sprite art. */
    private fun DrawScope.drawPixelFigure(
        fx: Float, fy: Float, u: Float, dir: Float, state: Int, speed: Float, t: Float, kit: Color, trim: Color, id: Int,
    ) {
        val px = max(2f, (u * 0.16f).toInt().toFloat())  // one "pixel"
        fun r(x: Float, y: Float, w: Float, h: Float, c: Color) =
            drawRect(c, Offset((fx + x * dir * px).let { if (dir < 0) it - w * px else it }, fy + y * px), Size(w * px, h * px))
        val lying = state == PState.SLIDE || state == PState.DIVE || state == PState.RECOVER
        if (lying) {
            val tilt = if (state == PState.SLIDE) 0.55f else 0.9f
            withTransform({ rotate(-dir * 80f * tilt, Offset(fx, fy - px * 5f)) }) {
                r(-2f, -12f, 4f, 6f, kit); r(-2f, -6f, 4f, 3f, Color(0xFFF2F2F2)); r(-1f, -16f, 3f, 4f, RxColors.Skin)
                r(-2f, -3f, 2f, 3f, Color(0xFF222222)); r(0f, -3f, 2f, 3f, Color(0xFF222222))
            }
            return
        }
        val phase = ((t * (5f + speed * 1.4f)) % 2f) < 1f
        val moving = speed > 0.8f
        val legA = if (moving && phase) 2f else 0f
        val legB = if (moving && !phase) 2f else 0f
        // legs
        r(-2f, -4f + legA * 0f, 2f, 4f - legA * 0.5f, Color(0xFFF2F2F2)); r(0f, -4f, 2f, 4f - legB * 0.5f, Color(0xFFF2F2F2))
        r(-2f + legA * 0.5f, -1f, 3f, 1.2f, Color(0xFF111111)); r(0f + legB * 0.5f, -1f, 3f, 1.2f, Color(0xFF111111))
        // body + trim
        r(-3f, -10f, 6f, 6f, kit)
        r(-3f, -10f, 6f, 1f, trim)
        // arms swing opposite legs
        val armUp = state == PState.CELEBRATE
        r(-4f, if (armUp) -14f else -9f - (if (phase) 1f else 0f), 1.5f, 4f, kit)
        r(2.5f, if (armUp) -14f else -9f - (if (!phase) 1f else 0f), 1.5f, 4f, kit)
        // head
        r(-2f, -14f, 4f, 4f, RxColors.Skin)
        r(-2f, -14.5f, 4f, 1.5f, if (id % 3 == 0) Color(0xFF2A1A10) else if (id % 3 == 1) Color(0xFF6B4A2A) else Color(0xFF111111))
        if (state == PState.KICK) r(2f, -2f, 4f, 1.5f, Color(0xFF111111))
    }

    // ---------------------------------------------------------------- overlays
    private fun DrawScope.drawPreview(p: Projection, s: EngineSession) {
        var prevX = p.sx(s.f(Snap.BALL_X), s.f(Snap.BALL_Y))
        var prevY = p.sy(s.f(Snap.BALL_Y), 0.1f)
        for (i in 0 until 12) {
            val x = s.predicted(i, 0); val y = s.predicted(i, 1); val z = s.predicted(i, 2)
            val sx = p.sx(x, y); val sy = p.sy(y, z)
            drawLine(RxColors.Accent.copy(alpha = 0.85f), Offset(prevX, prevY), Offset(sx, sy), 4f)
            drawCircle(RxColors.Accent, 5f, Offset(sx, sy))
            prevX = sx; prevY = sy
        }
    }

    private fun DrawScope.drawDebugUnder(p: Projection, s: EngineSession) {
        // Away team's defensive/attacking targets as faint lines.
        for (i in 11 until 22) {
            if (s.player(i, Snap.P_FLAGS).toInt() and Snap.FLAG_ACTIVE == 0) continue
            val x = s.player(i, Snap.P_X); val y = s.player(i, Snap.P_Y)
            val tx = s.target(i, 0); val ty = s.target(i, 1)
            drawLine(Color(0xFFFF8A80).copy(alpha = 0.35f), Offset(p.sx(x, y), p.sy(y)), Offset(p.sx(tx, ty), p.sy(ty)), 2f)
            drawCircle(Color(0xFFFF8A80).copy(alpha = 0.5f), 5f, Offset(p.sx(tx, ty), p.sy(ty)), style = Stroke(width = 2f))
        }
    }

    private fun DrawScope.drawDebugOver(p: Projection, s: EngineSession) {
        // Defensive line of the System (x of its deepest outfield player) and the ball's current speed vector.
        var minX = 200f; var maxX = -200f
        for (i in 12 until 22) {
            if (s.player(i, Snap.P_FLAGS).toInt() and Snap.FLAG_ACTIVE == 0) continue
            val x = s.player(i, Snap.P_X)
            minX = min(minX, x); maxX = max(maxX, x)
        }
        if (maxX > -100f) {
            drawLine(Color(0xFFFF5252).copy(alpha = 0.5f), Offset(p.sx(maxX, 0f), p.sy(0f)), Offset(p.sx(maxX, 68f), p.sy(68f)), 2f)
        }
        val bx = s.f(Snap.BALL_X); val by = s.f(Snap.BALL_Y)
        val vx = s.f(Snap.BALL_VX); val vy = s.f(Snap.BALL_VY)
        if (abs(vx) + abs(vy) > 1f) {
            drawLine(Color.White.copy(alpha = 0.6f), Offset(p.sx(bx, by), p.sy(by)), Offset(p.sx(bx + vx * 0.4f, by + vy * 0.4f), p.sy(by + vy * 0.4f)), 3f)
        }
    }
}
