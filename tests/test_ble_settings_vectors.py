"""Independent Python encoder for docs/mobile/BLE_SETTINGS_PROTOCOL.md.

Checks that tests/fixtures/ble_settings_v1.txt (shared with the C++ host test
and the future Kotlin test) matches the spec byte-for-byte, including the
HMAC-SHA256 envelope tag. Regenerate with:
    python tests/test_ble_settings_vectors.py --write
"""
import hashlib
import hmac
import struct
import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/fixtures/ble_settings_v1.txt'
KEY = bytes(range(32))
SESSION = 0x78563412
DOMAIN = b'RoboDesk-Bridge-Envelope-v2\x00'
PAGE_DATA = 480
RESPONSE_KIND = 0x7D
DOC = b'{"schema":1,"robotName":"' + b'R' * 620 + b'"}'


def envelope(direction, kind, counter, payload, age=0, session=SESSION):
    prefix = b'RDB2' + bytes([2, direction, kind, 0]) + struct.pack('<IIIH', session, counter, age, len(payload)) + payload
    return prefix + hmac.new(KEY, DOMAIN + prefix, hashlib.sha256).digest()


def page(doc, index):
    count = (len(doc) + PAGE_DATA - 1) // PAGE_DATA
    data = doc[index * PAGE_DATA:(index + 1) * PAGE_DATA]
    return bytes([index, count]) + struct.pack('<HI', len(doc), zlib.crc32(doc) & 0xFFFFFFFF) + data


def response(counter, kind, status, body=b''):
    return struct.pack('<I', counter) + bytes([kind, status]) + body


def crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def mem_entry(ident, key, value, kind, flags=3, stamp=1800000000, day=20833):
    return struct.pack('<IIHBBBB', ident, stamp, day, kind, flags, len(key), len(value)) + key + value


def mem_doc(entries, more=0):
    blob = b''.join(entries)
    return bytes([1]) + struct.pack('<I', crc(blob)) + bytes([len(entries), len(entries), more]) + blob


def rem_entry(ident, schedule, minute, due, text):
    return struct.pack('<IBHIB', ident, schedule, minute, due, len(text)) + text


def rem_doc(entries, stable):
    return bytes([1]) + struct.pack('<I', crc(b''.join(stable))) + bytes([len(entries)]) + b''.join(entries)


# Memory state used by the vectors: two facts written by the memory_add vectors.
FACT_DRINK = mem_entry(1, b'drink', b'kopi', 1)
FACT_NAME = mem_entry(2, 'nama'.encode(), 'Éric'.encode(), 0)
# Reminder state: timer 600 s (listed at 600 s remaining), daily 08:00, once 1800003600.
REM_TIMER, REM_TIMER_STABLE = rem_entry(1, 3, 0, 600, b'Minum air'), rem_entry(1, 3, 0, 0, b'Minum air')
REM_DAILY = rem_entry(2, 2, 480, 0, b'Sarapan')
REM_ONCE = rem_entry(3, 1, 0, 1800003600, b'Rapat')
CLEAR_TOKEN = b'MCLR' + struct.pack('<I', SESSION)


def rev(*entries):
    return struct.pack('<I', crc(b''.join(entries)))


def status_snapshot(schema=4):
    # Schema 2 appends OTA state 3 (flashing S3), error 0, 42 %, target 20261010.
    tail = bytes([3, 0, 42]) + struct.pack('<I', 20261010) if schema >= 2 else b''
    # Schema 3 appends: wifiActiveSlot 2, pomodoro focus (1) with 1234 s left, activity 5 running (1), game offered (1),
    # learning on, 2 IR profiles, IR learn listening (1), integrations idle (0).
    live = bytes([2, 1]) + struct.pack('<H', 1234) + bytes([5, 1, 1, 1, 2, 1, 0]) if schema >= 3 else b''
    # Schema 4 (PD-2.4) appends mood sleepy (7), cause isolation (9), sleep state drowsy (2).
    mood = bytes([7, 9, 2]) if schema >= 4 else b''
    return (bytes([schema]) + struct.pack('<IIIIIIIII', 20261009, 20261008, 123456, 120000, 48000, 30000, 24000, 200000, 150000)
            + bytes([3]) + struct.pack('<IIII', 12, 4, 1, 2) + bytes([1, (-61) & 0xFF, 0x0B, 0x03]) + tail + live + mood)


def ir_profiles(names):
    return bytes([len(names)]) + b''.join(bytes([len(n)]) + n for n in names)


def int_config_doc(weather, token, calendar, lat, lon, base, entities):
    flags = (1 if weather else 0) | (2 if token else 0) | (4 if calendar else 0)
    return (bytes([1, flags]) + struct.pack('<ff', lat, lon) + bytes([len(base)]) + base + bytes([len(entities)])
            + b''.join(bytes([len(e)]) + e for e in entities))


def int_set_config(weather, lat, lon, base, entities):
    return (bytes([1, 1 if weather else 0]) + struct.pack('<ff', lat, lon) + bytes([len(base)]) + base + bytes([len(entities)])
            + b''.join(bytes([len(e)]) + e for e in entities))


def int_status_doc():
    # busy 0; weather configured+valid; HA configured; calendar configured+valid -> flags 0b111110.
    # errors: weather 0, HA 4 (authorization_rejected), calendar 0. Entity slot 0 valid, slot 1 not yet read.
    weather, cal = b'21.5 C, clear', b'2026-10-12 09:00Z Rapat'
    return (bytes([1, 0b111110, 0, 4, 0]) + struct.pack('<I', 60000) + bytes([len(weather)]) + weather
            + bytes([2]) + bytes([0, 1]) + struct.pack('<I', 30000) + bytes([4]) + b'23.4' + bytes([1, 0]) + struct.pack('<I', 0) + bytes([0])
            + struct.pack('<I', 120000) + bytes([len(cal)]) + cal)


INT_BASE = b'https://ha.example'
INT_ENTITIES = [b'sensor.temp', b'light.lamp']


def vectors():
    snapshot = status_snapshot()
    assert len(snapshot) == 79 and len(status_snapshot(3)) == 76 and len(status_snapshot(2)) == 65 and len(status_snapshot(1)) == 58
    phone = [
        ('settings_get_page0', 8, 1, bytes([0])),
        ('settings_set_page0', 9, 2, page(DOC, 0)),
        ('settings_set_page1', 9, 3, page(DOC, 1)),
        ('secret_wifi_slot1', 10, 4, bytes([1, 1, 0, 7]) + b'HomeNet' + bytes([13]) + b'correct horse'),
        ('secret_wifi_keep_password_slot0', 10, 5, bytes([1, 0, 1, 6]) + b'Office' + bytes([0])),
        ('secret_wifi_clear_slot2', 10, 6, bytes([1, 2, 0, 0, 0])),
        ('secret_gemini_key', 10, 7, bytes([2, 18]) + b'AIzaSyTEST-key_123'),
        ('status_request', 11, 8, bytes([0])),
        ('command_reboot_both', 12, 9, bytes([1, 3])),
        ('command_factory_reset', 12, 10, bytes([2]) + b'WIPE' + struct.pack('<I', SESSION)),
        ('command_ota_start', 12, 11, bytes([3])),
        ('memory_list_page0', 13, 12, bytes(5)),
        ('memory_add_drink', 14, 13, bytes([1, 5, 4]) + b'drink' + b'kopi'),
        ('memory_add_name_utf8', 14, 14, bytes([0, 4, 5]) + 'nama'.encode() + 'Éric'.encode()),
        ('memory_delete', 15, 15, struct.pack('<I', 2)),
        ('memory_clear', 16, 16, CLEAR_TOKEN),
        ('reminder_list_page0', 17, 17, bytes([0])),
        ('reminder_add_timer', 18, 18, bytes([3]) + struct.pack('<HI', 0, 600) + bytes([9]) + b'Minum air'),
        ('reminder_add_daily', 18, 19, bytes([2]) + struct.pack('<HI', 480, 0) + bytes([7]) + b'Sarapan'),
        ('reminder_add_once', 18, 20, bytes([1]) + struct.pack('<HI', 0, 1800003600) + bytes([5]) + b'Rapat'),
        ('reminder_delete', 19, 21, struct.pack('<I', 2)),
        ('command_expression_love', 12, 22, bytes([4, 3])),
        ('command_talk', 12, 23, bytes([5])),
        ('command_pause_15m', 12, 24, bytes([6, 2])),
        ('command_resume', 12, 25, bytes([6, 0])),
        ('command_pomodoro_start', 12, 26, bytes([7, 1])),
        ('command_activity_start_5', 12, 27, bytes([8, 1, 5])),
        ('command_activity_skip', 12, 28, bytes([8, 5])),
        ('command_game_accept', 12, 29, bytes([9, 1])),
        ('command_learning_on', 12, 30, bytes([10, 1])),
        ('command_sound_test_shake', 12, 31, bytes([11, 11])),
        ('command_ir_send', 12, 32, bytes([12, 1]) + struct.pack('<I', 0x20DF10EF)),
        ('command_ir_learn_tv', 12, 33, bytes([12, 2, 8]) + b'TV power'),
        ('command_ir_replay_tv', 12, 34, bytes([12, 3, 8]) + b'TV power'),
        ('command_experiences_clear', 12, 35, bytes([13])),
        ('ir_profile_list', 20, 36, bytes([0])),
        ('integrations_get', 21, 37, bytes([0])),
        ('integrations_set_config', 22, 38, int_set_config(True, -6.2, 106.8, INT_BASE, INT_ENTITIES)),
        ('integrations_set_token', 22, 39, bytes([2, 17]) + b'TESTTOKEN-abc_123'),
        ('integrations_set_calendar', 22, 40, bytes([3, 32]) + b'https://cal.example/feed.ics?k=1'),
        ('integrations_set_token_clear', 22, 41, bytes([2, 0])),
        ('integrations_refresh', 23, 42, bytes([1])),
        ('integrations_clear', 23, 43, bytes([2])),
        ('integrations_status', 24, 44, bytes([0])),
        ('command_rest_sleep', 12, 45, bytes([14, 1])),
        ('command_rest_wake', 12, 46, bytes([14, 2])),
        ('command_pet', 12, 47, bytes([15])),
    ]
    robot = [
        ('response_get_page0', 1, response(1, 8, 0, page(DOC, 0))),
        ('response_set_page0_staged', 2, response(2, 9, 0, bytes([0, 0]))),
        ('response_set_page1_applied', 3, response(3, 9, 0, bytes([1, 1]))),
        ('response_set_crc_mismatch', 4, response(3, 9, 6, bytes([1, 0]))),
        ('response_secret_ok', 5, response(4, 10, 0)),
        ('response_status', 6, response(8, 11, 0, page(snapshot, 0))),
        ('response_command_busy', 7, response(9, 12, 1)),
        ('response_ota_accepted', 8, response(11, 12, 0)),
        ('response_memory_list_page0', 9, response(12, 13, 0, page(mem_doc([FACT_DRINK, FACT_NAME]), 0))),
        ('response_memory_add_drink', 10, response(13, 14, 0, struct.pack('<I', 1) + rev(FACT_DRINK))),
        ('response_memory_add_name_utf8', 11, response(14, 14, 0, struct.pack('<I', 2) + rev(FACT_DRINK, FACT_NAME))),
        ('response_memory_delete_ok', 12, response(15, 15, 0, rev(FACT_DRINK))),
        ('response_memory_delete_not_found', 13, response(15, 15, 19)),
        ('response_memory_clear_ok', 14, response(16, 16, 0, rev())),
        ('response_memory_clear_confirm_required', 15, response(16, 16, 10)),
        ('response_memory_add_content_rejected', 16, response(13, 14, 17)),
        ('response_memory_add_full', 17, response(13, 14, 18)),
        ('response_reminder_list_page0', 18, response(17, 17, 0, page(rem_doc([REM_TIMER, REM_DAILY, REM_ONCE], [REM_TIMER_STABLE, REM_DAILY, REM_ONCE]), 0))),
        ('response_reminder_add_timer', 19, response(18, 18, 0, struct.pack('<I', 1) + rev(REM_TIMER_STABLE))),
        ('response_reminder_add_daily', 20, response(19, 18, 0, struct.pack('<I', 2) + rev(REM_TIMER_STABLE, REM_DAILY))),
        ('response_reminder_add_once', 21, response(20, 18, 0, struct.pack('<I', 3) + rev(REM_TIMER_STABLE, REM_DAILY, REM_ONCE))),
        ('response_reminder_delete_ok', 22, response(21, 19, 0, rev(REM_TIMER_STABLE, REM_ONCE))),
        ('response_reminder_add_time_not_set', 23, response(20, 18, 21)),
        ('response_reminder_add_full', 24, response(20, 18, 18)),
        ('response_ota_update_busy', 25, response(11, 12, 11)),
        ('response_status_schema1', 26, response(8, 11, 0, page(status_snapshot(1), 0))),
        ('response_status_schema2', 27, response(8, 11, 0, page(status_snapshot(2), 0))),
        ('response_command_rejected', 28, response(27, 12, 23)),
        ('response_command_robot_unavailable', 29, response(27, 12, 4)),
        ('response_command_bad_request', 30, response(27, 12, 2)),
        ('response_ir_profile_list', 31, response(36, 20, 0, ir_profiles([b'TV power', b'Fan']))),
        ('response_ir_profile_list_robot_unavailable', 32, response(36, 20, 4)),
        ('response_integrations_get', 33, response(37, 21, 0, page(int_config_doc(True, True, True, -6.2, 106.8, INT_BASE, INT_ENTITIES), 0))),
        ('response_integrations_set_ok', 34, response(38, 22, 0)),
        ('response_integrations_busy', 35, response(43, 23, 1)),
        ('response_integrations_status', 36, response(44, 24, 0, page(int_status_doc(), 0))),
        ('response_status_schema3', 37, response(8, 11, 0, page(status_snapshot(3), 0))),
    ]
    out = []
    for name, kind, counter, payload in phone:
        out.append((name, 1, kind, counter, payload))
    for name, counter, payload in robot:
        out.append((name, 2, RESPONSE_KIND, counter, payload))
    return out


def render():
    lines = ['# BLE settings protocol v1 test vectors (docs/mobile/BLE_SETTINGS_PROTOCOL.md).',
             '# Envelope key = bytes 00..1f, session = 0x78563412, ageMs = 0. Hex is lowercase.',
             '# settings_set_page* carry this document (647 bytes): {"schema":1,"robotName":"R x620"}',
             '# memory_*/reminder_* vectors (phase 2): facts drink=kopi (kind 1, id 1) and nama=Eric (kind 0, id 2), updated 1800000000, day 20833;',
             '# reminders timer 600 s (id 1), daily minute 480 (id 2), once 1800003600 (id 3). Revision = CRC-32 of the concatenated entries.',
             '# ir_profile_list / integrations_* (kinds 20..24): HA token and calendar URL are write-only and appear only in the SET requests (never in a response).',
             '# Generated by: python tests/test_ble_settings_vectors.py --write', '']
    for name, direction, kind, counter, payload in vectors():
        lines += [f'[{name}]', f'direction={direction}', f'kind={kind}', f'session={SESSION:08x}', f'counter={counter}',
                  'age=0', f'payload={payload.hex()}', f'envelope={envelope(direction, kind, counter, payload).hex()}', '']
    return '\n'.join(lines)


def parse(text):
    sections, current = {}, None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('['):
            current = sections.setdefault(line[1:-1], {})
        else:
            key, value = line.split('=', 1)
            current[key] = value
    return sections


class BleSettingsVectorTest(unittest.TestCase):
    def test_fixture_matches_independent_encoder(self):
        self.assertEqual(FIXTURE.read_text(encoding='utf-8').replace('\r\n', '\n').rstrip('\n'), render().rstrip('\n'))

    def test_envelope_tags_verify_and_limits_hold(self):
        for name, fields in parse(FIXTURE.read_text(encoding='utf-8')).items():
            wire = bytes.fromhex(fields['envelope'])
            payload = bytes.fromhex(fields['payload'])
            self.assertLessEqual(len(payload), 500, name)
            self.assertEqual(wire[22:-32], payload, name)
            self.assertTrue(hmac.compare_digest(wire[-32:], hmac.new(KEY, DOMAIN + wire[:-32], hashlib.sha256).digest()), name)
            self.assertLessEqual(len(wire), 600, name)

    def test_document_pages_reassemble_with_crc(self):
        sections = parse(FIXTURE.read_text(encoding='utf-8'))
        pages = [bytes.fromhex(sections[f'settings_set_page{i}']['payload']) for i in range(2)]
        doc = b''.join(p[8:] for p in pages)
        length, crc = struct.unpack('<HI', pages[0][2:8])
        self.assertEqual((len(doc), zlib.crc32(doc) & 0xFFFFFFFF), (length, crc))
        self.assertEqual(doc, DOC)

    def test_memory_and_reminder_list_documents(self):
        sections = parse(FIXTURE.read_text(encoding='utf-8'))
        body = bytes.fromhex(sections['response_memory_list_page0']['payload'])[6:]
        count, length, checksum = body[1], *struct.unpack('<HI', body[2:8])
        self.assertEqual((count, length, checksum), (1, len(body) - 8, crc(body[8:])))
        doc = body[8:]
        self.assertEqual(struct.unpack('<I', doc[1:5])[0], crc(doc[8:]))
        self.assertEqual(doc[5:8], bytes([2, 2, 0]))  # total, entries in window, more
        reminders = bytes.fromhex(sections['response_reminder_list_page0']['payload'])[6 + 8:]
        self.assertEqual(reminders[5], 3)
        self.assertEqual(struct.unpack('<I', reminders[1:5])[0], crc(REM_TIMER_STABLE + REM_DAILY + REM_ONCE))
        add = bytes.fromhex(sections['memory_add_name_utf8']['payload'])
        self.assertEqual(add[3:].decode(), 'namaÉric')
        self.assertEqual(bytes.fromhex(sections['memory_clear']['payload']), b'MCLR' + bytes.fromhex('12345678'))


if __name__ == '__main__':
    if '--write' in sys.argv:
        FIXTURE.parent.mkdir(parents=True, exist_ok=True)
        FIXTURE.write_text(render(), encoding='utf-8', newline='\n')
    else:
        unittest.main()
