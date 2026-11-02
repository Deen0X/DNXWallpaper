plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "io.dnx.wallpaper.companion"
    compileSdk = 36

    defaultConfig {
        applicationId = "io.dnx.wallpaper.companion"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    // Versiones estables compatibles con AGP 8.13 / compileSdk 36.
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.compose.ui:ui:1.7.1")
    implementation("androidx.compose.ui:ui-graphics:1.7.1")
    implementation("androidx.compose.foundation:foundation:1.7.1")
    implementation("androidx.compose.material3:material3:1.3.0")
    implementation("androidx.compose.material:material-icons-extended:1.7.1")
    // Reproductor de vídeo para el fondo animado
    implementation("androidx.media3:media3-exoplayer:1.4.1")
    // QR (sin Google Play Services)
    implementation("com.journeyapps:zxing-android-embedded:4.3.0")
}
