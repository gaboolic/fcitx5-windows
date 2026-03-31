Fcitx5 TSF — graphical installer (Inno Setup 6)
===============================================

Produces:
  - fcitx5-windows-setup.exe — wizard (choose install folder, copies stage tree, regsvr32 /s)
  - After install: unins000.exe in the install directory — uninstaller
  - Start menu: "Uninstall" + "Fcitx5 user config" (opens %AppData%\Fcitx5)
  - Optional desktop shortcut to the uninstaller

Prerequisites:
  1) A full stage directory (e.g. from scripts/02-build-deps.sh or CI): must contain bin\libfcitx5-x86_64.dll (or fcitx5-x86_64.dll) plus share\fcitx5\profile.windows.example (fallback: profile.pinyin.example) and the rest of share/lib. Official CI stage also includes fcitx5-rime (librime-*.dll from MSYS2 prebuilt package, plus share\rime-data), fcitx5-table-extra, fcitx5-lua (libluaaddonloader + Lua DLL in bin), and default profile seeds pinyin + shuangpin + wbx + rime on first copy.
  2) Inno Setup 6: https://jrsoftware.org/isdl.php

Build (PowerShell):
  .\installer\build-installer.ps1 -StageDir C:\path\to\stage

Output:
  installer\dist\fcitx5-windows-setup.exe

Uninstall:
  - Settings -> Apps -> Fcitx5 (TSF IME), or
  - Run unins000.exe in the install folder, or
  - Start menu -> Fcitx5 (TSF IME) -> Uninstall

Notes:
  - x64 only (matches the TSF IME DLL).
  - Installer requests Administrator (HKCR registration).
  - Fixed AppId (inside Fcitx5Tsf.iss) — keep stable so Windows recognizes upgrades.
  - IME CLSID in the ISS must stay in sync with win32/dll and scripts/Fcitx5-Ime.Common.ps1.
