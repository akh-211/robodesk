package com.robodesk.phonebridge.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.UiNotice

private val NAV_ICONS = listOf("⌂", "⚙", "⌁", "✎", "⏰", "≡")

/** Whole app: onboarding overlay or tab shell, plus the toast and the sheet layer. */
@Composable
fun RoboRoot() {
    val app = RoboApp
    RoboTheme {
        BackHandler(enabled = !app.onboarding && (app.sheet != null || app.sub != null || app.tab != 0)) { app.back() }
        Box(Modifier.fillMaxSize().background(RoboColors.Bg).systemBarsPadding()) {
            if (app.onboarding) OnboardingScreen() else MainShell()
            ToastHost(app.toast)
            BottomSheetHost(app.sheet != null && !app.onboarding, onDismiss = { app.sheet = null }) { SheetContent() }
        }
    }
}

@Composable
private fun MainShell() {
    val app = RoboApp
    Column(Modifier.fillMaxSize()) {
        AppBar()
        app.notice?.let { NoticeBar(it) }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (app.tab) {
                0 -> HomeScreen()
                1 -> SettingsScreen()
                2 -> NetScreen()
                3 -> MemoryScreen()
                4 -> ReminderScreen()
                else -> MoreScreen()
            }
            val showSave = app.dirty && app.sub == null && app.tab in 0..1
            if (showSave) SaveBar(true, onSave = { app.save() }, enabled = app.canWrite,
                modifier = Modifier.align(Alignment.BottomCenter).padding(horizontal = 16.dp, vertical = 8.dp))
        }
        NavBar()
    }
}

@Composable
private fun AppBar() {
    val app = RoboApp
    val sub = app.sub
    Row(Modifier.fillMaxWidth().background(RoboColors.Panel).padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.weight(1f)) {
            if (sub != null) Text("‹", color = RoboColors.Text, fontSize = 26.sp, modifier = Modifier.clickable { app.sub = null }.padding(horizontal = 10.dp, vertical = 2.dp))
            Text(if (sub != null) SUB_TITLES[sub].orEmpty() else TAB_TITLES[app.tab], color = RoboColors.Text, fontSize = 20.sp,
                fontWeight = FontWeight.Bold, maxLines = 1, modifier = Modifier.padding(start = if (sub == null) 4.dp else 0.dp))
        }
        val updating = app.otaStartedAt != 0L && !app.ready
        StatusChip(app.ready, if (updating) "Update berjalan · terputus" else if (app.ready) "Terhubung · aman" else "Terputus")
    }
}

/** A10: one line explaining why the app cannot do its normal job (mockup `.banner` look). */
@Composable
private fun NoticeBar(notice: UiNotice) {
    val color = if (notice.bad) RoboColors.Bad else RoboColors.Warn
    Column(Modifier.fillMaxWidth().background(RoboColors.SaveBarBg).padding(horizontal = 16.dp, vertical = 8.dp)) {
        Text(notice.text, color = color, fontSize = 12.5.sp)
        notice.action?.let { RoboButton(it, { RoboApp.noticeAction(notice) }, Modifier.padding(top = 8.dp)) }
    }
}

@Composable
private fun NavBar() {
    val app = RoboApp
    Row(Modifier.fillMaxWidth().background(RoboColors.Panel).padding(vertical = 6.dp)) {
        TAB_TITLES.forEachIndexed { i, title ->
            val active = app.tab == i
            val label = if (i == 4) "Ingat" else title
            Column(Modifier.weight(1f).clickable { app.openTab(i) }.padding(vertical = 2.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Box(Modifier.clip(CircleShape).background(if (active) RoboColors.NavActivePill else RoboColors.Panel).padding(horizontal = 16.dp, vertical = 3.dp)) {
                    Text(NAV_ICONS[i], color = if (active) RoboColors.Accent else RoboColors.Mute, fontSize = 18.sp)
                }
                Text(label, color = if (active) RoboColors.Accent else RoboColors.Mute, fontSize = 10.5.sp, maxLines = 1)
            }
        }
    }
}

/** Scrolling column with the 16dp screen padding used by every tab. Leaves room for the SaveBar. */
@Composable
fun ScreenColumn(content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 16.dp, end = 16.dp, top = 12.dp, bottom = 84.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp), content = content)
}

@Composable
fun Note(text: String, modifier: Modifier = Modifier) = Text(text, color = RoboColors.Mute, fontSize = 11.5.sp, modifier = modifier.padding(top = 4.dp))

@Composable
fun ValueText(text: String, color: androidx.compose.ui.graphics.Color = RoboColors.Mute) = Text(text, color = color, fontSize = 13.sp)

/** `.row` with a plain value on the right. */
@Composable
fun InfoRow(label: String, value: String, valueColor: androidx.compose.ui.graphics.Color = RoboColors.Mute, last: Boolean = false, sublabel: String? = null) =
    RoboRow(label, sublabel, last) { ValueText(value, valueColor) }

@Composable
fun ButtonRow(content: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit) =
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), content = content)

@Composable
fun Gap(h: Int) = androidx.compose.foundation.layout.Spacer(Modifier.size(h.dp))
