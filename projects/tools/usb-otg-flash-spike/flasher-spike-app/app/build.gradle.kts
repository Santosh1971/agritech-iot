import java.io.FileInputStream
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Shared release keystore across all NB Agri apps (FG1, WM1, WPC) — see
// key.properties (gitignored, one per machine that builds a release).
val keystoreProperties = Properties()
val keystorePropertiesFile = rootProject.file("key.properties")
if (keystorePropertiesFile.exists()) {
    keystoreProperties.load(FileInputStream(keystorePropertiesFile))
}

android {
    namespace = "com.nbagri.flasherspike"
    compileSdk = 35
    // Pinned so CI's auto-installed NDK matches what's on dev machines exactly
    // (the native lib in src/main/cpp is otherwise built against whatever
    // NDK happens to be resolved, which can silently drift between machines).
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.nbagri.flasherspike"
        minSdk = 26 // USB Host API needs API 12+; 26 matches the phones this bench app targets
        targetSdk = 35
        versionCode = 1
        // CI passes -PappVersionName=dev-<shortsha>, matching the same string it uploads
        // to /api/admin/app-builds — so what's on screen (MainActivity's startup log
        // line) always matches the dashboard's Version column exactly. A local manual
        // build (no property passed) falls back to a plain "1.0.0".
        versionName = (project.findProperty("appVersionName") as String?) ?: "1.0.0"

        externalNativeBuild {
            cmake {
                // Pure C native lib (see src/main/cpp) — no C++ runtime to package.
                arguments += "-DANDROID_STL=none"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    signingConfigs {
        create("release") {
            if (keystorePropertiesFile.exists()) {
                storeFile = file(keystoreProperties["storeFile"] as String)
                storePassword = keystoreProperties["storePassword"] as String
                keyAlias = keystoreProperties["keyAlias"] as String
                keyPassword = keystoreProperties["keyPassword"] as String
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (keystorePropertiesFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("com.github.mik3y:usb-serial-for-android:3.11.0")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
}
