iclbench
========

Measure inter-core latency on Android.

Based on: https://github.com/ChipsandCheese/CnC-Tools

Also related: https://github.com/nviennot/core-to-core-latency/tree/main

How to build
------------

Edit Makefile so ANDROID_NDK_ROOT and CLANGPP are set correctly for your
system, then run make.

 $ make

How to use
----------

Copy the binary to your Android device and run it:

 $ adb push iclbench /data/local/tmp/
 $ adb shell /data/local/tmp/iclbench
