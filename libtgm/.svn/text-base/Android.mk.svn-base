# Copyright 2006 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    tgm_event.cpp \
    tgm.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    libhardware_legacy

LOCAL_C_INCLUDES += $(TOPDIR)hardware/tgm/include

LOCAL_PRELINK_MODULE:=false

LOCAL_CFLAGS := 

LOCAL_MODULE:= libtgm

LOCAL_LDLIBS += -lpthread

include $(BUILD_SHARED_LIBRARY)


# For RdoServD which needs a static library
# =========================================
ifneq ($(ANDROID_BIONIC_TRANSITION),)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    tgm.cpp

LOCAL_STATIC_LIBRARIES := \
    libutils_static \
    libcutils

LOCAL_CFLAGS := 

LOCAL_C_INCLUDES += $(TOPDIR)hardware/tgm/include

LOCAL_MODULE:= libtgm_static

LOCAL_LDLIBS += -lpthread

include $(BUILD_STATIC_LIBRARY)
endif # ANDROID_BIONIC_TRANSITION
