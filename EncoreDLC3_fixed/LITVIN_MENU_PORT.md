# Litvin Tap UI port for EncoreDLC

- The supplied `menu.cpp` / `menu.h` interface and visual layout are included as `jni/src/LitvinMenu.cpp` and `jni/include/LitvinMenu.h`.
- `menu::render()` now delegates to LitvinMenu instead of drawing the previous MenuEternal layout.
- Existing Android touch/key menu toggle is retained; close button updates `Vars.Menu.openmenu`.
- Open/close uses alpha and a small vertical easing transition.
- `Watermark` setting is connected to the existing watermark preference.
- The source menu's feature controls remain UI-local settings unless explicitly mapped in the existing feature layer; do not assume each control drives the game's runtime.
- The project was not built with Android NDK in this environment, so a device-ready binary is not claimed.
