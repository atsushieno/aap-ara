plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.dokka)
    alias(libs.plugins.vanniktech.maven.publish)
    signing
}

apply { from("../common.gradle") }

version = libs.versions.aap.ara.get()

android {
    namespace = "org.androidaudioplugin.ara"
    project.extra["description"] = "AndroidAudioPlugin - ARA extension package"

    defaultConfig {
        externalNativeBuild {
            cmake {
                arguments("-DANDROID_STL=c++_shared")
            }
        }
    }

    buildTypes {
        debug {
            packaging.jniLibs.keepDebugSymbols.add("**/*.so")
        }
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    buildFeatures {
        prefab = true
        prefabPublishing = true
    }

    externalNativeBuild {
        cmake {
            version = libs.versions.cmake.get()
            path("src/main/cpp/CMakeLists.txt")
        }
    }

    prefab {
        create("androidaudioplugin-ara") {
            headers = "../include"
            name = "androidaudioplugin-ara"
        }
    }

    packaging {
        jniLibs.excludes.add("**/libc++_shared.so")
        jniLibs.excludes.add("**/libandroidaudioplugin.so")
    }
}

dependencies {
    implementation(libs.aap.core)
    implementation(libs.androidx.core.ktx)
    implementation(libs.kotlin.stdlib.jdk8)
    implementation(libs.startup.runtime)
    testImplementation(libs.junit)
    androidTestImplementation(libs.test.ext.junit)
    androidTestImplementation(libs.test.espresso.core)
}

val packageUrl = "https://github.com/atsushieno/aap-ara"
val licenseUrl = "https://github.com/atsushieno/aap-ara/blob/main/LICENSE"

mavenPublishing {
    publishToMavenCentral()
    if (project.hasProperty("mavenCentralUsername") || System.getenv("ORG_GRADLE_PROJECT_mavenCentralUsername") != null)
        signAllPublications()
    coordinates(group.toString(), project.name, version.toString())
    pom {
        name.set(project.name)
        description.set(project.extra["description"].toString())
        url.set(packageUrl)
        scm { url.set(packageUrl) }
        licenses { license { name.set("MIT"); url.set(licenseUrl) } }
        developers {
            developer {
                id.set("atsushieno")
                name.set("Atsushi Eno")
                email.set("atsushieno@gmail.com")
            }
        }
    }
}
