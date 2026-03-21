# Updated to support both 32-bit and 64-bit architectures
# Removed LOCAL_MULTILIB := 32 to allow 64-bit builds

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

MY_LOCAL_PATH := $(LOCAL_PATH)

include $(MY_LOCAL_PATH)/src/mr/Android2.mk # Android.mk  Android2.mk
#include $(LOCAL_PATH)/src/mr_pre/Android.mk	# 使用预编译的精简/完整虚拟机

include $(MY_LOCAL_PATH)/src/Android2.mk		# 精简  完整
