SKSEMenuFramework.h - the client header of "SKSE Menu Framework" (Nexus 120352).

Vendored verbatim, never edited here. AudioUtilTTS.dll includes it (through
src\menu\Ui.h only) and calls into the framework's SKSEMenuFramework.dll at run
time through GetProcAddress: nothing is linked, and the framework's DLL is never
redistributed with AudioUtilTTS.

Source:  https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/master/resources/SKSEMenuFramework.h
License: GPL-3.0 (the framework's repository)
Taken:   2026-10-14, as a byte copy of the one SLO VE vendors (skse\extern\SKSEMenuFramework),
         which it took on 2026-10-07 from SexLab P+ (lib/ImGui, last changed 2026-07-11)
sha256:  48416e8220ca777e2fffc2ef2baf21f699ab2e6c409d437f44eec5e311c3524c

Why this version: it looks the framework module up lazily (older copies resolve
it at static-init time, which fails silently when our DLL loads first), and the
five functions it has that framework 3.8 lacks (AddWindowWithView, GetMainWindow,
SetHotkeyEnabled, IsHotkeyEnabled, PushFont) are null-guarded. The menu uses none
of those five.

Rules for using it (see src\menu\Ui.h):
- include it only through src\menu\Ui.h;
- call ImGuiMCP:: functions only inside a callback the framework invoked - the
  wrappers do not null-check, so a call without the framework DLL is a crash;
- every wrapper the menu calls has its export on the probe list in
  src\menu\Env.h (tests\menu_tests.cpp checks it);
- do not read ImGui structs (GetStyle, GetIO) directly.
