// Android app module. Fully offline: no INTERNET permission, no server, no accounts.
// The C++ engine (../engine) is compiled into librealxi_native.so through CMake.
plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.realxi"
    compileSdk = 36
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.realxi"
        minSdk = 26
        targetSdk = 36
        // Bump both for every release (versionCode must increase on each upload).
        versionCode = 1
        versionName = "1.0.0"
        externalNativeBuild {
            cmake {
                cppFlags("-std=c++20", "-O3", "-fexceptions")
                arguments("-DANDROID_STL=c++_shared")
            }
        }
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a") }
    }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
    // Release signing is optional: it is only applied when a keystore is supplied through environment variables or
    // gradle properties (~/.gradle/gradle.properties), so debug and CI builds work with no secrets. See docs/RELEASING.md.
    fun secret(name: String): String? = (System.getenv(name) ?: (project.findProperty(name) as String?))?.takeIf { it.isNotBlank() }
    val releaseKeystore = secret("KEYSTORE_FILE")?.let { file(it) }?.takeIf { it.exists() }
    signingConfigs {
        if (releaseKeystore != null) {
            create("release") {
                storeFile = releaseKeystore
                storePassword = secret("KEYSTORE_PASSWORD")
                keyAlias = secret("KEY_ALIAS")
                keyPassword = secret("KEY_PASSWORD") ?: secret("KEYSTORE_PASSWORD")
            }
        }
    }
    buildTypes {
        release {
            signingConfigs.findByName("release")?.let { signingConfig = it }
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    buildFeatures { compose = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    sourceSets["main"].assets.srcDirs("../assets")
}

kotlin { jvmToolchain(17) }

dependencies {
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.graphics)
    implementation(libs.compose.foundation)
    implementation(libs.compose.material3)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime)
    testImplementation(libs.junit)
    testImplementation(libs.org.json)
}
