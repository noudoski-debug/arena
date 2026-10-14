@echo off

REM edit paths here, important: \ at the end
set NDK=*/data/user/0/com.aide.ui.crustacean/no_backup/ndksupport-1710240003/android-ndk-aide/ndk-build*\
set PROJECT_ROOT=*ROOT FOLDER (WITH JNI, LIBS, OBJ)*\
set MAIN=%PROJECT_ROOT%jni\

call %NDK%ndk-build.cmd NDK_PROJECT_PATH=%PROJECT_ROOT% APP_BUILD_SCRIPT=%MAIN%Android.mk NDK_APPLICATION_MK=%MAIN%Application.mk
pause