// Single-module project: the screener's screen reader (an instrumentation, no UI, no dependencies beyond Kotlin).
// Its own APK, not part of the keyboard's: `am instrument` force-stops its target package on every run.
plugins {
    id("com.android.application") version "8.5.2"
    id("org.jetbrains.kotlin.android") version "2.0.20"
}

android {
    namespace = "buddy.screener.dump"
    compileSdk = 34
    defaultConfig {
        applicationId = "buddy.screener.dump"
        minSdk = 28 // the phone runs Android 9
        targetSdk = 34
        versionCode = 2
        versionName = "2"
    }
    buildTypes {
        // debug-signed is enough: sideloaded via adb, never published
        getByName("release") {
            signingConfig = signingConfigs.getByName("debug")
            isMinifyEnabled = true
            proguardFiles("proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
