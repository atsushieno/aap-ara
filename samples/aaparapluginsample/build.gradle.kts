plugins {
    alias(libs.plugins.android.application)
}

apply { from("../../common.gradle") }

android {
    namespace = "org.androidaudioplugin.aaparapluginsample"
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
    implementation(project(":androidaudioplugin-ara"))
    implementation(libs.aap.core)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.kotlin.stdlib.jdk8)
}
