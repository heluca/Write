#include $(call all-subdir-makefiles)

# Note that symlinking source dirs is a terrible idea which can create a huge mess when trying to open files,
#  esp. when debugging
# repo root is six levels up from this jni/ dir; capture before any include changes my-dir
WRITE_ROOT := $(abspath $(call my-dir)/../../../../../..)
include $(WRITE_ROOT)/SDL/Android.mk
include $(WRITE_ROOT)/syncscribble/Makefile
