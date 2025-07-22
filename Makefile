ANDROID_NDK_ROOT = ~/.mozbuild/android-ndk-r28b/
CLANGPP=$(ANDROID_NDK_ROOT)/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android26-clang++

iclbench: Makefile main.cpp
	$(CLANGPP) -o iclbench main.cpp -static-libstdc++ -Wall

clean:
	rm -f iclbench
