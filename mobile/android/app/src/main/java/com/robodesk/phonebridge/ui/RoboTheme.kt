package com.robodesk.phonebridge.ui

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** Design tokens copied from docs/mobile/mockup/android_app_mockup.html (dark only, no dynamic color). */
object RoboColors {
    val Bg = Color(0xFF0E1116)
    val Panel = Color(0xFF171B22)
    val Card = Color(0xFF1E232C)
    val Line = Color(0xFF2A313C)
    val Accent = Color(0xFF4FD1C5)
    val Accent2 = Color(0xFF7C9CFF)
    val Ok = Color(0xFF4ADE80)
    val Warn = Color(0xFFFBBF24)
    val Bad = Color(0xFFF87171)
    val Text = Color(0xFFE8ECF2)
    val Mute = Color(0xFF8D97A7)
    val Input = Color(0xFF12161C)
    val BtnSecondary = Color(0xFF262D39)
    val BtnDanger = Color(0xFF4A1C1C)
    val BtnDangerText = Color(0xFFFFB4B4)
    val BtnPrimaryText = Color(0xFF04201D)
    val ChipOkBg = Color(0xFF12301F); val ChipOkBorder = Color(0xFF1D5A35)
    val ChipOffBg = Color(0xFF3A1717); val ChipOffBorder = Color(0xFF6A2626)
    val TagWarnBg = Color(0xFF2E260C)
    val SaveBarBg = Color(0xFF231D0A); val SaveBarBorder = Color(0xFF6A5413)
    val NavActivePill = Color(0xFF173532)
    val Toast = Color(0xFF2B3340); val ToastBorder = Color(0xFF3A4454)
    val Scrim = Color(0xAA000000)
}

object RoboDims {
    val ScreenPadding = 16.dp
    val CardPadding = 14.dp
    val CardRadius = 16.dp
    val ButtonRadius = 12.dp
    val InputRadius = 10.dp
    val SheetRadius = 22.dp
    val ToastBottom = 84.dp
}

private val scheme = darkColorScheme(
    primary = RoboColors.Accent, onPrimary = RoboColors.BtnPrimaryText,
    secondary = RoboColors.Accent2, background = RoboColors.Bg, onBackground = RoboColors.Text,
    surface = RoboColors.Card, onSurface = RoboColors.Text, surfaceVariant = RoboColors.Panel,
    onSurfaceVariant = RoboColors.Mute, outline = RoboColors.Line, error = RoboColors.Bad
)

private val shapes = Shapes(
    small = RoundedCornerShape(RoboDims.InputRadius), medium = RoundedCornerShape(RoboDims.ButtonRadius),
    large = RoundedCornerShape(RoboDims.CardRadius), extraLarge = RoundedCornerShape(RoboDims.SheetRadius)
)

private val type = Typography(
    titleLarge = TextStyle(fontFamily = FontFamily.Default, fontSize = 20.sp, fontWeight = FontWeight.Bold),
    bodyMedium = TextStyle(fontFamily = FontFamily.Default, fontSize = 14.sp),
    labelSmall = TextStyle(fontFamily = FontFamily.Default, fontSize = 11.5.sp)
)

@Composable
fun RoboTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = scheme, shapes = shapes, typography = type, content = content)
}
