"""Source ordering guards; hardware power-cut tests remain separate."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DualBoardContractTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ required for AP recovery lifetime check')
    def test_gateway_releases_only_idle_ap_after_stable_station_and_uart(self):
        gateway = (ROOT / 'RoboC3Gateway.h').read_text(encoding='utf-8')
        recovery = gateway.split('inline void gatewayServiceSetupApRecovery(', 1)[1].split('inline void gatewayBeginWifi(', 1)[0]
        source = r'''
#include <cassert>
#include <cstdint>
constexpr int WL_CONNECTED=3;
struct FakeWifi{int state=0;int status(){return state;}} WiFi;
struct FakeLink{bool up=false;bool connected(){return up;}};
struct FakeDual{FakeLink link;} RoboDual;
struct FakeDashboard{bool active=true,hasClient=true;int attempts=0;bool apStarted(){return active;}bool stopSetupApIfUnused(){++attempts;if(hasClient)return false;active=false;return true;}} gatewayDashboard;
bool gatewaySetupApAttempted=true;
struct FakeLog{void println(const char*){}} RoboLog;
void gatewayServiceSetupApRecovery(
''' + recovery + r'''
int main(){
 gatewayServiceSetupApRecovery(10000);assert(gatewayDashboard.attempts==0);
 WiFi.state=WL_CONNECTED;gatewayServiceSetupApRecovery(12000);assert(gatewayDashboard.attempts==0);
 RoboDual.link.up=true;gatewayServiceSetupApRecovery(20000);gatewayServiceSetupApRecovery(49999);assert(gatewayDashboard.attempts==0);
 gatewayServiceSetupApRecovery(50000);assert(gatewayDashboard.active&&gatewayDashboard.attempts==1);
 gatewayServiceSetupApRecovery(54999);assert(gatewayDashboard.attempts==1);
 gatewayServiceSetupApRecovery(55000);assert(gatewayDashboard.active&&gatewayDashboard.attempts==2);
 gatewayDashboard.hasClient=false;gatewayServiceSetupApRecovery(60000);assert(!gatewayDashboard.active&&gatewayDashboard.attempts==3);
 gatewayServiceSetupApRecovery(70000);assert(gatewayDashboard.attempts==3);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'ap_recovery.cpp';exe=Path(directory)/'ap_recovery.exe';cpp.write_text(source,encoding='utf-8')
            compiled=subprocess.run([shutil.which('g++'),'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(compiled.returncode,0,compiled.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

    @unittest.skipUnless(shutil.which('g++'), 'g++ required for external worker lifetime check')
    def test_external_response_buffer_lifetime_and_allocation_failure(self):
        external = (ROOT / 'RoboExternalIntegrations.h').read_text(encoding='utf-8')
        worker = external.split('inline void worker(void*) {', 1)[1].split('inline void begin()', 1)[0]
        worker = worker.replace('std::malloc', 'allocate').replace('std::free', 'release')
        self.assertIn('if(!responseBody)return 1;', external)
        self.assertIn('(remoteFetch?MinFreeHeapRemote:MinFreeHeapBeforeRequest)+BodyCapacity+1', external)
        self.assertIn('(remoteFetch?MinLargestBlockRemote:MinLargestBlockBeforeRequest)+BodyCapacity+1', external)
        source = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
constexpr size_t BodyCapacity=8192;
char* responseBody=nullptr; bool failAllocation=false;int allocations=0,releases=0,calls=0,deletions=0;
void* allocate(size_t n){assert(n==8193);++allocations;return failAllocation?nullptr:std::malloc(n);}
void release(void* p){assert(p==responseBody);++releases;std::free(p);}
struct Busy{bool value=true;void operator=(bool v){assert(!v&&responseBody==nullptr&&releases==1);value=v;}} requestBusy;
struct Config{char entities[3][2]={{'x',0},{'x',0},{'x',0}};} config;
uint32_t nextWeatherAt=0,nextHomeAssistantAt=0,nextCalendarAt=0;
uint8_t selectedProvider=0,nextProvider=0,nextEntity=0;
constexpr uint32_t WeatherPeriodMs=1,HomeAssistantPeriodMs=2,CalendarPeriodMs=3;
uint32_t millis(){return 10;} bool validEntity(const char*,size_t){return true;}
uint8_t fetch(){assert(requestBusy.value);++calls;if(responseBody){responseBody[8192]=0;return 0;}return 1;}
uint8_t fetchWeather(const Config&){return fetch();}
uint8_t fetchEntity(const Config&,uint8_t){return fetch();}
uint8_t fetchCalendar(const Config&){return fetch();}
void vTaskDelete(void*){assert(!requestBusy.value&&responseBody==nullptr);++deletions;}
void worker(void*){
''' + worker + r'''
int main(){for(int failure=0;failure<2;++failure)for(int provider=0;provider<3;++provider){
  failAllocation=failure;selectedProvider=provider;allocations=releases=calls=deletions=0;requestBusy.value=true;
  worker(nullptr);assert(allocations==1&&releases==1&&calls==1&&deletions==1&&!responseBody);
}}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'external.cpp'; exe = Path(directory) / 'external.exe'
            cpp.write_text(source, encoding='utf-8')
            compiled = subprocess.run([shutil.which('g++'), '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    @unittest.skipUnless(shutil.which('g++'), 'g++ required for AP mode behavior check')
    def test_setup_ap_preserves_configured_sta_and_skips_unused_sta(self):
        dashboard = (ROOT / 'SettingsDashboard.h').read_text(encoding='utf-8')
        method = dashboard.split('  bool startSetupAp() {', 1)[1].split('  bool apStarted()', 1)[0]
        stop_method = dashboard.split('  bool stopSetupApIfUnused() {', 1)[1].split('  }\n#endif', 1)[0]
        source = r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
enum wifi_mode_t { WIFI_AP, WIFI_AP_STA };
struct FakeWifi {
  wifi_mode_t selected=WIFI_AP_STA; bool modeOk=true, disconnectOk=true; int apCalls=0, modeCalls=0, disconnectCalls=0, clients=0;
  bool mode(wifi_mode_t m){selected=m;++modeCalls;return modeOk;}
  bool softAP(const char*,const char*){++apCalls;return modeOk;}
  int softAPgetStationNum(){return clients;}
  bool softAPdisconnect(bool off){assert(off);++disconnectCalls;return disconnectOk;}
  struct Ip{struct Text{const char* c_str(){return "address";}};Text toString(){return {};}};
  Ip softAPIP(){return {};}
} WiFi;
struct {uint64_t getEfuseMac(){return 1;}} ESP;
struct {template<class... T> void printf(const char*,T...){} void println(const char*){}} RoboLog;
struct Settings{bool configured=false;bool wifiConfigured(){return configured;}};
struct Dashboard {
  bool apStarted_=false; Settings* settings_=nullptr; const char* setupApPassword_="test";
  bool startSetupAp(){
''' + method + r'''
  bool stopSetupApIfUnused(){
''' + stop_method + r'''
  }
};
int main(){
  for(int configured=0;configured<2;++configured){
    WiFi=FakeWifi{}; Settings s; s.configured=configured; Dashboard d;d.settings_=&s;
    assert(d.startSetupAp());assert(WiFi.selected==(configured?WIFI_AP_STA:WIFI_AP));
    assert(d.startSetupAp());assert(WiFi.modeCalls==1&&WiFi.apCalls==1);
  }
  WiFi=FakeWifi{};Dashboard missing;assert(missing.startSetupAp());assert(WiFi.selected==WIFI_AP);
  WiFi=FakeWifi{};WiFi.modeOk=false;Dashboard failed;assert(!failed.startSetupAp());assert(WiFi.apCalls==1);
  WiFi=FakeWifi{};Settings live;live.configured=true;Dashboard ap;ap.settings_=&live;assert(ap.startSetupAp());WiFi.clients=1;assert(!ap.stopSetupApIfUnused()&&ap.apStarted_&&WiFi.disconnectCalls==0);WiFi.clients=0;WiFi.disconnectOk=false;assert(!ap.stopSetupApIfUnused()&&ap.apStarted_);WiFi.disconnectOk=true;assert(ap.stopSetupApIfUnused()&&!ap.apStarted_&&WiFi.disconnectCalls==2);assert(!ap.stopSetupApIfUnused());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'ap.cpp'; exe = Path(directory) / 'ap.exe'
            cpp.write_text(source, encoding='utf-8')
            compile_result = subprocess.run([shutil.which('g++'), '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_wifi_credentials_use_a_compact_dedicated_post(self):
        dashboard = (ROOT / "SettingsDashboard.h").read_text(encoding="utf-8")
        self.assertIn('server_.on("/wifi-save", HTTP_POST, [this]() { handleWifiSave(); });', dashboard)
        self.assertIn("<form id='wifiSaveForm' method='post' action='/wifi-save'></form>", dashboard)
        wifi_save = dashboard.split("void handleWifiSave() {", 1)[1].split("void handleSave() {", 1)[0]
        for name in ("ssid", "wpass", "ssid2", "wpass2", "ssid3", "wpass3"):
            self.assertIn(f'hasArg("{name}")', wifi_save)
        self.assertIn("store_->save(next)", wifi_save)
        self.assertIn("wifiCredentialsCommittedThisSave()", wifi_save)
        self.assertIn("nvs-partial wifi-record-verified=1", wifi_save)
        self.assertIn("Wi-Fi saved with warning", wifi_save)
        self.assertIn("wifiSaveForm'", dashboard)
        self.assertIn("form='wifiSaveForm'", dashboard)

    def test_wifi_only_save_skips_s3_rpc_even_when_empty_owner_field_is_posted(self):
        dashboard = (ROOT / "SettingsDashboard.h").read_text(encoding="utf-8")
        self.assertIn('id=\'owner\' name=\'owner\'', dashboard)
        save = dashboard.split("void handleSave() {", 1)[1].split("void handleSoundTest", 1)[0]
        self.assertIn("location.replace('/')", save)
        self.assertIn("rebootAt_ = millis() + 5000", save)
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        save = gateway.split("inline bool gatewaySave(", 1)[1].split("inline bool gatewayPhonePlatformChanged", 1)[0]
        self.assertIn('server.hasArg("owner")&&server.arg("owner").length()>0', save)
        self.assertIn("const bool behaviorSame=roboBehaviorSettingsEqual(gatewaySettings,next)", save)
        self.assertIn("if(!ownerChanged&&behaviorSame)", save)

    def test_signed_dual_release_builds_c3_ble_owner_and_pins_nimble_host(self):
        release = (ROOT / "tools/prepare_github_release.ps1").read_text(encoding="utf-8")
        self.assertIn("prepare_nimble.ps1", release)
        self.assertIn("$extraFlags += @('-DROBODESK_DUAL_BOARD=1','-DROBODESK_PHONE_BLE_ROBOT=0')", release)
        self.assertIn("if($board.Id -eq 'esp32c3-gateway')", release)
        self.assertIn("-DROBODESK_NIMBLE_EXTERNAL=1", release)
        self.assertIn("$compileArgs += @('--library',$dualNimbleLibrary)", release)
        self.assertIn("$linkMap.Contains('NimBLE-Arduino-2.5.1')", release)
        self.assertIn("$linkMap.Contains('ble_hs.c.o')", release)
        self.assertIn("C3 release did not link the pinned external NimBLE host; refusing to sign.", release)

    def test_dual_local_build_keeps_ble_on_c3(self):
        build = (ROOT / "tools/build_dual_board.ps1").read_text(encoding="utf-8")
        self.assertIn("$ownsBle=$profile.Dual -and $profile.Id -eq 'gateway'", build)
        self.assertIn("$flags+=' -DROBODESK_PHONE_BLE_ROBOT=0'", build)
        self.assertNotIn("BleOwner", build)

    def test_phone_forget_persists_before_unpairing(self):
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        forget = gateway.split('if(ok&&action=="ble_pair")', 1)[1].split('else if', 1)[0]
        self.assertLess(forget.index('next.phoneBlePeer[0]=0'), forget.index('gatewayStore.save(next)'))
        self.assertLess(forget.index('gatewayStore.save(next)'), forget.index('gatewayBle.clearPeer()'))
        self.assertIn('if(ok){gatewaySettings=next;', forget)

    def test_android_removal_is_queued_while_disconnected(self):
        listener = (ROOT / "mobile/android/app/src/main/java/com/robodesk/phonebridge/RoboNotificationListener.kt").read_text(encoding="utf-8")
        removal = listener.split('override fun onNotificationRemoved', 1)[1].split('private fun keyFor', 1)[0]
        remove_sender = listener.split('private fun sendRemove', 1)[1].split('private fun clearPending', 1)[0]
        self.assertIn('sendRemove(sbn)', removal)
        self.assertIn('enqueue(Packet(2,', remove_sender)
        self.assertNotIn('if(ready', removal + remove_sender)
        self.assertIn('pending.remove(packet)', listener)
        self.assertIn('awaitingAck=true', listener)

    def test_android_never_downgrades_to_legacy_ble_writes(self):
        listener = (ROOT / "mobile/android/app/src/main/java/com/robodesk/phonebridge/RoboNotificationListener.kt").read_text(encoding="utf-8")
        transport = (ROOT / "PhoneBleTransport.h").read_text(encoding="utf-8")
        self.assertIn('firmware lacks authenticated bridge support', listener)
        self.assertIn('UUID.fromString("93de0009-', listener)
        self.assertIn('UUID.fromString("93de000a-', listener)
        self.assertIn('Legacy payload writes are intentionally not dispatched.', transport)
        self.assertIn('EnvelopeCallbacks', transport)

    def test_mobile_global_pause_clears_queued_and_robot_transient_data(self):
        listener = (ROOT / "mobile/android/app/src/main/java/com/robodesk/phonebridge/RoboNotificationListener.kt").read_text(encoding="utf-8")
        activity = (ROOT / "mobile/android/app/src/main/java/com/robodesk/phonebridge/MainActivity.kt").read_text(encoding="utf-8")
        transport = (ROOT / "PhoneBleTransport.h").read_text(encoding="utf-8")
        self.assertIn('putBoolean("paused",paused).commit()', listener)
        self.assertIn('pending.addFirst(Packet(5,', listener)
        self.assertIn('if(sharingPaused())return@post', listener)
        self.assertIn('text = "Pause all sharing"', activity)
        self.assertIn('message.kind==5&&message.payloadSize==0', transport)
        self.assertIn('if(bridge_)bridge_->clear();if(navigation_)navigation_->clear();makeAck_', transport)

    def test_unapproved_bond_is_deleted_on_disconnect_and_pairing_expiry(self):
        transport = (ROOT / "PhoneBleTransport.h").read_text(encoding="utf-8")
        self.assertIn('ble_store_util_delete_peer(&address)', transport)
        self.assertIn('if(owner_->candidateAuthenticated_||owner_->candidateBonded_)owner_->deleteCandidateBond_()', transport)
        self.assertIn('if(candidateAuthenticated_||candidateBonded_)deleteCandidateBond_()', transport)
        self.assertIn('candidateBondAddr_=d->peer_id_addr', transport)
        self.assertIn('bool candidateBondAddrValid_=false;\n#if defined(CONFIG_NIMBLE_ENABLED)\n  ble_addr_t candidateBondAddr_{};', transport)

    def test_privacy_mode_changes_purge_old_app_payloads_and_show_diagnostics(self):
        listener = (ROOT / "mobile/android/app/src/main/java/com/robodesk/phonebridge/RoboNotificationListener.kt").read_text(encoding="utf-8")
        activity = (ROOT / "mobile/android/app/src/main/java/com/robodesk/phonebridge/MainActivity.kt").read_text(encoding="utf-8")
        self.assertIn('PhoneBridgeStatus.appModeChanged?.invoke(app.packageName, selectedMode)', activity)
        self.assertIn('private fun applyAppModeChange(app:String,mode:AppContentMode)', listener)
        self.assertIn('pending.filter{it.kind==1&&it.pkg==app}', listener)
        self.assertIn('privacyRemovalPending[app]=revokeKeys', listener)
        self.assertIn('schedulePrivacyRemovals()', listener)
        self.assertIn('finishPrivacyRefresh(packet.pkg)', listener)
        self.assertIn('override fun onReadRemoteRssi', listener)
        self.assertIn('Oldest queued:', activity)
        self.assertIn('Reconnect now', activity)

    def test_authenticated_companion_commands_are_bounded_async_and_session_bound(self):
        command = (ROOT / "PhoneCompanionCommand.h").read_text(encoding="utf-8")
        transport = (ROOT / "PhoneBleTransport.h").read_text(encoding="utf-8")
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        robot = (ROOT / "RoboRobotRole.h").read_text(encoding="utf-8")
        activity = (ROOT / "CompanionActivityCatalog.h").read_text(encoding="utf-8")
        self.assertIn('message.kind==6', transport)
        self.assertIn('PhoneCompanionCommand::decode', transport)
        self.assertIn('result.sessionId!=sessionId_', transport)
        self.assertIn('0x7e', transport)
        self.assertIn('xQueueSend(gatewayPhoneCommandQueue,&command,0)', gateway)
        self.assertIn('gatewayPhoneCommandWorker', gateway)
        self.assertIn('gatewayRpc("/companion/action"', gateway)
        self.assertIn('activityCatalogSize() { return 13; }', activity)
        self.assertIn('s.on("/_companion/settings",HTTP_POST', robot)
        self.assertIn('applySonicSettings()', robot)
        self.assertNotIn('roboRobotRebootAt=millis()+1000', robot.split('s.on("/_companion/settings"', 1)[1].split('s.on("/_phone/capabilities"', 1)[0])

    def test_phone_credential_is_prepared_before_trust_persistence(self):
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        approval = gateway.split('action=="ble_approve"', 1)[1].split("else ok=false", 1)[0]
        self.assertLess(approval.index("snapshotCandidateApproval(candidate)"), approval.index("prepareCandidateApproval(candidate)"))
        self.assertLess(approval.index("prepareCandidateApproval(candidate)"), approval.index("gatewayStore.save(next)"))
        self.assertLess(approval.index("gatewayStore.save(next)"), approval.index("approveCandidate(candidate)"))
        transport = (ROOT / "PhoneBleTransport.h").read_text(encoding="utf-8")
        self.assertIn('AuthNamespace="robodesk_v2auth"', transport)
        self.assertIn("esp_fill_random(next+AuthKeyOffset,AuthKeySize)", transport)
        self.assertIn("persistAuthRecord_(next)", transport)
        self.assertIn("candidateLease_.matches(candidate)", transport)
        self.assertIn("sameCandidate_(candidate,current)", transport)
        self.assertIn("clearCredential(store,updated,present)", transport)
        self.assertIn("inline bool clearCredential", (ROOT / "PhoneBridgeAuthRecord.h").read_text(encoding="utf-8"))
        uart = (ROOT / "RoboLinkProtocol.h").read_text(encoding="utf-8").lower()
        self.assertNotIn("bridgekey", uart)

    def test_phone_revocation_is_required_for_forget_reset_and_platform_change(self):
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        self.assertIn("!gatewayClearLocalPhoneCredential()||!gatewayStore.clear()", gateway)
        self.assertIn("return gatewayBle.clearPeer(true)", gateway)
        self.assertIn("p.getBytesLength(\"record\")==0", gateway)
        self.assertIn("gatewayBle.clearPeer(true);const bool saved=gatewayStore.save(gatewaySettings)", gateway)
        dashboard = (ROOT / "SettingsDashboard.h").read_text(encoding="utf-8")
        self.assertIn("phonePlatformChange_(phonePlatformChangeContext_,settings_->phonePlatform,next.phonePlatform)", dashboard)
        firmware = (ROOT / "RoboDeskSonicCharacter.ino").read_text(encoding="utf-8")
        self.assertIn("if(!phoneBle.clearPeer(true))return false", firmware)

    def test_platform_switch_revokes_phone_trust_before_saving(self):
        dashboard = (ROOT / "SettingsDashboard.h").read_text(encoding="utf-8")
        save = dashboard.split("void handleSave() {", 1)[1].split("void handleSoundTest", 1)[0]
        self.assertLess(save.index("phonePlatformChange_(phonePlatformChangeContext_"), save.index("store_->save(next)"))
        self.assertIn("setPhonePlatformChange(dashboardPhonePlatformChanged", (ROOT / "RoboDeskSonicCharacter.ino").read_text(encoding="utf-8"))

    def test_factory_preparation_precedes_erasure(self):
        dashboard = (ROOT / "SettingsDashboard.h").read_text(encoding="utf-8")
        factory = dashboard.split("void handleFactory() {", 1)[1].split("void handleMemoryClear", 1)[0]
        self.assertLess(factory.index("factoryReset_(factoryResetContext_)"), factory.index("store_->clear()"))
        self.assertIn("if(ok)ok=store_->clear()", factory)
        firmware = (ROOT / "RoboDeskSonicCharacter.ino").read_text(encoding="utf-8")
        callback = firmware.split("bool dashboardClearPreferenceState(void*){", 1)[1].split("void endUtterance", 1)[0]
        self.assertLess(callback.index('p.putBool("reset",true)'), callback.index("preferenceStore.clear()"))
        self.assertLess(callback.index("if(!pairOk)return false"), callback.index('p.begin("robodesk_ir",false)'))

    def test_pending_factory_keeps_rollback_service(self):
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        loop = gateway.split("void loop(){", 1)[1]
        self.assertLess(loop.index("gatewayBootHealth()"), loop.index("if(gatewayFactoryState)"))
        self.assertIn('p.putUChar("factory",state)', gateway)

    def test_setup_ap_retries_are_throttled_when_wifi_start_fails(self):
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        retry = gateway.split("inline bool gatewayStartSetupApThrottled()", 1)[1].split("inline void gatewayBeginWifi", 1)[0]
        self.assertIn("gatewayDashboard.apStarted()", retry)
        self.assertIn("gatewaySetupApAttempted&&uint32_t(now-gatewaySetupApAttemptAt)<5000u", retry)
        self.assertLess(retry.index("gatewaySetupApAttemptAt=now"), retry.index("gatewayDashboard.startSetupAp()"))
        loop = gateway.split("void loop(){", 1)[1]
        self.assertIn("gatewayStartSetupApThrottled()", loop)
        self.assertNotIn("gatewayDashboard.startSetupAp()", loop)

    def test_gateway_link_diagnostics_use_rom_console(self):
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        diagnostics = gateway.split("inline void gatewayLogLinkDiagnostics(uint32_t now){", 1)[1].split("\n", 1)[0]
        self.assertIn("esp_rom_printf(\"[RoboDeskLink]", diagnostics)
        for field in ("connected=%u", "rx=%u", "tx=%u", "retries=%u", "crc=%u"):
            self.assertIn(field, diagnostics)
        self.assertIn("gatewayLogLinkDiagnostics(millis())", gateway)

    def test_gateway_reserves_wifi_before_link_and_ble(self):
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        setup = gateway.split("void setup(){", 1)[1].split("void loop(){", 1)[0]
        self.assertLess(setup.index("WiFi.mode(WIFI_STA)"), setup.index("RoboDual.begin()"))
        self.assertLess(setup.index("WiFi.mode(WIFI_STA)"), setup.index("gatewayStartPhoneBle(GatewayBleStartDeadlineMs)"))
        self.assertLess(setup.index("gatewayBle.setCompanionCommandDispatch("), setup.index("gatewayStartPhoneBle(GatewayBleStartDeadlineMs)"))
        self.assertLess(setup.index("gatewayStartPhoneBle(GatewayBleStartDeadlineMs)"), setup.index("gatewayDashboard.begin("))
        self.assertLess(setup.index("gatewayStartPhoneBle(GatewayBleStartDeadlineMs)"), setup.index("RoboDual.begin()"))
        self.assertNotIn("gatewayBle.begin(", setup)
        loop = gateway.split("void loop(){", 1)[1]
        self.assertLess(loop.index("gatewayStartPhoneBle(millis())"), loop.index("gatewayBle.service(millis())"))

    def test_gateway_wifi_save_reboots_without_robot_link(self):
        gateway = (ROOT / "RoboC3Gateway.h").read_text(encoding="utf-8")
        offline = gateway.split("if(!RoboDual.link.connected()){", 1)[1].split("\n  }", 1)[0]
        self.assertIn("gatewaySettings=next", offline)
        self.assertIn("gatewayRebootAt=millis()+1200", offline)
        self.assertIn("server.send_P(200", offline)
        self.assertIn("return false", offline)

    def test_robot_empty_pin_requires_durable_readback(self):
        settings = (ROOT / "RuntimeSettings.h").read_text(encoding="utf-8")
        pin = settings.split('const size_t pinWritten=p.putString("pin", s.adminPin);', 1)[1].split("#endif", 1)[0]
        self.assertIn("#if ROBODESK_DUAL_ROBOT", pin)
        self.assertIn('s.adminPin[0]==0 && p.isKey("pin") && p.getString("pin").isEmpty()', pin)
        self.assertIn("#else\n    ok &= pinWritten>0;", pin)

    def test_migration_requires_local_usb_window(self):
        robot = (ROOT / "RoboRobotRole.h").read_text(encoding="utf-8")
        for route in ("/_migration/get", "/_migration/commit"):
            handler = robot.split('s.on("' + route + '"', 1)[1].split("\n  });", 1)[0]
            self.assertIn("int32_t(roboMigrationUntil-millis())<=0", handler)
            self.assertIn("s.send(403", handler)
        firmware = (ROOT / "RoboDeskSonicCharacter.ino").read_text(encoding="utf-8")
        self.assertIn('!strcmp(command,"pair_migrate")', firmware)
        self.assertIn("roboMigrationUntil=now+60000u", firmware)


if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
