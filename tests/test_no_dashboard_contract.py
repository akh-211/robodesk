"""Source contracts for the compile-time ROBODESK_NO_DASHBOARD gateway option (default build keeps the dashboard)."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = shutil.which('powershell') or shutil.which('pwsh')


def read(name):
    return (ROOT / name).read_text(encoding='utf-8')


def occurrences(source, needle):
    """(total, removed_with_flag_on): occurrences of needle and how many sit in code dropped by ROBODESK_NO_DASHBOARD=1."""
    lines = source.splitlines()
    stack = []  # one entry per open #if: 'on' (active only with flag), 'off' (active only without flag), None (other)
    found = hidden = 0
    for line in lines:
        text = line.strip()
        if re.match(r'#\s*if\b', text):
            if re.match(r'#\s*if\s+!\s*ROBODESK_NO_DASHBOARD\b', text):
                stack.append('off')
            elif re.match(r'#\s*if\s+ROBODESK_NO_DASHBOARD\b', text):
                stack.append('on')
            else:
                stack.append(None)
        elif re.match(r'#\s*else\b', text) and stack:
            stack[-1] = {'on': 'off', 'off': 'on'}.get(stack[-1])
        elif re.match(r'#\s*endif\b', text) and stack:
            stack.pop()
        elif needle in line and not text.startswith('//'):
            found += 1
            hidden += 'off' in stack
    return found, hidden


def inactive_when_flag_on(source, needle):
    found, hidden = occurrences(source, needle)
    return found > 0 and found == hidden


def active_when_flag_on(source, needle):
    found, hidden = occurrences(source, needle)
    return found > 0 and hidden == 0


class NoDashboardContractTest(unittest.TestCase):
    def test_flag_defaults_off_and_is_gateway_only(self):
        role = read('RoboBuildRole.h')
        self.assertRegex(role, r'#ifndef ROBODESK_NO_DASHBOARD\s+#define ROBODESK_NO_DASHBOARD 0')
        self.assertRegex(role, r'#if ROBODESK_NO_DASHBOARD && !ROBODESK_DUAL_GATEWAY\s+#error')

    def test_default_gateway_still_builds_the_dashboard(self):
        gateway = read('RoboC3Gateway.h')
        include = re.search(r'#if ROBODESK_NO_DASHBOARD\n(.*?)#else\n#include "SettingsDashboard.h"\n#endif', gateway, re.S)
        self.assertIsNotNone(include, 'SettingsDashboard.h must be the #else branch of the flag')
        self.assertNotIn('#include "SettingsDashboard.h"', include.group(1))
        self.assertRegex(gateway, r'#else\ninline SettingsDashboard gatewayDashboard;\n#endif')
        for needle in ('gatewayDashboard.begin(', 'gatewayDashboard.startHttp()', 'gatewayDashboard.server()',
                       'gatewayDashboard.setRobotProxy(', 'gatewayDashboard.setRemoteSave(',
                       'gatewayDashboard.setPhonePlatformChange(', 'roboexternal::configureDashboardRoutes(',
                       'gatewayConfigureUpdateRoutes();', 'inline bool gatewayProxy(', 'inline bool gatewaySave(',
                       'inline bool gatewayPhonePlatformChanged(', 'inline void gatewayConfigureUpdateRoutes('):
            self.assertTrue(inactive_when_flag_on(gateway, needle), f'{needle} must be compiled out with ROBODESK_NO_DASHBOARD=1')

    def test_ble_ota_factory_and_tunnel_stay_outside_the_guard(self):
        gateway = read('RoboC3Gateway.h')
        for needle in ('gatewayStartPhoneBle(GatewayBleStartDeadlineMs)', 'gatewayServiceBleOta();', 'gatewayRunBleCommand(',
                       'inline bool gatewayCompleteFactory(', 'gatewayFactoryRetryAt=millis();gatewayCompleteFactory();', 'gatewayLocalOta.begin(', 'gatewayServiceTcp();', 'roboexternal::begin();'):
            self.assertTrue(active_when_flag_on(gateway, needle), f'{needle} must not depend on the dashboard')
        self.assertIn('struct GatewayNoDashboard{', gateway)

    def test_integrations_page_and_routes_are_dashboard_only(self):
        external = read('RoboExternalIntegrations.h')
        for needle in ('inline bool authorize(', 'inline void sendDashboard(', 'inline void configureDashboardRoutes('):
            self.assertTrue(inactive_when_flag_on(external, needle), needle)
        self.assertIn('#include "RoboBuildRole.h"', external)

    def test_message_max_bound_is_proved_in_source(self):
        runtime = read('RoboDualRuntime.h')
        self.assertRegex(runtime, r'#if ROBODESK_NO_DASHBOARD\n\s*static constexpr size_t MessageMax=(\d+);\n#else\n\s*static constexpr size_t MessageMax=12288;\n#endif')
        reduced = int(re.search(r'#if ROBODESK_NO_DASHBOARD\n\s*static constexpr size_t MessageMax=(\d+);', runtime).group(1))
        body_capacity = int(re.search(r'constexpr size_t BodyCapacity = (\d+);', read('RoboExternalIntegrations.h')).group(1))
        s3_body_max = int(re.search(r'constexpr size_t BodyMax=(\d+);', read('RoboExternalFetchS3.h')).group(1))
        self.assertEqual(body_capacity, s3_body_max)
        self.assertGreaterEqual(reduced, body_capacity + 3 + len('text/plain'))
        self.assertLess(reduced, 12288)
        self.assertIn('static_assert(roboexternal::BodyCapacity+3+10<=RoboDualRuntime::MessageMax', read('RoboC3Gateway.h'))
        self.assertIn('Gateway-only: S3 keeps 12288', runtime)

    def test_release_tooling_never_enables_the_flag(self):
        release = read('tools/prepare_github_release.ps1')
        self.assertNotIn('NO_DASHBOARD', release)
        self.assertNotIn('NoDashboard', release)
        self.assertNotIn('build_dual_board', release)
        build = read('tools/build_dual_board.ps1')
        self.assertEqual(build.count('ROBODESK_NO_DASHBOARD'), 1)
        self.assertRegex(build, r'\$ownsBle=\$profile\.Dual -and \$profile\.Id -eq \'gateway\'')
        owner_block = build.split('if($ownsBle){', 1)[1].split('  $args=', 1)[0]
        self.assertIn("if($NoDashboard){$flags+=' -DROBODESK_NO_DASHBOARD=1'}", owner_block)
        self.assertIn('[switch]$NoDashboard', build)
        self.assertIn('cannot be combined', build)

    @unittest.skipUnless(POWERSHELL, 'PowerShell is needed to evaluate the build script paths')
    def test_no_dashboard_gateway_uses_its_own_output_directory(self):
        result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-Command', r'''
        $ErrorActionPreference='Stop'; $tokens=$null; $errors=$null
        $ast=[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools/build_dual_board.ps1'),[ref]$tokens,[ref]$errors)
        if($errors.Count){throw $errors[0].Message}
        $node=$ast.Find({param($n) $n -is [System.Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq '$artifactId'},$true)
        $BleDiagnostic=$false; $BleProfile='baseline'; $ids=@()
        foreach($case in @(@('gateway',$false),@('gateway',$true),@('robot',$true))){
          $profile=@{Id=$case[0]}; $NoDashboard=$case[1]; Invoke-Expression $node.Extent.Text; $ids+=$artifactId
        }
        ConvertTo-Json -InputObject $ids -Compress
        '''], cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), ['gateway', 'gateway-nodashboard', 'robot'])

    def test_protocol_doc_records_recovery_and_the_pairing_gap(self):
        doc = read('docs/mobile/BLE_SETTINGS_PROTOCOL.md')
        section = doc.split('## 15. Dashboard-less gateway build', 1)[1]
        for text in ('ROBODESK_NO_DASHBOARD', '-NoDashboard', 'MessageMax', 'Open gap', 'openPairingWindow', 'factory-reset'):
            self.assertIn(text, section)


if __name__ == '__main__':
    unittest.main()
