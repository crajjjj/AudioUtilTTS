#pragma once

// The ONLY include site of the vendored SKSE Menu Framework header (see
// vendor\SKSEMenuFramework\README.txt). Rules for everything that includes this:
//  - call ImGuiMCP:: functions only inside a callback the framework invoked. The
//    wrappers resolve the framework's exports with GetProcAddress and do not check
//    for null, so a call without the framework DLL crashes.
//  - call only wrappers whose export is on the probe list in Env.cpp
//    (tests\menu_tests.cpp checks that list against Menu.cpp).
//  - never read ImGui structs (GetStyle, GetIO): their layout belongs to whatever
//    ImGui build the installed framework carries.
//  - text that comes from a file or from the plugin (labels, tips, values, the
//    status line) goes through TextUnformatted, never a printf-style wrapper.

#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)

namespace Ui = ImGuiMCP;
