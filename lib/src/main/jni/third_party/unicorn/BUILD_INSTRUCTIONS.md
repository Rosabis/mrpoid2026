# Unicorn Engine for Android NDK

## 编译 Unicorn Engine

需要为 Android NDK 交叉编译 Unicorn Engine v1.0.3。

### 步骤

1. 下载 Unicorn 源码:
```bash
git clone https://github.com/unicorn-engine/unicorn.git
cd unicorn
git checkout 1.0.3
```

2. 为每个 ABI 编译:
```bash
# 设置 NDK 路径
export NDK=/path/to/android-ndk

# armeabi-v7a
mkdir build-armeabi-v7a && cd build-armeabi-v7a
cmake .. -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-19 \
  -DUNICORN_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
cd ..

# arm64-v8a
mkdir build-arm64-v8a && cd build-arm64-v8a
cmake .. -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DUNICORN_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
cd ..

# x86
mkdir build-x86 && cd build-x86
cmake .. -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=x86 -DANDROID_PLATFORM=android-19 \
  -DUNICORN_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
cd ..

# x86_64
mkdir build-x86_64 && cd build-x86_64
cmake .. -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-21 \
  -DUNICORN_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
cd ..
```

3. 将编译好的 libunicorn.so 复制到对应目录:
```
lib/armeabi-v7a/libunicorn.so
lib/arm64-v8a/libunicorn.so
lib/x86/libunicorn.so
lib/x86_64/libunicorn.so
```

## cfunction.ext

将 vmrp 项目中的 cfunction.ext 文件放到:
```
app/src/main/assets/cfunction.ext
```

此文件可从 vmrp 的 wasm/dist/fs 目录获取，或使用斯凯 SDK 编译 vmrp 的 mythroad 层生成。
