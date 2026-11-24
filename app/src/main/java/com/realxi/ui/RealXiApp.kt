package com.realxi.ui

import android.content.Context
import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import com.realxi.data.AppSettings
import com.realxi.data.CoachStore
import com.realxi.data.Content
import com.realxi.data.ControlLayoutParser
import com.realxi.data.DrillInfo
import com.realxi.engine.MatchConfig
import com.realxi.ui.theme.RealXiTheme

/** Simple state-based navigation; the app has only a handful of screens. */
private sealed interface Screen {
    data object Home : Screen
    data object Practice : Screen
    data object Settings : Screen
    data object Help : Screen
    data object Career : Screen
    data class Setup(val drill: DrillInfo?) : Screen
    data class Play(val config: MatchConfig, val drill: DrillInfo?) : Screen
}

@Composable
fun RealXiApp() {
    val context: Context = LocalContext.current
    val settings = remember { AppSettings(context) }
    val coach = remember { CoachStore(context) }
    val drills = remember { Content.loadDrills(context) }
    val layout = remember { ControlLayoutParser.load(context) }
    var screen by remember { mutableStateOf<Screen>(Screen.Home) }

    RealXiTheme {
        when (val s = screen) {
            Screen.Home -> HomeScreen(
                coach = coach, drills = drills,
                onTrain = { screen = Screen.Setup(it) },
                onQuickMatch = { screen = Screen.Setup(null) },
                onPractice = { screen = Screen.Practice },
                onSettings = { screen = Screen.Settings },
                onCareer = { screen = Screen.Career },
            )
            Screen.Practice -> {
                BackHandler { screen = Screen.Home }
                PracticeScreen(drills, coach, onDrill = { screen = Screen.Setup(it) }, onBack = { screen = Screen.Home })
            }
            Screen.Settings -> {
                BackHandler { screen = Screen.Home }
                SettingsScreen(settings, onHelp = { screen = Screen.Help }, onBack = { screen = Screen.Home })
            }
            Screen.Help -> {
                BackHandler { screen = Screen.Settings }
                ControllerHelpScreen(onBack = { screen = Screen.Settings })
            }
            Screen.Career -> {
                BackHandler { screen = Screen.Home }
                CareerScreen(coach, drills, onBack = { screen = Screen.Home })
            }
            is Screen.Setup -> {
                val back = if (s.drill == null) Screen.Home else Screen.Practice
                BackHandler { screen = back }
                SetupScreen(
                    settings, s.drill,
                    onStart = {
                        val cfg = if (s.drill == null) settings.matchConfig() else settings.drillConfig(s.drill)
                        screen = Screen.Play(cfg, s.drill)
                    },
                    onBack = { screen = back },
                )
            }
            is Screen.Play -> MatchScreen(
                startConfig = s.config, settings = settings, drill = s.drill, layout = layout, coach = coach, drills = drills,
                onTrain = { screen = Screen.Setup(it) },
                onExit = { screen = if (s.drill == null) Screen.Home else Screen.Practice },
            )
        }
    }
}
