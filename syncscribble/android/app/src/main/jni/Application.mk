# See https://developer.android.com/ndk/guides/cpp-support
APP_STL := c++_shared

# 16 KB page size support, required by Play for apps targeting API 35+. Android 15+
# devices may boot with 16 KB pages and cannot load .so files aligned for 4 KB.
# NDK r28 defaults to this; r27 needs the flag. Harmless on 4 KB devices and on
# 32-bit ABIs (which never use 16 KB pages).
APP_LDFLAGS := -Wl,-z,max-page-size=16384

# these are set in build.gradle now
#APP_ABI := armeabi-v7a arm64-v8a x86 x86_64
#APP_PLATFORM=android-18

#APP_CPPFLAGS := -fno-exceptions -fno-rtti -Wno-narrowing
