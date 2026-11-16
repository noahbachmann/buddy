// Single-module project: the keyboard the screener types through (no UI, no dependencies beyond Kotlin).
plugins {
    id("com.android.application") version "8.5.2"
    id("org.jetbrains.kotlin.android") version "2.0.20"
}

android {
    namespace = "buddy.screener.ime"
    compileSdk = 34
    defaultConfig {
        applicationId = "buddy.screener.ime"
        minSdk = 28 // the phone runs Android 9
        targetSdk = 34
        versionCode = 1
        versionName = "1"
    }
    buildTypes {
        // debug-signed is enough: sideloaded via adb, never published
        getByName("release") {
            signingConfig = signingConfigs.getByName("debug")
            isMinifyEnabled = true // drops the unused Kotlin stdlib: ~2 MB → a few KB
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
