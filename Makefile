ANDROID_NDK_ROOT = ~/.mozbuild/android-ndk-r28b/
CLANGPP=$(ANDROID_NDK_ROOT)/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android26-clang++

ipbench: Makefile main.cpp
	 $(CLANGPP) -o ipbench main.cpp -static-libstdc++
