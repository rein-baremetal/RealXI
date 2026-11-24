package com.realxi.engine

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.nio.IntBuffer

/** Raw JNI entry points (implemented in app/src/main/cpp/jni_bridge.cpp). Use [EngineSession] instead. */
object NativeBindings {
    init {
        System.loadLibrary("realxi_native")
    }

    @JvmStatic external fun nativeCreate(
        seed: Int, mode: Int, drillId: Int, difficulty: Int, formationHome: Int, formationAway: Int,
        halfSeconds: Float, referee: Int, assist: Float, flags: Int, attempts: Int, homeDifficulty: Int,
    ): Long

    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeInputBuffer(handle: Long): ByteBuffer
    @JvmStatic external fun nativeSnapshotBuffer(handle: Long): ByteBuffer
    @JvmStatic external fun nativeEventBuffer(handle: Long): ByteBuffer
    @JvmStatic external fun nativeEventCount(handle: Long): Int
    @JvmStatic external fun nativeStep(handle: Long, dt: Float)
    @JvmStatic external fun nativeCommand(handle: Long, cmd: Int, arg: Float)
    @JvmStatic external fun nativeSnapshotFloats(): Int
}

/** Everything needed to start a match or a drill. */
data class MatchConfig(
    val seed: Int = (System.nanoTime() and 0x7fffffff).toInt(),
    val drillId: Int = -1,                 // -1 = full match, otherwise practice drill id
    val difficulty: Int = 1,               // System tier: 0 Regular, 1 Hard, 2 Pro, 3 Legend
    val formationHome: Int = 0,
    val formationAway: Int = 0,
    val halfSeconds: Float = 180f,
    val referee: Int = 1,
    val assist: Float = 0.25f,
    val offside: Boolean = true,
    val fouls: Boolean = true,
    val adaptive: Boolean = true,
    val infiniteStamina: Boolean = false,
    val drillAttempts: Int = 10,
) {
    val isDrill: Boolean get() = drillId >= 0
}

/** One running engine instance. Not thread-safe: drive it from a single thread (the game loop). */
class EngineSession(val config: MatchConfig) : AutoCloseable {
    private var handle: Long = 0L
    private val inputBuf: FloatBuffer
    private val snapBuf: FloatBuffer
    private val eventBuf: IntBuffer

    init {
        val flags = (if (config.offside) 1 else 0) or (if (config.fouls) 2 else 0) or
            (if (config.adaptive) 4 else 0) or (if (config.infiniteStamina) 8 else 0)
        handle = NativeBindings.nativeCreate(
            config.seed, if (config.isDrill) 1 else 0, maxOf(config.drillId, 0), config.difficulty,
            config.formationHome, config.formationAway, config.halfSeconds, config.referee, config.assist,
            flags, config.drillAttempts, -1,
        )
        inputBuf = NativeBindings.nativeInputBuffer(handle).order(ByteOrder.nativeOrder()).asFloatBuffer()
        snapBuf = NativeBindings.nativeSnapshotBuffer(handle).order(ByteOrder.nativeOrder()).asFloatBuffer()
        eventBuf = NativeBindings.nativeEventBuffer(handle).order(ByteOrder.nativeOrder()).asIntBuffer()
    }

    /** Writes the controller state for the next step. Buttons are "held" flags; the engine detects press and release edges. */
    fun setInput(
        moveX: Float, moveY: Float, sprint: Boolean, pass: Boolean, shoot: Boolean, lob: Boolean,
        tackle: Boolean, finesse: Boolean, call: Boolean, switchPlayer: Boolean, skill: Boolean,
    ) {
        inputBuf.put(0, moveX); inputBuf.put(1, moveY)
        inputBuf.put(2, if (sprint) 1f else 0f); inputBuf.put(3, if (pass) 1f else 0f)
        inputBuf.put(4, if (shoot) 1f else 0f); inputBuf.put(5, if (lob) 1f else 0f)
        inputBuf.put(6, if (tackle) 1f else 0f); inputBuf.put(7, if (finesse) 1f else 0f)
        inputBuf.put(8, if (call) 1f else 0f); inputBuf.put(9, if (switchPlayer) 1f else 0f)
        inputBuf.put(10, if (skill) 1f else 0f)
    }

    /** Advances the simulation by [dtSeconds] of real time (the engine runs fixed 60 Hz steps internally). */
    fun step(dtSeconds: Float) = NativeBindings.nativeStep(handle, dtSeconds)

    fun command(cmd: Int, arg: Float = 0f) = NativeBindings.nativeCommand(handle, cmd, arg)

    /** Reads one snapshot value. */
    fun f(index: Int): Float = snapBuf.get(index)
    fun i(index: Int): Int = snapBuf.get(index).toInt()

    fun player(id: Int, field: Int): Float = snapBuf.get(Snap.PLAYERS + id * Snap.PLAYER_STRIDE + field)
    fun stat(team: Int, field: Int): Float = snapBuf.get(Snap.STATS + team * Snap.STATS_STRIDE + field)
    fun target(id: Int, axis: Int): Float = snapBuf.get(Snap.TARGETS + id * 2 + axis)
    fun predicted(index: Int, axis: Int): Float = snapBuf.get(Snap.PREDICT + index * 3 + axis)

    fun eventCount(): Int = NativeBindings.nativeEventCount(handle)
    fun eventAt(index: Int): Int = eventBuf.get(index)

    /** Calls [sink] for every event produced by the last [step]. */
    inline fun forEachEvent(sink: (type: Int, team: Int, player: Int, arg: Int) -> Unit) {
        val n = eventCount()
        for (k in 0 until n) sink(eventAt(k * 4), eventAt(k * 4 + 1), eventAt(k * 4 + 2), eventAt(k * 4 + 3))
    }

    override fun close() {
        if (handle != 0L) {
            NativeBindings.nativeDestroy(handle)
            handle = 0L
        }
    }
}
