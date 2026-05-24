# Android integration notes

The library is intended to be used from Android through the NDK/JNI. The Python wrapper is only for local development and tests.

## Intended data flow

```text
CameraX ImageProxy or Android Bitmap
→ RGBA byte buffer
→ JNI wrapper
→ instant_scan_rgba(...)
→ Kotlin/Java result object
→ optional instant_extract_rgba(...)
→ Bitmap containing corrected crop with border
```

## Build with Android NDK

The repository includes a starter file:

```text
android/CMakeLists.txt
```

In a real app, you can either copy the `include/` and `src/` directories into your Android project or add this repository as a Git submodule.

Example Android project layout:

```text
app/src/main/cpp/
  CMakeLists.txt
  instant_scan/              Git submodule or copied source
    include/
    src/
```

Your app-level `CMakeLists.txt` can add the library source files:

```cmake
add_library(instant_scan SHARED
    instant_scan/src/classify.c
    instant_scan/src/detect.c
    instant_scan/src/geometry.c
    instant_scan/src/image_ops.c
)

target_include_directories(instant_scan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/instant_scan/include
)
```

Then add your JNI wrapper library and link it against `instant_scan`.

## JNI wrapper shape

Recommended JNI functions:

```c
JNIEXPORT jobject JNICALL
Java_com_example_instantscan_InstantScanNative_scanRgba(
    JNIEnv *env,
    jclass clazz,
    jbyteArray rgba,
    jint width,
    jint height,
    jint stride
);

JNIEXPORT jbyteArray JNICALL
Java_com_example_instantscan_InstantScanNative_extractRgba(
    JNIEnv *env,
    jclass clazz,
    jbyteArray rgba,
    jint width,
    jint height,
    jint stride,
    jfloatArray corners,
    jint outputWidth,
    jint outputHeight
);
```

Recommended Kotlin API:

```kotlin
data class InstantPoint(val x: Float, val y: Float)

data class InstantScanResult(
    val success: Boolean,
    val filmType: Int,
    val filmName: String,
    val confidence: Float,
    val corners: List<InstantPoint>,
    val correctedWidth: Int,
    val correctedHeight: Int,
    val outerAspect: Float,
    val innerAspect: Float,
    val error: String
)

object InstantScanNative {
    init { System.loadLibrary("instant_scan_jni") }

    external fun scanRgba(
        rgba: ByteArray,
        width: Int,
        height: Int,
        stride: Int
    ): InstantScanResult

    external fun extractRgba(
        rgba: ByteArray,
        width: Int,
        height: Int,
        stride: Int,
        corners: FloatArray,
        outputWidth: Int,
        outputHeight: Int
    ): ByteArray
}
```

## Bitmap handling

The C API expects RGBA byte order. On Android, be careful with bitmap formats:

- `Bitmap.Config.ARGB_8888` is common on Android, but memory byte order may not be the same as RGBA on every path.
- The safest first integration is to explicitly copy/convert pixels into an RGBA `ByteArray` before calling JNI.
- Once working, optimize by using direct buffers or native bitmap access.

## Threading

Run scanning off the UI thread. The detector is CPU-bound and should be called from a background dispatcher/thread.

## Memory ownership

The C library does not allocate returned image buffers. Android/JNI should allocate output memory, pass it into `instant_extract_rgba`, then wrap the result as a `Bitmap`.

## Suggested Android milestones

1. Add a JNI wrapper that accepts an RGBA `ByteArray` and returns metadata only.
2. Add crop extraction returning a `ByteArray`.
3. Convert the crop `ByteArray` to a `Bitmap`.
4. Replace `ByteArray` copies with direct buffers/native bitmap access if performance needs it.
5. Add app-side debug overlays using the returned corners.
