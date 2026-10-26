# ndk-build: SDL2 (jni/SDL) and the game as libmain.so
JNI_DIR := $(call my-dir)
include $(JNI_DIR)/SDL/Android.mk

LOCAL_PATH := $(JNI_DIR)
include $(CLEAR_VARS)
LOCAL_MODULE := main
ROOT := ../../..
LOCAL_SRC_FILES := $(addprefix $(ROOT)/, \
    src/png.c src/hdpack.c src/resample.c src/qol.c src/tips.c src/shop.c src/bestiary.c \
    src/m68k.c src/md.c src/vdp.c src/run.c src/z80.c src/ym2612.c src/psg.c src/sound.c \
    src/ps4_ws.c src/hdscale.c src/rewind.c src/emu.c \
    win/main.c win/ui_soft.c port/sdl_win32.c) fonts.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(ROOT)/port/include $(LOCAL_PATH)/$(ROOT)/src \
    $(LOCAL_PATH)/$(ROOT)/third_party/stb $(LOCAL_PATH)/SDL/include
LOCAL_CFLAGS := -O2 -w -fno-strict-aliasing -DPS4_NO_GL -D_DEFAULT_SOURCE
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -llog -landroid -lm
include $(BUILD_SHARED_LIBRARY)
