param([string]$KotlinHome='C:\Program Files\Android\Android Studio\plugins\Kotlin\kotlinc',[string]$Java='C:\Program Files\Java\jdk-17\bin\java.exe')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $root '.verification\AndroidLocalSecurityTests.jar'
New-Item -ItemType Directory -Force (Split-Path $output) | Out-Null
& $Java -cp "$KotlinHome\lib\*" org.jetbrains.kotlin.cli.jvm.K2JVMCompiler -kotlin-home $KotlinHome -jvm-target 17 -include-runtime -d $output `
  (Join-Path $root 'mobile\android\app\src\main\java\com\robodesk\phonebridge\NotificationTextRedactor.kt') `
  (Join-Path $root 'mobile\android\app\src\main\java\com\robodesk\phonebridge\BridgeAuthenticatedEnvelope.kt') `
  (Join-Path $root 'mobile\android\app\src\main\java\com\robodesk\phonebridge\BridgeGattFraming.kt') `
  (Join-Path $root 'mobile\android\app\src\main\java\com\robodesk\phonebridge\BridgeSessionHandshake.kt') `
  (Join-Path $root 'mobile\android\app\src\main\java\com\robodesk\phonebridge\BridgeCompanionCommand.kt') `
  (Join-Path $root 'mobile\android\parser_tests\NotificationTextRedactorTest.kt') `
  (Join-Path $root 'mobile\android\parser_tests\BridgeAuthenticatedEnvelopeTest.kt') `
  (Join-Path $root 'mobile\android\parser_tests\BridgeGattFramingTest.kt') `
  (Join-Path $root 'mobile\android\parser_tests\BridgeSessionHandshakeTest.kt') `
  (Join-Path $root 'mobile\android\parser_tests\BridgeCompanionCommandTest.kt') `
  (Join-Path $root 'mobile\android\parser_tests\BridgeFakeGattTest.kt')
if($LASTEXITCODE -ne 0){throw 'Android local security test compilation failed.'}
& $Java -jar $output
if($LASTEXITCODE -ne 0){throw 'Android notification privacy tests failed.'}
