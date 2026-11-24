package com.realxi

import com.realxi.data.Content
import com.realxi.data.ControlLayoutParser
import com.realxi.engine.Snap
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ContentTest {
    private fun asset(name: String) = File("../assets/data/$name").readText()

    @Test fun drillsParse() {
        val drills = Content.parseDrills(asset("drills.json"))
        assertEquals(8, drills.size)
        assertEquals((0..7).toList(), drills.map { it.id })
        assertTrue(drills.all { it.attempts > 0 && it.title.isNotBlank() })
    }

    @Test fun controlLayoutParsesAndCoversAllActions() {
        val layout = ControlLayoutParser.parse(asset("default_control_layout.json"))
        val actions = layout.buttons.map { it.action }.toSet()
        for (a in listOf("tl", "tr", "bl", "br", "s1", "s2")) {
            assertTrue("missing button $a", a in actions)
        }
        assertTrue(layout.buttons.all { it.x in 0f..1f && it.y in 0f..1f && it.size > 0f })
    }

    @Test fun snapshotLayoutIsSelfConsistent() {
        assertEquals(Snap.PLAYERS + 22 * Snap.PLAYER_STRIDE, Snap.STATS)
        assertEquals(Snap.STATS + 2 * Snap.STATS_STRIDE, Snap.TARGETS)
        assertEquals(Snap.TARGETS + 44, Snap.PREDICT)
        assertEquals(Snap.PREDICT + 36, Snap.FLOATS)
    }

    @Test fun formationNamesMatchEngineTable() {
        val json = asset("formations.json")
        for (name in Content.formations) assertTrue("formation $name missing from formations.json", json.contains("\"$name\""))
    }
}
