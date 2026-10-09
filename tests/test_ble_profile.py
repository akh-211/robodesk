"""Build/security contracts for the C3 source-host profile (not radio tests)."""
import json
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = shutil.which('powershell') or shutil.which('pwsh')


@unittest.skipUnless(POWERSHELL, 'PowerShell is needed to exercise build safety')
class BleBuildSafetyTest(unittest.TestCase):
    def run_script(self, script):
        result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-Command', script],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_diagnostic_build_paths_do_not_replace_production_gateway(self):
        paths = self.run_script(r'''
        $ErrorActionPreference='Stop'; $tokens=$null; $errors=$null
        $ast=[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools/build_dual_board.ps1'),[ref]$tokens,[ref]$errors)
        if($errors.Count){throw $errors[0].Message}
        $assignments=@{}
        foreach($name in @('$artifactId','$stage','$build')){
          $assignments[$name]=$ast.Find({param($n) $n -is [System.Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq $name},$true)
          if(!$assignments[$name]){throw "Missing build assignment $name"}
        }
        $output=Join-Path (Get-Location) '.verification/test-output'; $profile=@{Id='gateway'}; $BleProfile='baseline'
        $paths=@()
        foreach($BleDiagnostic in @($false,$true)){
          foreach($name in @('$artifactId','$stage','$build')){Invoke-Expression $assignments[$name].Extent.Text}
          $paths+=@{stage=$stage;build=$build}
        }
        ConvertTo-Json -InputObject $paths -Compress
        ''')
        self.assertNotEqual(paths[0]['stage'], paths[1]['stage'])
        self.assertNotEqual(paths[0]['build'], paths[1]['build'])
        self.assertEqual(Path(paths[0]['build']).parent.name, 'gateway')
        self.assertEqual(Path(paths[1]['build']).parent.name, 'gateway-diagnostic')

    def test_reduced_pool_profile_is_named_and_diagnostic_only(self):
        script = (ROOT / 'tools/build_dual_board.ps1').read_text()
        config = (ROOT / 'tools/nimble_gateway_config.h').read_text()
        self.assertIn("[ValidateSet('baseline','msys1-6')][string]$BleProfile='baseline'", script)
        self.assertIn("if($BleProfile -ne 'baseline' -and !$BleDiagnostic)", script)
        self.assertIn("gateway-diagnostic-$BleProfile", script)
        self.assertIn('-DROBODESK_NIMBLE_PROFILE_MSYS1_6=1', script)
        self.assertIn('#if !defined(ROBODESK_BLE_DIAGNOSTIC) || !ROBODESK_BLE_DIAGNOSTIC', config)
        self.assertIn('#define CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT 6', config)
        self.assertIn('#define CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT 8', config)
        self.assertIn('#define MYNEWT_VAL_MSYS_1_BLOCK_COUNT 6', config)
        self.assertIn('#define MYNEWT_VAL_MSYS_1_BLOCK_COUNT 8', config)
        cases = [
            (['-BleProfile', 'msys1-6'], 'diagnostic-only'),
            (['-Board', 'standalone-c3', '-BleDiagnostic', '-BleProfile', 'msys1-6'], 'gateway'),
            (['-Board', 'robot', '-BleDiagnostic'], 'gateway'),
        ]
        for args, expected in cases:
            result = subprocess.run(
                [POWERSHELL, '-NoProfile', '-NonInteractive', '-File',
                 'tools/build_dual_board.ps1', *args],
                cwd=ROOT, capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(expected, result.stderr + result.stdout)

    def test_release_rejects_every_defined_diagnostic_macro(self):
        rejected = self.run_script(r'''
        $ErrorActionPreference='Stop'; $tokens=$null; $errors=$null
        $ast=[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools/prepare_github_release.ps1'),[ref]$tokens,[ref]$errors)
        if($errors.Count){throw $errors[0].Message}
        $guard=$ast.Find({param($n) $n -is [System.Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.Contains('includes the BLE diagnostic profile; refusing to sign.')},$true)
        if(!$guard){throw 'Diagnostic release guard missing'}
        $board=@{Id='verification'}; $results=@()
        foreach($buildOptions in @('-DROBODESK_NIMBLE_EXTERNAL=1','-DROBODESK_BLE_DIAGNOSTIC=1','-DROBODESK_BLE_DIAGNOSTIC=0','-DROBODESK_BLE_DIAGNOSTIC')){
          $rejected=$false
          try{Invoke-Expression $guard.Extent.Text}catch{
            if(!$_.Exception.Message.Contains('includes the BLE diagnostic profile; refusing to sign.')){throw}
            $rejected=$true
          }
          $results+=$rejected
        }
        ConvertTo-Json -InputObject $results -Compress
        ''')
        self.assertEqual(rejected, [False, True, True, True])


class BleProfileTest(unittest.TestCase):
    def test_dependency_is_pinned_and_config_applied_to_host(self):
        pin = json.loads((ROOT / 'tools/nimble-pin.json').read_text())
        self.assertEqual(pin['version'], '2.5.1')
        self.assertEqual(len(pin['sha256']), 64)
        prepare = (ROOT / 'tools/prepare_nimble.ps1').read_text()
        self.assertLess(prepare.index('Get-FileHash'), prepare.index('stage_nimble.py'))
        stage = (ROOT / 'tools/stage_nimble.py').read_text()
        self.assertIn('src/nimconfig.h', stage)
        self.assertIn('robodesk_gateway_config.h', stage)
        self.assertLess(stage.index('hashlib.sha256'), stage.index('zipfile.ZipFile'))

    def test_connection_identity_and_security_survive_adapter(self):
        adapter = (ROOT / 'PhoneBleBackend.h').read_text()
        for value in ('getConnHandle()', 'getIdAddress()', 'isEncrypted()',
                      'isAuthenticated()', 'isBonded()', 'getSecKeySize()'):
            self.assertIn(value, adapter)
        self.assertIn('security->onPassKeyNotify(code)', adapter)
        self.assertIn('return code;', adapter)
        self.assertIn('security->onAuthenticationComplete(&d)', adapter)
        self.assertIn('c->notify(connection)', adapter)

    def test_strong_security_and_legacy_rejection_retained(self):
        transport = (ROOT / 'PhoneBleTransport.h').read_text()
        for value in ('sm_sc_only=1', 'setAuthenticationMode(true,true,true)',
                      'sec_state.key_size<16', 'sec_state.authenticated',
                      'Legacy payload writes are intentionally not dispatched.',
                      'PhoneBridgeProtocol::hmacKnownAnswerSelfTest()'):
            self.assertIn(value, transport)

    def test_ancs_cleanup_precedes_host_deinit(self):
        transport = (ROOT / "PhoneBleTransport.h").read_text()
        failure = transport.split('if(!ready){', 1)[1].split('return ready;', 1)[0]
        self.assertLess(failure.index('end();'), failure.index('phoneBleMemory("startup-failed")'))
        cleanup = transport.split("bool end(){", 1)[1]
        self.assertLess(cleanup.index("ancs_.end()"), cleanup.index("NimBLEDevice::deinit(true)"))
        ancs = (ROOT / "PhoneAncsClient.h").read_text()
        end = ancs.split("void end(){", 1)[1].split("bool ready()", 1)[0]
        self.assertLess(end.index("ble_gap_event_listener_unregister"), end.index("vQueueDelete"))
        self.assertIn("overflow_=false;parser_=", ancs)

    def test_ancs_uses_same_external_host_headers(self):
        ancs = (ROOT / 'PhoneAncsClient.h').read_text()
        self.assertIn('ROBODESK_NIMBLE_EXTERNAL', ancs)
        self.assertIn('nimble/nimble/host/include/host/ble_gatt.h', ancs)
        self.assertIn('ble_gap_event_listener_register', ancs)

    def test_ancs_notification_payload_is_allocated_only_for_queued_data(self):
        ancs = (ROOT / 'PhoneAncsClient.h').read_text()
        support = (ROOT / 'PhoneAncsEventSupport.h').read_text()
        self.assertIn('static constexpr uint16_t MaxNotifyPayload=384;', ancs)
        self.assertIn('PhoneAncsEventPayload payload;', ancs)
        self.assertIn('static constexpr size_t Capacity=384;', support)
        self.assertIn('allocator(size?size:1)', support)
        self.assertNotIn('e.data', ancs)
        self.assertIn('e.payload.release();overflow_=true;', ancs)
        self.assertIn('EventDataRelease release{e};', ancs)
        self.assertIn('while(events_&&xQueueReceive(events_,&e,0)==pdTRUE)e.payload.release();', ancs)
        self.assertIn('discardQueuedEvents();if(events_){vQueueDelete(events_);', ancs)
        self.assertNotIn('uint8_t data[384]', ancs)

    def test_ancs_shutdown_closes_enqueue_admission_before_queue_delete(self):
        ancs = (ROOT / 'PhoneAncsClient.h').read_text()
        end = ancs.split('void end(){', 1)[1].split('bool ready()', 1)[0]
        self.assertLess(end.index('pauseEventAdmission()'), end.index('ble_gap_event_listener_unregister'))
        self.assertLess(end.index('pauseEventAdmission()'), end.index('vQueueDelete'))
        support = (ROOT / 'PhoneAncsEventSupport.h').read_text()
        self.assertIn('eventGate_.closeAndWait([](){vTaskDelay(1);});', ancs)
        self.assertIn('inFlight_.fetch_add(1,std::memory_order_acq_rel);', support)
        self.assertIn('if(!accepting_.load(std::memory_order_acquire))', support)
        self.assertIn('if(!eventGate_.enter()){e.payload.release();return;}', ancs)

    def test_ancs_gatt_callbacks_are_bound_to_immutable_session_tokens(self):
        ancs = (ROOT / 'PhoneAncsClient.h').read_text()
        support = (ROOT / 'PhoneAncsEventSupport.h').read_text()
        transport = (ROOT / 'PhoneBleTransport.h').read_text()
        self.assertIn('PhoneAncsOperationTokenPool operationTokens_', ancs)
        self.assertIn('session_.accepts(e.generation,e.conn)', ancs)
        for callback in ('serviceFound,operation', 'chrFound,operation',
                         'descFound,operation', 'written,operation'):
            self.assertIn(callback, ancs)
        self.assertIn('generation==currentGeneration&&connection==currentConnection', support)
        self.assertIn('void hostStopped(){for(auto&token:tokens_)', support)
        self.assertIn('ancs_.hostStopped(stopped)', transport)

    def test_ancs_failure_rotates_generation_before_same_connection_retry(self):
        ancs = (ROOT / 'PhoneAncsClient.h').read_text()
        failure = ancs.split('void fail(uint32_t now){', 1)[1].split('\n', 1)[0]
        ordered = ('pauseEventAdmission();', 'session_.invalidate();', 'overflow_=false;',
                   'discardQueuedEvents();', 'session_.begin(conn_);', 'eventGate_.open();')
        positions = [failure.index(marker) for marker in ordered]
        self.assertEqual(positions, sorted(positions))

    def test_diagnostic_profile_bypasses_only_production_heap_reserves(self):
        transport = (ROOT / 'PhoneBleTransport.h').read_text()
        self.assertEqual(transport.count('#if !defined(ROBODESK_BLE_DIAGNOSTIC) || !ROBODESK_BLE_DIAGNOSTIC'), 3)
        gateway = (ROOT / 'RoboC3Gateway.h').read_text()
        self.assertEqual(gateway.count('#if defined(ROBODESK_BLE_DIAGNOSTIC) && ROBODESK_BLE_DIAGNOSTIC'), 4)
        self.assertIn('(!defined(ROBODESK_BLE_DIAGNOSTIC) || !ROBODESK_BLE_DIAGNOSTIC)', gateway)
        self.assertIn('phoneBleMemory("host-init")', transport)
        self.assertIn('phoneBleMemory("characteristics")', transport)
        self.assertIn('phoneBleMemory("advertising")', transport)
        self.assertIn('try{ready=initialize();}catch(const std::bad_alloc&)', transport)

    def test_production_c3_ble_guard_preserves_deferred_rpc_and_contiguous_memory(self):
        gateway = (ROOT / 'RoboC3Gateway.h').read_text()
        self.assertIn('GatewayBleStartupReserve=96*1024', gateway)
        self.assertIn('GatewayBleStartupLargestBlock=80*1024', gateway)
        self.assertIn('<GatewayBleStartupLargestBlock', gateway)
        self.assertIn('phoneBleStartupLargestBlockReserve', gateway)
        self.assertIn('GatewayBleStartupLargestBlock=32768', gateway)  # diagnostic/fallback profiles


if __name__ == '__main__':
    unittest.main()
