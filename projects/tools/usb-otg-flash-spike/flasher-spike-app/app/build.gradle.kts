plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.nbagri.flasherspike"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.nbagri.flasherspike"
        minSdk = 26 // USB Host API needs API 12+; 26 matches the phones this bench app targets
        targetSdk = 35
        versionCode = 1
        versionName = "0.1-spike"

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

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
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
