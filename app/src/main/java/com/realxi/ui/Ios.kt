package com.realxi.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** iOS-style palette and building blocks (grouped lists, large titles, segmented controls, green switches). */
object Ios {
    val Background = Color(0xFFF2F2F7)
    val Card = Color(0xFFFFFFFF)
    val Label = Color(0xFF000000)
    val Secondary = Color(0xFF8E8E93)
    val Separator = Color(0xFFD1D1D6)
    val Blue = Color(0xFF007AFF)
    val Green = Color(0xFF34C759)
    val Orange = Color(0xFFFF9500)
    val Red = Color(0xFFFF3B30)
    val Fill = Color(0x1F767680)
}

/** Large-title screen with a "‹ Back" link, on the iOS grouped background. */
@Composable
fun IosScreen(title: String, onBack: (() -> Unit)?, trailing: @Composable () -> Unit = {}, content: @Composable () -> Unit) {
    Column(
        Modifier.fillMaxSize().background(Ios.Background).windowInsetsPadding(WindowInsets.safeDrawing)
            .padding(horizontal = 20.dp, vertical = 6.dp),
    ) {
        Row(Modifier.fillMaxWidth().heightIn(min = 44.dp), verticalAlignment = Alignment.CenterVertically) {
            if (onBack != null) {
                Text("‹ Back", color = Ios.Blue, fontSize = 17.sp, maxLines = 1, softWrap = false,
                    modifier = Modifier.clip(RoundedCornerShape(8.dp)).clickable(onClick = onBack).padding(end = 14.dp, top = 8.dp, bottom = 8.dp))
            }
            Text(title, color = Ios.Label, fontSize = 26.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f))
            trailing()
        }
        Box(Modifier.padding(top = 6.dp).fillMaxSize()) { content() }
    }
}

/** Rounded white group. Children are separated by hairlines via [IosDivider]. */
@Composable
fun IosGroup(header: String? = null, modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Column(modifier.fillMaxWidth()) {
        if (header != null) Text(header.uppercase(), color = Ios.Secondary, fontSize = 12.sp, modifier = Modifier.padding(start = 14.dp, bottom = 5.dp))
        Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(Ios.Card)) { content() }
    }
}

@Composable
fun IosDivider() {
    Box(Modifier.fillMaxWidth().padding(start = 14.dp).height(0.5.dp).background(Ios.Separator))
}

/** A tappable list row with title, optional subtitle/value and a chevron. */
@Composable
fun IosNavRow(title: String, subtitle: String? = null, value: String? = null, tint: Color = Ios.Label, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick).defaultMinSize(minHeight = 50.dp).padding(horizontal = 14.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, color = tint, fontSize = 17.sp)
            if (subtitle != null) Text(subtitle, color = Ios.Secondary, fontSize = 13.sp)
        }
        if (value != null) Text(value, color = Ios.Secondary, fontSize = 15.sp, modifier = Modifier.padding(start = 8.dp), maxLines = 1)
        Text("›", color = Ios.Separator, fontSize = 24.sp, modifier = Modifier.padding(start = 8.dp))
    }
}

@Composable
fun IosToggleRow(label: String, value: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().defaultMinSize(minHeight = 50.dp).padding(horizontal = 14.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = Ios.Label, fontSize = 17.sp, modifier = Modifier.weight(1f).padding(end = 12.dp))
        Switch(
            checked = value, onCheckedChange = onChange,
            colors = SwitchDefaults.colors(
                checkedTrackColor = Ios.Green, checkedThumbColor = Color.White, checkedBorderColor = Ios.Green,
                uncheckedTrackColor = Color(0xFFE9E9EA), uncheckedThumbColor = Color.White, uncheckedBorderColor = Color(0xFFE9E9EA),
            ),
        )
    }
}

@Composable
fun IosSliderRow(label: String, value: Float, display: String, onChange: (Float) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 8.dp)) {
        Row(Modifier.fillMaxWidth()) {
            Text(label, color = Ios.Label, fontSize = 17.sp, modifier = Modifier.weight(1f))
            Text(display, color = Ios.Secondary, fontSize = 15.sp)
        }
        Slider(
            value = value, onValueChange = onChange,
            colors = SliderDefaults.colors(thumbColor = Color.White, activeTrackColor = Ios.Blue, inactiveTrackColor = Ios.Fill),
        )
    }
}

/** iOS segmented control: equal-width segments, selected one is a raised white pill. Use for up to ~5 short labels. */
@Composable
fun IosSegmented(options: List<String>, selected: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    val outer = RoundedCornerShape(9.dp)
    Row(modifier.fillMaxWidth().clip(outer).background(Ios.Fill).padding(2.dp)) {
        options.forEachIndexed { i, text ->
            val sel = i == selected
            Box(
                Modifier.weight(1f).height(34.dp).clip(RoundedCornerShape(7.dp))
                    .background(if (sel) Color.White else Color.Transparent)
                    .clickableNoRipple { onSelect(i) },
                contentAlignment = Alignment.Center,
            ) {
                Text(text, color = Ios.Label, fontSize = 13.sp, fontWeight = if (sel) FontWeight.SemiBold else FontWeight.Normal,
                    maxLines = 1, softWrap = false, textAlign = TextAlign.Center, modifier = Modifier.padding(horizontal = 2.dp))
            }
        }
    }
}

/** Labelled setting with a segmented control and optional caption under it. */
@Composable
fun IosSegRow(label: String, options: List<String>, selected: Int, caption: String? = null, onSelect: (Int) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(label, color = Ios.Label, fontSize = 17.sp)
        IosSegmented(options, selected, onSelect)
        if (caption != null) Text(caption, color = Ios.Secondary, fontSize = 12.sp)
    }
}

/** Wrapping pill choices for longer lists (formations). Pills keep their natural width, so nothing is squeezed. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun IosPillRow(label: String, options: List<String>, selected: Int, onSelect: (Int) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(label, color = Ios.Label, fontSize = 17.sp)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEachIndexed { i, text ->
                val sel = i == selected
                Box(
                    Modifier.defaultMinSize(minHeight = 36.dp).clip(RoundedCornerShape(18.dp))
                        .background(if (sel) Ios.Blue else Ios.Fill).clickableNoRipple { onSelect(i) }.padding(horizontal = 14.dp, vertical = 7.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(text, color = if (sel) Color.White else Ios.Label, fontSize = 14.sp, fontWeight = FontWeight.Medium, maxLines = 1, softWrap = false)
                }
            }
        }
    }
}

/** Filled capsule button. Always fits its label. */
@Composable
fun IosButton(text: String, onClick: () -> Unit, modifier: Modifier = Modifier, color: Color = Ios.Green, filled: Boolean = true) {
    val shape = RoundedCornerShape(14.dp)
    Box(
        modifier.defaultMinSize(minHeight = 50.dp).clip(shape)
            .background(if (filled) color else color.copy(alpha = 0.14f)).clickable(onClick = onClick).padding(horizontal = 20.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = if (filled) Color.White else color, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, softWrap = false)
    }
}

/** Small tinted pill, e.g. "Train" or "Recommended". */
@Composable
fun IosTag(text: String, color: Color, onClick: (() -> Unit)? = null) {
    Box(
        Modifier.clip(RoundedCornerShape(12.dp)).background(color.copy(alpha = 0.14f))
            .let { if (onClick != null) it.clickable(onClick = onClick) else it }.padding(horizontal = 12.dp, vertical = 6.dp),
        contentAlignment = Alignment.Center,
    ) { Text(text, color = color, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, softWrap = false) }
}
