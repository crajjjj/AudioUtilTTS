#pragma once

// Is SKSE Menu Framework here, with every function the menu calls? Found once at
// kDataLoaded: every SKSE plugin is loaded by then, so one that is not there now
// will not appear later.

#include <array>
#include <string>

namespace Env
{
	// Every SKSEMenuFramework.dll export Menu.cpp reaches, by the name the vendored
	// header binds it to. The header's ImGui wrappers call through GetProcAddress
	// with NO null check, so a missing export is a crash at the call site; probing
	// them all at load turns that into "menu off". Add a name here when Menu.cpp
	// starts calling a new wrapper (tests\menu_tests.cpp compares the two).
	inline constexpr std::array kFrameworkExports{
		"AddSectionItem",
		"RegisterEventPriority",
		"UnregisterEvent",
		"igBeginCombo",
		"igBeginTooltip",
		"igButton",
		"igCheckbox",
		"igCollapsingHeader_TreeNodeFlags",
		"igEndCombo",
		"igEndTooltip",
		"igGetFontSize",
		"igInputFloat",
		"igInputInt",
		"igInputText",
		"igIsItemActive",
		"igIsItemHovered",
		"igPopID",
		"igPopStyleColor",
		"igPopTextWrapPos",
		"igPushID_Int",
		"igPushStyleColor_Vec4",
		"igPushTextWrapPos",
		"igSameLine",
		"igSelectable_Bool",
		"igSeparator",
		"igSetNextItemWidth",
		"igSliderFloat",
		"igSliderInt",
		"igSmallButton",
		"igSpacing",
		"igTextUnformatted",
	};

	struct State
	{
		bool        frameworkOk = false;  // loaded, with every export above
		std::string frameworkWhy;         // why not, for the log
	};

	void         Detect();
	const State& Get();
}
