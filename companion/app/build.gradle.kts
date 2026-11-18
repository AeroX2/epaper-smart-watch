plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.james.epaperwatch"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.james.epaperwatch"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1-test"
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
}

dependencies {
    // Health Connect step sync (the only AndroidX use in the app) and its suspend API.
    implementation("androidx.health.connect:connect-client:1.1.0-alpha08")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
    // Camera remote (the watch's Camera app): preview and capture.
    implementation("androidx.activity:activity:1.8.2")
    implementation("androidx.camera:camera-camera2:1.4.2")
    implementation("androidx.camera:camera-lifecycle:1.4.2")
    implementation("androidx.camera:camera-view:1.4.2")
    // ProcessCameraProvider.getInstance() returns a Guava ListenableFuture.
    // (The standalone listenablefuture artifact resolves to an empty placeholder here.)
    implementation("com.google.guava:guava:33.2.1-android")

    testImplementation("junit:junit:4.13.2")
}
