plugins { id("com.android.application"); id("org.jetbrains.kotlin.android"); id("org.jetbrains.kotlin.plugin.compose") }

val releaseStorePath = providers.environmentVariable("ROBODESK_ANDROID_KEYSTORE").orNull
val releaseStorePassword = providers.environmentVariable("ROBODESK_ANDROID_STORE_PASSWORD").orNull
val releaseKeyAlias = providers.environmentVariable("ROBODESK_ANDROID_KEY_ALIAS").orNull
val releaseKeyPassword = providers.environmentVariable("ROBODESK_ANDROID_KEY_PASSWORD").orNull
val hasReleaseSigning = listOf(releaseStorePath, releaseStorePassword, releaseKeyAlias, releaseKeyPassword).all { !it.isNullOrBlank() }

android {
    namespace = "com.robodesk.phonebridge"
    compileSdk = 35
    defaultConfig { applicationId = namespace; minSdk = 26; targetSdk = 35; versionCode = 2; versionName = "1.1.0" }
    signingConfigs {
        if (hasReleaseSigning) create("release") {
            storeFile = file(releaseStorePath!!)
            storePassword = releaseStorePassword!!
            keyAlias = releaseKeyAlias!!
            keyPassword = releaseKeyPassword!!
        }
    }
    buildTypes {
        debug { applicationIdSuffix = ".debug" }
        release {
            isDebuggable = false
            isMinifyEnabled = false
            if (hasReleaseSigning) signingConfig = signingConfigs.getByName("release")
        }
    }
    buildFeatures { compose = true }
    compileOptions { sourceCompatibility=JavaVersion.VERSION_17; targetCompatibility=JavaVersion.VERSION_17 }
}

kotlin { jvmToolchain(17) }

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2025.02.00")
    implementation(composeBom)
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    debugImplementation("androidx.compose.ui:ui-tooling")
}

val verifyReleaseSigning = tasks.register("verifyReleaseSigning") {
    doLast {
        check(hasReleaseSigning) {
            "Release APK signing requires ROBODESK_ANDROID_KEYSTORE, ROBODESK_ANDROID_STORE_PASSWORD, ROBODESK_ANDROID_KEY_ALIAS, and ROBODESK_ANDROID_KEY_PASSWORD."
        }
        check(file(releaseStorePath!!).isFile) { "The configured release keystore file does not exist." }
    }
}
tasks.matching { it.name == "preReleaseBuild" }.configureEach {
    dependsOn(verifyReleaseSigning)
}
