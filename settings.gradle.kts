// Root Gradle settings. The app is the only Gradle module; the C++ engine is built by CMake (engine/CMakeLists.txt)
// and consumed through app/src/main/cpp/CMakeLists.txt.
pluginManagement {
    repositories { gradlePluginPortal(); google(); mavenCentral() }
}
dependencyResolutionManagement {
    repositories { google(); mavenCentral() }
}
rootProject.name = "realxi"
include(":app")
