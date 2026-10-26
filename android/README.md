# Android

The APK is built by GitHub Actions (`.github/workflows/builds.yml`): it is the
free build (CPU interpreter only, nothing from the game inside). On the first
start the game asks for the ROM file with the system file picker and copies it
into the app's folder (`Android/data/com.ps4recomp.game/files`), where the
saves, settings and screenshots also live.

Controls: an on-screen Mega Drive pad (d-pad, A B C, Start, «Меню», «»» for
turbo); it hides when a gamepad is connected. The system Back button opens the
menu. Touching anywhere else works as a mouse click in the menus.

To build by hand: Android SDK + NDK, JDK 17 and Gradle 8.9, then

    git clone --depth 1 -b release-2.30.9 https://github.com/libsdl-org/SDL android/app/jni/SDL
    python3 tools/embed_fonts.py android/app/jni/fonts.c
    cd android && gradle assembleRelease
