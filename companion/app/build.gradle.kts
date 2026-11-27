import java.util.Properties

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

    // Release signing key. CI passes it in environment variables (from the
    // repository secrets); on a PC it is read from
    // ~/.android/epaper-watch-release/keystore.properties. Without either,
    // release builds come out unsigned. Keep the keystore backed up: an app
    // signed with a different key cannot be installed over this one.
    val releaseSigning = run {
        val env = System.getenv()
        val local = Properties().apply {
            val file = File(System.getProperty("user.home"),
                ".android/epaper-watch-release/keystore.properties")
            if (file.isFile) file.inputStream().use { load(it) }
        }
        val store = env["ANDROID_KEYSTORE_FILE"] ?: local.getProperty("storeFile")
        val password = env["ANDROID_KEYSTORE_PASSWORD"] ?: local.getProperty("storePassword")
        val alias = env["ANDROID_KEY_ALIAS"] ?: local.getProperty("keyAlias")
        if (store != null && password != null && alias != null && File(store).isFile) {
            signingConfigs.create("release") {
                storeFile = File(store)
                storePassword = password
                keyAlias = alias
                keyPassword = password
            }
        } else {
            null
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = releaseSigning
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
