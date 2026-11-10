# DebloatWand - MUX HUD (Windows, ADB, no root)

Build (MSYS2 UCRT64): `pacman -S --needed mingw-w64-ucrt-x86_64-toolchain make`, then `make` -> `dist/DebloatWand.exe`.
Put `adb.exe` + `AdbWinApi.dll` + `AdbWinUsbApi.dll` (Android platform-tools) next to the exe, or have adb in PATH.

Installer
- Open `setup/setup.iss` with Inno Setup and compile it to create the installer.
- The installer packages `dist/DebloatWand.exe`, asks the user for an installation directory, and adds that directory to the system `PATH`.
- The installation directory is removed from the system `PATH` when DebloatWand is uninstalled.

Sections
- DEVICE INFO: model, Android, patch, bootloader lock, verified boot / verity, system-as-root, dynamic partitions, A/B, RAM, screen, battery, storage, root status.
- APPS LIST: every package with TYPE / STATE / CLASS (SAFE or RISKY) / NEEDS/BY counts. Click a row for the detail panel:
  what the app needs, which installed apps need it (shared UID, uses-library, Google Play services chain) and a note.
  Filters: ALL / SAFE / RISKY / REMOVED, SYSTEM / USER toggles, search box, PRESET (common bloat), CLEAR.
- INSTALL APK: pick one or many .apk files, or drag & drop them on the window. `-g` grants all permissions, `-d` allows downgrade.

Removing a package that other installed apps still need, or a RISKY one, shows a warning first.
Without root, a SYSTEM app is only hidden for user 0 (`pm uninstall -k --user 0`), storage is not freed; RESTORE TICKED undoes it
(`pm install-existing`). USER apps are fully uninstalled.

Layout: src/ (main.cpp = HUD GUI, adb.cpp = adb + dependency parser), build/, dist/.
