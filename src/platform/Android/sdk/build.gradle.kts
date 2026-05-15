plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.slark.sdk"
    compileSdk = 35

    defaultConfig {
        minSdk = 29

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.pro")
        externalNativeBuild {
            cmake {
                abiFilters("arm64-v8a")
                cppFlags("-std=c++23")
                arguments(
                    "-DCMAKE_BUILD_TYPE=Debug",
                    "-DDISABLE_TEST=ON",
                    "-DDISABLE_HTTP=OFF"
                )
            }
        }
    }

    buildTypes {
        debug {
            externalNativeBuild {
                cmake {
                    arguments(
                        "-DCMAKE_BUILD_TYPE=Debug",
                        "-DDISABLE_TEST=ON",
                        "-DDISABLE_HTTP=OFF"
                    )
                }
            }
        }
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            externalNativeBuild {
                cmake {
                    arguments(
                        "-DCMAKE_BUILD_TYPE=Release",
                        "-DDISABLE_TEST=ON",
                        "-DDISABLE_HTTP=OFF"
                    )
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlinOptions {
        jvmTarget = "11"
    }
    ndkVersion = "27.0.12077973"
    buildToolsVersion = "35.0.0"
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}

// 打印 AAR 产物路径，方便 CI/脚本集成
android.libraryVariants.configureEach {
    val variant = this
    tasks.named("assemble${variant.name.replaceFirstChar { it.uppercase() }}") {
        doLast {
            val aarDir = layout.buildDirectory.dir("outputs/aar").get().asFile
            val aar = aarDir.listFiles()?.firstOrNull { it.name.endsWith(".aar") }
            if (aar != null) {
                println("[slark] AAR output: ${aar.absolutePath}")
            }
        }
    }
}