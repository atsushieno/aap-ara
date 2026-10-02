plugins {
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.android.application)
}

apply { from("../../common.gradle") }

android {
    namespace = "org.androidaudioplugin.aaparapluginsample"
    // Keep the standalone host and plugin application on the same editor UI/model.
    sourceSets.getByName("main").kotlin.srcDir("../aaparahostsample/src/main/java")
    sourceSets.getByName("androidTest").kotlin.srcDir("../aaparahostsample/src/androidTest/java")
    defaultConfig {
        applicationId = "org.androidaudioplugin.aaparapluginsample"
        externalNativeBuild {
            cmake {
                arguments("-DANDROID_STL=c++_shared")
            }
        }
    }
    externalNativeBuild {
        cmake {
            version = libs.versions.cmake.get()
            path("src/main/cpp/CMakeLists.txt")
        }
    }
    buildFeatures {
        compose = true
        prefab = true
    }
    buildTypes {
        debug {
            packaging.jniLibs.keepDebugSymbols.add("**/*.so")
        }
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2026.04.01"))
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    androidTestImplementation(platform("androidx.compose:compose-bom:2026.04.01"))
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation(libs.test.espresso.core)
    androidTestImplementation(libs.junit)
    androidTestImplementation(libs.test.ext.junit)
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    implementation(project(":androidaudioplugin-ara"))
    implementation(libs.aap.core)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.kotlin.stdlib.jdk8)
}
