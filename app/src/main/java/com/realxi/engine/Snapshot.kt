package com.realxi.engine

/**
 * Offsets into the float snapshot written by the C++ engine every frame.
 * Mirrors engine/include/realxi/snapshot.h; `python3 tools/check_layout.py` verifies the two stay in sync.
 */
object Snap {
    const val PHASE = 0
    const val RESTART = 1
    const val RESTART_TEAM = 2
    const val CLOCK = 3
    const val HALF = 4
    const val SCORE_HOME = 5
    const val SCORE_AWAY = 6
    const val ACTIVE = 7
    const val BALL_X = 8
    const val BALL_Y = 9
    const val BALL_Z = 10
    const val BALL_VX = 11
    const val BALL_VY = 12
    const val BALL_VZ = 13
    const val OWNER = 14
    const val REPLAYING = 15
    const val REPLAY_PROGRESS = 16
    const val TIME_SCALE = 17
    const val CHARGE_PASS = 18
    const val CHARGE_SHOOT = 19
    const val ACTIVE_STAMINA = 20
    const val DRILL_ID = 21
    const val DRILL_ATTEMPT = 22
    const val DRILL_SUCCESS = 23
    const val DRILL_FAIL = 24
    const val DRILL_LAST = 25
    const val DRILL_TIME_LEFT = 26
    const val DRILL_STREAK = 27
    const val DRILL_BEST = 28
    const val PAUSED = 29
    const val TAKER = 30
    const val DRILL_ATTEMPTS_MAX = 31
    const val DRILL_COMPLETE = 32
    const val PREVIEW = 33
    const val END_PENDING = 34

    const val HEADER_SIZE = 40
    const val PLAYER_STRIDE = 10
    const val PLAYERS = 40
    const val STATS_STRIDE = 14
    const val STATS = 260
    const val TARGETS = 288
    const val PREDICT = 332
    const val FLOATS = 368
    const val MAX_EVENTS = 48

    // Player record offsets (add PLAYERS + id * PLAYER_STRIDE)
    const val P_X = 0
    const val P_Y = 1
    const val P_VX = 2
    const val P_VY = 3
    const val P_FACING = 4
    const val P_STATE = 5
    const val P_STAMINA = 6
    const val P_FLAGS = 7
    const val P_ROLE = 8
    const val P_STATE_T = 9

    // Player flags
    const val FLAG_ACTIVE = 1
    const val FLAG_YELLOW = 2
    const val FLAG_RED = 4
    const val FLAG_HAS_BALL = 8

    // Stats record offsets (add STATS + team * STATS_STRIDE)
    const val S_POSSESSION = 0
    const val S_SHOTS = 1
    const val S_ON_TARGET = 2
    const val S_PASSES = 3
    const val S_PASSES_OK = 4
    const val S_TACKLES = 5
    const val S_TACKLES_WON = 6
    const val S_FOULS = 7
    const val S_YELLOWS = 8
    const val S_REDS = 9
    const val S_CORNERS = 10
    const val S_OFFSIDES = 11
    const val S_SAVES = 12
    const val S_XG = 13
}

/** Game phases (engine Phase enum). */
object Phase {
    const val KICKOFF = 0
    const val LIVE = 1
    const val DEAD_BALL = 2
    const val GOAL_PAUSE = 3
    const val HALF_TIME = 4
    const val FULL_TIME = 5
    const val DRILL_RESULT = 6
}

/** Player animation states (engine PState enum). */
object PState {
    const val IDLE = 0
    const val RUN = 1
    const val SPRINT = 2
    const val KICK = 3
    const val TACKLE = 4
    const val SLIDE = 5
    const val RECOVER = 6
    const val DIVE = 7
    const val HEADER = 8
    const val CELEBRATE = 9
}

/** Event ids reported by the engine (engine EventType enum). */
object Ev {
    const val KICK = 1
    const val PASS = 2
    const val SHOT = 3
    const val GOAL = 4
    const val SAVE = 5
    const val POST = 6
    const val FOUL = 7
    const val YELLOW = 8
    const val RED = 9
    const val OFFSIDE = 10
    const val CORNER = 11
    const val GOAL_KICK = 12
    const val THROW_IN = 13
    const val WHISTLE = 14
    const val HALF_TIME = 15
    const val FULL_TIME = 16
    const val TACKLE = 17
    const val TACKLE_WON = 18
    const val HEADER = 20
    const val DRILL_SUCCESS = 21
    const val DRILL_FAIL = 22
    const val DRILL_COMPLETE = 23
    const val CATCH = 24
    const val SKILL = 25
}

/** Engine commands (Match::Cmd). */
object Cmd {
    const val PAUSE = 1
    const val TIME_SCALE = 2
    const val REPLAY_START = 3
    const val REPLAY_STOP = 4
    const val SELECT_SKILL = 5
    const val DRILL_NEXT = 6
    const val DRILL_RESTART = 7
    const val SET_ASSIST = 8
    const val SET_FORMATION_HOME = 9
    const val SET_DIFFICULTY = 10
    const val REPLAY_SPEED = 11
    const val SET_ADAPTIVE = 12
}
