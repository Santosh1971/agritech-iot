import java.io.FileInputStream
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Shares the same release keystore as the other NB Agri apps (see
// usb-otg-flash-spike/flasher-spike-app/app/build.gradle.kts's comment) --
// key.properties is gitignored, one per machine that builds a release.
val keystoreProperties = Properties()
val keystorePropertiesFile = rootProject.file("key.properties")
if (keystorePropertiesFile.exists()) {
    keystoreProperties.load(FileInputStream(keystorePropertiesFile))
}

// Broker + device SoftAP credentials -- secrets.properties is gitignored (this
// repo is public). Copy secrets.properties.example and fill it in to build.
val secretsProperties = Properties()
val secretsPropertiesFile = rootProject.file("secrets.properties")
if (secretsPropertiesFile.exists()) {
    secretsProperties.load(FileInputStream(secretsPropertiesFile))
}
fun secret(name: String): String {
    val value = secretsProperties.getProperty(name)
        ?: throw GradleException("Missing $name in secrets.properties -- copy secrets.properties.example and fill it in.")
    return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\""
}

android {
    namespace = "com.nbagri.flashertester"
    compileSdk = 35
    // Pinned for the same reason as flasher-spike-app: keeps the native lib
    // build reproducible across dev machines/CI.
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.nbagri.flashertester"
        minSdk = 26 // USB Host API
        targetSdk = 35
        versionCode = 22
        // Bump this on every single build sent for testing, no exceptions --
        // 2026-09-15/16 both had "is this the right build?" confusion because
        // several sends in a row kept the same version string, and there's no
        // other way to tell which APK is actually running from the log line.
        versionName = (project.findProperty("appVersionName") as String?) ?: "2.12.2-collapse-repeated-log-lines"

        buildConfigField("String", "MQTT_USER", secret("MQTT_USER"))
        buildConfigField("String", "MQTT_PASS", secret("MQTT_PASS"))
        buildConfigField("String", "SOFTAP_PASSWORD", secret("SOFTAP_PASSWORD"))

        externalNativeBuild {
            cmake {
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

    buildFeatures {
        buildConfig = true
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
    // DUT local WebSocket API (factory_reset/calibrate/relay_test/device_info/
    // wifi_config) -- everything else (Flash Bridge, jig) is plain HTTP via
    // java.net.HttpURLConnection to match flasher-spike-app's existing style;
    // OkHttp is pulled in for its WebSocket client specifically, not to
    // replace that.
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    // Broker-side proof for the WiFi+MQTT step -- the phone subscribes to
    // the DUT's own status topic directly, independent of anything the DUT
    // self-reports over its local WS API (see MqttChecker.kt). Plain Paho
    // client (not the Android service-wrapper artifact) -- used
    // synchronously from a background thread, no bound Service needed for
    // a one-shot subscribe-and-wait like this.
    implementation("org.eclipse.paho:org.eclipse.paho.client.mqttv3:1.2.5")
}
