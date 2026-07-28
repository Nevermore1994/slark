plugins {
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.android) apply false
}

subprojects {
    buildDir = file("${rootDir}/../../../build/android/${project.name}")
}