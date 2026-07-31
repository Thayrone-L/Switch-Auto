plugins {
    id("com.android.application")
}

android {
    namespace = "dev.switchauto.companion.v2"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        applicationId = "dev.switchauto.companion.v2"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0-v2"
    }
}
