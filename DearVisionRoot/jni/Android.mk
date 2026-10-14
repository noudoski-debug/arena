LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := dobby
LOCAL_SRC_FILES := lib/arm64-v8a/libdobby.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := Dear
LOCAL_CPPFLAGS := -std=c++17 -fvisibility=hidden -fexceptions -Wall -Wextra
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/include/ImGui \
    $(LOCAL_PATH)/include/ImGui/backends $(LOCAL_PATH)/include/ImGui/font \
    $(LOCAL_PATH)/include/Hook/Dobby
LOCAL_SRC_FILES := src/Main.cpp src/Runtime.cpp src/Input.cpp src/Menu.cpp src/Chams.cpp src/Config.cpp src/Effects.cpp \
    src/ImGui/imgui.cpp src/ImGui/imgui_draw.cpp src/ImGui/imgui_tables.cpp \
    src/ImGui/imgui_widgets.cpp src/ImGui/backends/imgui_impl_opengl3.cpp
LOCAL_STATIC_LIBRARIES := dobby
LOCAL_LDLIBS := -llog -ldl -landroid -lEGL -lGLESv3
LOCAL_LDFLAGS := -Wl,-z,max-page-size=16384
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := dear-loader
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wextra -fexceptions
LOCAL_SRC_FILES := loader/Loader.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_LDLIBS := -ldl
LOCAL_LDFLAGS := -Wl,-z,max-page-size=16384
include $(BUILD_EXECUTABLE)
