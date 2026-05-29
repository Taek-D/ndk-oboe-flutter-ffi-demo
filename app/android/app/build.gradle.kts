plugins {
    id("com.android.application")
    id("kotlin-android")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

android {
    namespace = "com.example.noise_meter"
    compileSdk = flutter.compileSdkVersion
    // 설치된 NDK r28로 고정(Oboe 1.9.3 소스 빌드 검증 대상).
    ndkVersion = "28.2.13676358"

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = JavaVersion.VERSION_17.toString()
    }

    defaultConfig {
        // TODO: Specify your own unique Application ID (https://developer.android.com/studio/build/application-id.html).
        applicationId = "com.example.noise_meter"
        // Oboe 권장 minSdk(LowLatency/AAudio)는 26 이상. 계획서 §Guardrails.
        minSdk = 26
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName

        // 네이티브 빌드: x86_64(에뮬레이터) 단일 ABI + Oboe 권장 STL c++_shared.
        ndk {
            abiFilters.add("x86_64")
        }
        externalNativeBuild {
            cmake {
                // NDK r28은 legacy toolchain file을 제거 → OFF로 unified Clang toolchain 강제.
                // (legacy 호출 시 cmake가 0xC0000409 STACK_BUFFER_OVERRUN 크래시)
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF"
                )
                cppFlags += "-std=c++17"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            // NDK r28과 호환되는 최신 CMake(3.22.1은 NDK28 unified toolchain에서 불안정).
            version = "3.31.6"
        }
    }

    buildTypes {
        release {
            // TODO: Add your own signing config for the release build.
            // Signing with the debug keys for now, so `flutter run --release` works.
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}

flutter {
    source = "../.."
}
