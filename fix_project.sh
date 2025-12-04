#!/bin/bash

echo "Iniciando correcoes do projeto..."

# 1. Reescrever android/app/build.gradle com as dependencias corretas
echo "Atualizando android/app/build.gradle..."
cat > android/app/build.gradle <<EOF
plugins {
    id 'com.android.application'
}

android {
    namespace 'com.barracoder.android'
    compileSdk 34

    defaultConfig {
        applicationId "com.barracoder.android"
        minSdk 21
        targetSdk 34
        versionCode 1
        versionName "1.0"

        testInstrumentationRunner "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                cppFlags ""
                arguments "-DANDROID_STL=c++_shared"
            }
        }
    }

    buildTypes {
        release {
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
        }
        debug {
            debuggable true
            jniDebuggable true
        }
    }
    
    compileOptions {
        sourceCompatibility JavaVersion.VERSION_1_8
        targetCompatibility JavaVersion.VERSION_1_8
    }

    externalNativeBuild {
        cmake {
            path file("../../CMakeLists.txt")
            version "3.22.1"
        }
    }

    packagingOptions {
        jniLibs {
            pickFirsts += ['**/*.so']
        }
    }
}

dependencies {
    implementation 'androidx.appcompat:appcompat:1.6.1'
    implementation 'com.google.android.material:material:1.11.0'
    implementation 'androidx.constraintlayout:constraintlayout:2.1.4'
    
    // Android TV / Leanback
    implementation 'androidx.leanback:leanback:1.0.0'
    
    // Glide for Image Loading
    implementation 'com.github.bumptech.glide:glide:4.16.0'
    annotationProcessor 'com.github.bumptech.glide:compiler:4.16.0'

    // Annotations (Fixes org.jspecify vs androidx issue)
    implementation 'androidx.annotation:annotation:1.7.1'

    testImplementation 'junit:junit:4.13.2'
    androidTestImplementation 'androidx.test.ext:junit:1.1.5'
    androidTestImplementation 'androidx.test.espresso:espresso-core:3.5.1'
}
EOF

# 2. Corrigir os imports nos arquivos Java
# Substitui "org.jspecify.annotations" por "androidx.annotation"
echo "Corrigindo imports Java..."

# Lista de arquivos que precisam ser corrigidos
JAVA_FILES=(
    "android/app/src/main/java/com/barracoder/android/tv/MainFragment.java"
    "android/app/src/main/java/com/barracoder/android/tv/BackgroundProvisioner.java"
    "android/app/src/main/java/com/barracoder/android/tv/CardPresenter.java"
    "android/app/src/main/java/com/barracoder/android/NESItemAdapter.java"
)

for file in "${JAVA_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "Corrigindo $file..."
        # Substitui NonNull
        sed -i 's/import org.jspecify.annotations.NonNull;/import androidx.annotation.NonNull;/g' "$file"
        # Substitui Nullable
        sed -i 's/import org.jspecify.annotations.Nullable;/import androidx.annotation.Nullable;/g' "$file"
        # Substitui uso inline se houver (ex: @org.jspecify...)
        sed -i 's/org.jspecify.annotations/androidx.annotation/g' "$file"
    else
        echo "Aviso: Arquivo $file nao encontrado."
    fi
done

echo "Correcoes aplicadas com sucesso."
