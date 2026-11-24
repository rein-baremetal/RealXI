package com.realxi.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

object RxColors {
    val Night = Color(0xFF0B1F12)
    val Panel = Color(0xE6102A19)
    val PanelSolid = Color(0xFF102A19)
    val Grass = Color(0xFF2E8B3D)
    val GrassDark = Color(0xFF277A35)
    val Line = Color(0xFFEAF2E8)
    val Accent = Color(0xFFFFD84A)
    val Danger = Color(0xFFE5534B)
    val Good = Color(0xFF59D26B)
    val Text = Color(0xFFF2F7F0)
    val Muted = Color(0xFF9DB5A2)
    val Home = Color(0xFFE23B3B)
    val HomeTrim = Color(0xFFFFFFFF)
    val Away = Color(0xFF2F6FE0)
    val AwayTrim = Color(0xFFFFFFFF)
    val Skin = Color(0xFFE8B98F)
}

private val scheme = darkColorScheme(
    primary = RxColors.Accent,
    onPrimary = Color(0xFF1A1A00),
    secondary = RxColors.Good,
    background = RxColors.Night,
    surface = RxColors.PanelSolid,
    onSurface = RxColors.Text,
    onBackground = RxColors.Text,
    error = RxColors.Danger,
)

@Composable
fun RealXiTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = scheme, content = content)
}
