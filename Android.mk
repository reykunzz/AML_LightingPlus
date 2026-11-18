LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc

# Nama beda per ABI (sama seperti template AML) supaya 32-bit & 64-bit bisa ditaruh di folder mods yang sama
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_MODULE := LightingPlus
    LOCAL_CFLAGS += -mfloat-abi=softfp
else
    LOCAL_MODULE := LightingPlus64
endif

LOCAL_SRC_FILES := main.cpp mod/logger.cpp mod/config.cpp mod/flogger.cpp
LOCAL_CFLAGS += -O2 -DNDEBUG
LOCAL_CPPFLAGS += -std=c++17
LOCAL_C_INCLUDES += $(LOCAL_PATH) $(LOCAL_PATH)/mod
LOCAL_LDLIBS += -llog -ldl

include $(BUILD_SHARED_LIBRARY)
