#include $(call all-subdir-makefiles)

# Include SDL and the Write build relative to this jni dir so the build works on any machine
# (the original hardcoded /home/mwhite/... paths only worked on the upstream author's box).
# use absolute paths so $(call my-dir) inside the included makefiles resolves correctly
MY_JNI_DIR := $(abspath $(call my-dir))
include $(MY_JNI_DIR)/../../../../../../SDL/Android.mk
include $(MY_JNI_DIR)/../../../../../Makefile
