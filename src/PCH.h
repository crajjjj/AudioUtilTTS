#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

// CommonLib (REX, since v7) pulls real <Windows.h> into the include chain; scrub the
// macro collisions we actually hit (min/max are handled by NOMINMAX in xmake.lua)
#undef GetObject

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std::literals;

namespace logger = SKSE::log;

#define REGISTERFUNC(func, classname) a_vm->RegisterFunction(#func##sv, classname, func, true)
