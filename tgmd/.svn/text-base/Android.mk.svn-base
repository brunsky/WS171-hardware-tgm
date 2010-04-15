# Copyright 2006 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	tgmd.c


LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libtgm

LOCAL_C_INCLUDES += $(TOPDIR)hardware/tgm/include

LOCAL_CFLAGS := -DRIL_SHLIB

LOCAL_MODULE:= tgmd

include $(BUILD_EXECUTABLE)
