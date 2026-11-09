# DebloatWand (C++ Win32, ADB, no root)

Build (MSYS2 UCRT64): `pacman -S --needed mingw-w64-ucrt-x86_64-toolchain make` then `make` -> `dist/DebloatWand.exe`.
Put `adb.exe` (+ AdbWinApi.dll, AdbWinUsbApi.dll from Android platform-tools) next to the exe, or have adb in PATH.

Use: enable USB debugging on the phone, connect, accept the prompt, Refresh devices -> Load packages ->
tick apps -> Remove ticked. Restore ticked brings them back. Export removed list saves names to a txt file.

Method: `pm uninstall -k --user 0 <pkg>` / `pm install-existing --user 0 <pkg>`.
Without root this hides/disables the app for user 0 only: the APK stays in /system, so free storage does NOT grow.
A factory reset restores everything. Packages marked RISKY ask for confirmation.
