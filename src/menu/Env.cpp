#include "menu/Env.h"

namespace Env
{
	namespace
	{
		State g_state;
	}

	void Detect()
	{
		g_state = {};
		const HMODULE module = ::GetModuleHandleW(L"SKSEMenuFramework");
		if (!module) {
			g_state.frameworkWhy = "SKSE Menu Framework is not installed";
			return;
		}
		std::string missing;
		for (const char* name : kFrameworkExports) {
			if (!::GetProcAddress(module, name)) {
				missing += missing.empty() ? "" : ", ";
				missing += name;
			}
		}
		if (!missing.empty()) {
			g_state.frameworkWhy = "this SKSE Menu Framework lacks: " + missing;
			return;
		}
		g_state.frameworkOk = true;
	}

	const State& Get()
	{
		return g_state;
	}
}
