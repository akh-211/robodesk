plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }

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
    compileOptions { sourceCompatibility=JavaVersion.VERSION_17; targetCompatibility=JavaVersion.VERSION_17 }
}

kotlin { jvmToolchain(17) }

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
