# Guest allocator BLOB support

`HAL_PIXEL_FORMAT_BLOB` describes a one-dimensional byte buffer. The guest
allocator uses its width as both the allocation's logical byte size and its
stride. Height must be one. The existing shared-memory allocation still rounds
the physical backing region to a page boundary. Image formats retain their
existing alignment and padding.

## Android platform build

The allocator remains the `gralloc.redroid` Soong module. In a ReDroid Android 14
source checkout, select the target architecture and run:

```sh
m gralloc.redroid redroid_hardware_buffer_test
```

## Build a module for the pinned Android 14 image

The standalone build uses Android NDK 27.2.12479018, official Android 14 headers,
and the target image's own `libcutils.so` and `libc++.so`. It does not replace
those runtime libraries. Use the same architecture for the image, copied
libraries, and `--abi` argument.

Fetch the header checkouts once:

```sh
mkdir -p headers
git clone --depth=1 --branch android-14.0.0_r1 https://android.googlesource.com/platform/hardware/libhardware headers/libhardware
git clone --depth=1 --branch android-14.0.0_r1 https://android.googlesource.com/platform/system/core headers/core
git clone --depth=1 --branch android-14.0.0_r1 https://android.googlesource.com/platform/system/logging headers/logging
```

For the currently running pinned image, copy its libraries:

```sh
mkdir -p runtime-libs
docker cp -L DEVICE_CONTAINER:/system/apex/com.android.vndk.current/lib64/libcutils.so runtime-libs/libcutils.so
docker cp -L DEVICE_CONTAINER:/system/lib64/libc++.so runtime-libs/libc++.so
```

Build the module and native regression probe:

```sh
python3 gralloc/tools/build.py \
  --ndk "$ANDROID_NDK_ROOT" \
  --headers headers \
  --runtime-libs runtime-libs \
  --abi arm64-v8a \
  --output build/arm64-v8a
```

The build records the compiler, pinned header revisions, source hashes, runtime
library hashes, and output hashes in `build.json`. The script verifies the header
revisions before compiling. For x86_64 use `--abi x86_64` and libraries copied
from the x86_64 image.

Create an experimental image containing the replacement allocator:

```sh
docker build -f gralloc/Dockerfile \
  --build-arg BASE_IMAGE=redroid/redroid@sha256:0a611199ba2e0b5d60af39b3327a517f6407231f4352114ed3bd3cbfe2be69aa \
  --tag redroid-blob:android14-arm64 build/arm64-v8a
```

Run this image on an isolated device using the same guest rendering mode and
resource profile as the base image. Restart Android after changing its allocator;
existing processes may retain the previously loaded library.

## Regression probes

The native probe allocates BLOB buffers of 1, 3, 4095, 4096, 4097, and 12345 bytes.
It verifies the description and stride, writes the buffer, sends its handle
through a Unix socket, imports it, and verifies every byte through the imported
mapping. RGB565, RGBA8888, and FP16 buffers exercise the existing image path.

```sh
adb push build/arm64-v8a/hardware-buffer-test /data/local/tmp/
adb shell chmod 755 /data/local/tmp/hardware-buffer-test
adb shell /data/local/tmp/hardware-buffer-test
```

The Java probe exercises `HardwareBuffer.create` with CPU read/write usage and
the same BLOB sizes. Compile it with an Android 34 SDK and run it through ADB so
it inherits Android's runtime environment:

```sh
mkdir -p build/java build/dex
javac --release 8 -classpath "$ANDROID_HOME/platforms/android-34/android.jar" \
  -d build/java gralloc/tests/HardwareBufferJavaTest.java
"$ANDROID_HOME/build-tools/36.0.0/d8" \
  --lib "$ANDROID_HOME/platforms/android-34/android.jar" --min-api 29 \
  --output build/dex build/java/HardwareBufferJavaTest.class
adb push build/dex/classes.dex /data/local/tmp/hardware-buffer-test.dex
adb shell 'CLASSPATH=/data/local/tmp/hardware-buffer-test.dex app_process /system/bin HardwareBufferJavaTest'
```

Both probes exit nonzero on a failure. Run them on the original image first to
retain the failing baseline, then on the experimental image. Run the complete
application test suite before adopting the image in a device pool.

## Recorded validation

On 2026-11-06, Android 14 ARM64 with guest rendering rejected all six BLOB sizes
on the original image. The experimental image passed all nine native cases and
all six Java cases. The original Nitro APK then passed all 18 suites: 1,418 tests
passed, six skipped, zero failed. Every test name and status matched the original
software-rendered emulator result. Android's launcher also rendered correctly.

The [ARM64 evidence](validation/arm64-v8a) includes the before/after probe logs,
build hashes, and Nitro summary. The [x86_64 build record](validation/x86_64/build.json)
records a successful cross-build; x86_64 runtime verification is still pending.
