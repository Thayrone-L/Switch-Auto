plugins {
    id("com.android.application")
}

android {
    namespace = "dev.switchauto.companion"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        applicationId = "dev.switchauto.companion"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
    }
}

dependencies {
    implementation("com.google.android.gms:play-services-code-scanner:16.1.0")
    implementation("org.maplibre.gl:android-sdk:13.0.2")
}
