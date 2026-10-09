param([string]$KotlinHome='C:\Program Files\Android\Android Studio\plugins\Kotlin\kotlinc',[string]$Java='C:\Program Files\Java\jdk-17\bin\java.exe')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $root '.verification\MapsNavigationParserTest.jar'
New-Item -ItemType Directory -Force (Split-Path $output) | Out-Null
& $Java -cp "$KotlinHome\lib\*" org.jetbrains.kotlin.cli.jvm.K2JVMCompiler -kotlin-home $KotlinHome -jvm-target 17 -include-runtime -d $output (Join-Path $root 'mobile\android\app\src\main\java\com\robodesk\phonebridge\MapsNavigationParser.kt') (Join-Path $root 'mobile\android\parser_tests\MapsNavigationParserTest.kt')
if($LASTEXITCODE -ne 0){throw 'Android navigation parser compilation failed.'}
& $Java -jar $output
if($LASTEXITCODE -ne 0){throw 'Android navigation parser tests failed.'}
