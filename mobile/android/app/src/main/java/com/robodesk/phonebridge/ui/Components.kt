package com.robodesk.phonebridge.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay

private val cardShape = RoundedCornerShape(RoboDims.CardRadius)
private val inputShape = RoundedCornerShape(RoboDims.InputRadius)
private val buttonShape = RoundedCornerShape(RoboDims.ButtonRadius)

/** `.card`: rounded panel with optional 12sp uppercase header. */
@Composable
fun RoboCard(title: String? = null, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier.fillMaxWidth().clip(cardShape).background(RoboColors.Card).border(1.dp, RoboColors.Line, cardShape).padding(RoboDims.CardPadding)
    ) {
        if (title != null) {
            Text(title.uppercase(), color = RoboColors.Mute, fontSize = 12.sp, letterSpacing = 0.72.sp, fontWeight = FontWeight.SemiBold,
                modifier = Modifier.padding(bottom = 10.dp))
        }
        content()
    }
}

/** `.row`: label (+ small sublabel) on the left, control on the right; separator line unless [last]. */
@Composable
fun RoboRow(label: String, sublabel: String? = null, last: Boolean = false, trailing: @Composable RowScope.() -> Unit = {}) {
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().padding(vertical = 9.dp), horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(label, color = RoboColors.Text, fontSize = 14.sp)
                if (sublabel != null) Text(sublabel, color = RoboColors.Mute, fontSize = 11.5.sp)
            }
            trailing()
        }
        if (!last) Box(Modifier.fillMaxWidth().height(1.dp).background(RoboColors.Line))
    }
}

/** `.field`: small label above a full-width control. */
@Composable
fun RoboField(label: String, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(bottom = 10.dp)) {
        Text(label, color = RoboColors.Mute, fontSize = 11.5.sp, modifier = Modifier.padding(bottom = 4.dp))
        content()
    }
}

@Composable
fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit, sublabel: String? = null, enabled: Boolean = true, last: Boolean = false) {
    RoboRow(label, sublabel, last) {
        Switch(checked = checked, onCheckedChange = onChange, enabled = enabled, colors = SwitchDefaults.colors(
            checkedTrackColor = RoboColors.Accent, checkedThumbColor = RoboColors.BtnPrimaryText,
            uncheckedTrackColor = RoboColors.BtnSecondary, uncheckedThumbColor = RoboColors.Mute, uncheckedBorderColor = RoboColors.Line))
    }
}

/** Slider 130dp wide, accent. */
@Composable
fun SliderRow(label: String, value: Float, range: ClosedFloatingPointRange<Float>, onChange: (Float) -> Unit, sublabel: String? = null,
              steps: Int = 0, enabled: Boolean = true, last: Boolean = false) {
    RoboRow(label, sublabel, last) {
        Slider(value = value, onValueChange = onChange, valueRange = range, steps = steps, enabled = enabled, modifier = Modifier.width(130.dp),
            colors = SliderDefaults.colors(thumbColor = RoboColors.Accent, activeTrackColor = RoboColors.Accent, inactiveTrackColor = RoboColors.Line))
    }
}

@Composable
private fun BoxedInput(modifier: Modifier = Modifier, enabled: Boolean = true, content: @Composable BoxScope.() -> Unit) {
    Box(modifier.alpha(if (enabled) 1f else 0.4f).clip(inputShape).background(RoboColors.Input).border(1.dp, RoboColors.Line, inputShape).padding(10.dp),
        contentAlignment = Alignment.CenterStart, content = content)
}

private val inputText = TextStyle(color = RoboColors.Text, fontSize = 14.sp)

@Composable
fun SelectBox(options: List<String>, selected: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        BoxedInput(Modifier.fillMaxWidth().clickable(enabled = enabled) { open = true }, enabled) {
            Text(options.getOrElse(selected) { "" } + "  ▾", color = RoboColors.Text, fontSize = 14.sp, maxLines = 1)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEachIndexed { index, option ->
                DropdownMenuItem(text = { Text(option, color = RoboColors.Text) }, onClick = { open = false; onSelect(index) })
            }
        }
    }
}

/** Select 140dp wide inside a row. */
@Composable
fun SelectRow(label: String, options: List<String>, selected: Int, onSelect: (Int) -> Unit, sublabel: String? = null, enabled: Boolean = true, last: Boolean = false) {
    RoboRow(label, sublabel, last) { SelectBox(options, selected, onSelect, Modifier.width(140.dp), enabled) }
}

@Composable
fun TextFieldRobo(value: String, onChange: (String) -> Unit, modifier: Modifier = Modifier, placeholder: String = "", enabled: Boolean = true,
                  keyboardType: KeyboardType = KeyboardType.Text, visual: VisualTransformation = VisualTransformation.None, maxLength: Int = Int.MAX_VALUE, singleLine: Boolean = true) {
    BoxedInput(modifier.fillMaxWidth(), enabled) {
        if (value.isEmpty() && placeholder.isNotEmpty()) Text(placeholder, color = RoboColors.Mute, fontSize = 14.sp)
        BasicTextField(value = value, onValueChange = { if (it.length <= maxLength) onChange(it) }, enabled = enabled, singleLine = singleLine, minLines = if (singleLine) 1 else 3,
            textStyle = inputText, cursorBrush = SolidColor(RoboColors.Accent), visualTransformation = visual,
            keyboardOptions = KeyboardOptions(keyboardType = keyboardType), modifier = Modifier.fillMaxWidth())
    }
}

@Composable
fun PasswordField(value: String, onChange: (String) -> Unit, modifier: Modifier = Modifier, placeholder: String = "", enabled: Boolean = true, maxLength: Int = Int.MAX_VALUE) =
    TextFieldRobo(value, onChange, modifier, placeholder, enabled, KeyboardType.Password, PasswordVisualTransformation(), maxLength)

@Composable
fun NumberField(value: String, onChange: (String) -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, decimal: Boolean = false) =
    TextFieldRobo(value, onChange, modifier, "", enabled, if (decimal) KeyboardType.Decimal else KeyboardType.Number)

/** HH:MM field backed by minutes-of-day (0..1439); invalid text is held locally and not reported. */
@Composable
fun TimeField(minutes: Int, onChange: (Int) -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true) {
    var text by remember(minutes) { mutableStateOf("%02d:%02d".format(minutes / 60, minutes % 60)) }
    TextFieldRobo(text, { value ->
        text = value
        val parts = value.split(":")
        val h = parts.getOrNull(0)?.toIntOrNull()
        val m = parts.getOrNull(1)?.toIntOrNull()
        if (parts.size == 2 && h != null && m != null && h in 0..23 && m in 0..59) onChange(h * 60 + m)
    }, modifier, "HH:MM", enabled, KeyboardType.Number, maxLength = 5)
}

enum class ButtonKind { PRIMARY, SECONDARY, DANGER }

@Composable
fun RoboButton(text: String, onClick: () -> Unit, modifier: Modifier = Modifier, kind: ButtonKind = ButtonKind.PRIMARY, enabled: Boolean = true) {
    val (bg, fg) = when (kind) {
        ButtonKind.PRIMARY -> RoboColors.Accent to RoboColors.BtnPrimaryText
        ButtonKind.SECONDARY -> RoboColors.BtnSecondary to RoboColors.Text
        ButtonKind.DANGER -> RoboColors.BtnDanger to RoboColors.BtnDangerText
    }
    Box(modifier.alpha(if (enabled) 1f else 0.4f).clip(buttonShape).background(bg)
        .clickable(enabled = enabled, onClick = onClick).padding(horizontal = 14.dp, vertical = 11.dp), contentAlignment = Alignment.Center) {
        Text(text, color = fg, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
    }
}

enum class TagKind { OK, WARN, NEUTRAL }

@Composable
fun Tag(text: String, kind: TagKind = TagKind.NEUTRAL) {
    val (bg, fg) = when (kind) {
        TagKind.OK -> RoboColors.ChipOkBg to RoboColors.Ok
        TagKind.WARN -> RoboColors.TagWarnBg to RoboColors.Warn
        TagKind.NEUTRAL -> RoboColors.BtnSecondary to RoboColors.Mute
    }
    Text(text, color = fg, fontSize = 10.5.sp, modifier = Modifier.clip(CircleShape).background(bg).padding(horizontal = 8.dp, vertical = 2.dp))
}

/** Connection chip with status dot: green "Terhubung" or red "Terputus". */
@Composable
fun StatusChip(connected: Boolean, text: String = if (connected) "Terhubung · aman" else "Terputus") {
    val bg = if (connected) RoboColors.ChipOkBg else RoboColors.ChipOffBg
    val fg = if (connected) RoboColors.Ok else RoboColors.Bad
    val border = if (connected) RoboColors.ChipOkBorder else RoboColors.ChipOffBorder
    Row(Modifier.clip(CircleShape).background(bg).border(BorderStroke(1.dp, border), CircleShape).padding(horizontal = 10.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Box(Modifier.size(7.dp).clip(CircleShape).background(fg))
        Text(text, color = fg, fontSize = 11.5.sp)
    }
}

/** Card whose header toggles the body; closed by default except where the mockup opens it. */
@Composable
fun AccordionCard(title: String, initiallyOpen: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
    var open by remember { mutableStateOf(initiallyOpen) }
    RoboCard {
        Row(Modifier.fillMaxWidth().clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { open = !open }.padding(vertical = 2.dp),
            horizontalArrangement = Arrangement.SpaceBetween) {
            Text(title, color = RoboColors.Text, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
            Text(if (open) "▴" else "▾", color = RoboColors.Mute)
        }
        AnimatedVisibility(open) { Column(Modifier.padding(top = 10.dp), content = content) }
    }
}

/** Yellow sticky bar at the bottom of a screen with unsaved changes. */
@Composable
fun SaveBar(visible: Boolean, onSave: () -> Unit, enabled: Boolean = true, modifier: Modifier = Modifier) {
    if (!visible) return
    val shape = RoundedCornerShape(14.dp)
    Row(modifier.fillMaxWidth().clip(shape).background(RoboColors.SaveBarBg).border(1.dp, RoboColors.SaveBarBorder, shape).padding(horizontal = 12.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
        Text("Ada perubahan belum disimpan", color = RoboColors.Warn, fontSize = 13.sp, modifier = Modifier.weight(1f))
        RoboButton("Simpan", onSave, enabled = enabled)
    }
}

@Stable
class ToastState {
    var message by mutableStateOf<String?>(null)
        private set
    var token by mutableStateOf(0)
        private set

    fun show(text: String) { message = text; token++ }
    internal fun clear(forToken: Int) { if (forToken == token) message = null }
}

/** Floating toast 84dp above the bottom; disappears after 2.2 s. Place inside a Box that fills the screen. */
@Composable
fun BoxScope.ToastHost(state: ToastState) {
    val text = state.message
    val token = state.token
    LaunchedEffect(token) { if (text != null) { delay(2200); state.clear(token) } }
    if (text != null) {
        val shape = RoundedCornerShape(RoboDims.ButtonRadius)
        Box(Modifier.align(Alignment.BottomCenter).padding(start = 16.dp, end = 16.dp, bottom = RoboDims.ToastBottom).fillMaxWidth()
            .clip(shape).background(RoboColors.Toast).border(1.dp, RoboColors.ToastBorder, shape).padding(horizontal = 14.dp, vertical = 10.dp)) {
            Text(text, color = RoboColors.Text, fontSize = 13.sp)
        }
    }
}

/** Modal bottom sheet: translucent scrim, panel with 22dp top corners. Tap on the scrim dismisses. */
@Composable
fun BoxScope.BottomSheetHost(visible: Boolean, onDismiss: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    if (!visible) return
    Box(Modifier.fillMaxSize().background(RoboColors.Scrim).clickable(interactionSource = remember { MutableInteractionSource() }, indication = null, onClick = onDismiss)) {
        val shape = RoundedCornerShape(topStart = RoboDims.SheetRadius, topEnd = RoboDims.SheetRadius)
        Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().clip(shape).background(RoboColors.Panel)
            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {}
            .padding(start = 16.dp, end = 16.dp, top = 18.dp, bottom = 24.dp), content = content)
    }
}

/** 8dp progress bar: accent fill over #12161C. */
@Composable
fun ProgressBar(fraction: Float, modifier: Modifier = Modifier) {
    val shape = RoundedCornerShape(99.dp)
    Box(modifier.fillMaxWidth().height(8.dp).clip(shape).background(RoboColors.Input)) {
        Box(Modifier.fillMaxWidth(fraction.coerceIn(0f, 1f)).height(8.dp).clip(shape).background(RoboColors.Accent))
    }
}
