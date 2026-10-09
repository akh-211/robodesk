"""Create an original SVG assembly diagram from the RoboDesk dual-board netlist.

Terminal locations are functional illustration coordinates, not header pin order.
No source bitmap is edited. Render the resulting SVG to PNG with a browser.
"""
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/hardware/robodesk-s3-c3-assembly-wiring.svg"
RED, ORANGE, GND = "#e32932", "#ee850c", "#303a43"
UART, SDA, SCL = "#8946db", "#1375dc", "#179451"
MIC, AMP, TOUCH, IR = "#11acbd", "#007d86", "#b132ca", "#865022"
BAT = "#b35c13"
INK = "#152b42"
parts, wires, dots, labels = [], [], [], []


def text(x, y, value, size=24, color=INK, anchor="start", layer=None, weight=400):
    (labels if layer is None else layer).append(
        f'<text x="{x}" y="{y}" fill="{color}" font-size="{size}" '
        f'text-anchor="{anchor}" font-weight="{weight}">{escape(value)}</text>')


def rect(x, y, w, h, fill, stroke="#a5b9c7", radius=14, layer=parts):
    layer.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" '
                 f'fill="{fill}" stroke="{stroke}" stroke-width="2"/>')


def wire(points, color, name):
    d = "M " + " L ".join(f"{x},{y}" for x, y in points)
    wires.append(f'<g><title>{escape(name)}</title><path d="{d}" fill="none" '
                 f'stroke="white" stroke-width="13" stroke-linejoin="round"/>'
                 f'<path d="{d}" fill="none" stroke="{color}" stroke-width="6" '
                 f'stroke-linecap="round" stroke-linejoin="round"/></g>')


def node(x, y, color):
    dots.append(f'<circle cx="{x}" cy="{y}" r="7" fill="{color}"/>')


def pad(x, y, value, color, side="right", dark=False):
    parts.append(f'<circle cx="{x}" cy="{y}" r="10" fill="{color}" stroke="#f0cb5d" stroke-width="3"/>')
    tx = x + 18 if side == "left" else x - 18
    text(tx, y + 8, value, 23, "white" if dark else INK,
         "start" if side == "left" else "end", weight=600)


def module(x, y, w, h, title, subtitle="", fill="#eaf4ff"):
    rect(x, y, w, h, fill)
    text(x + w / 2, y + 31, title, 27, anchor="middle", weight=700)
    if subtitle:
        text(x + w / 2, y + 58, subtitle, 20, anchor="middle")


def resistor(x1, x2, y, label, color, label_y=None):
    mid = (x1 + x2) / 2
    wire([(x1, y), (mid - 30, y)], color, label)
    wire([(mid + 30, y), (x2, y)], color, label)
    rect(mid - 30, y - 13, 60, 26, "#f5d5a3", "#ad8253", 5)
    multiplier = "#8e4d25" if label == "100 Ω" else "#cc403a"
    for dx, c in [(-16, "#8e4d25"), (-5, "#20252b"), (7, multiplier), (19, "#c2a549")]:
        parts.append(f'<path d="M{mid+dx},{y-12} V{y+12}" stroke="{c}" stroke-width="5"/>')
    text(mid, label_y or y - 23, label, 22, anchor="middle", weight=600)


def vertical_resistor(x, top, bottom, label, color):
    middle = (top + bottom) / 2
    wire([(x, top), (x, middle - 33)], color, label)
    wire([(x, middle + 33), (x, bottom)], GND, label)
    rect(x - 13, middle - 33, 26, 66, "#f5d5a3", "#ad8253", 5)
    for dy, c in [(-18, "#8e4d25"), (-5, "#20252b"), (9, "#f2c744"), (23, "#c2a549")]:
        parts.append(f'<path d="M{x-12},{middle+dy} H{x+12}" stroke="{c}" stroke-width="5"/>')
    text(x + 23, middle + 8, label, 22, weight=600)


def capacitor(x, top, bottom, label, color=RED, polarized=False, label_side=1):
    mid = (top + bottom) / 2
    wire([(x, top), (x, mid - 9)], color, label + " positive/supply")
    wire([(x, mid + 9), (x, bottom)], GND, label + " ground")
    parts.append(f'<path d="M{x-25},{mid-9} H{x+25} M{x-25},{mid+9} H{x+25}" stroke="{INK}" stroke-width="6"/>')
    text(x + label_side * 38, mid + 7, label, 21, anchor="start" if label_side > 0 else "end")
    if polarized:
        text(x - 33, mid - 20, "+", 23, RED, anchor="end", weight=700)


# Power source, terminals and isolation switch.
text(70, 76, "RoboDesk — Rangkaian Kabel ESP32-S3 + ESP32-C3", 54, weight=700)
text(70, 118, "Terminal ilustratif: ikuti nama GPIO/pin pada board. Titik = sambungan; persilangan tanpa titik tidak tersambung.", 27)
text(80, 195, "1. DAYA: baterai → IP5310 → saklar → cabang 5 V", 30, RED, weight=700)
rect(80, 240, 150, 230, "#c5e8fb", "#168cb7", 28)
text(155, 288, "+", 45, anchor="middle", weight=700)
text(155, 430, "−", 45, anchor="middle", weight=700)
pad(230, 300, "", RED)
pad(230, 430, "", GND)
text(80, 510, "Baterai terlindungi 1S", 25, weight=700)
text(80, 543, "3,7 V nominal • 4,2 V penuh", 23)
module(390, 220, 400, 250, "IP5310 MODULE", "Terminal sesuai label modul", "#e8efe8")
pad(390, 300, "B+", RED, "left")
pad(390, 420, "B−", GND, "left")
pad(790, 300, "OUT +5V", RED)
pad(790, 420, "OUT GND", GND)
rect(525, 370, 125, 37, "#222e38", radius=15)
text(585, 448, "USB-C: charging", 21, anchor="middle")
rect(850, 273, 65, 57, "#38414c", "#101920", 7)
text(882, 268, "ON/OFF", 21, anchor="middle", weight=700)
wire([(230, 300), (390, 300)], RED, "Battery positive → IP5310 B+")
wire([(230, 430), (310, 430), (310, 420), (390, 420)], GND, "Battery negative → IP5310 B−")
wire([(790, 300), (850, 300)], RED, "IP5310 OUT5V → switch")
wire([(915, 300), (1100, 300), (1100, 220), (1720, 220), (1720, 245), (1810, 245)], RED, "Switch → separate C3 5V branch")
wire([(1100, 300), (1100, 1810)], RED, "Switched 5V distribution trunk")
wire([(1100, 580), (1300, 580), (1300, 650)], RED, "Separate S3 5V branch")
wire([(1100, 1360), (760, 1360)], RED, "Separate amplifier 5V branch")
wire([(1100, 1810), (1820, 1810), (1820, 1900), (1910, 1900)], RED, "Switched 5V → IR current-limiting resistor")
wire([(790, 420), (920, 420), (920, 2200), (2960, 2200), (2960, 570)], GND, "Common ground star return")
for p in [(1100, 300), (1100, 580), (1100, 1360), (1100, 1810)]:
    node(*p, RED)
text(1210, 193, "+5 V SETELAH SAKLAR", 24, RED, weight=700)

# MCU functional terminals. No physical header order is asserted.
rect(1200, 650, 420, 1140, "#18342f", "#0c2422", 19)
text(1410, 698, "ESP32-S3 DEVKIT N16R8", 28, "white", "middle", weight=700)
text(1410, 729, "Robot • audio • sensor • IR", 22, "#b9e9df", "middle")
text(1410, 759, "Wi-Fi / BLE OFF", 23, "#ffca75", "middle", weight=700)
rect(1360, 822, 100, 155, "#242f37", "#8b9a9f", 6)
text(1410, 886, "ESP32", 20, "white", "middle")
text(1410, 916, "S3", 24, "white", "middle", weight=700)
rect(1345, 1725, 130, 40, "#a4b5c0", "#dfebf2", 8)
pad(1300, 650, "", RED)
pad(1400, 650, "", ORANGE)
text(1300, 637, "5V IN", 22, RED, "middle", weight=700)
text(1400, 637, "3V3 OUT", 22, ORANGE, "middle", weight=700)
for y, label, color in [(990, "GPIO4 / WS", MIC), (1050, "GPIO5 / BCLK", MIC),
                         (1110, "GPIO6 / DATA", MIC), (1280, "GPIO11 / DIN", AMP),
                         (1340, "GPIO12 / BCLK", AMP), (1400, "GPIO13 / LRC", AMP),
                         (1700, "GND", GND)]:
    pad(1200, y, label, color, "left", True)
for y, label, color in [(810, "GPIO17 / TX", UART), (870, "GPIO18 / RX", UART),
                         (960, "GPIO8 / SDA", SDA), (1020, "GPIO9 / SCL", SCL),
                         (1150, "GPIO7 / touch", TOUCH), (1210, "GPIO16 / touch", TOUCH),
                         (1270, "GPIO15 / PIR", TOUCH), (1400, "GPIO1 / IR RX", IR),
                         (1470, "GPIO2 / IR TX", IR), (1530, "GPIO10 / BAT ADC", BAT)]:
    pad(1620, y, label, color, "right", True)
rect(1810, 180, 370, 330, "#174563", "#0d3048", 17)
text(1995, 212, "ESP32-C3 SuperMini", 28, "white", "middle", weight=700)
text(1995, 464, "Wi-Fi / BLE ON", 25, "#bce9ff", "middle", weight=700)
for y, label, color in [(245, "5V IN", RED), (330, "3V3: NC", "#87949d"), (425, "GND", GND)]:
    pad(1810, y, label, color, "left", True)
pad(2180, 320, "GPIO4 / TX", UART, "right", True)
pad(2180, 400, "GPIO5 / RX", UART, "right", True)
wire([(1620, 810), (2330, 810), (2330, 400), (2180, 400)], UART, "S3 GPIO17 TX → C3 GPIO5 RX")
wire([(2180, 320), (2290, 320), (2290, 870), (1620, 870)], UART, "C3 GPIO4 TX → S3 GPIO18 RX")
wire([(1810, 425), (1700, 425), (1700, 570), (2960, 570)], GND, "C3 ground → common ground")
wire([(1200, 1700), (1180, 1700), (1180, 2200)], GND, "S3 ground → common ground")
text(1810, 535, "UART 921600 • 8N1 • 3,3 V", 23, UART, weight=700)
text(1810, 600, "C3 GPIO8/9: tidak dipakai", 23)
text(1810, 632, "Output 3V3 C3 tidak disambung", 23, RED, weight=700)
capacitor(1660, 245, 340, "470 µF / 10 V", polarized=True, label_side=-1)
wire([(1720, 245), (1660, 245)], RED, "C3 bulk capacitor supply")
wire([(1660, 340), (1700, 340), (1700, 425)], GND, "C3 bulk capacitor ground")
node(1720, 245, RED)
node(1700, 425, GND)
capacitor(1150, 580, 690, "470 µF / 10 V", polarized=True, label_side=-1)
wire([(1150, 690), (1180, 690), (1180, 1700)], GND, "S3 bulk capacitor ground")
node(1150, 580, RED)
node(1180, 1700, GND)

# 3V3 comes exclusively from the S3 regulator; both trunks are continuous.
wire([(1400, 650), (1400, 550), (2900, 550), (2900, 2080)], ORANGE, "S3 3V3 → peripheral supply distribution")
wire([(1400, 550), (1000, 550), (1000, 1960), (1220, 1960)], ORANGE, "S3 3V3 left peripheral supply distribution")
node(1400, 550, ORANGE)
text(2590, 525, "3V3 DARI S3", 24, ORANGE, weight=700)

# Protected-cell voltage monitor. The divider sense stays below the ADC input limit.
module(2240, 180, 680, 310, "BATTERY VOLTAGE MONITOR", "S3 ADC • 100 kΩ : 100 kΩ", "#fff3e6")
pad(2240, 250, "CELL B+", RED, "left")
pad(2240, 440, "COMMON GND", GND, "left")
vertical_resistor(2510, 250, 370, "100 kΩ", RED)
vertical_resistor(2510, 370, 470, "100 kΩ", BAT)
wire([(230, 300), (300, 300), (300, 500), (2190, 500), (2190, 250), (2240, 250)], RED, "Battery B+ branch to divider input; crosses other nets without joining")
node(300, 300, RED)
node(2510, 370, BAT)
pad(2840, 370, "GPIO10 ADC", BAT)
wire([(2510, 370), (2840, 370)], BAT, "1:1 divider midpoint to S3 GPIO10 ADC")
capacitor(2720, 370, 470, "100 nF", BAT, label_side=1)
wire([(2510, 470), (2680, 470), (2680, 500), (2960, 500), (2960, 570)], GND, "Divider and ADC filter return to common ground")
wire([(2240, 440), (2220, 440), (2220, 500), (2960, 500)], GND, "Battery monitor common reference")
node(2960, 500, GND)
wire([(1620, 1530), (1750, 1530), (1750, 2350), (3000, 2350), (3000, 370), (2840, 370)], BAT, "S3 GPIO10 ADC link to divider midpoint")
node(1620, 1530, BAT)

# I2C modules: all four wires are shown, with branches on their own nets.
text(2440, 490, "2. SENSOR I2C PARALEL", 29, SDA, weight=700)
wire([(1620, 960), (2110, 960), (2110, 645), (2110, 1335)], SDA, "S3 GPIO8 SDA shared bus")
wire([(1620, 1020), (2160, 1020), (2160, 700), (2160, 1390)], SCL, "S3 GPIO9 SCL shared bus")
node(2110, 960, SDA)
node(2160, 1020, SCL)
for y, title, address in [(580, "OLED SSD1306", "0x3C"), (810, "MPU6050", "0x68"),
                           (1040, "AHT20", "0x38"), (1270, "BMP280", "0x77")]:
    module(2440, y, 440, 180, title + "  " + address, fill="#edf6ff")
    if title.startswith("OLED"):
        rect(2570, y+68, 180, 82, "#172c3e", "#558399", 4)
        text(2660, y+115, "OLED", 23, "#6ccfff", "middle")
    else:
        rect(2620, y+76, 72, 66, "#26394a", "#7791a1", 4)
    pad(2440, y + 65, "SDA", SDA, "left")
    pad(2440, y + 120, "SCL", SCL, "left")
    pad(2880, y + 65, "VCC", ORANGE)
    pad(2880, y + 120, "GND", GND)
    wire([(2110, y + 65), (2440, y + 65)], SDA, title + " SDA → S3 GPIO8")
    wire([(2160, y + 120), (2440, y + 120)], SCL, title + " SCL → S3 GPIO9")
    wire([(2880, y + 65), (2900, y + 65)], ORANGE, title + " VCC → S3 3V3")
    wire([(2880, y + 120), (2960, y + 120)], GND, title + " ground")
    for x, yy, c in [(2110, y+65, SDA), (2160, y+120, SCL), (2900, y+65, ORANGE), (2960, y+120, GND)]:
        node(x, yy, c)
pad(2880, 965, "AD0", GND)
wire([(2880, 965), (2960, 965)], GND, "MPU6050 AD0 to GND for address 0x68")
node(2960, 965, GND)
pad(2880, 1425, "SDO / CS*", ORANGE)
wire([(2880, 1425), (2900, 1425)], ORANGE, "BMP280 SDO and CS to 3V3 if exposed")
node(2900, 1425, ORANGE)
text(2440, 1491, "* SDO dan CS ke 3V3 jika tersedia.", 21)
text(2440, 1520, "Pull-up bus hanya ke 3,3 V.", 21)

# Digital input modules.
for y, name, gpio, route_x, board_y in [(1540, "TTP223 — KEPALA", 7, 2340, 1150),
                                        (1750, "TTP223 — SAMPING", 16, 2360, 1210),
                                        (1960, "AM312 — PIR", 15, 2380, 1270)]:
    module(2440, y, 400, 160, name, fill="#f9eaff")
    parts.append(f'<circle cx="2660" cy="{y+90}" r="32" fill="{("#e45e66" if gpio!=15 else "#eef4f5")}" stroke="#adb9c1" stroke-width="3"/>')
    pad(2440, y + 80, "OUT", TOUCH, "left")
    pad(2840, y + 50, "VCC", ORANGE)
    pad(2840, y + 120, "GND", GND)
    wire([(1620, board_y), (route_x, board_y), (route_x, y + 80), (2440, y + 80)], TOUCH, f"{name} OUT → S3 GPIO{gpio}")
    wire([(2840, y+50), (2900, y+50)], ORANGE, name + " VCC → S3 3V3")
    wire([(2840, y+120), (2960, y+120)], GND, name + " GND")
    node(2900, y+50, ORANGE)
    node(2960, y+120, GND)

# Microphone power and each of its I2S channels, with auxiliary pins.
module(260, 670, 430, 400, "INMP441", fill="#e7fafd")
parts.append('<circle cx="440" cy="810" r="48" fill="#293e4a" stroke="#83a4b4" stroke-width="8"/>')
rect(400, 900, 80, 75, "#233c4c", radius=4)
for y, label, c in [(720, "VDD", ORANGE), (790, "GND", GND), (850, "L/R", GND),
                     (910, "WS", MIC), (960, "SCK / BCLK", MIC), (1010, "SD / DOUT", MIC),
                     (1045, "CHIPEN*", ORANGE)]:
    pad(690, y, label, c)
wire([(690, 720), (1000, 720)], ORANGE, "INMP441 VDD → S3 3V3")
wire([(690, 790), (780, 790), (780, 2200)], GND, "INMP441 ground return")
wire([(690, 850), (780, 850)], GND, "INMP441 L/R → GND: left channel")
wire([(690, 1045), (1000, 1045)], ORANGE, "INMP441 CHIPEN → 3V3 if exposed")
for points, name in [([(690, 910), (1120, 910), (1120, 990), (1200, 990)], "WS → S3 GPIO4"),
                      ([(690, 960), (1080, 960), (1080, 1050), (1200, 1050)], "BCLK → S3 GPIO5"),
                      ([(690, 1010), (1040, 1010), (1040, 1110), (1200, 1110)], "SD → S3 GPIO6")]:
    wire(points, MIC, "INMP441 " + name)
capacitor(750, 720, 790, "100 nF", ORANGE, label_side=-1)
wire([(750, 790), (780, 790)], GND, "Mic capacitor ground")
vertical_resistor(710, 1010, 1170, "100 kΩ", MIC)
wire([(710, 1170), (780, 1170)], GND, "Mic SD pulldown to GND")
for x, y, c in [(1000, 720, ORANGE), (1000, 1045, ORANGE), (750, 720, ORANGE),
                  (780, 790, GND), (780, 850, GND), (780, 1170, GND), (710, 1010, MIC)]:
    node(x, y, c)
text(260, 1120, "* CHIPEN hanya jika pin tersedia.", 22)
text(260, 1156, "L/R ke GND; jangan beri 5 V ke mic.", 22, weight=600)

# Amplifier terminals stay separate from speaker outputs.
module(260, 1300, 500, 600, "MAX98357A", fill="#e5f7f4")
rect(380, 1490, 185, 180, "#284137", "#719184", 5)
text(472, 1575, "AMP", 28, "#c7dfd9", "middle", weight=700)
for y, label, c in [(1360, "VIN", RED), (1420, "GND", GND), (1480, "DIN", AMP),
                     (1540, "BCLK", AMP), (1600, "LRC", AMP), (1660, "SD / MODE", ORANGE),
                     (1720, "GAIN: NC", "#849297")]:
    pad(760, y, label, c)
wire([(760, 1420), (810, 1420), (810, 2200)], GND, "Amplifier ground")
wire([(760, 1660), (1000, 1660)], ORANGE, "Amplifier SD/MODE enable → S3 3V3")
for points, name in [([(760, 1480), (1120, 1480), (1120, 1280), (1200, 1280)], "DIN → S3 GPIO11"),
                      ([(760, 1540), (1080, 1540), (1080, 1340), (1200, 1340)], "BCLK → S3 GPIO12"),
                      ([(760, 1600), (1040, 1600), (1040, 1400), (1200, 1400)], "LRC → S3 GPIO13")]:
    wire(points, AMP, "MAX98357A " + name)
capacitor(960, 1360, 1520, "470 µF / 10 V", polarized=True, label_side=-1)
wire([(960, 1520), (810, 1520)], GND, "Amp bulk capacitor ground")
capacitor(860, 1360, 1420, "100 nF")
wire([(860, 1420), (810, 1420)], GND, "Amp local capacitor ground")
for x, y, c in [(1000, 1660, ORANGE), (960, 1360, RED), (860, 1360, RED),
                  (810, 1520, GND), (810, 1420, GND)]:
    node(x, y, c)
pad(390, 1900, "SPK+", "#cb9a00", "left")
pad(540, 1900, "SPK−", "#e04e8a", "left")
wire([(390, 1900), (390, 2010), (480, 2010)], "#cb9a00", "SPK+ → speaker positive ONLY")
wire([(540, 1900), (730, 1900), (730, 2160), (440, 2160), (440, 2070), (480, 2070)], "#e04e8a", "SPK− → speaker negative ONLY")
parts.append('<circle cx="580" cy="2040" r="90" fill="#323d47" stroke="#77909e" stroke-width="5"/><circle cx="580" cy="2040" r="61" fill="#171f27" stroke="#576975" stroke-width="4"/>')
parts.append('<path d="M480,2010 H505" stroke="#cb9a00" stroke-width="6"/><path d="M480,2070 H505" stroke="#e04e8a" stroke-width="6"/>')
pad(480, 2010, "+", "#cb9a00", "left")
pad(480, 2070, "−", "#e04e8a", "left")
text(260, 2180, "Speaker 8 Ω • SPK− bukan GND", 25, RED, weight=700)

# IR receiver.
module(1220, 1900, 370, 255, "IR RX 38 kHz", fill="#fff2e5")
rect(1350, 1990, 88, 98, "#292f35", "#5d6c79", 12)
text(1405, 2134, "Kompatibel 3,3 V", 21, anchor="middle")
pad(1220, 1960, "VCC", ORANGE, "left")
pad(1220, 2020, "GND", GND, "left")
pad(1350, 1900, "", IR)
text(1350, 1887, "OUT", 22, IR, "middle", weight=700)
wire([(1620, 1400), (1720, 1400), (1720, 1830), (1350, 1830), (1350, 1900)], IR, "IR receiver OUT → S3 GPIO1")
wire([(1220, 2020), (1160, 2020), (1160, 2200)], GND, "IR receiver ground")
capacitor(1140, 1960, 2020, "100 nF", ORANGE, label_side=-1)
wire([(1140, 2020), (1160, 2020)], GND, "IR receiver capacitor ground")
node(1140, 1960, ORANGE)
node(1160, 2020, GND)

# IR LED active-high low-side NPN driver; B/C/E wires are actual SVG paths.
text(1880, 1790, "3. IR BLASTER — NPN", 28, IR, weight=700)
resistor(1910, 2030, 1900, "100 Ω", RED)
wire([(2030, 1900), (2070, 1900)], RED, "IR LED anode")
parts.append('<path d="M2070,1875 L2070,1925 L2110,1900 Z M2117,1875 V1925" fill="#ed4d46" stroke="#932b25" stroke-width="3"/>')
wire([(2117, 1900), (2300, 1900), (2300, 2020)], RED, "IR LED cathode → Q1 collector")
text(2070, 1946, "A", 23, RED, "middle", weight=700)
text(2117, 1946, "K", 23, INK, "middle", weight=700)
text(2078, 1870, "IR LED 940 nm", 24, weight=700)
parts.append('<path d="M2100,1850 l26,-25 m-13,2 l13,-2 -2,13 M2126,1861 l26,-25 m-13,2 l13,-2 -2,13" fill="none" stroke="#d34b36" stroke-width="3"/>')
wire([(1620, 1470), (1680, 1470), (1680, 1780), (1850, 1780), (1850, 2050), (1930, 2050)], IR, "S3 GPIO2 → Q1 base resistor")
resistor(1930, 2050, 2050, "1 kΩ", IR)
wire([(2050, 2050), (2250, 2050)], IR, "1 kΩ → NPN base B")
vertical_resistor(2090, 2050, 2200, "100 kΩ", IR)
node(2090, 2050, IR)
parts.append('<circle cx="2290" cy="2060" r="55" fill="none" stroke="#172e42" stroke-width="3"/><path d="M2250,2050 H2265 M2265,2028 V2092 M2265,2037 L2300,2020 M2265,2078 L2300,2100" fill="none" stroke="#172e42" stroke-width="5"/><path d="M2280,2098 L2300,2100 L2290,2080 Z" fill="#172e42"/>')
wire([(2300, 2100), (2300, 2200)], GND, "Q1 emitter E → common GND")
text(2328, 2027, "C", 24, weight=700)
text(2215, 2032, "B", 24, weight=700)
text(2328, 2127, "E", 24, weight=700)
text(2370, 2150, "Q1: NPN / 2N2222", 23, weight=700)
text(1820, 2182, "Cek B/C/E transistor.", 21)
for x in [780, 810, 920, 1160, 1180, 2090, 2300, 2960]:
    node(x, 2200, GND)
node(2960, 570, GND)

# Compact mini-robot packaging guide. The mechanical drawing is illustrative:
# board/module dimensions vary between clones and must be measured before a case
# or mounting plate is fabricated.
parts.append('<rect x="3070" y="145" width="930" height="2290" rx="24" fill="#f2f7fa" stroke="#7893a5" stroke-width="3"/>')
text(3110, 205, "4. SUSUNAN MINI ROBOT", 34, INK, weight=700)
text(3110, 245, "Diagram mekanik ilustratif • ukur modul sebelum membuat casing", 20)

# Front view: sensor-facing head, protected grille, compact chassis and feet.
text(3120, 315, "TAMPAK DEPAN", 22, INK, weight=700)
rect(3170, 350, 370, 440, "#dcecf2", "#436777", 95)
rect(3215, 397, 280, 175, "#213847", "#7aa0ae", 32)
rect(3267, 435, 176, 92, "#101e2a", "#8bc5d9", 8)
text(3355, 490, "OLED", 24, "#74d8ff", "middle", weight=700)
parts.append('<circle cx="3250" cy="630" r="28" fill="#f5f8f9" stroke="#748b97" stroke-width="3"/><circle cx="3460" cy="630" r="28" fill="#f5f8f9" stroke="#748b97" stroke-width="3"/>')
text(3355, 695, "IR RX • mic port", 18, INK, "middle")
rect(3220, 818, 270, 735, "#e4e9ec", "#71818a", 46)
rect(3245, 925, 220, 180, "#d7e5d8", "#456d59", 18)
text(3355, 995, "S3 + C3", 23, INK, "middle", weight=700)
text(3355, 1032, "standoff / insulator", 17, INK, "middle")
rect(3250, 1150, 210, 150, "#f0e2c8", "#9b7342", 20)
text(3355, 1210, "IP5310", 23, INK, "middle", weight=700)
rect(3275, 1310, 160, 82, "#f5df9f", "#ad7d28", 16)
text(3355, 1345, "1S CELL", 19, INK, "middle", weight=700)
text(3355, 1372, "retained", 16, INK, "middle")
rect(3245, 1412, 76, 90, "#284137", "#719184", 9)
text(3283, 1453, "MAX", 15, "white", "middle", weight=700)
text(3283, 1477, "AMP", 15, "white", "middle", weight=700)
rect(3335, 1412, 145, 90, "#293641", "#667984", 18)
parts.append('<circle cx="3407" cy="1457" r="31" fill="#151c22" stroke="#87949c" stroke-width="4"/>')
text(3407, 1528, "speaker grille", 16, INK, "middle")
rect(3200, 1575, 310, 80, "#385064", "#233a4d", 20)
text(3355, 1627, "stable base / feet", 20, "white", "middle", weight=700)
for x, y, num in [(3187, 470, "1"), (3500, 630, "2"), (3187, 975, "3"),
                  (3500, 1195, "4"), (3187, 1415, "5"), (3500, 1635, "6")]:
    parts.append(f'<circle cx="{x}" cy="{y}" r="18" fill="#157a9a" stroke="white" stroke-width="3"/>')
    text(x, y+7, num, 17, "white", "middle", weight=700)

# Stack-up / placement notes; keyed to the numbered front view.
text(3570, 315, "URUTAN & PENEMPATAN", 22, INK, weight=700)
for y, num, title, line1, line2 in [
        (365, "1", "KEPALA", "OLED di muka; touch di permukaan", "atas/samping. Mic menghadap lubang."),
        (595, "2", "SENSOR DEPAN", "IR RX menghadap luar; jauhkan dari", "IR LED TX untuk cegah umpan balik."),
        (825, "3", "RANGKA TENGAH", "S3 dan C3 di dudukan terisolasi.", "Sisakan akses USB dan antena C3."),
        (1055, "4", "DAYA", "IP5310 dekat sel 1S terlindungi.", "Jauhkan kabel daya dari mic/I2C."),
        (1285, "5", "AUDIO", "Amp dekat speaker; kisi speaker", "terbuka. Jaga jalur panas/udara."),
        (1515, "6", "DASAR", "Baterai ditahan kuat, tidak menekan", "board; kabel rapi dan tak terjepit.")]:
    parts.append(f'<circle cx="3595" cy="{y+9}" r="17" fill="#157a9a"/>')
    text(3595, y+15, num, 17, "white", "middle", weight=700)
    text(3625, y+8, title, 20, INK, weight=700)
    text(3625, y+39, line1, 17)
    text(3625, y+66, line2, 17)
    parts.append(f'<path d="M3575,{y+102} H3970" stroke="#cad7df" stroke-width="2"/>')

text(3120, 1775, "URUTAN RAKIT YANG DISARANKAN", 21, INK, weight=700)
for y, n, line in [(1820, "1", "Pasang rangka + spacer non-konduktif;"),
                   (1860, "2", "pasang board, sensor dan kabel sesuai warna;"),
                   (1900, "3", "uji tiap rail tanpa board, lalu ukur 5 V/3V3;"),
                   (1940, "4", "pasang amp/speaker dan baterai terakhir;"),
                   (1980, "5", "uji USB satu board pada satu waktu.")]:
    text(3130, y, n + ".", 19, INK, weight=700)
    text(3170, y, line, 18)
rect(3110, 2045, 850, 355, "#fff0ee", "#d84b43", 14)
text(3140, 2090, "WAJIB DIJAGA", 22, "#a51d19", weight=700)
text(3140, 2130, "• USB PC dan keluaran boost IP5310 jangan", 18, "#71211e")
text(3160, 2160, "  memberi daya ke board yang sama bersamaan.", 18, "#71211e")
text(3140, 2200, "• SPK− adalah keluaran amp, bukan GND.", 18, "#71211e")
text(3140, 2240, "• Cek label 5V/VBUS, pin transistor B/C/E,", 18, "#71211e")
text(3160, 2270, "  dan polaritas konektor pada modul aktual.", 18, "#71211e")
text(3140, 2310, "• Uji daya dan panas bertahap sebelum casing", 18, "#71211e")
text(3160, 2340, "  ditutup; jangan mengurung sel atau IP5310.", 18, "#71211e")

# Legend and explicit assembly constraints.
text(80, 2470, "Kabel:", 25, weight=700)
for x, c, name in [(210, RED, "5 V"), (385, ORANGE, "3,3 V S3"), (645, GND, "GND"),
                    (855, UART, "UART"), (1090, SDA, "SDA"), (1320, SCL, "SCL"),
                    (1545, MIC, "mic"), (1745, AMP, "amp"), (1960, TOUCH, "touch/PIR"),
                    (2265, IR, "IR"), (2460, BAT, "battery sense")]:
    labels.append(f'<path d="M{x},2462 h45" stroke="{c}" stroke-width="7" stroke-linecap="round"/>')
    text(x+58, 2470, name, 23)
text(80, 2510, "Semua GND bersama. Output 3V3 kedua board tidak disatukan. Saat USB PC dipakai, putus cabang IP5310 ke board itu. Jangan beri 5 V ke GPIO.", 24, RED, weight=600)

svg = '<svg xmlns="http://www.w3.org/2000/svg" width="4050" height="2550" viewBox="0 0 4050 2550"><rect width="4050" height="2550" fill="white"/><g font-family="Arial, sans-serif">' + "".join(wires+parts+dots+labels) + '</g></svg>'
OUT.write_text(svg, encoding="utf-8")
print(OUT)
